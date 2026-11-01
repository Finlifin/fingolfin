#ifndef FG_POOL_H
#define FG_POOL_H
#include "vm.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_VM_POOL_MAX_SLOTS 64u
typedef struct FgVmPool FgVmPool;
/* Linear, caller-owned token. Zero-initialize before first acquire. Do not copy,
 * move or concurrently access an acquired lease; release before its storage dies.
 * Private fields must not be modified. A lease pins one exclusive VM slot. */
typedef struct FgVmLease {
    FgVmPool *pool;
    size_t slot;
    uint64_t generation;
} FgVmLease;
/* Control path: load one independent VM per slot, all-or-nothing. No input
 * pointers retained. Host state is supplied per run and synchronized by caller. */
FgStatus fg_vm_pool_create(const void *elf, size_t elf_bytes, const FgVmOptions *options,
                           size_t slots, FgVmPool **out, char *diagnostic, size_t diagnostic_bytes);
/* Acquire never waits for an execution slot or allocates. Short metadata mutex
 * may contend. Full pool returns BUSY; closed pool returns CLOSED. */
FgStatus fg_vm_pool_acquire(FgVmPool *pool, FgVmLease *lease);
FgStatus fg_vm_lease_run(FgVmLease *lease, void *frame, size_t frame_bytes, FgHost *host,
                         FgVmResult *result);
FgStatus fg_vm_lease_release(FgVmLease *lease);
/* Close rejects new leases; existing leases remain runnable until release. */
FgStatus fg_vm_pool_close(FgVmPool *pool);
int fg_vm_pool_closed_and_drained(FgVmPool *pool);
/* Requires external exclusion of all API users, including new callers. Returns
 * BUSY until closed and drained. Successful destroy invalidates pool pointer. */
FgStatus fg_vm_pool_destroy(FgVmPool *pool);
#ifdef __cplusplus
}
#endif
#endif
