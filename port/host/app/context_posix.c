#include "context.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#if !defined(__aarch64__)
#include <ucontext.h>
#endif

enum { STACK_SIZE = 1024 * 1024 };
struct KhdaysContext {
#if defined(__aarch64__)
    uint64_t registers[22]; /* x19-x30, SP, FPCR, d8-d15 */
#else
    ucontext_t registers;
#endif
    void *stack;
    void (*release)(void *, size_t);
    void (*entry)(void *);
    void *argument;
};

static KhdaysContext *current;
static void *(*stack_alloc)(size_t);
static void (*stack_free)(void *, size_t);

void khdays_context_stack_source(void *(*allocate)(size_t), void (*release)(void *, size_t))
{
    stack_alloc = allocate;
    stack_free = release;
}

static void start(void)
{
    current->entry(current->argument);
    abort();
}

static void *guarded_stack(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    char *base = mmap(NULL, page + STACK_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return NULL;
    if (mprotect(base + page, STACK_SIZE, PROT_READ | PROT_WRITE) == 0)
        return base + page;
    munmap(base, page + STACK_SIZE);
    return NULL;
}

static void free_guarded(void *stack, size_t size)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    munmap((char *)stack - page, size + page);
}

KhdaysContext *khdays_context_init(void)
{
    if (!current)
        current = calloc(1, sizeof(*current));
    return current;
}

KhdaysContext *khdays_context_current(void)
{
    return current;
}

KhdaysContext *khdays_context_create(void (*entry)(void *), void *argument)
{
    KhdaysContext *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->stack = stack_alloc ? stack_alloc(STACK_SIZE) : guarded_stack();
    if (!c->stack) {
        free(c);
        return NULL;
    }
    c->release = stack_alloc ? stack_free : free_guarded;
    c->entry = entry;
    c->argument = argument;
#if defined(__aarch64__)
    c->registers[11] = (uintptr_t)start;
    c->registers[12] = ((uintptr_t)c->stack + STACK_SIZE) & ~(uintptr_t)15;
    __asm__("mrs %0, fpcr" : "=r"(c->registers[13]));
#else
    if (getcontext(&c->registers) != 0) {
        khdays_context_delete(c);
        return NULL;
    }
    c->registers.uc_stack.ss_sp = c->stack;
    c->registers.uc_stack.ss_size = STACK_SIZE;
    c->registers.uc_link = NULL;
    makecontext(&c->registers, start, 0);
#endif
    return c;
}

void khdays_context_switch(KhdaysContext *to)
{
    KhdaysContext *from = current;
    if (from == to)
        return;
    current = to;
#if defined(__aarch64__)
    extern void khdays_context_jump_arm64(uint64_t *, const uint64_t *);
    khdays_context_jump_arm64(from->registers, to->registers);
#else
    if (swapcontext(&from->registers, &to->registers) != 0)
        abort();
#endif
}

void khdays_context_delete(KhdaysContext *c)
{
    if (c->stack)
        c->release(c->stack, STACK_SIZE);
    if (current == c)
        current = NULL;
    free(c);
}
