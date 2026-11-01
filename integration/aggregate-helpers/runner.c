#include "frame.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ubpf.h>
struct State {
    unsigned used;
    struct {
        uint64_t key, value;
    } map[2];
    uint64_t counts[3], total;
};
struct Call {
    struct FgBpfFrame frame;
    struct State *state;
};
static bool valid(unsigned id, const struct ubpf_vm *vm) {
    (void)vm;
    return id == 3 || id == 4;
}
static uint64_t dispatch(uint64_t slot, uint64_t key, uint64_t weight, uint64_t d, uint64_t e,
                         unsigned id, void *cookie) {
    (void)d;
    (void)e;
    struct State *s = ((struct Call *)cookie)->state;
    if (slot)
        return 1;
    if (id == 3) {
        unsigned i = 0;
        while (i < s->used && s->map[i].key != key)
            ++i;
        if (i == s->used) {
            if (i == 2)
                return 1;
            s->map[i].key = key;
            s->map[i].value = 0;
            ++s->used;
        }
        if (UINT64_MAX - s->map[i].value < weight)
            return 1;
        s->map[i].value += weight;
        return 0;
    }
    if (id == 4) {
        int64_t sample = (int64_t)key;
        unsigned i = sample < 0 ? 0 : (sample < 10 ? 1 : 2);
        if (UINT64_MAX - s->counts[i] < weight || UINT64_MAX - s->total < weight)
            return 1;
        s->counts[i] += weight;
        s->total += weight;
        return 0;
    }
    return 1;
}
static uint64_t map_helper(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                           void *cookie) {
    return dispatch(a, b, c, d, e, 3, cookie);
}
static uint64_t histogram_helper(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                                 void *cookie) {
    return dispatch(a, b, c, d, e, 4, cookie);
}
static void invoke(struct ubpf_vm *vm, struct State *state, uint64_t key, int64_t sample,
                   uint64_t weight, unsigned expected) {
    struct Call call = {
        .frame = {.steps = 10000, .max_depth = 64, .arg0 = key, .arg1 = sample, .arg2 = weight},
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
    struct ubpf_safe_helper_descriptor map = {.index = 3,
                                              .name = "map_add",
                                              .fn = as_external_function_t(map_helper),
                                              .result_kind = UBPF_SAFE_HELPER_RESULT_SCALAR};
    struct ubpf_safe_helper_descriptor histogram = {.index = 4,
                                                    .name = "histogram_add",
                                                    .fn = as_external_function_t(histogram_helper),
                                                    .result_kind = UBPF_SAFE_HELPER_RESULT_SCALAR};
    assert(!ubpf_register_safe_helper(vm, &map));
    assert(!ubpf_register_safe_helper(vm, &histogram));
    assert(!ubpf_register_external_dispatcher(vm, dispatch, valid));
    int loaded = ubpf_load_elf_ex(vm, elf, (size_t)size, "entry", &error);
    if (loaded)
        fprintf(stderr, "%s\n", error);
    assert(!loaded);
    free(error);
    free(elf);
    struct State a = {0}, b = {0}, overflow = {.counts = {UINT64_MAX, 0, 0}, .total = UINT64_MAX};
    invoke(vm, &a, 7, INT64_MIN, 2, 0);
    invoke(vm, &b, 7, INT64_MAX, 9, 0);
    invoke(vm, &a, 9, 0, 3, 0);
    invoke(vm, &a, 7, 10, 4, 0);
    assert(a.used == 2 && a.map[0].value == 6 && a.map[1].value == 3 && a.total == 9 &&
           a.counts[0] == 2 && a.counts[1] == 3 && a.counts[2] == 4);
    assert(b.used == 1 && b.map[0].value == 9 && b.total == 9 && b.counts[2] == 9);
    invoke(vm, &a, 11, 0, 1, 6);
    assert(a.total == 9 && a.used == 2);
    invoke(vm, &a, 7, 0, UINT64_MAX, 6);
    assert(a.total == 9 && a.map[0].value == 6);
    invoke(vm, &overflow, 7, 0, 1, 6);
    assert(overflow.total == UINT64_MAX && overflow.counts[1] == 0 && overflow.map[0].value == 1);
    ubpf_destroy(vm);
    puts("PASS: map/histogram helpers, signed boundaries, instance isolation, capacity, "
         "counter/total overflow");
    return 0;
}
