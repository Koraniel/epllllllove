#include "async.hpp"

namespace Async {
class AcceptInspector : public Inspector {
public:
    void operator()(Action &act, Context &context) override {
        EpollScheduler::current_scheduler->await_accept(std::move(context), context.yield_data);
        act.action = Action::STOP;
    }
};

int accept(int fd, sockaddr *addr, socklen_t *addrlen) {
    if (!EpollScheduler::current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    AcceptData data{fd, addr, addrlen};
    create_current_fiber_inspector<AcceptInspector>();
    auto res = EpollScheduler::current_scheduler->yield({.ptr=&data});
    return res.i;
}

class ReadInspector : public Inspector {
    void operator()(Action &act, Context &context) override {
        EpollScheduler::current_scheduler->await_read(std::move(context), context.yield_data);
        act.action = Action::STOP;
    }
};

ssize_t read(int fd, char *buf, size_t size) {
    if (!EpollScheduler::current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    ReadData data{fd, buf, size};
    create_current_fiber_inspector<ReadInspector>();
    auto res = EpollScheduler::current_scheduler->yield({.ptr=&data});
    return res.ss;
}

class WriteInspector : public Inspector {
    void operator()(Action &act, Context &context) override {
        EpollScheduler::current_scheduler->await_write(std::move(context), context.yield_data);
        act.action = Action::STOP;
    }
};

ssize_t write(int fd, const char *buf, size_t size) {
    if (!EpollScheduler::current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    WriteData data{fd, buf, size};
    create_current_fiber_inspector<WriteInspector>();
    auto res = EpollScheduler::current_scheduler->yield({.ptr=&data});
    return res.ss;
}
}  // namespace Async

