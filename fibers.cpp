#include "fibers.hpp"

StackPool stack_pool;
static thread_local Context* current_ctx = nullptr;
static thread_local Action current_action;

Context::Context(Fiber fiber)
        : fiber(std::make_unique<Fiber>(std::move(fiber))),
          stack(stack_pool.alloc()),
          rsp(reinterpret_cast<intptr_t>(stack.ptr) + StackPool::STACK_SIZE) {
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
