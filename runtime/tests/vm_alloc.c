#include "fingolfin/vm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
/* Link-time interception covers runtime + statically linked uBPF, including
 * unsuccessful allocation attempts. Not an audit of shared libc internals. */
static int tracking;
static size_t allocations, frees;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__real_aligned_alloc(size_t, size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n) {
    if (tracking)
        allocations++;
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t s) {
    if (tracking)
        allocations++;
    return __real_calloc(n, s);
}
void *__wrap_realloc(void *p, size_t n) {
    if (tracking)
        allocations++;
    return __real_realloc(p, n);
}
void *__wrap_aligned_alloc(size_t a, size_t n) {
    if (tracking)
        allocations++;
    return __real_aligned_alloc(a, n);
}
void __wrap_free(void *p) {
    if (tracking)
        frees++;
    __real_free(p);
}
struct Frame {
    uint64_t steps;
    uint32_t depth, error, slot;
    uint64_t amount;
};
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long n = ftell(f);
    assert(n > 0);
    rewind(f);
    void *elf = malloc((size_t)n);
    assert(elf);
    assert(fread(elf, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    FgVmOptions options = {1, sizeof(options), 10000, sizeof(struct Frame), 6};
    FgVm *vm = NULL, *limited = NULL;
    char diagnostic[256];
    assert(fg_vm_load_elf(elf, (size_t)n, &options, &vm, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    options.instruction_limit = 1;
    assert(fg_vm_load_elf(elf, (size_t)n, &options, &limited, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    free(elf);
    FgCounterConfig counter = {0, 0};
    FgHostConfig config = {.version = 1,
                           .struct_bytes = sizeof(config),
                           .counters = &counter,
                           .counter_count = 1,
                           .output_capacity = 1};
    FgHost *host = NULL;
    assert(fg_host_create(&config, NULL, &host) == FG_STATUS_OK);
    tracking = 1;
    for (size_t i = 0; i < 10000; i++) {
        struct Frame frame = {.steps = 10000, .depth = 64, .amount = 1};
        FgVmResult result;
        if (i % 4 == 1)
            frame.slot = 99;
        if (i % 4 == 2)
            frame.steps = 0;
        size_t bytes = i % 4 == 3 ? sizeof(frame) - 1 : sizeof(frame);
        FgStatus status = fg_vm_run(vm, &frame, bytes, host, &result);
        if (i % 4 == 0) {
            assert(status == FG_STATUS_OK && !result.entry_status);
            FgRecord record;
            assert(fg_host_pop(host, &record) == FG_STATUS_OK && record.value == 1);
        } else if (i % 4 == 1)
            assert(status == FG_STATUS_NOT_FOUND && result.helper_status == FG_STATUS_NOT_FOUND);
        else
            assert(status == FG_STATUS_OK && result.entry_status);
        frame = (struct Frame){.steps = 10000, .depth = 64, .amount = 1};
        assert(fg_vm_run(limited, &frame, sizeof(frame), host, &result) == FG_STATUS_VM);
    }
    tracking = 0;
#ifdef FG_EXPECT_ALLOCATIONS
    assert(allocations && frees);
#else
    assert(!allocations && !frees);
#endif
    uint64_t value;
    assert(fg_host_counter(host, 0, &value) == FG_STATUS_OK && value == 2500);
    fg_vm_release(vm);
    fg_vm_release(limited);
    fg_host_destroy(host);
    printf("allocation attempts: %zu; frees: %zu\n", allocations, frees);
    puts("20000 VM runs: success/helper failure/frame budget/size/instruction failure");
    return 0;
}
