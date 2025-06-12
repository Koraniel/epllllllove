#include <functional>
#include <cinttypes>
#include <stdexcept>
#include <memory>
#include <optional>
#include <ucontext.h>

#include "stack_pool.hpp"
#include <ucontext.h>


using Fiber = std::function<void()>;

union YieldData {
    void * ptr = {};
    int32_t i;
    uint32_t u;
    size_t s;
    ssize_t ss;
};
static_assert(sizeof(YieldData) <= sizeof(void*));


struct Action {
    enum {
        /// Scheduler -> Fiber
        START,
        THROW,

        /// Fiber -> Scheduler
        STOP,
        SCHED,
    } action;

    YieldData user_data{};
};


class Inspector;

struct Context {
    std::unique_ptr<Fiber> fiber;
    StackPool::Stack stack;
    ucontext_t ctx{};

    intptr_t rip = 0;
    intptr_t rsp = 0;
    std::shared_ptr<Inspector> inspector;
    std::exception_ptr exception{};
    YieldData yield_data = {};

    Context() = default;

    explicit Context(Fiber fiber);

    Context(Context &&other) = default;

    Context(const Context &other) = delete;

    Context &operator=(Context &&other) = default;

    void operator=(const Context &other) = delete;

    /// swap current rip and rsp
    Action switch_context(Action);
};

class Inspector {
public:
    virtual ~Inspector() = default;

    /// Inspect context after execution
    virtual void operator()(Action &, Context &) = 0;
};


/// Pointer to currently running context
extern thread_local Context* current_ctx;

/// Last action passed to Context::switch_context
extern thread_local Action current_action;

/// Scheduler main context used for switching back from fibers
extern thread_local Context scheduler_main_ctx;
