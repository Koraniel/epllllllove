#include "fibers.hpp"

// Forward declaration of the trampoline function implemented in epoll.cpp.
void trampoline(Fiber *fiber);

StackPool stack_pool;

// Thread local pointer to the currently running context.  It is used by
// Context::switch_context to know from which context we are switching.
thread_local Context* current_ctx = nullptr;

// Thread local storage for the action being transferred between contexts.
thread_local Action current_action{};

Context::Context(Fiber fiber)
        : fiber(std::make_unique<Fiber>(std::move(fiber))),
          stack(stack_pool.alloc()) {
    getcontext(&uc);
    uc.uc_stack.ss_sp = stack.ptr;
    uc.uc_stack.ss_size = StackPool::STACK_SIZE;
    uc.uc_link = nullptr;
    // The trampoline will start execution of the stored fiber.
    makecontext(&uc, (void (*)())trampoline, 1, this->fiber.get());
}

Action Context::switch_context(Action action) {
    // Save action that should be passed to the target context.
    current_action = action;

    Context* prev = current_ctx;
    current_ctx = this;

    swapcontext(&prev->uc, &uc);

    // When we are resumed, current_action contains the value passed by the
    // context that resumed us.
    Action ret = current_action;
    current_ctx = prev;
    return ret;
}
