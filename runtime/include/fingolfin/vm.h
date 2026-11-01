#ifndef FG_VM_H
#define FG_VM_H
#include "host.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_VM_OPTIONS_VERSION 1u
#define FG_HELPER_MASK(ID) (UINT32_C(1) << (ID))
#define FG_HELPERS_ALL UINT32_C(0x1e)
typedef struct FgVm FgVm;
typedef struct FgVmOptions {
    uint32_t version;
    size_t struct_bytes;
    uint32_t instruction_limit;
    size_t frame_capacity;
    uint32_t helper_mask;
} FgVmOptions;
typedef struct FgVmResult {
    uint64_t entry_status;
    int vm_status;
    FgStatus helper_status;
} FgVmResult;
/* Development relocation-free ELF entry loader with structural/allowlist checks.
 * No package/schema/authenticity claims. Options
 * and ELF are copied during load; diagnostic is optional and always terminated.
 * Per-VM exclusive execution. Host synchronization belongs to its owner. */
FgStatus fg_vm_load_elf(const void *elf, size_t elf_bytes, const FgVmOptions *options, FgVm **out,
                        char *diagnostic, size_t diagnostic_bytes);
/* Caller must stop new runs and drain all in-flight users before release. */
void fg_vm_release(FgVm *vm);
/* Nonzero generated entry_status is distinct from runtime/helper failure.
 * Completed helper effects and copied-back frame survive execution errors.
 * Same VM concurrent/reentrant execution returns BUSY without touching frame. */
FgStatus fg_vm_run(FgVm *vm, void *frame, size_t frame_bytes, FgHost *host, FgVmResult *result);
#ifdef __cplusplus
}
#endif
#endif
