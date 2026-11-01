#include "fingolfin/route.h"
#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <string.h>
struct Route {
    FgAllocator allocator;
    atomic_size_t refs;
    uint64_t generation;
    size_t count;
    FgSubscription subscribers[];
};
struct FgPort {
    FgAllocator allocator;
    pthread_mutex_t mutex;
    FgObjectKey object;
    uint64_t hook, generation;
    size_t max_subscribers, active;
    int closed, quiesced;
    uint64_t ticket, pause_generation;
    struct Route *current;
};
static void drop_route(struct Route *route) {
    if (!route || atomic_fetch_sub_explicit(&route->refs, 1, memory_order_acq_rel) != 1)
        return;
    for (size_t i = 0; i < route->count; i++)
        if (route->subscribers[i].release)
            route->subscribers[i].release(route->subscribers[i].context);
    route->allocator.deallocate(route->allocator.context, route);
}
static int same_object(const FgObjectKey *a, const FgObjectKey *b) {
    return a->run == b->run && a->launch == b->launch && a->slot == b->slot &&
           a->generation == b->generation;
}
FgStatus fg_port_create(uint64_t hook, const FgObjectKey *object, size_t max,
                        const FgAllocator *allocator, FgPort **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgAllocator a = allocator ? *allocator : fg_allocator_default();
    if (!object || max > FG_PORT_MAX_SUBSCRIBERS || !a.allocate || !a.deallocate)
        return FG_STATUS_INVALID;
    FgPort *p = a.allocate(a.context, sizeof(*p), alignof(FgPort));
    if (!p)
        return FG_STATUS_NOMEM;
    *p = (FgPort){.allocator = a, .object = *object, .hook = hook, .max_subscribers = max};
    if (pthread_mutex_init(&p->mutex, NULL)) {
        a.deallocate(a.context, p);
        return FG_STATUS_INVALID;
    }
    *out = p;
    return FG_STATUS_OK;
}
static FgStatus publication_status(FgPort *p, uint64_t expected, uint64_t ticket) {
    if (p->closed)
        return FG_STATUS_CLOSED;
    if (ticket ? (!p->quiesced || p->ticket != ticket) : p->quiesced)
        return ticket ? FG_STATUS_CONFLICT : FG_STATUS_BUSY;
    if (p->generation != expected)
        return FG_STATUS_CONFLICT;
    if (p->generation == UINT64_MAX || p->active == SIZE_MAX)
        return FG_STATUS_OVERFLOW;
    return FG_STATUS_OK;
}
static void finish_publication(FgPort *p) {
    pthread_mutex_lock(&p->mutex);
    p->active--;
    pthread_mutex_unlock(&p->mutex);
}
static FgStatus publish(FgPort *p, uint64_t expected, uint64_t ticket, const FgSubscription *subs,
                        size_t count, uint64_t *generation) {
    if (!p || (count && !subs))
        return FG_STATUS_INVALID;
    if (count > p->max_subscribers)
        return FG_STATUS_CAPACITY;
    for (size_t i = 0; i < count; i++) {
        if (!subs[i].dispatch || !!subs[i].retain != !!subs[i].release)
            return FG_STATUS_INVALID;
        for (size_t j = 0; j < i; j++)
            if (subs[j].id == subs[i].id)
                return FG_STATUS_INVALID;
    }
    /* Reserve before allocation/retain: quiescence must include callbacks of
     * candidate publications which have not reached the commit lock yet. */
    pthread_mutex_lock(&p->mutex);
    FgStatus status = publication_status(p, expected, ticket);
    if (status == FG_STATUS_OK && ticket && p->active)
        status = FG_STATUS_BUSY;
    if (status == FG_STATUS_OK)
        p->active++;
    pthread_mutex_unlock(&p->mutex);
    if (status != FG_STATUS_OK)
        return status;
    size_t bytes;
    status = fg_size_mul(count, sizeof(*subs), &bytes);
    if (status == FG_STATUS_OK)
        status = fg_size_add(sizeof(struct Route), bytes, &bytes);
    if (status != FG_STATUS_OK) {
        finish_publication(p);
        return status;
    }
    struct Route *r = p->allocator.allocate(p->allocator.context, bytes, alignof(struct Route));
    if (!r) {
        finish_publication(p);
        return FG_STATUS_NOMEM;
    }
    r->allocator = p->allocator;
    r->count = count;
    atomic_init(&r->refs, 1);
    if (count)
        memcpy(r->subscribers, subs, count * sizeof(*subs));
    for (size_t i = 0; i < count; i++)
        if (subs[i].retain)
            subs[i].retain(subs[i].context);
    pthread_mutex_lock(&p->mutex);
    status = publication_status(p, expected, ticket);
    if (status != FG_STATUS_OK) {
        pthread_mutex_unlock(&p->mutex);
        drop_route(r);
        finish_publication(p);
        return status;
    }
    r->generation = ++p->generation;
    struct Route *old = p->current;
    p->current = r;
    if (generation)
        *generation = r->generation;
    pthread_mutex_unlock(&p->mutex);
    drop_route(old);
    finish_publication(p);
    return FG_STATUS_OK;
}
FgStatus fg_port_publish(FgPort *p, uint64_t expected, const FgSubscription *subs, size_t count,
                         uint64_t *generation) {
    return publish(p, expected, 0, subs, count, generation);
}
FgStatus fg_port_publish_quiesced(FgPort *p, uint64_t ticket, uint64_t expected,
                                  const FgSubscription *subs, size_t count, uint64_t *generation) {
    if (!ticket)
        return FG_STATUS_INVALID;
    return publish(p, expected, ticket, subs, count, generation);
}
FgStatus fg_port_quiesce(FgPort *p, uint64_t expected, uint64_t *ticket) {
    if (!p || !ticket)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&p->mutex);
    FgStatus status = p->closed                   ? FG_STATUS_CLOSED
                      : p->quiesced               ? FG_STATUS_BUSY
                      : p->generation != expected ? FG_STATUS_CONFLICT
                      : p->ticket == UINT64_MAX   ? FG_STATUS_OVERFLOW
                                                  : FG_STATUS_OK;
    if (status == FG_STATUS_OK) {
        p->quiesced = 1;
        p->pause_generation = p->generation;
        *ticket = ++p->ticket;
    }
    pthread_mutex_unlock(&p->mutex);
    return status;
}
int fg_port_quiesced_and_drained(FgPort *p, uint64_t ticket) {
    if (!p || !ticket)
        return 0;
    pthread_mutex_lock(&p->mutex);
    int drained = !p->closed && p->quiesced && p->ticket == ticket && !p->active;
    pthread_mutex_unlock(&p->mutex);
    return drained;
}
FgStatus fg_port_resume(FgPort *p, uint64_t ticket, uint64_t expected) {
    if (!p || !ticket)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&p->mutex);
    FgStatus status = p->closed ? FG_STATUS_CLOSED
                      : !p->quiesced || p->ticket != ticket || p->generation != expected ||
                              p->generation == p->pause_generation
                          ? FG_STATUS_CONFLICT
                      : p->active ? FG_STATUS_BUSY
                                  : FG_STATUS_OK;
    if (status == FG_STATUS_OK)
        p->quiesced = 0;
    pthread_mutex_unlock(&p->mutex);
    return status;
}
static int32_t dispatch_lease(FgLeaseV2 *lease, const FgCallHeader *frame) {
    if (lease->dispatched || !lease->owner)
        return FG_V2_ERROR_LEASE;
    lease->dispatched = 1;
    struct Route *r = lease->route;
    for (size_t i = 0; i < r->count; i++)
        if (lease->selected & (UINT64_C(1) << i)) {
            lease->report.attempted++;
            int32_t error = r->subscribers[i].dispatch(r->subscribers[i].context, frame);
            if (error) {
                lease->report.failed++;
                if (!lease->report.first_error)
                    lease->report.first_error = error;
            }
        }
    return lease->report.first_error;
}
static void release_lease(FgLeaseV2 *lease) {
    FgPort *p = lease->owner;
    struct Route *r = lease->route;
    memset(lease, 0, sizeof(*lease));
    /* Drop resources before reporting drain; callbacks may reenter the port. */
    drop_route(r);
    pthread_mutex_lock(&p->mutex);
    p->active--;
    pthread_mutex_unlock(&p->mutex);
}
static int32_t acquire(void *context, uint64_t hook, const FgObjectKey *object, FgLeaseV2 *lease) {
    FgPort *p = context;
    if (!p || !object || !lease || hook != p->hook || !same_object(object, &p->object))
        return -(int32_t)FG_STATUS_INVALID;
    pthread_mutex_lock(&p->mutex);
    struct Route *r = p->current;
    if (p->closed) {
        pthread_mutex_unlock(&p->mutex);
        return -(int32_t)FG_STATUS_CLOSED;
    }
    if (p->quiesced) {
        pthread_mutex_unlock(&p->mutex);
        return -(int32_t)FG_STATUS_BUSY;
    }
    if (p->active == SIZE_MAX ||
        (r && atomic_load_explicit(&r->refs, memory_order_relaxed) == SIZE_MAX)) {
        pthread_mutex_unlock(&p->mutex);
        return -(int32_t)FG_STATUS_OVERFLOW;
    }
    p->active++;
    if (r)
        atomic_fetch_add_explicit(&r->refs, 1, memory_order_relaxed);
    *lease = (FgLeaseV2){.abi_version = FG_SDK_V2,
                         .struct_bytes = sizeof(*lease),
                         .generation = p->generation,
                         .hook_id = hook,
                         .object = *object,
                         .owner = p,
                         .route = r,
                         .dispatch = dispatch_lease,
                         .release = release_lease};
    pthread_mutex_unlock(&p->mutex);
    if (r)
        for (size_t i = 0; i < r->count; i++) {
            FgSubscription *sub = &r->subscribers[i];
            if (!sub->gate || sub->gate(sub->context, object, r->generation) != 0)
                lease->selected |= UINT64_C(1) << i;
        }
    lease->enabled = lease->selected != 0;
    return 0;
}
FgRuntimeV2 fg_port_runtime(FgPort *p) {
    return (FgRuntimeV2){FG_SDK_V2, sizeof(FgRuntimeV2), p, acquire};
}
FgStatus fg_port_close(FgPort *p) {
    if (!p)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&p->mutex);
    if (p->closed) {
        pthread_mutex_unlock(&p->mutex);
        return FG_STATUS_OK;
    }
    if (p->active == SIZE_MAX) {
        pthread_mutex_unlock(&p->mutex);
        return FG_STATUS_OVERFLOW;
    }
    p->closed = 1;
    struct Route *r = p->current;
    p->current = NULL;
    p->active++;
    pthread_mutex_unlock(&p->mutex);
    drop_route(r);
    pthread_mutex_lock(&p->mutex);
    p->active--;
    pthread_mutex_unlock(&p->mutex);
    return FG_STATUS_OK;
}
int fg_port_closed_and_drained(FgPort *p) {
    if (!p)
        return 0;
    pthread_mutex_lock(&p->mutex);
    int drained = p->closed && p->active == 0;
    pthread_mutex_unlock(&p->mutex);
    return drained;
}
uint64_t fg_port_generation(FgPort *p) {
    if (!p)
        return 0;
    pthread_mutex_lock(&p->mutex);
    uint64_t generation = p->generation;
    pthread_mutex_unlock(&p->mutex);
    return generation;
}
FgStatus fg_port_destroy(FgPort *p) {
    if (!p)
        return FG_STATUS_OK;
    if (!fg_port_closed_and_drained(p))
        return FG_STATUS_BUSY;
    FgAllocator a = p->allocator;
    pthread_mutex_destroy(&p->mutex);
    a.deallocate(a.context, p);
    return FG_STATUS_OK;
}
