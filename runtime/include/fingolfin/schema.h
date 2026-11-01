#ifndef FG_SCHEMA_H
#define FG_SCHEMA_H
#include "bytes.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_SCHEMA_MAX_BYTES (1024u * 1024u)
#define FG_SCHEMA_MAX_NODES 4096u
#define FG_FRAME_INPUT_CHECK_BUDGET UINT64_C(1000000)
/* Read-only borrowed view; obtain only via check. Backing bytes remain immutable
 * and alive while view/accessors are used. Wire is little endian, schema v1. */
typedef struct FgFrameSchema {
    FgBytes bytes;
    uint64_t frame_bytes;
    uint32_t alignment, parameters, bindings, has_result, node_count, member_count, slot_count;
    size_t member_offset, slot_offset, name_offset;
} FgFrameSchema;
typedef struct FgSchemaSlot {
    uint64_t offset, size;
    uint32_t alignment, type_index, role;
    uint8_t kind;
    FgBytes name;
} FgSchemaSlot;
typedef struct FgSchemaType {
    uint8_t kind, is_signed, bits;
    uint32_t alignment, element_type, member_start, member_count;
    uint64_t size, length;
} FgSchemaType;
typedef struct FgSchemaMember {
    uint64_t value;
    uint32_t type_index;
    FgBytes name;
} FgSchemaMember;
FgStatus fg_frame_schema_type(const FgFrameSchema *schema, uint32_t index, FgSchemaType *out);
FgStatus fg_frame_schema_member(const FgFrameSchema *schema, uint32_t index, FgSchemaMember *out);
FgStatus fg_frame_schema_check(FgBytes bytes, FgFrameSchema *out, char *diagnostic,
                               size_t diagnostic_bytes);
FgStatus fg_frame_schema_slot(const FgFrameSchema *schema, uint32_t index, FgSchemaSlot *out);
/* Requires a validated immutable schema. Read-only recursive input validation:
 * bool and enum values, including nested records/arrays. Ignores result/padding
 * and budget prefix; does not dereference or authorize pointers. Counts one
 * visit per value and per enum candidate; exhausted max_visits returns BUDGET. */
FgStatus fg_frame_schema_validate_inputs(const FgFrameSchema *schema, FgBytes frame,
                                         uint64_t max_visits);
/* Zero padding/result and initialize the fixed budget header. Exact size only. */
FgStatus fg_frame_schema_init(const FgFrameSchema *schema, void *frame, size_t frame_bytes,
                              uint64_t steps, uint32_t max_depth);
#ifdef __cplusplus
}
#endif
#endif
