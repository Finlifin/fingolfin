#include "fingolfin/state.h"
#include <stdalign.h>
struct FgStateStore {
    FgAllocator allocator;
    FgMap *values;
};
static int compare(const void *a, const void *b, void *context) {
    (void)context;
    const FgStateKey *x = a, *y = b;
#define COMPARE(FIELD)                                                                             \
    if (x->FIELD != y->FIELD)                                                                      \
    return (x->FIELD > y->FIELD) ? 1 : -1
    COMPARE(program);
    COMPARE(configuration);
    COMPARE(run);
    COMPARE(launch);
    COMPARE(slot);
    COMPARE(generation);
#undef COMPARE
    return 0;
}
FgStatus fg_state_create(size_t capacity, size_t size, size_t alignment,
                         const FgAllocator *allocator, FgStateStore **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgAllocator a = allocator ? *allocator : fg_allocator_default();
    if (!size || !alignment || (alignment & (alignment - 1)) || !a.allocate || !a.deallocate)
        return FG_STATUS_INVALID;
    FgStateStore *s = a.allocate(a.context, sizeof(*s), alignof(FgStateStore));
    if (!s)
        return FG_STATUS_NOMEM;
    *s = (FgStateStore){a, NULL};
    if (alignment < alignof(FgStateKey))
        alignment = alignof(FgStateKey);
    FgStatus status =
        fg_map_create(capacity, sizeof(FgStateKey), size, alignment, compare, NULL, &a, &s->values);
    if (status != FG_STATUS_OK) {
        a.deallocate(a.context, s);
        return status;
    }
    *out = s;
    return FG_STATUS_OK;
}
void fg_state_destroy(FgStateStore *s) {
    if (s) {
        FgAllocator a = s->allocator;
        fg_map_destroy(s->values);
        a.deallocate(a.context, s);
    }
}
size_t fg_state_length(const FgStateStore *s) {
    return s ? fg_map_length(s->values) : 0;
}
FgStatus fg_state_put(FgStateStore *s, const FgStateKey *key, const void *value, int *inserted) {
    return s ? fg_map_put(s->values, key, value, inserted) : FG_STATUS_INVALID;
}
FgStatus fg_state_find(FgStateStore *s, const FgStateKey *key, void **value) {
    if (!s) {
        if (value)
            *value = NULL;
        return FG_STATUS_INVALID;
    }
    return fg_map_find(s->values, key, value);
}
FgStatus fg_state_remove(FgStateStore *s, const FgStateKey *key, void *old_value) {
    return s ? fg_map_remove(s->values, key, old_value) : FG_STATUS_INVALID;
}
enum Scope { PROGRAM, CONFIGURATION, OBJECT, LAUNCH };
static size_t remove_scope(FgStateStore *s, enum Scope scope, FgStateKey match) {
    if (!s)
        return 0;
    size_t removed = 0, i = 0;
    while (i < fg_map_length(s->values)) {
        const void *view = NULL;
        (void)fg_map_at(s->values, i, &view, NULL);
        FgStateKey key = *(const FgStateKey *)view;
        int remove = 0;
        switch (scope) {
        case PROGRAM:
            remove = key.program == match.program;
            break;
        case CONFIGURATION:
            remove = key.program == match.program && key.configuration == match.configuration;
            break;
        case OBJECT:
            remove = key.run == match.run && key.launch == match.launch && key.slot == match.slot &&
                     key.generation == match.generation;
            break;
        case LAUNCH:
            remove = key.run == match.run && key.launch == match.launch;
            break;
        }
        if (remove) {
            (void)fg_map_remove(s->values, &key, NULL);
            removed++;
        } else
            i++;
    }
    return removed;
}
size_t fg_state_remove_program(FgStateStore *s, uint64_t program) {
    return remove_scope(s, PROGRAM, (FgStateKey){.program = program});
}
size_t fg_state_remove_configuration(FgStateStore *s, uint64_t program, uint64_t configuration) {
    return remove_scope(s, CONFIGURATION,
                        (FgStateKey){.program = program, .configuration = configuration});
}
size_t fg_state_remove_object(FgStateStore *s, uint64_t run, uint64_t launch, uint64_t slot,
                              uint64_t generation) {
    return remove_scope(
        s, OBJECT,
        (FgStateKey){.run = run, .launch = launch, .slot = slot, .generation = generation});
}
size_t fg_state_remove_launch(FgStateStore *s, uint64_t run, uint64_t launch) {
    return remove_scope(s, LAUNCH, (FgStateKey){.run = run, .launch = launch});
}
