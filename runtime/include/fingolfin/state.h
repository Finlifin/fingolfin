#ifndef FG_STATE_H
#define FG_STATE_H
#include "containers.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Identities, never native addresses. Configuration/object generations isolate
 * reused slots. No implicit migration; clean old state only after leases drain. */
typedef struct FgStateKey {
    uint64_t program, configuration, run, launch, slot, generation;
} FgStateKey;
typedef struct FgStateStore FgStateStore;
FgStatus fg_state_create(size_t capacity, size_t value_size, size_t value_alignment,
                         const FgAllocator *allocator, FgStateStore **out);
void fg_state_destroy(FgStateStore *store);
size_t fg_state_length(const FgStateStore *store);
FgStatus fg_state_put(FgStateStore *store, const FgStateKey *key, const void *value, int *inserted);
FgStatus fg_state_find(FgStateStore *store, const FgStateKey *key, void **value);
FgStatus fg_state_remove(FgStateStore *store, const FgStateKey *key, void *old_value);
size_t fg_state_remove_program(FgStateStore *store, uint64_t program);
size_t fg_state_remove_configuration(FgStateStore *store, uint64_t program, uint64_t configuration);
size_t fg_state_remove_object(FgStateStore *store, uint64_t run, uint64_t launch, uint64_t slot,
                              uint64_t generation);
size_t fg_state_remove_launch(FgStateStore *store, uint64_t run, uint64_t launch);
#ifdef __cplusplus
}
#endif
#endif
