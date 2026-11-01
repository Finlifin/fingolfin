#ifndef FG_TT_PROPERTIES_H
#define FG_TT_PROPERTIES_H
#include "fingolfin/facts.h"
#include "fingolfin/objects.h"
/* Caller excludes registry/model mutations for the entire facts collection.
 * A live registry must outlive the context; this does not pin target memory. */
typedef struct FgTtProperties {
    FgObjects *registry;
    FgObjectKey leaf;
} FgTtProperties;
/* Identity properties of the observed launch tree, not mutable hardware state.
 * Unknown paths or stale keys return NOT_FOUND; failures preserve *value. */
FgStatus fg_tt_property(void *context, const FgPropertyPath *path, uint64_t *remaining,
                        FgFactValue *value);
#endif
