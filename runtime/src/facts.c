#include "fingolfin/facts.h"
#include <string.h>
static int compare(FgFactInteger a, FgFactInteger b) {
    unsigned sa = (unsigned)(a.high >> 63), sb = (unsigned)(b.high >> 63);
    if (sa != sb)
        return sa ? -1 : 1;
    if (a.high != b.high)
        return a.high < b.high ? -1 : 1;
    return a.low == b.low ? 0 : a.low < b.low ? -1 : 1;
}
static int utf8(const unsigned char *data, size_t size) {
    for (size_t i = 0; i < size;) {
        unsigned c = data[i++], count, code, min;
        if (c < 128)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            count = 1;
            code = c & 31;
            min = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            count = 2;
            code = c & 15;
            min = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            count = 3;
            code = c & 7;
            min = 0x10000;
        } else
            return 0;
        if (count > size - i)
            return 0;
        for (unsigned j = 0; j < count; j++) {
            c = data[i++];
            if ((c & 0xc0) != 0x80)
                return 0;
            code = (code << 6) | (c & 63);
        }
        if (code < min || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return 0;
    }
    return 1;
}
FgStatus fg_fact_test(const void *facts, size_t bytes, uint32_t read, const FgFactTest *test,
                      uint64_t *budget, FgTruth *truth) {
    if (!facts || !test || !budget || !truth || bytes % sizeof(FgFactValue) ||
        read >= bytes / sizeof(FgFactValue) || test->op > FG_FACT_TEST_RANGE)
        return FG_STATUS_INVALID;
    if (fg_budget_charge(budget, 1) != FG_STATUS_OK)
        return FG_STATUS_BUDGET;
    FgFactValue value;
    memcpy(&value, (const unsigned char *)facts + (size_t)read * sizeof(value), sizeof(value));
    if (value.kind > FG_FACT_VARIANT || value.text_bytes > FG_FACT_TEXT_BYTES)
        return FG_STATUS_INVALID;
    if (value.kind == FG_FACT_STRING || value.kind == FG_FACT_VARIANT) {
        if (value.integer.low || value.integer.high)
            return FG_STATUS_INVALID;
        if (fg_budget_charge(budget, value.text_bytes) != FG_STATUS_OK)
            return FG_STATUS_BUDGET;
        if (!utf8(value.text, value.text_bytes))
            return FG_STATUS_INVALID;
    } else {
        if (value.text_bytes)
            return FG_STATUS_INVALID;
        if (value.kind != FG_FACT_INTEGER &&
            (value.integer.high || value.integer.low > (value.kind == FG_FACT_BOOL ? 1u : 0u)))
            return FG_STATUS_INVALID;
    }
    if (value.kind == FG_FACT_UNKNOWN) {
        *truth = FG_TRUTH_UNKNOWN;
        return FG_STATUS_OK;
    }
    int yes = 0;
    switch (test->op) {
    case FG_FACT_TEST_KNOWN:
        yes = 1;
        break;
    case FG_FACT_TEST_RECORD:
        if (value.kind != FG_FACT_RECORD)
            return FG_STATUS_INVALID;
        yes = 1;
        break;
    case FG_FACT_TEST_RANGE:
        if (value.kind != FG_FACT_INTEGER || test->inclusive > 1)
            return FG_STATUS_INVALID;
        yes = compare(value.integer, test->low) >= 0 &&
              (test->inclusive ? compare(value.integer, test->high) <= 0
                               : compare(value.integer, test->high) < 0);
        break;
    case FG_FACT_TEST_LITERAL:
        if (value.kind != test->kind)
            return FG_STATUS_INVALID;
        if (value.kind == FG_FACT_INTEGER)
            yes = compare(value.integer, test->low) == 0;
        else if (value.kind == FG_FACT_BOOL) {
            if (test->low.high || test->low.low > 1)
                return FG_STATUS_INVALID;
            yes = value.integer.low == test->low.low;
        } else if (value.kind == FG_FACT_STRING || value.kind == FG_FACT_VARIANT) {
            if (!test->text && test->text_bytes)
                return FG_STATUS_INVALID;
            yes = value.text_bytes == test->text_bytes &&
                  (!value.text_bytes || !memcmp(value.text, test->text, value.text_bytes));
        } else
            return FG_STATUS_INVALID;
        break;
    }
    *truth = yes ? FG_TRUTH_TRUE : FG_TRUTH_FALSE;
    return FG_STATUS_OK;
}

static int valid_name(const char *name) {
    if (!name || !*name)
        return 0;
    for (size_t i = 0; i <= 4096; i++)
        if (!name[i])
            return 1;
    return 0;
}
static int valid_path(const FgPropertyPath *path) {
    if (!path || !valid_name(path->object) || path->field_count > FG_FACT_MAX_PATH ||
        (!path->fields && path->field_count))
        return 0;
    for (size_t i = 0; i < path->field_count; i++)
        if (!valid_name(path->fields[i]))
            return 0;
    return 1;
}
static int valid_reads(const FgPropertyRead *reads, size_t count) {
    if (!reads && count)
        return 0;
    for (size_t i = 0; i < count; i++)
        if ((unsigned)reads[i].stage > FG_GATE_INVOCATION || !valid_path(&reads[i].property))
            return 0;
    return 1;
}
FgStatus fg_facts_collect(const FgPropertyRead *reads, size_t count, FgGateStage stage,
                          FgPropertyProvider provider, void *context, uint64_t *budget,
                          FgFactValue *output, size_t capacity) {
    if (count > FG_FACT_MAX_READS || count > capacity || (!output && count))
        return FG_STATUS_INVALID;
    if (count)
        memset(output, 0, count * sizeof(*output));
    if (!budget || (unsigned)stage > FG_GATE_INVOCATION || !valid_reads(reads, count))
        return FG_STATUS_INVALID;
    FgStatus status = FG_STATUS_OK;
    const FgFactTest known = {.op = FG_FACT_TEST_KNOWN};
    for (size_t i = 0; i < count; i++) {
        status = fg_budget_charge(budget, 1);
        if (status != FG_STATUS_OK)
            goto fail;
        if (reads[i].stage > stage)
            continue;
        if (!provider) {
            status = FG_STATUS_INVALID;
            goto fail;
        }
        FgFactValue value = {0};
        uint64_t before = *budget;
        status = provider(context, &reads[i].property, budget, &value);
        if (*budget > before) {
            status = FG_STATUS_INVALID;
            goto fail;
        }
        if (status == FG_STATUS_NOT_FOUND || status == FG_STATUS_CAPACITY)
            continue;
        if (status != FG_STATUS_OK)
            goto fail;
        FgTruth truth;
        status = fg_fact_test(&value, sizeof(value), 0, &known, budget, &truth);
        if (status != FG_STATUS_OK)
            goto fail;
        output[i] = value;
    }
    return FG_STATUS_OK;
fail:
    if (count)
        memset(output, 0, count * sizeof(*output));
    return status;
}
FgStatus fg_facts_invalidate(const FgPropertyRead *reads, size_t count, const FgPropertyPath *path,
                             FgFactValue *values, size_t capacity, size_t *cleared) {
    if (!cleared || count > FG_FACT_MAX_READS || count > capacity || (!values && count) ||
        !valid_path(path) || !valid_reads(reads, count))
        return FG_STATUS_INVALID;
    size_t changed = 0;
    for (size_t i = 0; i < count; i++) {
        const FgPropertyPath *candidate = &reads[i].property;
        if (strcmp(candidate->object, path->object) || candidate->field_count < path->field_count)
            continue;
        size_t j = 0;
        while (j < path->field_count && !strcmp(candidate->fields[j], path->fields[j]))
            j++;
        if (j == path->field_count) {
            memset(&values[i], 0, sizeof(values[i]));
            changed++;
        }
    }
    *cleared = changed;
    return FG_STATUS_OK;
}
