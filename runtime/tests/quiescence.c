#include "fingolfin/route.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
struct Context {
    atomic_uint refs, calls;
    atomic_int entered, proceed;
    int block;
};
static void retain(void *context) {
    struct Context *c = context;
    atomic_fetch_add(&c->refs, 1);
    if (c->block) {
        atomic_store(&c->entered, 1);
        while (!atomic_load(&c->proceed))
            sched_yield();
    }
}
static void release(void *context) {
    struct Context *c = context;
    assert(atomic_fetch_sub(&c->refs, 1) > 0);
}
static int32_t dispatch(void *context, const FgCallHeader *header) {
    (void)header;
    struct Context *c = context;
    atomic_fetch_add(&c->calls, 1);
    return 0;
}
static FgSubscription sub(struct Context *c) {
    return (FgSubscription){1, c, NULL, dispatch, retain, release};
}
struct Factory {
    FgCallHeader frame;
    size_t calls;
};
static const FgCallHeader *factory(void *context, const FgLeaseV2 *lease) {
    struct Factory *f = context;
    f->calls++;
    f->frame = (FgCallHeader){FG_ABI_VERSION, sizeof(f->frame), lease->hook_id, lease->generation,
                              lease->object};
    return &f->frame;
}
struct Publication {
    FgPort *port;
    FgSubscription sub;
    FgStatus status;
};
static void *publish_thread(void *context) {
    struct Publication *p = context;
    p->status = fg_port_publish(p->port, 4, &p->sub, 1, NULL);
    return NULL;
}
int main(void) {
    FgObjectKey object = {1, 2, 3, 4};
    FgPort *port = NULL;
    assert(fg_port_create(7, &object, 2, NULL, &port) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_port_runtime(port);
    struct Context old = {0}, fresh = {0}, pending = {.block = 1};
    FgSubscription a = sub(&old), b = sub(&fresh);
    uint64_t generation, ticket, next;
    assert(fg_port_publish(port, 0, &a, 1, &generation) == FG_STATUS_OK && generation == 1);
    FgLeaseV2 first, second;
    assert(!fg_v2_acquire(&runtime, 7, &object, &first));
    assert(fg_port_publish(port, 1, &a, 1, &generation) == FG_STATUS_OK && generation == 2);
    assert(!fg_v2_acquire(&runtime, 7, &object, &second));
    assert(fg_port_quiesce(port, 2, &ticket) == FG_STATUS_OK);
    assert(!fg_port_quiesced_and_drained(port, ticket));
    struct Factory f = {0};
    assert(fg_v2_observe(&runtime, 7, &object, factory, &f, NULL) == -(int32_t)FG_STATUS_BUSY &&
           !f.calls);
    assert(fg_port_publish(port, 2, &b, 1, NULL) == FG_STATUS_BUSY && !atomic_load(&fresh.refs));
    assert(fg_port_publish_quiesced(port, ticket, 2, &b, 1, NULL) == FG_STATUS_BUSY);
    assert(!fg_v2_dispatch(&first, factory(&f, &first)));
    fg_v2_release(&first);
    assert(!fg_port_quiesced_and_drained(port, ticket));
    assert(!fg_v2_dispatch(&second, factory(&f, &second)));
    fg_v2_release(&second);
    assert(fg_port_quiesced_and_drained(port, ticket) && atomic_load(&old.calls) == 2);
    assert(fg_port_resume(port, ticket, 2) == FG_STATUS_CONFLICT);
    assert(fg_port_publish_quiesced(port, ticket + 1, 2, &b, 1, NULL) == FG_STATUS_CONFLICT);
    assert(fg_port_publish_quiesced(port, ticket, 2, &b, 1, &generation) == FG_STATUS_OK &&
           generation == 3);
    assert(!atomic_load(&old.refs));
    assert(fg_port_resume(port, ticket, 2) == FG_STATUS_CONFLICT);
    assert(fg_port_resume(port, ticket, 3) == FG_STATUS_OK);
    assert(!fg_v2_observe(&runtime, 7, &object, factory, &f, NULL) &&
           f.frame.config_generation == 3 && atomic_load(&fresh.calls) == 1);
    assert(fg_port_quiesce(port, 3, &next) == FG_STATUS_OK && next > ticket);
    assert(fg_port_resume(port, ticket, 3) == FG_STATUS_CONFLICT);
    assert(fg_port_publish_quiesced(port, ticket, 3, NULL, 0, NULL) == FG_STATUS_CONFLICT);
    assert(fg_port_publish_quiesced(port, next, 3, NULL, 0, &generation) == FG_STATUS_OK &&
           generation == 4);
    assert(fg_port_resume(port, next, 4) == FG_STATUS_OK);
    /* A publication already inside retain must be covered by the barrier. */
    struct Publication publication = {port, sub(&pending), FG_STATUS_OK};
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, publish_thread, &publication));
    while (!atomic_load(&pending.entered))
        sched_yield();
    assert(fg_port_quiesce(port, 4, &ticket) == FG_STATUS_OK);
    assert(!fg_port_quiesced_and_drained(port, ticket));
    atomic_store(&pending.proceed, 1);
    assert(!pthread_join(thread, NULL));
    assert(publication.status == FG_STATUS_BUSY && !atomic_load(&pending.refs));
    assert(fg_port_quiesced_and_drained(port, ticket));
    assert(fg_port_publish_quiesced(port, ticket, 4, NULL, 0, &generation) == FG_STATUS_OK &&
           generation == 5);
    assert(fg_port_close(port) == FG_STATUS_OK);
    assert(fg_port_resume(port, ticket, 5) == FG_STATUS_CLOSED &&
           !fg_port_quiesced_and_drained(port, ticket));
    assert(fg_port_closed_and_drained(port) && fg_port_destroy(port) == FG_STATUS_OK);
    puts("Port quiescence: all generations/candidate callbacks drain, ticket ownership, "
         "replacement-before-resume passed");
    return 0;
}
