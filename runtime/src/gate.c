#include "fingolfin/gate.h"
static int same(const FgStateKey *a, const FgStateKey *b) {
    return a && b && a->program == b->program && a->configuration == b->configuration &&
           a->run == b->run && a->launch == b->launch && a->slot == b->slot &&
           a->generation == b->generation;
}
static FgGateDecision allow(FgGateReason reason) {
    return (FgGateDecision){1, reason};
}
FgGateDecision fg_gate_evaluate(FgGatePlan plan, const FgStateKey *expected,
                                const FgStateKey *actual, FgGateStage stage, uint64_t budget,
                                FgGateTest test, void *context) {
    if (!same(expected, actual))
        return allow(FG_GATE_STALE_IDENTITY);
    if (!plan.nodes || !plan.count || plan.count > FG_GATE_MAX_NODES ||
        (unsigned)stage > FG_GATE_INVOCATION)
        return allow(FG_GATE_INVALID_PLAN);
    for (size_t i = 0; i < plan.count; i++) {
        const FgGateNode *n = &plan.nodes[i];
        switch (n->op) {
        case FG_GATE_TRUE:
            if (n->left || n->right || n->test || n->stage)
                return allow(FG_GATE_INVALID_PLAN);
            break;
        case FG_GATE_TEST:
            if (n->left || n->right || n->stage > FG_GATE_INVOCATION)
                return allow(FG_GATE_INVALID_PLAN);
            break;
        case FG_GATE_AND:
        case FG_GATE_OR:
            if (n->left >= i || n->right >= i || n->test || n->stage)
                return allow(FG_GATE_INVALID_PLAN);
            break;
        case FG_GATE_NOT:
            if (n->left >= i || n->right || n->test || n->stage)
                return allow(FG_GATE_INVALID_PLAN);
            break;
        default:
            return allow(FG_GATE_INVALID_PLAN);
        }
    }
    FgTruth values[FG_GATE_MAX_NODES];
    for (size_t i = 0; i < plan.count; i++) {
        if (!budget)
            return allow(FG_GATE_BUDGET);
        budget--;
        const FgGateNode *n = &plan.nodes[i];
        FgTruth value = FG_TRUTH_UNKNOWN;
        switch (n->op) {
        case FG_GATE_TRUE:
            value = FG_TRUTH_TRUE;
            break;
        case FG_GATE_TEST:
            if (n->stage <= (unsigned)stage) {
                if (!test)
                    return allow(FG_GATE_INVALID_FACTS);
                uint64_t before = budget;
                FgStatus status = test(context, n->test, &budget, &value);
                if (budget > before)
                    return allow(FG_GATE_INVALID_FACTS);
                if (status == FG_STATUS_BUDGET)
                    return allow(FG_GATE_BUDGET);
                if (status != FG_STATUS_OK || (unsigned)value > FG_TRUTH_TRUE)
                    return allow(FG_GATE_INVALID_FACTS);
            }
            break;
        case FG_GATE_NOT:
            value = values[n->left] == FG_TRUTH_UNKNOWN ? FG_TRUTH_UNKNOWN
                    : values[n->left] == FG_TRUTH_TRUE  ? FG_TRUTH_FALSE
                                                        : FG_TRUTH_TRUE;
            break;
        case FG_GATE_AND:
            value = values[n->left] == FG_TRUTH_FALSE || values[n->right] == FG_TRUTH_FALSE
                        ? FG_TRUTH_FALSE
                    : values[n->left] == FG_TRUTH_TRUE && values[n->right] == FG_TRUTH_TRUE
                        ? FG_TRUTH_TRUE
                        : FG_TRUTH_UNKNOWN;
            break;
        case FG_GATE_OR:
            value = values[n->left] == FG_TRUTH_TRUE || values[n->right] == FG_TRUTH_TRUE
                        ? FG_TRUTH_TRUE
                    : values[n->left] == FG_TRUTH_FALSE && values[n->right] == FG_TRUTH_FALSE
                        ? FG_TRUTH_FALSE
                        : FG_TRUTH_UNKNOWN;
            break;
        }
        values[i] = value;
    }
    FgTruth result = values[plan.count - 1];
    return (FgGateDecision){result != FG_TRUTH_FALSE, result == FG_TRUTH_FALSE ? FG_GATE_KNOWN_FALSE
                                                      : result == FG_TRUTH_TRUE ? FG_GATE_KNOWN_TRUE
                                                                                : FG_GATE_UNKNOWN};
}
