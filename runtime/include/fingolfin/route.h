#ifndef FG_ROUTE_H
#define FG_ROUTE_H
#include "fingolfin_v2.h"
#include "util.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_PORT_MAX_SUBSCRIBERS 64u
typedef struct FgPort FgPort;
typedef struct FgSubscription {
    uint64_t id;
    void *context;
    /* Only zero means known false. NULL or any other value conservatively allows.
     * Facts belong to the immutable subscription generation. */
    int (*gate)(void *, const FgObjectKey *, uint64_t generation);
    int32_t (*dispatch)(void *, const FgCallHeader *);
    /* Both NULL means borrowed context. Otherwise both must be supplied.
     * Retain once per candidate route, release after failure or final route drop.
     * All callbacks run outside the publication mutex; retain/release must not fail. */
    void (*retain)(void *);
    void (*release)(void *);
} FgSubscription;
FgStatus fg_port_create(uint64_t hook, const FgObjectKey *object, size_t max_subscribers,
                        const FgAllocator *allocator, FgPort **out);
/* Atomic conditional publication, one generation for facts/code/context. */
FgStatus fg_port_publish(FgPort *port, uint64_t expected_generation,
                         const FgSubscription *subscribers, size_t count,
                         uint64_t *published_generation);
/* Control transition: stop new acquire/publication, keep old leases runnable.
 * Serial controller owns returned ticket; it must not be reused by concurrent
 * controllers. Poll drain outside callbacks. Includes pending publish callbacks.
 * This pauses observations (acquire returns BUSY), not the underlying model. */
FgStatus fg_port_quiesce(FgPort *port, uint64_t expected_generation, uint64_t *ticket);
int fg_port_quiesced_and_drained(FgPort *port, uint64_t ticket);
/* Only after drain: mutate model at its own safe point, collect consistent facts,
 * and publish replacement subscriptions with the ticket. Resume requires a new
 * generation and no pending work, preventing reuse of old known facts. */
FgStatus fg_port_publish_quiesced(FgPort *port, uint64_t ticket, uint64_t expected_generation,
                                  const FgSubscription *subscribers, size_t count,
                                  uint64_t *published_generation);
FgStatus fg_port_resume(FgPort *port, uint64_t ticket, uint64_t expected_generation);
FgStatus fg_port_close(FgPort *port);
int fg_port_closed_and_drained(FgPort *port);
uint64_t fg_port_generation(FgPort *port);
/* Requires closed+drained AND external exclusion of all concurrent API callers.
 * BUSY does not free the port. Successful destroy invalidates exported tables. */
FgStatus fg_port_destroy(FgPort *port);
/* Table is a borrowed view. Caller must hold port alive throughout each acquire. */
FgRuntimeV2 fg_port_runtime(FgPort *port);
#ifdef __cplusplus
}
#endif
#endif
