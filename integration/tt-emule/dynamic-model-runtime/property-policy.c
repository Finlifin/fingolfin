#include "adapter.h"
#include "probe.adapter.h"
#include "properties.h"
struct PolicyFacts {
    const FgFactValue *values;
    size_t bytes;
};
static FgStatus test_policy(void *opaque, uint32_t test, uint64_t *budget, FgTruth *truth) {
    const struct PolicyFacts *facts = opaque;
    return fg_generated_predicate(facts->values, facts->bytes, test, budget, truth);
}
FgStatus fg_tt_policy(FgTtProperties *context, FgStateKey identity, FgFactValue *facts,
                      FgBindingGate *gate, int *allow) {
    size_t count;
    const FgPropertyRead *reads = fg_generated_reads(&count);
    if (count > FG_TT_POLICY_MAX_READS)
        return FG_STATUS_CAPACITY;
    for (size_t i = 0; i < count; i++)
        if (reads[i].stage != FG_GATE_OBJECT)
            return FG_STATUS_INVALID;
    uint64_t budget = 16384;
    FgStatus status = fg_generated_collect(FG_GATE_OBJECT, fg_tt_property, context, &budget, facts,
                                           FG_TT_POLICY_MAX_READS);
    if (status != FG_STATUS_OK)
        return status;
    for (size_t i = 0; i < count; i++)
        if (facts[i].kind == FG_FACT_UNKNOWN)
            return FG_STATUS_NOT_FOUND;
    struct PolicyFacts snapshot = {facts, count * sizeof(*facts)};
    FgGatePlan plan = fg_generated_gate_plan();
    FgGateDecision decision =
        fg_gate_evaluate(plan, &identity, &identity, FG_GATE_OBJECT, 16384, test_policy, &snapshot);
    if (decision.reason != FG_GATE_KNOWN_TRUE && decision.reason != FG_GATE_KNOWN_FALSE)
        return FG_STATUS_INVALID;
    *gate = (FgBindingGate){.version = FG_BINDING_GATE_VERSION,
                            .struct_bytes = sizeof(*gate),
                            .plan = plan,
                            .fact_identity = identity,
                            .stage = FG_GATE_OBJECT,
                            .budget = 16384,
                            .facts = {(const unsigned char *)facts, snapshot.bytes},
                            .predicate = fg_generated_predicate};
    *allow = decision.allow;
    return FG_STATUS_OK;
}
