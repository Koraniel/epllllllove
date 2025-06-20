#include "fibers.hpp"

// Forward declaration of the trampoline function implemented in epoll.cpp.
void trampoline(Fiber *fiber);

StackPool stack_pool;
thread_local Context* current_ctx = nullptr;
thread_local Action current_action;
thread_local Context scheduler_main_ctx;

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
