#include "fingolfin/pool.h"
#include <pthread.h>
#include <stdlib.h>
struct Slot {
    FgVm *vm;
    FgVmLease *owner;
    uint64_t generation;
};
struct FgVmPool {
    pthread_mutex_t mutex;
    size_t count, active;
    int closed;
    struct Slot slots[];
};
static void dispose(FgVmPool *pool) {
    for (size_t i = 0; i < pool->count; i++)
        fg_vm_release(pool->slots[i].vm);
    pthread_mutex_destroy(&pool->mutex);
    free(pool);
}
FgStatus fg_vm_pool_create(const void *elf, size_t elf_bytes, const FgVmOptions *options,
                           size_t slots, FgVmPool **out, char *diagnostic,
                           size_t diagnostic_bytes) {
    if (diagnostic && diagnostic_bytes)
        diagnostic[0] = 0;
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!slots || slots > FG_VM_POOL_MAX_SLOTS || (!diagnostic && diagnostic_bytes))
        return FG_STATUS_INVALID;
    FgVmPool *pool = calloc(1, sizeof(*pool) + slots * sizeof(struct Slot));
    if (!pool)
        return FG_STATUS_NOMEM;
    if (pthread_mutex_init(&pool->mutex, NULL)) {
        free(pool);
        return FG_STATUS_NOMEM;
    }
    pool->count = slots;
    for (size_t i = 0; i < slots; i++) {
        FgStatus status = fg_vm_load_elf(elf, elf_bytes, options, &pool->slots[i].vm, diagnostic,
                                         diagnostic_bytes);
        if (status != FG_STATUS_OK) {
            dispose(pool);
            return status;
        }
    }
    *out = pool;
    return FG_STATUS_OK;
}
FgStatus fg_vm_pool_acquire(FgVmPool *pool, FgVmLease *lease) {
    if (!pool || !lease || lease->pool)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&pool->mutex);
    FgStatus status = pool->closed ? FG_STATUS_CLOSED : FG_STATUS_BUSY;
    if (!pool->closed) {
        for (size_t i = 0; i < pool->count; i++) {
            struct Slot *slot = &pool->slots[i];
            /* Retire exhausted generations instead of admitting stale tokens. */
            if (!slot->owner && slot->generation != UINT64_MAX) {
                slot->generation++;
                slot->owner = lease;
                pool->active++;
                *lease = (FgVmLease){pool, i, slot->generation};
                status = FG_STATUS_OK;
                break;
            }
        }
    }
    pthread_mutex_unlock(&pool->mutex);
    return status;
}
/* Caller owns the lease exclusively. Other slots can run/release concurrently. */
static struct Slot *owned_slot(FgVmLease *lease) {
    if (!lease || !lease->pool || lease->slot >= lease->pool->count)
        return NULL;
    struct Slot *slot = &lease->pool->slots[lease->slot];
    return slot->owner == lease && slot->generation == lease->generation ? slot : NULL;
}
FgStatus fg_vm_lease_run(FgVmLease *lease, void *frame, size_t frame_bytes, FgHost *host,
                         FgVmResult *result) {
    if (!lease || !lease->pool)
        return FG_STATUS_INVALID;
    FgVmPool *pool = lease->pool;
    pthread_mutex_lock(&pool->mutex);
    struct Slot *slot = owned_slot(lease);
    FgVm *vm = slot ? slot->vm : NULL;
    pthread_mutex_unlock(&pool->mutex);
    return vm ? fg_vm_run(vm, frame, frame_bytes, host, result) : FG_STATUS_INVALID;
}
FgStatus fg_vm_lease_release(FgVmLease *lease) {
    if (!lease || !lease->pool)
        return FG_STATUS_INVALID;
    FgVmPool *pool = lease->pool;
    pthread_mutex_lock(&pool->mutex);
    struct Slot *slot = owned_slot(lease);
    if (!slot) {
        pthread_mutex_unlock(&pool->mutex);
        return FG_STATUS_INVALID;
    }
    slot->owner = NULL;
    pool->active--;
    *lease = (FgVmLease){0};
    pthread_mutex_unlock(&pool->mutex);
    return FG_STATUS_OK;
}
FgStatus fg_vm_pool_close(FgVmPool *pool) {
    if (!pool)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&pool->mutex);
    pool->closed = 1;
    pthread_mutex_unlock(&pool->mutex);
    return FG_STATUS_OK;
}
int fg_vm_pool_closed_and_drained(FgVmPool *pool) {
    if (!pool)
        return 0;
    pthread_mutex_lock(&pool->mutex);
    int drained = pool->closed && !pool->active;
    pthread_mutex_unlock(&pool->mutex);
    return drained;
}
FgStatus fg_vm_pool_destroy(FgVmPool *pool) {
    if (!pool)
        return FG_STATUS_INVALID;
    if (!fg_vm_pool_closed_and_drained(pool))
        return FG_STATUS_BUSY;
    dispose(pool);
    return FG_STATUS_OK;
}
