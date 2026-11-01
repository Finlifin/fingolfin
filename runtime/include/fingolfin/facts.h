#ifndef FG_FACTS_H
#define FG_FACTS_H
#include "gate.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_FACT_TEXT_BYTES 256u
typedef enum FgFactKind {
    FG_FACT_UNKNOWN = 0,
    FG_FACT_RECORD,
    FG_FACT_BOOL,
    FG_FACT_INTEGER,
    FG_FACT_STRING,
    FG_FACT_VARIANT
} FgFactKind;
/* Signed two's-complement 128-bit value, without compiler integer extensions. */
typedef struct FgFactInteger {
    uint64_t low, high;
} FgFactInteger;
/* Flat native snapshot slot, indexed by Plan.reads. Zero-initialize; no pointers.
 * UNKNOWN represents missing/invalidated/unrepresentable data, never truncation.
 * Strings/variants are UTF-8 bytes (embedded NUL allowed), length <=256. */
typedef struct FgFactValue {
    uint32_t kind, text_bytes;
    FgFactInteger integer;
    unsigned char text[FG_FACT_TEXT_BYTES];
} FgFactValue;
typedef enum FgFactOp {
    FG_FACT_TEST_RECORD = 0,
    FG_FACT_TEST_KNOWN,
    FG_FACT_TEST_LITERAL,
    FG_FACT_TEST_RANGE
} FgFactOp;
typedef struct FgFactTest {
    uint32_t op, kind, inclusive;
    FgFactInteger low, high;
    const unsigned char *text;
    size_t text_bytes;
} FgFactTest;
/* Read one flat slot via memcpy (alignment-independent), validate representation,
 * and perform the exact typed predicate. Read-only; no allocation. Budget charged
 * for the read and text bytes. Output unchanged on error, UNKNOWN on unknown slot.
 * Malformed/type-mismatched facts return INVALID so gate conservatively allows. */
FgStatus fg_fact_test(const void *facts, size_t fact_bytes, uint32_t read, const FgFactTest *test,
                      uint64_t *remaining, FgTruth *truth);
#define FG_FACT_MAX_READS 1024u
#define FG_FACT_MAX_PATH 32u
typedef struct FgPropertyPath {
    const char *object;
    const char *const *fields;
    size_t field_count;
} FgPropertyPath;
typedef struct FgPropertyRead {
    FgPropertyPath property;
    FgGateStage stage;
} FgPropertyRead;
/* Provider resolves the complete path, checking every parent. Caller must pin a
 * coherent model snapshot for the whole collection. No borrowed pointers may be
 * returned in value; charge work to remaining. NOT_FOUND/CAPACITY mean UNKNOWN. */
typedef FgStatus (*FgPropertyProvider)(void *context, const FgPropertyPath *property,
                                       uint64_t *remaining, FgFactValue *value);
/* Allocation-free, only calls provider for currently available properties.
 * With valid output bounds, errors clear the entire result to UNKNOWN; never
 * publishes partial known facts on provider error/budget failure. Output must
 * not overlap provider state, metadata or budget. */
FgStatus fg_facts_collect(const FgPropertyRead *reads, size_t count, FgGateStage stage,
                          FgPropertyProvider provider, void *context, uint64_t *remaining,
                          FgFactValue *output, size_t capacity);
/* Invalidate a path and descendants of the same canonical object. Mutate only
 * unpublished snapshots. Before model change, publish invalidation and drain
 * older leases, or use a host safe point/coherent snapshot contract. */
FgStatus fg_facts_invalidate(const FgPropertyRead *reads, size_t count, const FgPropertyPath *path,
                             FgFactValue *values, size_t capacity, size_t *cleared);
#ifdef __cplusplus
}
#endif
#endif
