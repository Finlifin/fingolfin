#include "fingolfin/vm.h"
#include "fingolfin/elf.h"
#include <stdalign.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ubpf.h>
#ifdef FG_UBPF_SCRATCH
#if !defined(UBPF_FG_SCRATCH_API) || UBPF_FG_SCRATCH_API != 1
#error "Apply integration/ubpf/patches/safe-scratch-v1.patch first"
#endif
#endif
struct Call {
    FgHost *host;
    FgStatus error;
    uint32_t helper_mask;
    max_align_t alignment;
    unsigned char data[];
};
struct FgVm {
    struct ubpf_vm *machine;
    struct Call *call;
#ifdef FG_UBPF_SCRATCH
    struct ubpf_safe_scratch *scratch;
#endif
    size_t capacity;
    atomic_flag busy;
};
/* Execution failures are returned structurally, never synchronously printed on
 * a cmodel hook. The upstream callback has no per-invocation diagnostic cookie. */
static int quiet_error(FILE *stream, const char *format, ...) {
    (void)stream;
    (void)format;
    return 0;
}
static uint64_t dispatch(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, unsigned id,
                         void *cookie) {
    struct Call *call = (struct Call *)((unsigned char *)cookie - offsetof(struct Call, data));
    if (call->error != FG_STATUS_OK)
        return 1;
    if (id < 1 || id > 4 || !(call->helper_mask & FG_HELPER_MASK(id)))
        call->error = FG_STATUS_INVALID;
    else {
        const uint64_t args[5] = {a, b, c, d, e};
        call->error = fg_host_invoke(call->host, id, args);
    }
    return call->error == FG_STATUS_OK ? 0 : 1;
}
static bool valid(unsigned id, const struct ubpf_vm *vm) {
    (void)vm;
    return id >= 1 && id <= 4;
}
/* Safe metadata requires a registered function. Execution always uses dispatch;
 * no function-pointer ABI casts and no dependency on the legacy sixth argument. */
static uint64_t unavailable(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    return 1;
}
void fg_vm_release(FgVm *vm) {
    if (vm) {
        if (vm->machine)
            ubpf_destroy(vm->machine);
#ifdef FG_UBPF_SCRATCH
        ubpf_safe_scratch_destroy(vm->scratch);
#endif
        free(vm->call);
        free(vm);
    }
}
FgStatus fg_vm_load_elf(const void *elf, size_t elf_bytes, const FgVmOptions *o, FgVm **out,
                        char *error, size_t error_bytes) {
    if (error && error_bytes)
        error[0] = 0;
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!elf || !elf_bytes || !o || o->version != FG_VM_OPTIONS_VERSION ||
        o->struct_bytes != sizeof(*o) || !o->instruction_limit || !o->frame_capacity ||
        (o->helper_mask & ~FG_HELPERS_ALL) || (!error && error_bytes))
        return FG_STATUS_INVALID;
    FgElfInfo info;
    FgStatus checked = fg_elf_check((FgBytes){elf, elf_bytes}, &info, error, error_bytes);
    if (checked != FG_STATUS_OK)
        return checked;
    if (info.helper_mask & ~o->helper_mask) {
        if (error && error_bytes)
            snprintf(error, error_bytes, "ELF helper is not authorized");
        return FG_STATUS_INVALID;
    }
    size_t bytes;
    FgStatus status = fg_size_add(sizeof(struct Call), o->frame_capacity, &bytes);
    if (status != FG_STATUS_OK)
        return status;
    FgVm *vm = malloc(sizeof(*vm));
    if (!vm)
        return FG_STATUS_NOMEM;
    *vm = (FgVm){.capacity = o->frame_capacity, .busy = ATOMIC_FLAG_INIT};
    vm->call = calloc(1, bytes);
    vm->machine = ubpf_create();
#ifdef FG_UBPF_SCRATCH
    vm->scratch = ubpf_safe_scratch_create();
    if (!vm->scratch) {
        fg_vm_release(vm);
        return FG_STATUS_NOMEM;
    }
#endif
    char *detail = NULL;
    void *owned_elf = NULL;
    if (!vm->call || !vm->machine) {
        status = FG_STATUS_NOMEM;
        goto fail;
    }
    ubpf_set_error_print(vm->machine, quiet_error);
    ubpf_toggle_undefined_behavior_check(vm->machine, true);
    vm->call->helper_mask = o->helper_mask;
    status = FG_STATUS_VM;
    if (ubpf_set_execution_profile(vm->machine, UBPF_EXECUTION_PROFILE_SAFE) ||
        ubpf_set_instruction_limit(vm->machine, o->instruction_limit, NULL))
        goto fail;
    const char *names[] = {"counter_add", "emit", "map_add", "histogram_add"};
    for (unsigned id = 1; id <= 4; id++)
        if (o->helper_mask & FG_HELPER_MASK(id)) {
            struct ubpf_safe_helper_descriptor descriptor = {.index = id,
                                                             .name = names[id - 1],
                                                             .fn = unavailable,
                                                             .result_kind =
                                                                 UBPF_SAFE_HELPER_RESULT_SCALAR};
            if (ubpf_register_safe_helper(vm->machine, &descriptor))
                goto fail;
        }
    /* Validate load against only the registered allowlist first. A dispatcher
     * validator has no user context and must not widen that load-time check. */
    /* The byte-view validator accepts unaligned buffers; upstream uses native
     * ELF struct pointers, so give it an aligned owned copy during loading. */
    owned_elf = malloc(elf_bytes);
    if (!owned_elf) {
        status = FG_STATUS_NOMEM;
        goto fail;
    }
    memcpy(owned_elf, elf, elf_bytes);
    int loaded = ubpf_load_elf_ex(vm->machine, owned_elf, elf_bytes, "entry", &detail);
    free(owned_elf);
    owned_elf = NULL;
    if (loaded)
        goto fail;
    if (ubpf_register_external_dispatcher(vm->machine, dispatch, valid))
        goto fail;
    free(detail);
    *out = vm;
    return FG_STATUS_OK;
fail:
    free(owned_elf);
    if (error && error_bytes)
        snprintf(error, error_bytes, "%s", detail ? detail : fg_status_string(status));
    free(detail);
    fg_vm_release(vm);
    return status;
}
FgStatus fg_vm_run(FgVm *vm, void *frame, size_t bytes, FgHost *host, FgVmResult *result) {
    if (!vm || !frame || !host || !result)
        return FG_STATUS_INVALID;
    if (bytes > vm->capacity)
        return FG_STATUS_CAPACITY;
    if (atomic_flag_test_and_set_explicit(&vm->busy, memory_order_acquire))
        return FG_STATUS_BUSY;
    struct Call *call = vm->call;
    call->host = host;
    call->error = FG_STATUS_OK;
    memcpy(call->data, frame, bytes);
    FgVmResult outcome = {0};
#ifdef FG_UBPF_SCRATCH
    outcome.vm_status =
        ubpf_exec_safe_scratch(vm->machine, call->data, bytes, &outcome.entry_status, vm->scratch);
#else
    outcome.vm_status = ubpf_exec(vm->machine, call->data, bytes, &outcome.entry_status);
#endif
    outcome.helper_status = call->error;
    memcpy(frame, call->data, bytes);
    call->host = NULL;
    *result = outcome;
    atomic_flag_clear_explicit(&vm->busy, memory_order_release);
    if (outcome.helper_status != FG_STATUS_OK)
        return outcome.helper_status;
    return outcome.vm_status ? FG_STATUS_VM : FG_STATUS_OK;
}
