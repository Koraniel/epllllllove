#include "epoll.hpp"

EpollScheduler* EpollScheduler::current_scheduler = nullptr;

void trampoline(Fiber *fiber) {
    // Execute the fiber. In this simplified implementation we ignore
    // exceptions.
    (*fiber)();

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

template <class Inspector, class... Args>
void create_current_fiber_inspector(Args... args) {
    if (!EpollScheduler::current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    EpollScheduler::current_scheduler->create_current_fiber_inspector<Inspector>(args...);
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

    // Prepare ucontext for the fiber so that it starts execution in the
    // trampoline which in turn will call the stored callable.
    getcontext(&context.uc);
    context.uc.uc_stack.ss_sp = context.stack.ptr;
    context.uc.uc_stack.ss_size = StackPool::STACK_SIZE;
    context.uc.uc_link = nullptr;
    makecontext(&context.uc, (void (*)())trampoline, 1, context.fiber.get());

    return context;
}

YieldData FiberScheduler::yield(YieldData data) {
    auto *sched = EpollScheduler::current_scheduler;
    if (!sched) {
        throw std::runtime_error("Global scheduler is empty");
    }

    // Store the data that the fiber wants to pass to the scheduler and switch
    // to the scheduler context.  The returned action may carry data back.
    current_action.user_data = data;
    Action act = sched->sched_context.switch_context({Action::SCHED});
    return act.user_data;
}

void FiberScheduler::run_one() {
    Context ctx = std::move(queue.front());
    queue.pop();

    // Set the current context pointer to the scheduler context so that the
    // running fiber can switch back to us.
    current_ctx = &sched_context;

    Action to_fiber;
    if (sched_context.exception) {
        to_fiber.action = Action::THROW;
    } else {
        to_fiber.action = Action::START;
    }

    Action result = ctx.switch_context(to_fiber);

    if (ctx.inspector) {
        (*ctx.inspector)(result, ctx);
    }

    if (result.action == Action::SCHED) {
        schedule(std::move(ctx));
    } else if (result.action == Action::THROW) {
        // Unused in this simplified version
    }

    current_ctx = nullptr;
}

void EpollScheduler::await_read(Context context, YieldData data) {
    /// Subscribe epoll
    auto fd = static_cast<ReadData *>(data.ptr)->fd;
    auto &elem = wait_list[fd];
    /// TODO
}

void EpollScheduler::do_read(Node node) {
    /// call read
    /// schedule fiber
    /// TODO
}

void EpollScheduler::await_write(Context context, YieldData data) {
    auto fd = static_cast<WriteData *>(data.ptr)->fd;
    auto &elem = wait_list[fd];
    /// TODO
}

void EpollScheduler::do_write(Node node) {
    /// TODO
}

void EpollScheduler::await_accept(Context context, YieldData data) {
    auto fd = static_cast<AcceptData *>(data.ptr)->fd;
    auto &elem = wait_list[fd];
    /// TODO
}

void EpollScheduler::do_accept(Node node) {
    /// TODO
}

void EpollScheduler::do_error(Node node) {
    /// "throw" exception in context
    /// TODO
}

void EpollScheduler::run() {
    while (true) {
        while (!empty()) {
            run_one();
        }
        if (wait_list.empty()) {
            break;
        }
        /// Wait any fd
        /// If error do_error
        /// Else if in or out process it
        /// TODO
    }
}
