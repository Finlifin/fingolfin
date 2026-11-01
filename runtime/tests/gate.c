#include "fingolfin/gate.h"
#include <assert.h>
#include <stdio.h>
struct Facts {
    FgTruth values[2];
    unsigned calls;
    int mode;
};
static FgStatus test(void *context, uint32_t index, uint64_t *budget, FgTruth *out) {
    struct Facts *facts = context;
    facts->calls++;
    if (facts->mode == 1) {
        (*budget)++;
        return FG_STATUS_OK;
    }
    if (facts->mode == 2) {
        *out = (FgTruth)99;
        return FG_STATUS_OK;
    }
    if (facts->mode == 3)
        return FG_STATUS_INVALID;
    if (!*budget)
        return FG_STATUS_BUDGET;
    (*budget)--;
    if (index >= 2)
        return FG_STATUS_INVALID;
    *out = facts->values[index];
    return FG_STATUS_OK;
}
int main(void) {
    FgStateKey stamp = {1, 2, 3, 4, 5, 6};
    FgGateNode nodes[] = {{.op = FG_GATE_TEST, .test = 0},
                          {.op = FG_GATE_TEST, .test = 1},
                          {.op = FG_GATE_AND, .left = 0, .right = 1}};
    const FgTruth ands[3][3] = {{FG_TRUTH_UNKNOWN, FG_TRUTH_FALSE, FG_TRUTH_UNKNOWN},
                                {FG_TRUTH_FALSE, FG_TRUTH_FALSE, FG_TRUTH_FALSE},
                                {FG_TRUTH_UNKNOWN, FG_TRUTH_FALSE, FG_TRUTH_TRUE}};
    const FgTruth ors[3][3] = {{FG_TRUTH_UNKNOWN, FG_TRUTH_UNKNOWN, FG_TRUTH_TRUE},
                               {FG_TRUTH_UNKNOWN, FG_TRUTH_FALSE, FG_TRUTH_TRUE},
                               {FG_TRUTH_TRUE, FG_TRUTH_TRUE, FG_TRUTH_TRUE}};
    for (unsigned op = FG_GATE_AND; op <= FG_GATE_OR; op++)
        for (unsigned a = 0; a < 3; a++)
            for (unsigned b = 0; b < 3; b++) {
                nodes[2].op = op;
                struct Facts facts = {{(FgTruth)a, (FgTruth)b}, 0, 0};
                FgTruth expected = op == FG_GATE_AND ? ands[a][b] : ors[a][b];
                FgGateDecision d = fg_gate_evaluate((FgGatePlan){nodes, 3}, &stamp, &stamp,
                                                    FG_GATE_INVOCATION, 5, test, &facts);
                assert(d.allow == (expected != FG_TRUTH_FALSE));
                assert(facts.calls == 2);
                assert(d.reason == (expected == FG_TRUTH_TRUE    ? FG_GATE_KNOWN_TRUE
                                    : expected == FG_TRUTH_FALSE ? FG_GATE_KNOWN_FALSE
                                                                 : FG_GATE_UNKNOWN));
            }
    nodes[1] = (FgGateNode){.op = FG_GATE_NOT, .left = 0};
    for (unsigned value = 0; value < 3; value++) {
        struct Facts facts = {{(FgTruth)value, FG_TRUTH_UNKNOWN}, 0, 0};
        FgGateDecision d = fg_gate_evaluate((FgGatePlan){nodes, 2}, &stamp, &stamp,
                                            FG_GATE_ATTACHMENT, 3, test, &facts);
        assert(d.allow == (value != FG_TRUTH_TRUE));
    }
    struct Facts facts = {{FG_TRUTH_FALSE, FG_TRUTH_TRUE}, 0, 0};
    nodes[0].stage = FG_GATE_INVOCATION;
    FgGateDecision d =
        fg_gate_evaluate((FgGatePlan){nodes, 1}, &stamp, &stamp, FG_GATE_UPDATE, 1, test, &facts);
    assert(d.allow && d.reason == FG_GATE_UNKNOWN && !facts.calls);
    d = fg_gate_evaluate((FgGatePlan){nodes, 1}, &stamp, &stamp, FG_GATE_INVOCATION, 1, test,
                         &facts);
    assert(d.allow && d.reason == FG_GATE_BUDGET);
    for (int mode = 1; mode <= 3; mode++) {
        facts.mode = mode;
        d = fg_gate_evaluate((FgGatePlan){nodes, 1}, &stamp, &stamp, FG_GATE_INVOCATION, 10, test,
                             &facts);
        assert(d.allow && d.reason == FG_GATE_INVALID_FACTS);
    }
    facts.mode = 0;
    facts.calls = 0;
    nodes[1].left = 1;
    d = fg_gate_evaluate((FgGatePlan){nodes, 2}, &stamp, &stamp, FG_GATE_INVOCATION, 10, test,
                         &facts);
    assert(d.allow && d.reason == FG_GATE_INVALID_PLAN && !facts.calls);
    FgStateKey stale = stamp;
    stale.generation++;
    d = fg_gate_evaluate((FgGatePlan){nodes, 1}, &stamp, &stale, FG_GATE_INVOCATION, 10, test,
                         &facts);
    assert(d.allow && d.reason == FG_GATE_STALE_IDENTITY && !facts.calls);
    FgGateNode chain[FG_GATE_MAX_NODES];
    chain[0] = (FgGateNode){.op = FG_GATE_TRUE};
    for (size_t i = 1; i < FG_GATE_MAX_NODES; i++)
        chain[i] = (FgGateNode){.op = FG_GATE_NOT, .left = (uint32_t)(i - 1)};
    d = fg_gate_evaluate((FgGatePlan){chain, FG_GATE_MAX_NODES}, &stamp, &stamp, FG_GATE_ATTACHMENT,
                         FG_GATE_MAX_NODES, NULL, NULL);
    assert(!d.allow && d.reason == FG_GATE_KNOWN_FALSE);
    d = fg_gate_evaluate((FgGatePlan){chain, FG_GATE_MAX_NODES}, &stamp, &stamp, FG_GATE_ATTACHMENT,
                         FG_GATE_MAX_NODES - 1, NULL, NULL);
    assert(d.allow && d.reason == FG_GATE_BUDGET);
    puts("C gate: three-valued truth tables, stages, stale identity, invalid facts/plan and "
         "bounded deep topology passed");
    return 0;
}
