#include <libco.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#define SYMBOL(name) "_" name
#else
#define SYMBOL(name) name
#endif

// Preserve our caller's registers, seed all eight ABI-preserved FP registers,
// switch coroutines, and copy their values on resumption to the output array.
asm(
    ".text\n"
    ".p2align 2\n"
    ".globl " SYMBOL("switch_registers") "\n"
    SYMBOL("switch_registers") ":\n"
    "stp x29, x30, [sp, #-96]!\n"
    "stp d8, d9, [sp, #16]\n"
    "stp d10, d11, [sp, #32]\n"
    "stp d12, d13, [sp, #48]\n"
    "stp d14, d15, [sp, #64]\n"
    "stp x19, x20, [sp, #80]\n"
    "mov x19, x2\n"
    "ldp d8, d9, [x1]\n"
    "ldp d10, d11, [x1, #16]\n"
    "ldp d12, d13, [x1, #32]\n"
    "ldp d14, d15, [x1, #48]\n"
    "bl " SYMBOL("co_switch") "\n"
    "stp d8, d9, [x19]\n"
    "stp d10, d11, [x19, #16]\n"
    "stp d12, d13, [x19, #32]\n"
    "stp d14, d15, [x19, #48]\n"
    "ldp d8, d9, [sp, #16]\n"
    "ldp d10, d11, [sp, #32]\n"
    "ldp d12, d13, [sp, #48]\n"
    "ldp d14, d15, [sp, #64]\n"
    "ldp x19, x20, [sp, #80]\n"
    "ldp x29, x30, [sp], #96\n"
    "ret\n"
);

void switch_registers(cothread_t handle, const uint64_t *input, uint64_t *output);

static cothread_t main_thread;
static unsigned completed;
static const uint64_t worker_values[8] = {11, 22, 33, 44, 55, 66, 77, 88};

static void check_registers(const uint64_t *expected, const uint64_t *actual)
{
    if (memcmp(expected, actual, 8 * sizeof(uint64_t)))
    {
        fputs("AArch64 coroutine corrupted d8-d15\n", stderr);
        exit(1);
    }
}

static void worker(void)
{
    uint64_t observed[8];
    for (;;)
    {
        switch_registers(main_thread, worker_values, observed);
        check_registers(worker_values, observed);
        ++completed;
    }
}

int main(void)
{
    const uint64_t main_values[8] = {101, 202, 303, 404, 505, 606, 707, 808};
    uint64_t observed[8];
    main_thread = co_active();
    cothread_t child = co_create(65536, worker);
    if (!child)
        return 1;
    for (unsigned i = 0; i < 200; ++i)
    {
        switch_registers(child, main_values, observed);
        check_registers(main_values, observed);
    }
    co_delete(child);
    if (completed != 199)
        return 1;
    puts("AArch64 coroutine preserved d8-d15 across 400 context switches");
    return 0;
}
