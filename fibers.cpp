#include "fibers.hpp"

// Forward declaration of the trampoline function implemented in epoll.cpp.
void trampoline(Fiber *fiber);

StackPool stack_pool;
static thread_local Context* current_ctx = nullptr;
static thread_local Action current_action;

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
    current_action = action;
    Context* prev = current_ctx;
    current_ctx = this;
    if (prev) {
        if (swapcontext(&prev->ctx, &ctx) != 0) {
            throw std::runtime_error("swapcontext failed");
        }
    } else {
        if (setcontext(&ctx) != 0) {
            throw std::runtime_error("setcontext failed");
        }
    }
    return current_action;
}
