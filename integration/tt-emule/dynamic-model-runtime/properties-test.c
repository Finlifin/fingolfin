#include "probe.adapter.h"
#include "properties.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static FgStatus predicate(void *facts, uint32_t test, uint64_t *budget, FgTruth *truth) {
    return fg_generated_predicate(facts, 8 * sizeof(FgFactValue), test, budget, truth);
}
static void evaluate(FgTtProperties *context, FgGateReason expected) {
    FgFactValue facts[8];
    size_t count = 0;
    fg_generated_reads(&count);
    assert(count == 8);
    uint64_t budget = 4096;
    assert(fg_generated_collect(FG_GATE_INVOCATION, fg_tt_property, context, &budget, facts, 8) ==
           FG_STATUS_OK);
    FgObjectKey k = context->leaf;
    FgStateKey identity = {1, 1, k.run, k.launch, k.slot, k.generation};
    FgGateDecision result = fg_gate_evaluate(fg_generated_gate_plan(), &identity, &identity,
                                             FG_GATE_INVOCATION, 4096, predicate, facts);
    assert(result.reason == expected && result.allow == (expected != FG_GATE_KNOWN_FALSE));
    budget = 4;
    memset(facts, 1, sizeof(facts));
    assert(fg_generated_collect(FG_GATE_INVOCATION, fg_tt_property, context, &budget, facts, 8) ==
           FG_STATUS_BUDGET);
    for (size_t i = 0; i < 8; i++)
        assert(facts[i].kind == FG_FACT_UNKNOWN);
}
int main(void) {
    FgObjects *r;
    assert(fg_objects_create(71, 1, 16, &r) == FG_STATUS_OK);
    uint64_t launch;
    assert(fg_objects_begin(r, &launch) == FG_STATUS_OK);
    FgObjectIdentity id = {.kind = 4};
    FgObjectKey root, device, core, cb;
    assert(fg_objects_register(r, launch, &id, &root) == FG_STATUS_OK);
    uint64_t devices[] = {0, 1, UINT64_C(1) << 32, UINT64_C(1) << 63};
    for (size_t i = 0; i < 4; i++) {
        id = (FgObjectIdentity){.kind = 5, .device = devices[i]};
        assert(fg_objects_register_child(r, launch, &id, &root, &device) == FG_STATUS_OK);
        id.kind = 6;
        assert(fg_objects_register_child(r, launch, &id, &device, &core) == FG_STATUS_OK);
        id.kind = 1;
        assert(fg_objects_register_child(r, launch, &id, &core, &cb) == FG_STATUS_OK);
        FgTtProperties context = {r, cb};
        evaluate(&context, i ? FG_GATE_KNOWN_FALSE : FG_GATE_KNOWN_TRUE);
        const char *fields[] = {"device", "id"};
        FgPropertyPath path = {"tt_emule", fields, 2};
        FgFactValue value = {0};
        uint64_t budget = 4096;
        assert(fg_tt_property(&context, &path, &budget, &value) == FG_STATUS_OK);
        assert(value.kind == FG_FACT_INTEGER && value.integer.low == devices[i] &&
               !value.integer.high);
        path.object = "tt_emule.device";
        path.fields = fields + 1;
        path.field_count = 1;
        assert(fg_tt_property(&context, &path, &budget, &value) == FG_STATUS_OK &&
               value.integer.low == devices[i]);
        const char *missing[] = {"device", "core", "noc"};
        path = (FgPropertyPath){"tt_emule", missing, 3};
        FgFactValue saved = value;
        assert(fg_tt_property(&context, &path, &budget, &value) == FG_STATUS_NOT_FOUND &&
               !memcmp(&saved, &value, sizeof(value)));
        const char *scalar_child[] = {"device", "id", "x"};
        path.fields = scalar_child;
        assert(fg_tt_property(&context, &path, &budget, &value) == FG_STATUS_NOT_FOUND &&
               !memcmp(&saved, &value, sizeof(value)));
        path.field_count = FG_FACT_MAX_PATH + 1;
        assert(fg_tt_property(&context, &path, &budget, &value) == FG_STATUS_INVALID &&
               !memcmp(&saved, &value, sizeof(value)));
        assert(fg_objects_retire(r, &cb) == FG_STATUS_OK);
        evaluate(&context, FG_GATE_UNKNOWN);
        FgObjectKey replacement;
        assert(fg_objects_register_child(r, launch, &id, &core, &replacement) == FG_STATUS_OK);
        assert(replacement.slot == cb.slot && replacement.generation > cb.generation);
        evaluate(&context, FG_GATE_UNKNOWN);
    }
    /* Registry permits arbitrary trees; provider must reject incorrect model ancestry. */
    id = (FgObjectIdentity){.kind = 1, .local = 99};
    assert(fg_objects_register_child(r, launch, &id, &root, &cb) == FG_STATUS_OK);
    FgTtProperties malformed = {r, cb};
    FgFactValue value = {.kind = FG_FACT_BOOL};
    uint64_t budget = 100;
    FgPropertyPath path = {"tt_emule", NULL, 0};
    assert(fg_tt_property(&malformed, &path, &budget, &value) == FG_STATUS_NOT_FOUND &&
           value.kind == FG_FACT_BOOL);
    assert(fg_objects_end(r, launch) == FG_STATUS_OK);
    assert(fg_objects_destroy(r) == FG_STATUS_OK);
    puts("generated .fg predicate: true/false/unknown; 64-bit devices; stale/ABA; budget; "
         "malformed paths/tree passed");
}
