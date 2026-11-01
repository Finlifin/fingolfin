#ifndef FG_BINDING_H
#define FG_BINDING_H
#include "attachment.h"
#include "gate.h"
#include "route.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FgBinding FgBinding;
/* Native adapter encodes into zeroed, schema-sized BPF frame. It must validate
 * payload semantics/layout, write only within frame, allocate no transient
 * retained data and return normally (no exceptions/longjmp). */
typedef FgStatus (*FgBindingEncode)(const FgCallHeader *, const FgFrameSchema *, void *, size_t);
/* Explicit development adapter binding; hook ID/name mapping is caller-owned.
 * Pins attachment until destroy. Captures exact route generation/native size.
 * Encoder function must outlive binding. No automatic pattern/gate compilation. */
FgStatus fg_binding_create(FgAttachment *attachment, uint64_t hook, uint64_t route_generation,
                           size_t native_frame_bytes, FgBindingEncode encode, FgBinding **out);
#define FG_BINDING_GATE_VERSION 1u
#define FG_BINDING_MAX_FACT_BYTES (64u * 1024u)
/* Facts are a copied flat value snapshot, not an object graph: no borrowed
 * pointers or mutable handles. Predicate must be thread-safe, read-only and
 * charge its work to remaining; missing/invalidated properties yield UNKNOWN. */
typedef FgStatus (*FgBindingPredicate)(const void *facts, size_t fact_bytes, uint32_t test,
                                       uint64_t *remaining, FgTruth *truth);
typedef struct FgBindingGate {
    uint32_t version;
    size_t struct_bytes;
    FgGatePlan plan;
    FgStateKey fact_identity;
    FgGateStage stage;
    uint64_t budget;
    FgBytes facts;
    FgBindingPredicate predicate;
} FgBindingGate;
/* Copies plan + facts before publication. Acquisition has no invocation frame,
 * so configured stage must not exceed UPDATE. Later-stage tests remain UNKNOWN.
 * Facts and plan never mutate; changes require a new binding/route generation.
 * Invalid topology is conservatively allowed by the evaluator, not executed.
 * Predicate code must outlive binding. Old routes keep their copied snapshot. */
FgStatus fg_binding_create_gated(FgAttachment *attachment, uint64_t hook, uint64_t route_generation,
                                 size_t native_frame_bytes, FgBindingEncode encode,
                                 const FgBindingGate *gate, FgBinding **out);
/* Borrowed descriptor. Binding must remain live through publish; published
 * routes retain it, including stale routes held by SDK leases. */
FgStatus fg_binding_subscription(FgBinding *binding, uint64_t id, FgSubscription *out);
/* Control-path copy for a new route generation. Shares the pinned Attachment
 * (and thus state), copies immutable gate/facts, allocates independent scratch.
 * Source must remain live; no concurrent destroy. Does not refresh model facts. */
FgStatus fg_binding_clone(FgBinding *source, uint64_t route_generation, FgBinding **out);
/* Requires external exclusion of all new/direct users. BUSY while a candidate,
 * published or retired leased route retains this binding. Does not close the
 * attachment. Unpublish/drain routes before destroying binding/attachment. */
FgStatus fg_binding_destroy(FgBinding *binding);
#ifdef __cplusplus
}
#endif
#endif
