#include "fingolfin/route.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Context {
    FgPort *port;
    atomic_uint references, calls, gates, drops;
    int decision, error;
    int reenter;
    uint64_t expected_generation;
} Context;
static void retain(void *context) {
    Context *c = context;
    atomic_fetch_add(&c->references, 1);
}
static void release(void *context) {
    Context *c = context;
    assert(atomic_fetch_sub(&c->references, 1) > 0);
    atomic_fetch_add(&c->drops, 1);
    /* Would deadlock if the destructor ran under the publication mutex. */
    (void)fg_port_generation(c->port);
    if (c->reenter == 2) {
        assert(!fg_port_closed_and_drained(c->port));
        assert(fg_port_destroy(c->port) == FG_STATUS_BUSY);
    }
}
static int gate(void *context, const FgObjectKey *object, uint64_t generation) {
    Context *c = context;
    assert(object->slot == 3);
    atomic_fetch_add(&c->gates, 1);
    if (c->expected_generation)
        assert(generation == c->expected_generation);
    return c->decision;
}
static int32_t dispatch(void *context, const FgCallHeader *header) {
    Context *c = context;
    assert(header->object.slot == 3);
    atomic_fetch_add(&c->calls, 1);
    if (c->expected_generation)
        assert(header->config_generation == c->expected_generation);
    if (c->reenter == 1) {
        uint64_t gen = fg_port_generation(c->port);
        assert(fg_port_publish(c->port, gen, NULL, 0, NULL) == FG_STATUS_OK);
    }
    return c->error;
}
static FgSubscription subscription(uint64_t id, Context *context) {
    return (FgSubscription){id, context, gate, dispatch, retain, release};
}
typedef struct Factory {
    FgCallHeader frame;
    size_t count;
    int corrupt;
    FgPort *publish;
} Factory;
static const FgCallHeader *factory(void *context, const FgLeaseV2 *lease) {
    Factory *f = context;
    f->count++;
    f->frame = (FgCallHeader){FG_ABI_VERSION, sizeof(FgCallHeader), lease->hook_id,
                              lease->generation, lease->object};
    if (f->corrupt)
        f->frame.config_generation++;
    if (f->publish) {
        uint64_t gen = fg_port_generation(f->publish);
        assert(fg_port_publish(f->publish, gen, NULL, 0, NULL) == FG_STATUS_OK);
    }
    return &f->frame;
}
static const FgObjectKey object = {1, 2, 3, 4};
static void lifecycle(void) {
    FgPort *port;
    assert(fg_port_create(7, &object, 4, NULL, &port) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_port_runtime(port);
    Factory f = {0};
    FgDispatchReportV2 report;
    assert(fg_v2_observe(&runtime, 7, &object, factory, &f, &report) == 0 && f.count == 0);
    Context rejected = {.port = port, .decision = 0},
            allowed = {.port = port, .decision = 1, .expected_generation = 1},
            unknown = {.port = port, .decision = -1, .error = -99, .expected_generation = 1};
    FgSubscription subs[] = {subscription(1, &rejected), subscription(2, &unknown),
                             subscription(3, &allowed)};
    uint64_t gen;
    assert(fg_port_publish(port, 0, subs, 3, &gen) == FG_STATUS_OK && gen == 1);
    assert(fg_port_publish(port, 0, subs, 3, NULL) == FG_STATUS_CONFLICT);
    assert(atomic_load(&allowed.references) == 1);
    FgLeaseV2 old;
    assert(fg_v2_acquire(&runtime, 7, &object, &old) == 0 && old.enabled);
    /* Publish between gate and frame construction: old lease stays coherent. */
    assert(fg_port_publish(port, 1, NULL, 0, &gen) == FG_STATUS_OK && gen == 2);
    assert(atomic_load(&allowed.references) == 1);
    const FgCallHeader *frame = factory(&f, &old);
    assert(fg_v2_dispatch(&old, frame) == -99);
    assert(old.report.attempted == 2 && old.report.failed == 1 && old.report.first_error == -99);
    assert(atomic_load(&allowed.calls) == 1 && atomic_load(&rejected.calls) == 0);
    assert(fg_v2_dispatch(&old, frame) == FG_V2_ERROR_LEASE);
    FgLeaseV2 latest;
    assert(fg_v2_acquire(&runtime, 7, &object, &latest) == 0 && !latest.enabled);
    assert(fg_port_close(port) == FG_STATUS_OK);
    assert(!fg_port_closed_and_drained(port));
    fg_v2_release(&latest);
    assert(!fg_port_closed_and_drained(port));
    assert(fg_port_destroy(port) == FG_STATUS_BUSY);
    allowed.reenter = 2;
    unknown.reenter = 2;
    rejected.reenter = 2;
    fg_v2_release(&old);
    fg_v2_release(&old);
    assert(fg_port_closed_and_drained(port) && atomic_load(&allowed.references) == 0);
    assert(fg_v2_acquire(&runtime, 7, &object, &latest) == -(int32_t)FG_STATUS_CLOSED);
    assert(fg_port_destroy(port) == FG_STATUS_OK);
}
static void guards_and_reentrancy(void) {
    FgPort *port;
    assert(fg_port_create(7, &object, 2, NULL, &port) == FG_STATUS_OK);
    Context c = {.port = port, .decision = 0};
    FgSubscription sub = subscription(1, &c);
    FgRuntimeV2 runtime = fg_port_runtime(port);
    Factory f = {0};
    assert(fg_port_publish(port, 0, &sub, 1, NULL) == FG_STATUS_OK);
    assert(fg_v2_observe(&runtime, 7, &object, factory, &f, NULL) == 0 && f.count == 0);
    /* Context mutation here is test-only and happens with no outstanding users. */
    c.decision = 1;
    c.reenter = 1;
    assert(fg_v2_observe(&runtime, 7, &object, factory, &f, NULL) == 0 && f.count == 1);
    assert(fg_port_generation(port) == 2);
    assert(atomic_load(&c.references) == 0);
    c.reenter = 0;
    assert(fg_port_publish(port, 2, &sub, 1, NULL) == FG_STATUS_OK);
    f.corrupt = 1;
    assert(fg_v2_observe(&runtime, 7, &object, factory, &f, NULL) == FG_V2_ERROR_FRAME);
    assert(atomic_load(&c.calls) == 1);
    f.corrupt = 0;
    f.publish = port;
    assert(fg_v2_observe(&runtime, 7, &object, factory, &f, NULL) == 0 &&
           atomic_load(&c.calls) == 2);
    assert(atomic_load(&c.references) == 0);
    FgObjectKey wrong = object;
    wrong.generation++;
    FgLeaseV2 lease;
    assert(fg_v2_acquire(&runtime, 7, &wrong, &lease) != 0);
    FgSubscription duplicate[] = {sub, sub};
    assert(fg_port_publish(port, 4, duplicate, 2, NULL) == FG_STATUS_INVALID);
    assert(fg_port_close(port) == FG_STATUS_OK && fg_port_closed_and_drained(port));
    assert(fg_port_publish(port, 4, &sub, 1, NULL) == FG_STATUS_CLOSED);
    assert(atomic_load(&c.references) == 0);
    assert(fg_port_destroy(port) == FG_STATUS_OK);
}
typedef struct Worker {
    FgPort *port;
    atomic_uint *started;
} Worker;
static void *reader(void *context) {
    Worker *w = context;
    FgRuntimeV2 runtime = fg_port_runtime(w->port);
    Factory f = {0};
    atomic_fetch_add(w->started, 1);
    for (size_t i = 0; i < 10000; i++)
        assert(fg_v2_observe(&runtime, 7, &object, factory, &f, NULL) == 0);
    return NULL;
}
static void concurrent(void) {
    FgPort *port;
    assert(fg_port_create(7, &object, 1, NULL, &port) == FG_STATUS_OK);
    Context a = {.port = port, .decision = 1}, b = {.port = port, .decision = 1};
    atomic_uint started = 0;
    Worker w = {port, &started};
    pthread_t threads[4];
    FgSubscription sub = subscription(1, &a);
    assert(fg_port_publish(port, 0, &sub, 1, NULL) == FG_STATUS_OK);
    for (size_t i = 0; i < 4; i++)
        assert(!pthread_create(&threads[i], NULL, reader, &w));
    while (atomic_load(&started) != 4) {
    }
    for (uint64_t gen = 1; gen <= 10000; gen++) {
        sub = subscription(1, gen % 2 ? &b : &a);
        assert(fg_port_publish(port, gen, &sub, 1, NULL) == FG_STATUS_OK);
    }
    for (size_t i = 0; i < 4; i++)
        assert(!pthread_join(threads[i], NULL));
    assert(atomic_load(&a.calls) + atomic_load(&b.calls) == 40000);
    assert(fg_port_close(port) == FG_STATUS_OK && fg_port_closed_and_drained(port));
    assert(atomic_load(&a.references) == 0 && atomic_load(&b.references) == 0);
    assert(fg_port_destroy(port) == FG_STATUS_OK);
}
typedef struct Allocations {
    size_t calls, frees;
    int fail;
} Allocations;
static void *allocate(void *context, size_t bytes, size_t alignment) {
    Allocations *a = context;
    a->calls++;
    if (a->fail)
        return NULL;
    FgAllocator d = fg_allocator_default();
    return d.allocate(NULL, bytes, alignment);
}
static void deallocate(void *context, void *pointer) {
    Allocations *a = context;
    a->frees++;
    free(pointer);
}
static void allocation_free_observation(void) {
    Allocations a = {0};
    FgAllocator allocator = {&a, allocate, deallocate};
    FgPort *port;
    assert(fg_port_create(7, &object, 1, &allocator, &port) == FG_STATUS_OK);
    Context c = {.port = port, .decision = 1};
    FgSubscription sub = subscription(1, &c);
    assert(fg_port_publish(port, 0, &sub, 1, NULL) == FG_STATUS_OK);
    size_t calls = a.calls;
    FgRuntimeV2 runtime = fg_port_runtime(port);
    Factory f = {0};
    a.fail = 1;
    for (size_t i = 0; i < 10000; i++)
        assert(fg_v2_observe(&runtime, 7, &object, factory, &f, NULL) == 0);
    assert(a.calls == calls && f.count == 10000);
    assert(fg_port_publish(port, 1, &sub, 1, NULL) == FG_STATUS_NOMEM);
    assert(fg_port_generation(port) == 1);
    assert(fg_port_close(port) == FG_STATUS_OK);
    assert(fg_port_destroy(port) == FG_STATUS_OK);
    assert(a.frees == calls);
}
static void mask_boundary(void) {
    FgPort *port;
    assert(fg_port_create(7, &object, 64, NULL, &port) == FG_STATUS_OK);
    Context c = {.port = port, .decision = 1};
    FgSubscription subs[64];
    for (uint64_t i = 0; i < 64; ++i)
        subs[i] = subscription(i, &c);
    assert(fg_port_publish(port, 0, subs, 64, NULL) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_port_runtime(port);
    Factory f = {0};
    FgDispatchReportV2 report;
    assert(fg_v2_observe(&runtime, 7, &object, factory, &f, &report) == 0);
    assert(f.count == 1 && report.attempted == 64 && atomic_load(&c.calls) == 64);
    assert(fg_port_close(port) == FG_STATUS_OK);
    assert(atomic_load(&c.references) == 0);
    assert(fg_port_destroy(port) == FG_STATUS_OK);
}
int main(void) {
    lifecycle();
    guards_and_reentrancy();
    concurrent();
    mask_boundary();
    allocation_free_observation();
    puts("PASS: C SDK v2 leases, generation coherence, drain/reentrancy, 10000 publications + "
         "40000 calls, no observation allocations");
}
