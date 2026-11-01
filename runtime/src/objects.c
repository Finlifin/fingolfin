#include "fingolfin/objects.h"
#include <pthread.h>
#include <stdlib.h>
struct Entry {
    uint64_t launch, generation;
    FgObjectIdentity identity;
    FgObjectKey parent;
    int live, has_parent;
};
struct FgObjects {
    pthread_mutex_t mutex;
    uint64_t run, last_launch;
    size_t launch_capacity, object_capacity;
    uint64_t *launches;
    struct Entry *entries;
};
static int equal(const FgObjectIdentity *a, const FgObjectIdentity *b) {
    return a->device == b->device && a->local == b->local && a->kind == b->kind &&
           a->core_x == b->core_x && a->core_y == b->core_y;
}
static int active(FgObjects *r, uint64_t launch) {
    if (!launch)
        return 0;
    for (size_t i = 0; i < r->launch_capacity; i++)
        if (r->launches[i] == launch)
            return 1;
    return 0;
}
static struct Entry *entry(FgObjects *r, const FgObjectKey *key) {
    if (key->run != r->run || key->slot >= r->object_capacity)
        return NULL;
    struct Entry *e = &r->entries[key->slot];
    return e->live && e->launch == key->launch && e->generation == key->generation ? e : NULL;
}
FgStatus fg_objects_create(uint64_t run, size_t launches, size_t objects, FgObjects **out) {
    if (!run || !launches || !objects || !out)
        return FG_STATUS_INVALID;
    if (launches > SIZE_MAX / sizeof(uint64_t) || objects > SIZE_MAX / sizeof(struct Entry))
        return FG_STATUS_OVERFLOW;
    FgObjects *r = calloc(1, sizeof(*r));
    if (!r)
        return FG_STATUS_NOMEM;
    r->launches = calloc(launches, sizeof(*r->launches));
    r->entries = calloc(objects, sizeof(*r->entries));
    if (!r->launches || !r->entries) {
        free(r->launches);
        free(r->entries);
        free(r);
        return FG_STATUS_NOMEM;
    }
    if (pthread_mutex_init(&r->mutex, NULL)) {
        free(r->launches);
        free(r->entries);
        free(r);
        return FG_STATUS_NOMEM;
    }
    r->run = run;
    r->launch_capacity = launches;
    r->object_capacity = objects;
    *out = r;
    return FG_STATUS_OK;
}
FgStatus fg_objects_destroy(FgObjects *r) {
    if (!r)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&r->mutex);
    for (size_t i = 0; i < r->launch_capacity; i++)
        if (r->launches[i]) {
            pthread_mutex_unlock(&r->mutex);
            return FG_STATUS_BUSY;
        }
    pthread_mutex_unlock(&r->mutex);
    pthread_mutex_destroy(&r->mutex);
    free(r->entries);
    free(r->launches);
    free(r);
    return FG_STATUS_OK;
}
FgStatus fg_objects_begin(FgObjects *r, uint64_t *launch) {
    if (!r || !launch)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&r->mutex);
    FgStatus status = FG_STATUS_CAPACITY;
    if (r->last_launch == UINT64_MAX)
        status = FG_STATUS_OVERFLOW;
    else
        for (size_t i = 0; i < r->launch_capacity; i++)
            if (!r->launches[i]) {
                *launch = r->launches[i] = ++r->last_launch;
                status = FG_STATUS_OK;
                break;
            }
    pthread_mutex_unlock(&r->mutex);
    return status;
}
FgStatus fg_objects_end(FgObjects *r, uint64_t launch) {
    if (!r || !launch)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&r->mutex);
    if (!active(r, launch)) {
        pthread_mutex_unlock(&r->mutex);
        return FG_STATUS_NOT_FOUND;
    }
    for (size_t i = 0; i < r->object_capacity; i++)
        if (r->entries[i].launch == launch)
            r->entries[i].live = 0;
    for (size_t i = 0; i < r->launch_capacity; i++)
        if (r->launches[i] == launch)
            r->launches[i] = 0;
    pthread_mutex_unlock(&r->mutex);
    return FG_STATUS_OK;
}
static FgStatus register_object(FgObjects *r, uint64_t launch, const FgObjectIdentity *id,
                                const FgObjectKey *parent, FgObjectKey *out) {
    if (!r || !launch || !id || !id->kind || !out)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&r->mutex);
    if (!active(r, launch)) {
        pthread_mutex_unlock(&r->mutex);
        return FG_STATUS_NOT_FOUND;
    }
    if (parent) {
        if (parent->launch != launch || parent->run != r->run) {
            pthread_mutex_unlock(&r->mutex);
            return FG_STATUS_INVALID;
        }
        if (!entry(r, parent)) {
            pthread_mutex_unlock(&r->mutex);
            return FG_STATUS_NOT_FOUND;
        }
    }
    size_t slot = r->object_capacity;
    for (size_t i = 0; i < r->object_capacity; i++) {
        struct Entry *e = &r->entries[i];
        if (e->live && e->launch == launch && equal(&e->identity, id)) {
            pthread_mutex_unlock(&r->mutex);
            return FG_STATUS_CONFLICT;
        }
        if (!e->live && e->generation < UINT64_MAX && slot == r->object_capacity)
            slot = i;
    }
    if (slot == r->object_capacity) {
        pthread_mutex_unlock(&r->mutex);
        return FG_STATUS_CAPACITY;
    }
    struct Entry *e = &r->entries[slot];
    e->launch = launch;
    e->generation++;
    e->identity = *id;
    e->live = 1;
    e->has_parent = parent != NULL;
    e->parent = parent ? *parent : (FgObjectKey){0};
    *out = (FgObjectKey){r->run, launch, slot, e->generation};
    pthread_mutex_unlock(&r->mutex);
    return FG_STATUS_OK;
}
FgStatus fg_objects_register(FgObjects *r, uint64_t launch, const FgObjectIdentity *id,
                             FgObjectKey *out) {
    return register_object(r, launch, id, NULL, out);
}
FgStatus fg_objects_register_child(FgObjects *r, uint64_t launch, const FgObjectIdentity *id,
                                   const FgObjectKey *parent, FgObjectKey *out) {
    if (!parent)
        return FG_STATUS_INVALID;
    return register_object(r, launch, id, parent, out);
}
FgStatus fg_objects_parent(FgObjects *r, const FgObjectKey *child, FgObjectKey *out) {
    if (!r || !child || !out)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&r->mutex);
    struct Entry *e = entry(r, child);
    FgStatus status = e && e->has_parent ? FG_STATUS_OK : FG_STATUS_NOT_FOUND;
    if (status == FG_STATUS_OK)
        *out = e->parent;
    pthread_mutex_unlock(&r->mutex);
    return status;
}
FgStatus fg_objects_find(FgObjects *r, uint64_t launch, const FgObjectIdentity *id,
                         FgObjectKey *out) {
    if (!r || !launch || !id || !id->kind || !out)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&r->mutex);
    FgStatus status = FG_STATUS_NOT_FOUND;
    for (size_t i = 0; i < r->object_capacity; i++) {
        struct Entry *e = &r->entries[i];
        if (e->live && e->launch == launch && equal(&e->identity, id)) {
            *out = (FgObjectKey){r->run, launch, i, e->generation};
            status = FG_STATUS_OK;
            break;
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return status;
}
FgStatus fg_objects_resolve(FgObjects *r, const FgObjectKey *key, FgObjectIdentity *out) {
    if (!r || !key || !out)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&r->mutex);
    struct Entry *e = entry(r, key);
    FgStatus status = e ? FG_STATUS_OK : FG_STATUS_NOT_FOUND;
    if (e)
        *out = e->identity;
    pthread_mutex_unlock(&r->mutex);
    return status;
}
FgStatus fg_objects_retire(FgObjects *r, const FgObjectKey *key) {
    if (!r || !key)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&r->mutex);
    struct Entry *e = entry(r, key);
    FgStatus status = e ? FG_STATUS_OK : FG_STATUS_NOT_FOUND;
    if (e) {
        for (size_t i = 0; i < r->object_capacity; i++) {
            struct Entry *child = &r->entries[i];
            if (child->live && child->has_parent && child->parent.run == key->run &&
                child->parent.launch == key->launch && child->parent.slot == key->slot &&
                child->parent.generation == key->generation) {
                pthread_mutex_unlock(&r->mutex);
                return FG_STATUS_BUSY;
            }
        }
        e->live = 0;
    }
    pthread_mutex_unlock(&r->mutex);
    return status;
}
