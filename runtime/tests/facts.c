#include "fingolfin/facts.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static FgTruth run(FgFactValue *value, FgFactTest test) {
    uint64_t budget = 1000;
    FgTruth truth = FG_TRUTH_UNKNOWN;
    assert(fg_fact_test(value, sizeof(*value), 0, &test, &budget, &truth) == FG_STATUS_OK);
    return truth;
}
struct Provider {
    unsigned calls;
    int mode;
};
static FgStatus property(void *context, const FgPropertyPath *path, uint64_t *budget,
                         FgFactValue *out) {
    struct Provider *p = context;
    p->calls++;
    if (p->mode == 3) {
        (*budget)++;
        return FG_STATUS_OK;
    }
    if (!*budget)
        return FG_STATUS_BUDGET;
    (*budget)--;
    if (path->field_count == 2 && p->mode == 1)
        return FG_STATUS_INVALID;
    if (path->field_count == 2 && p->mode == 2)
        return FG_STATUS_NOT_FOUND;
    out->kind = path->field_count == 2 ? FG_FACT_INTEGER : FG_FACT_RECORD;
    return FG_STATUS_OK;
}
static void collection(void) {
    const char *group[] = {"group"}, *leaf[] = {"group", "x"};
    FgPropertyRead reads[] = {{{"root", NULL, 0}, FG_GATE_ATTACHMENT},
                              {{"root", group, 1}, FG_GATE_OBJECT},
                              {{"root", leaf, 2}, FG_GATE_UPDATE},
                              {{"other", NULL, 0}, FG_GATE_INVOCATION}};
    FgFactValue values[4];
    struct Provider provider = {0};
    uint64_t budget = 100;
    assert(fg_facts_collect(reads, 4, FG_GATE_UPDATE, property, &provider, &budget, values, 4) ==
           FG_STATUS_OK);
    assert(provider.calls == 3 && values[0].kind == FG_FACT_RECORD &&
           values[2].kind == FG_FACT_INTEGER && values[3].kind == FG_FACT_UNKNOWN);
    size_t cleared = 0;
    assert(fg_facts_invalidate(reads, 4, &reads[1].property, values, 4, &cleared) == FG_STATUS_OK &&
           cleared == 2);
    assert(values[0].kind == FG_FACT_RECORD && !values[1].kind && !values[2].kind);
    budget = 100;
    assert(fg_facts_collect(reads, 4, FG_GATE_INVOCATION, property, &provider, &budget, values,
                            4) == FG_STATUS_OK);
    assert(fg_facts_invalidate(reads, 4, &reads[0].property, values, 4, &cleared) == FG_STATUS_OK &&
           cleared == 3 && values[3].kind == FG_FACT_RECORD);
    for (int mode = 1; mode <= 3; mode++) {
        provider.mode = mode;
        budget = 100;
        FgStatus status =
            fg_facts_collect(reads, 4, FG_GATE_INVOCATION, property, &provider, &budget, values, 4);
        if (mode == 2)
            assert(status == FG_STATUS_OK && values[0].kind == FG_FACT_RECORD && !values[2].kind);
        else {
            assert(status == FG_STATUS_INVALID);
            for (size_t i = 0; i < 4; i++)
                assert(!values[i].kind);
        }
    }
    provider.mode = 0;
    budget = 2;
    assert(fg_facts_collect(reads, 4, FG_GATE_INVOCATION, property, &provider, &budget, values,
                            4) == FG_STATUS_BUDGET);
    for (size_t i = 0; i < 4; i++)
        assert(!values[i].kind);
}
int main(void) {
    collection();
    FgFactValue v = {.kind = FG_FACT_INTEGER, .integer = {0, UINT64_C(0x8000000000000000)}};
    FgFactTest t = {.op = FG_FACT_TEST_RANGE,
                    .inclusive = 1,
                    .low = {0, UINT64_C(0x8000000000000000)},
                    .high = {UINT64_MAX, UINT64_C(0x7fffffffffffffff)}};
    assert(run(&v, t) == FG_TRUTH_TRUE);
    v.integer = t.high;
    assert(run(&v, t) == FG_TRUTH_TRUE);
    t.inclusive = 0;
    assert(run(&v, t) == FG_TRUTH_FALSE);
    t = (FgFactTest){.op = FG_FACT_TEST_LITERAL, .kind = FG_FACT_INTEGER, .low = {UINT64_MAX, 0}};
    v.integer = t.low;
    assert(run(&v, t) == FG_TRUTH_TRUE);
    v.integer.high = UINT64_MAX;
    assert(run(&v, t) == FG_TRUTH_FALSE);
    const unsigned char text[] = {0xc3, 0xa9, 0, 0xf0, 0x9f, 0x98, 0x80};
    v = (FgFactValue){.kind = FG_FACT_STRING, .text_bytes = sizeof(text)};
    memcpy(v.text, text, sizeof(text));
    t = (FgFactTest){.op = FG_FACT_TEST_LITERAL,
                     .kind = FG_FACT_STRING,
                     .text = text,
                     .text_bytes = sizeof(text)};
    assert(run(&v, t) == FG_TRUTH_TRUE);
    v.kind = FG_FACT_VARIANT;
    t.kind = FG_FACT_VARIANT;
    assert(run(&v, t) == FG_TRUTH_TRUE);
    v.kind = FG_FACT_STRING;
    t.kind = FG_FACT_STRING;
    t.text_bytes--;
    assert(run(&v, t) == FG_TRUTH_FALSE);
    t.op = FG_FACT_TEST_KNOWN;
    assert(run(&v, t) == FG_TRUTH_TRUE);
    v.text[0] = 0xff;
    uint64_t budget = 1000;
    FgTruth truth = FG_TRUTH_TRUE;
    assert(fg_fact_test(&v, sizeof(v), 0, &t, &budget, &truth) == FG_STATUS_INVALID &&
           truth == FG_TRUTH_TRUE);
    v = (FgFactValue){.kind = FG_FACT_BOOL, .integer = {1, 0}};
    FgFactTest boolean = {.op = FG_FACT_TEST_LITERAL, .kind = FG_FACT_BOOL, .low = {1, 0}};
    assert(run(&v, boolean) == FG_TRUTH_TRUE);
    boolean.low.low = 0;
    assert(run(&v, boolean) == FG_TRUTH_FALSE);
    v.integer.low = 2;
    assert(fg_fact_test(&v, sizeof(v), 0, &t, &budget, &truth) == FG_STATUS_INVALID);
    v = (FgFactValue){0};
    assert(run(&v, t) == FG_TRUTH_UNKNOWN);
    budget = 0;
    assert(fg_fact_test(&v, sizeof(v), 0, &t, &budget, &truth) == FG_STATUS_BUDGET);
    budget = 100;
    assert(fg_fact_test(&v, sizeof(v) - 1, 0, &t, &budget, &truth) == FG_STATUS_INVALID);
    v = (FgFactValue){.kind = FG_FACT_RECORD};
    t.op = FG_FACT_TEST_RECORD;
    assert(run(&v, t) == FG_TRUTH_TRUE);
    unsigned char unaligned[sizeof(v) + 1];
    memcpy(unaligned + 1, &v, sizeof(v));
    assert(fg_fact_test(unaligned + 1, sizeof(v), 0, &t, &budget, &truth) == FG_STATUS_OK);
    puts("C facts: i128 bounds, typed comparisons, UTF-8/NUL, unknown, budget and unaligned "
         "snapshots passed");
    return 0;
}
