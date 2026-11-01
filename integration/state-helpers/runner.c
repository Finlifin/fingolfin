#include "frame.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ubpf.h>
struct State {
    uint64_t counter;
    unsigned count;
    struct {
        uint32_t tag;
        uint64_t value;
    } output[2];
};
struct Call {
    struct FgBpfFrame frame;
    struct State *state;
};
static bool valid(unsigned id, const struct ubpf_vm *vm) {
    (void)vm;
    return id == 1 || id == 2;
}
static uint64_t dispatch(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, unsigned id,
                         void *cookie) {
    (void)c;
    (void)d;
    (void)e;
    struct Call *call = cookie;
    struct State *s = call->state;
    if (id == 1) {
        if (a != 0 || UINT64_MAX - s->counter < b)
            return 1;
        s->counter += b;
        return 0;
    }
    if (id == 2) {
        if (a > UINT32_MAX || s->count == 2)
            return 1;
        s->output[s->count].tag = (uint32_t)a;
        s->output[s->count++].value = b;
        return 0;
    }
    return 1;
}
static uint64_t counter_helper(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                               void *cookie) {
    return dispatch(a, b, c, d, e, 1, cookie);
}
static uint64_t emit_helper(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                            void *cookie) {
    return dispatch(a, b, c, d, e, 2, cookie);
}
static void invoke(struct ubpf_vm *vm, struct State *state, uint32_t slot, uint64_t amount,
                   unsigned expected) {
    struct Call call = {.frame = {.steps = 10000, .max_depth = 64, .arg0 = slot, .arg1 = amount},
                        .state = state};
    uint64_t result = 0;
    assert(!ubpf_exec(vm, &call.frame, sizeof(call.frame), &result));
    assert(result == expected);
    assert(call.frame.error == expected);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long size = ftell(f);
    assert(size > 0);
    rewind(f);
    void *elf = malloc((size_t)size);
    assert(elf);
    assert(fread(elf, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    struct ubpf_vm *vm = ubpf_create();
    assert(vm);
    assert(!ubpf_set_execution_profile(vm, UBPF_EXECUTION_PROFILE_SAFE));
    assert(!ubpf_set_instruction_limit(vm, 10000, NULL));
    char *error = NULL;
    assert(ubpf_load_elf_ex(vm, elf, (size_t)size, "entry", &error) != 0);
    free(error);
    error = NULL;
    struct ubpf_safe_helper_descriptor counter = {.index = 1,
                                                  .name = "counter_add",
                                                  .fn = as_external_function_t(counter_helper),
                                                  .result_kind = UBPF_SAFE_HELPER_RESULT_SCALAR};
    struct ubpf_safe_helper_descriptor emit = {.index = 2,
                                               .name = "emit",
                                               .fn = as_external_function_t(emit_helper),
                                               .result_kind = UBPF_SAFE_HELPER_RESULT_SCALAR};
    assert(!ubpf_register_safe_helper(vm, &counter));
    assert(!ubpf_register_safe_helper(vm, &emit));
    assert(!ubpf_register_external_dispatcher(vm, dispatch, valid));
    int loaded = ubpf_load_elf_ex(vm, elf, (size_t)size, "entry", &error);
    if (loaded)
        fprintf(stderr, "%s\n", error);
    assert(!loaded);
    free(error);
    free(elf);
    struct State a = {0}, b = {.counter = 100}, overflow = {.counter = UINT64_MAX};
    invoke(vm, &a, 0, 3, 0);
    invoke(vm, &b, 0, 9, 0);
    invoke(vm, &a, 0, 5, 0);
    assert(a.counter == 8 && a.count == 2 && a.output[0].tag == 7 && a.output[0].value == 3 &&
           a.output[1].value == 5);
    assert(b.counter == 109 && b.count == 1 && b.output[0].value == 9);
    invoke(vm, &a, 0, 1, 6);
    assert(a.counter == 9 && a.count == 2);
    invoke(vm, &overflow, 0, 1, 6);
    assert(overflow.counter == UINT64_MAX && overflow.count == 0);
    invoke(vm, &b, 1, 1, 6);
    assert(b.counter == 109 && b.count == 1);
    ubpf_destroy(vm);
    puts("PASS: missing-helper rejection, counter/emit, per-invocation state isolation, capacity, "
         "overflow, unknown counter");
    return 0;
}
