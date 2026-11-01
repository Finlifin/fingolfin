#ifndef FG_GATE_H
#define FG_GATE_H
#include "state.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_GATE_MAX_NODES 1024u
typedef enum FgTruth { FG_TRUTH_UNKNOWN = 0, FG_TRUTH_FALSE = 1, FG_TRUTH_TRUE = 2 } FgTruth;
typedef enum FgGateOp {
    FG_GATE_TRUE = 0,
    FG_GATE_TEST,
    FG_GATE_AND,
    FG_GATE_OR,
    FG_GATE_NOT
} FgGateOp;
typedef enum FgGateStage {
    FG_GATE_ATTACHMENT = 0,
    FG_GATE_OBJECT,
    FG_GATE_UPDATE,
    FG_GATE_INVOCATION
} FgGateStage;
/* Postorder nodes: child indices must precede parent, final node is root.
 * Unused fields must be zero. TEST's test is a model adapter predicate index;
 * callback evaluates that predicate from immutable facts, never mutable state. */
typedef struct FgGateNode {
    uint32_t op, left, right, test, stage;
} FgGateNode;
typedef struct FgGatePlan {
    const FgGateNode *nodes;
    size_t count;
} FgGatePlan;
typedef enum FgGateReason {
    FG_GATE_KNOWN_FALSE = 0,
    FG_GATE_KNOWN_TRUE,
    FG_GATE_UNKNOWN,
    FG_GATE_STALE_IDENTITY,
    FG_GATE_BUDGET,
    FG_GATE_INVALID_PLAN,
    FG_GATE_INVALID_FACTS
} FgGateReason;
typedef struct FgGateDecision {
    int allow;
    FgGateReason reason;
} FgGateDecision;
/* Callback must charge *remaining for predicate work, return BUDGET on exhaustion,
 * and UNKNOWN for missing/invalidated properties. No longjmp, mutation or
 * unbounded work. Only valid false permits rejection. */
typedef FgStatus (*FgGateTest)(void *context, uint32_t test, uint64_t *remaining, FgTruth *truth);
/* Read-only, bounded, allocation-free. Pins/fact immutability belong to caller.
 * Checks entire plan before callbacks. Any bad plan/facts, stale full identity,
 * or exhausted budget conservatively allows. A callback cannot increase budget.
 * This evaluates lowered predicate topology; JSON Plan decoding/property
 * resolution and model-specific literal tests are separate adapter work. */
FgGateDecision fg_gate_evaluate(FgGatePlan plan, const FgStateKey *expected,
                                const FgStateKey *actual, FgGateStage stage, uint64_t budget,
                                FgGateTest test, void *context);
#ifdef __cplusplus
}
#endif
#endif
