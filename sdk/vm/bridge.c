#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ubpf.h>
typedef uint64_t (*callback)(void *, uint32_t, const uint64_t *);
struct call {
    callback invoke;
    void *user;
    max_align_t alignment;
    unsigned char data[];
};
struct bridge {
    struct ubpf_vm *vm;
    struct call *call;
    size_t capacity;
};
static uint64_t dispatch(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, unsigned id,
                         void *cookie) {
    struct call *call = (struct call *)((unsigned char *)cookie - offsetof(struct call, data));
    const uint64_t args[5] = {a, b, c, d, e};
    return call->invoke(call->user, id, args);
}
static bool valid(unsigned id, const struct ubpf_vm *vm) {
    (void)vm;
    return id >= 1 && id <= 4;
}
#define HELPER(N)                                                                                  \
    static uint64_t h##N(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,               \
                         void *cookie) {                                                           \
        return dispatch(a, b, c, d, e, N, cookie);                                                 \
    }
HELPER(1) HELPER(2) HELPER(3) HELPER(4) void fg_vm_destroy(struct bridge *b) {
    if (b) {
        if (b->vm)
            ubpf_destroy(b->vm);
        free(b->call);
        free(b);
    }
}
struct bridge *fg_vm_create(const void *elf, size_t len, uint32_t limit, size_t capacity,
                            char *error, size_t error_len) {
    struct bridge *b = calloc(1, sizeof(*b));
    char *detail = NULL;
    if (!b)
        goto fail;
    b->vm = ubpf_create();
    if (capacity > SIZE_MAX - sizeof(struct call))
        goto fail;
    b->call = calloc(1, sizeof(struct call) + capacity);
    b->capacity = capacity;
    if (!b->vm || !b->call || !limit)
        goto fail;
    if (ubpf_set_execution_profile(b->vm, UBPF_EXECUTION_PROFILE_SAFE) ||
        ubpf_set_instruction_limit(b->vm, limit, NULL))
        goto fail;
    external_function_t functions[] = {as_external_function_t(h1), as_external_function_t(h2),
                                       as_external_function_t(h3), as_external_function_t(h4)};
    const char *names[] = {"counter_add", "emit", "map_add", "histogram_add"};
    for (unsigned i = 0; i < 4; i++) {
        struct ubpf_safe_helper_descriptor descriptor = {.index = i + 1,
                                                         .name = names[i],
                                                         .fn = functions[i],
                                                         .result_kind =
                                                             UBPF_SAFE_HELPER_RESULT_SCALAR};
        if (ubpf_register_safe_helper(b->vm, &descriptor))
            goto fail;
    }
    if (ubpf_register_external_dispatcher(b->vm, dispatch, valid) ||
        ubpf_load_elf_ex(b->vm, elf, len, "entry", &detail))
        goto fail;
    free(detail);
    return b;
fail:
    snprintf(error, error_len, "%s", detail ? detail : "VM allocation or configuration failed");
    free(detail);
    fg_vm_destroy(b);
    return NULL;
}
int fg_vm_execute(struct bridge *b, void *frame, size_t len, callback invoke, void *user,
                  uint64_t *result) {
    if (len > b->capacity)
        return -2;
    b->call->invoke = invoke;
    b->call->user = user;
    memcpy(b->call->data, frame, len);
    int status = ubpf_exec(b->vm, b->call->data, len, result);
    memcpy(frame, b->call->data, len);
    b->call->invoke = NULL;
    b->call->user = NULL;
    return status;
}
