#include "epoll.hpp"
#include <cstring>
#include <cerrno>

EpollScheduler* EpollScheduler::current_scheduler = nullptr;

void trampoline(Fiber *fiber) {
    try {
        (*fiber)();
    } catch (...) {
        EpollScheduler::current_scheduler->sched_context.exception = std::current_exception();
    }

    // When the fiber finishes, notify the scheduler.
    EpollScheduler::current_scheduler->sched_context.switch_context({Action::STOP});
    __builtin_unreachable();
}

void schedule(Fiber fiber) {
    if (!EpollScheduler::current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    EpollScheduler::current_scheduler->schedule(std::move(fiber));
}

void yield() {
    if (!EpollScheduler::current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    EpollScheduler::current_scheduler->yield({});
}


void scheduler_run(EpollScheduler &sched) {
    if (EpollScheduler::current_scheduler) {
        throw std::runtime_error("Global scheduler is not empty");
    }
    EpollScheduler::current_scheduler = &sched;
    try {
        EpollScheduler::current_scheduler->run();
    } catch (...) {
        EpollScheduler::current_scheduler = nullptr;
        throw;
    }
    EpollScheduler::current_scheduler = nullptr;
}

Context FiberScheduler::create_context_from_fiber(Fiber fiber) {
    Context context(std::move(fiber));

    getcontext(&context.ctx);
    context.ctx.uc_stack.ss_sp = context.stack.ptr;
    context.ctx.uc_stack.ss_size = StackPool::STACK_SIZE;
    context.ctx.uc_link = nullptr;
    makecontext(&context.ctx, (void (*)())trampoline, 1, context.fiber.get());
    return context;
}

YieldData FiberScheduler::yield(YieldData data) {
    auto &ctx = EpollScheduler::current_scheduler->sched_context;
    Action act = ctx.switch_context(Action{Action::SCHED, data});
    if (act.action == Action::THROW) {
        auto ex = ctx.exception;
        ctx.exception = nullptr;
        std::rethrow_exception(ex);
    }
    return act.user_data;
}

void FiberScheduler::run_one() {
    Context ctx = std::move(queue.front());
    queue.pop();
    Action act;
    if (sched_context.exception) {
        act = sched_context.switch_context(Action{Action::THROW});
        sched_context.exception = nullptr;
    } else {
        act = sched_context.switch_context(Action{Action::START});
    }

    sched_context.yield_data = act.user_data;

    if (sched_context.inspector) {
        (*sched_context.inspector)(act, sched_context);
        sched_context.inspector.reset();
    }

    if (act.action == Action::SCHED) {
        schedule(std::move(sched_context));
    } else if (act.action == Action::STOP) {
        if (sched_context.exception) {
            auto ex = sched_context.exception;
            sched_context.exception = nullptr;
            std::rethrow_exception(ex);
        }
        // context destroyed
    }
}

void EpollScheduler::await_read(Context context, YieldData data) {
    /// Subscribe epoll
    auto fd = static_cast<ReadData *>(data.ptr)->fd;
    auto &elem = wait_list[fd];
    Node node{std::move(context), fd, data, &EpollScheduler::do_read};
    bool existed = elem.in.has_value() || elem.out.has_value();
    elem.in = std::move(node);
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = (elem.in ? EPOLLIN : 0) | (elem.out ? EPOLLOUT : 0) | EPOLLERR;
    int op = existed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_ctl(epoll_fd, op, fd, &ev);
}

void EpollScheduler::do_read(Node node) {
    /// call read
    /// schedule fiber
    auto *rd = static_cast<ReadData *>(node.data.ptr);
    ssize_t r = ::read(rd->fd, rd->data, rd->size);
    if (r < 0) {
        node.context.exception = std::make_exception_ptr(std::runtime_error(strerror(errno)));
    } else {
        node.context.yield_data.ss = r;
    }
    schedule(std::move(node.context));
}

void EpollScheduler::await_write(Context context, YieldData data) {
    auto fd = static_cast<WriteData *>(data.ptr)->fd;
    auto &elem = wait_list[fd];
    Node node{std::move(context), fd, data, &EpollScheduler::do_write};
    bool existed = elem.in.has_value() || elem.out.has_value();
    elem.out = std::move(node);
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = (elem.in ? EPOLLIN : 0) | (elem.out ? EPOLLOUT : 0) | EPOLLERR;
    int op = existed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_ctl(epoll_fd, op, fd, &ev);
}

void EpollScheduler::do_write(Node node) {
    auto *wd = static_cast<WriteData *>(node.data.ptr);
    ssize_t w = ::write(wd->fd, wd->data, wd->size);
    if (w < 0) {
        node.context.exception = std::make_exception_ptr(std::runtime_error(strerror(errno)));
    } else {
        node.context.yield_data.ss = w;
    }
    schedule(std::move(node.context));
}

void EpollScheduler::await_accept(Context context, YieldData data) {
    auto fd = static_cast<AcceptData *>(data.ptr)->fd;
    auto &elem = wait_list[fd];
    Node node{std::move(context), fd, data, &EpollScheduler::do_accept};
    bool existed = elem.in.has_value() || elem.out.has_value();
    elem.in = std::move(node);
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = (elem.in ? EPOLLIN : 0) | (elem.out ? EPOLLOUT : 0) | EPOLLERR;
    int op = existed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_ctl(epoll_fd, op, fd, &ev);
}

void EpollScheduler::do_accept(Node node) {
    auto *ad = static_cast<AcceptData *>(node.data.ptr);
    int res = ::accept(ad->fd, ad->addr, ad->addrlen);
    if (res < 0) {
        node.context.exception = std::make_exception_ptr(std::runtime_error(strerror(errno)));
    } else {
        node.context.yield_data.i = res;
    }
    schedule(std::move(node.context));
}

void EpollScheduler::do_error(Node node) {
    /// "throw" exception in context
    node.context.exception = std::make_exception_ptr(std::runtime_error("epoll error"));
    schedule(std::move(node.context));
}

void EpollScheduler::run() {
    while (true) {
        while (!empty()) {
            run_one();
        }
        if (wait_list.empty()) {
            break;
        }
        epoll_event ev{};
        int r = epoll_wait(epoll_fd, &ev, 1, -1);
        if (r <= 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("epoll_wait failed");
        }
        auto it = wait_list.find(ev.data.fd);
        if (it == wait_list.end()) {
            continue;
        }
        if (ev.events & (EPOLLERR | EPOLLHUP)) {
            if (it->second.in) do_error(std::move(*it->second.in));
            if (it->second.out) do_error(std::move(*it->second.out));
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev.data.fd, nullptr);
            wait_list.erase(it);
            continue;
        }
        if ((ev.events & EPOLLIN) && it->second.in) {
            Node node = std::move(*it->second.in);
            it->second.in.reset();
            (this->*node.callback)(std::move(node));
        }
        if ((ev.events & EPOLLOUT) && it->second.out) {
            Node node = std::move(*it->second.out);
            it->second.out.reset();
            (this->*node.callback)(std::move(node));
        }
        if (!it->second.in && !it->second.out) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev.data.fd, nullptr);
            wait_list.erase(it);
        } else {
            epoll_event ev2{};
            ev2.data.fd = ev.data.fd;
            ev2.events = (it->second.in ? EPOLLIN : 0) | (it->second.out ? EPOLLOUT : 0) | EPOLLERR;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ev.data.fd, &ev2);
        }
    }
}
