#ifndef FG_OBJECTS_H
#define FG_OBJECTS_H
#include "fingolfin.h"
#include "fingolfin/util.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FgObjects FgObjects;
/* Identity within a launch. Device/local IDs are adapter-owned stable numbers,
 * not host pointers. Kind must be nonzero; all other fields may be zero. */
typedef struct FgObjectIdentity {
    uint64_t device, local;
    uint32_t kind, core_x, core_y;
} FgObjectIdentity;
/* Thread-safe identity table, bounded at creation. No allocation after create.
 * Run must be unique for this registry's lifetime domain; caller owns that ID.
 * Destroy requires external quiescence and no active launches. */
FgStatus fg_objects_create(uint64_t run, size_t launches, size_t objects, FgObjects **out);
FgStatus fg_objects_destroy(FgObjects *objects);
FgStatus fg_objects_begin(FgObjects *objects, uint64_t *launch);
/* End invalidates all keys from this launch, including objects not retired. */
FgStatus fg_objects_end(FgObjects *objects, uint64_t launch);
FgStatus fg_objects_register(FgObjects *objects, uint64_t launch, const FgObjectIdentity *identity,
                             FgObjectKey *out);
/* Parent must be live in the same launch. Parentage is immutable; only new
 * objects can be attached, so cycles cannot be introduced. Identity uniqueness
 * remains launch-wide, not per-parent. Failed calls leave outputs unchanged. */
FgStatus fg_objects_register_child(FgObjects *objects, uint64_t launch,
                                   const FgObjectIdentity *identity, const FgObjectKey *parent,
                                   FgObjectKey *out);
/* NOT_FOUND for roots or stale keys; returned parent is a copied identity. */
FgStatus fg_objects_parent(FgObjects *objects, const FgObjectKey *child, FgObjectKey *out);
FgStatus fg_objects_find(FgObjects *objects, uint64_t launch, const FgObjectIdentity *identity,
                         FgObjectKey *out);
FgStatus fg_objects_resolve(FgObjects *objects, const FgObjectKey *key, FgObjectIdentity *out);
/* BUSY while any direct child is live. Retire leaves before parents; end()
 * atomically invalidates the complete launch without an ordering requirement. */
FgStatus fg_objects_retire(FgObjects *objects, const FgObjectKey *key);
/* Returned identities are copies. Resolve does not pin target memory or routes;
 * adapters must separately coordinate target teardown and in-flight leases. */
#ifdef __cplusplus
}
#endif
#endif
