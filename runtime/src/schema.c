#include "fingolfin/schema.h"
#include <stdio.h>
#include <string.h>
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64(const unsigned char *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32);
}
static uint64_t aligned(uint64_t n, uint64_t a) {
    return (n + a - 1) / a * a;
}
static int utf8(FgBytes b) {
    for (size_t i = 0; i < b.size;) {
        unsigned c = b.data[i++];
        if (!c)
            return 0;
        if (c < 128)
            continue;
        unsigned count, code, min;
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
        if (count > b.size - i)
            return 0;
        for (unsigned j = 0; j < count; j++) {
            c = b.data[i++];
            if ((c & 0xc0) != 0x80)
                return 0;
            code = (code << 6) | (c & 63);
        }
        if (code < min || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return 0;
    }
    return 1;
}
static const unsigned char *node(FgFrameSchema *s, uint32_t i) {
    return s->bytes.data + 64 + (size_t)i * 40;
}
static FgBytes name(FgFrameSchema *s, const unsigned char *record) {
    return (FgBytes){s->bytes.data + s->name_offset + u32(record + 12), u32(record + 16)};
}
static int compare(FgBytes a, FgBytes b) {
    size_t n = a.size < b.size ? a.size : b.size;
    int result = memcmp(a.data, b.data, n);
    return result ? result : (a.size > b.size) - (a.size < b.size);
}
FgStatus fg_frame_schema_check(FgBytes bytes, FgFrameSchema *out, char *diagnostic,
                               size_t capacity) {
    const char *reason = "invalid frame schema";
    if (diagnostic && capacity)
        diagnostic[0] = 0;
#define CHECK(COND, WHY)                                                                           \
    do {                                                                                           \
        if (!(COND)) {                                                                             \
            reason = WHY;                                                                          \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)
    CHECK(out && (!capacity || diagnostic) && bytes.data && bytes.size >= 64 &&
              bytes.size <= FG_SCHEMA_MAX_BYTES,
          "schema size or output invalid");
    const unsigned char *b = bytes.data;
    CHECK(!memcmp(b, "FGFRAME\0", 8) && u32(b + 8) == 1 && u32(b + 12) == bytes.size &&
              !u32(b + 52) && !u32(b + 60),
          "schema header invalid");
    FgFrameSchema s = {.bytes = bytes,
                       .frame_bytes = u64(b + 16),
                       .alignment = u32(b + 24),
                       .parameters = u32(b + 28),
                       .bindings = u32(b + 32),
                       .has_result = u32(b + 36),
                       .node_count = u32(b + 40),
                       .member_count = u32(b + 44),
                       .slot_count = u32(b + 56)};
    CHECK(s.node_count <= FG_SCHEMA_MAX_NODES && s.slot_count <= FG_SCHEMA_MAX_NODES &&
              s.member_count <= FG_SCHEMA_MAX_BYTES / 24,
          "schema counts exceed limits");
    CHECK(s.has_result <= 1 && (uint64_t)s.parameters + s.bindings + s.has_result == s.slot_count,
          "schema slot roles invalid");
    s.member_offset = 64 + (size_t)s.node_count * 40;
    s.slot_offset = s.member_offset + (size_t)s.member_count * 24;
    s.name_offset = s.slot_offset + (size_t)s.slot_count * 24;
    CHECK(s.name_offset <= bytes.size && u32(b + 48) == bytes.size - s.name_offset,
          "schema table ranges invalid");
    size_t next_name = 0;
    for (size_t i = 0; i < (size_t)s.member_count + s.slot_count; i++) {
        const unsigned char *r = b + s.member_offset + i * 24;
        uint32_t offset = u32(r + 12), len = u32(r + 16);
        CHECK(offset == next_name && len && len <= 4096 &&
                  len <= bytes.size - s.name_offset - next_name,
              "schema name range invalid");
        CHECK(utf8(name(&s, r)), "schema name is not nonempty UTF-8");
        next_name += len;
    }
    CHECK(next_name == u32(b + 48), "unused schema names");
    unsigned char used[FG_SCHEMA_MAX_NODES] = {0}, depths[FG_SCHEMA_MAX_NODES] = {0};
    uint32_t next_member = 0;
    for (uint32_t i = 0; i < s.node_count; i++) {
        const unsigned char *n = node(&s, i);
        unsigned kind = n[0], sign = n[1], bits = n[2];
        uint64_t a = u32(n + 4), size = u64(n + 8), length = u64(n + 32);
        uint32_t element = u32(n + 16), start = u32(n + 20), count = u32(n + 24);
        unsigned depth = 1;
        CHECK(!n[3] && !u32(n + 28) && (a == 1 || a == 2 || a == 4 || a == 8) && size &&
                  size <= 32u * 1024u * 1024u && size % a == 0 && sign <= 1,
              "schema type layout invalid");
        CHECK(kind == 6 || (!element && !length), "unexpected schema array fields");
        CHECK(kind == 4 || kind == 5 || (!start && !count), "unexpected schema members");
        CHECK(kind == 1 || kind == 4 || (!sign && !bits), "unexpected scalar representation");
        switch (kind) {
        case 1:
        case 4:
            CHECK((bits == 8 || bits == 16 || bits == 32 || bits == 64) && size == bits / 8 &&
                      a == size,
                  "integer/enum layout invalid");
            break;
        case 2:
            CHECK(size == 1 && a == 1, "boolean layout invalid");
            break;
        case 3:
            CHECK(size == 8 && a == 8, "pointer layout invalid");
            break;
        case 5:
            break;
        case 6:
            CHECK(element < i && !used[element] && length, "array child invalid");
            used[element] = 1;
            CHECK(length <= size && u64(node(&s, element) + 8) == size / length &&
                      size % length == 0 && u32(node(&s, element) + 4) == a,
                  "array layout invalid");
            depth = depths[element] + 1;
            break;
        default:
            CHECK(0, "unknown schema kind");
        }
        if (kind == 4 || kind == 5) {
            CHECK(count && count <= 256 && start == next_member && start <= s.member_count &&
                      count <= s.member_count - start,
                  "schema member range invalid");
            next_member += count;
            uint64_t end = 0, max_align = 1;
            for (uint32_t j = 0; j < count; j++) {
                const unsigned char *m = b + s.member_offset + (size_t)(start + j) * 24;
                uint64_t value = u64(m);
                uint32_t child = u32(m + 8);
                CHECK(!u32(m + 20), "schema member flags invalid");
                for (uint32_t k = 0; k < j; k++) {
                    const unsigned char *previous = b + s.member_offset + (size_t)(start + k) * 24;
                    CHECK(compare(name(&s, m), name(&s, previous)) != 0, "duplicate member name");
                    if (kind == 4)
                        CHECK(value != u64(previous), "duplicate enum value");
                }
                if (kind == 4) {
                    CHECK(child == UINT32_MAX, "enum child invalid");
                    if (bits < 64) {
                        uint64_t limit = UINT64_C(1) << (sign ? bits - 1 : bits);
                        CHECK(sign ? (value < limit || value >= UINT64_MAX - limit + 1)
                                   : value < limit,
                              "enum discriminant out of range");
                    }
                } else {
                    CHECK(child < i && !used[child], "record child invalid");
                    used[child] = 1;
                    uint64_t ca = u32(node(&s, child) + 4), cs = u64(node(&s, child) + 8);
                    CHECK(value == aligned(end, ca) && value <= size && cs <= size - value,
                          "record field offset invalid");
                    end = value + cs;
                    if (ca > max_align)
                        max_align = ca;
                    if (depths[child] + 1u > depth)
                        depth = depths[child] + 1u;
                }
            }
            if (kind == 5)
                CHECK(a == max_align && size == aligned(end, a), "record size invalid");
        }
        CHECK(depth <= 32, "schema nesting too deep");
        depths[i] = (unsigned char)depth;
    }
    CHECK(next_member == s.member_count, "unused schema members");
    uint64_t end = 16, max_align = 8;
    FgBytes previous_binding = {0};
    for (uint32_t i = 0; i < s.slot_count; i++) {
        const unsigned char *r = b + s.slot_offset + (size_t)i * 24;
        uint32_t child = u32(r + 8), role = u32(r + 20);
        FgBytes text = name(&s, r);
        CHECK(child < s.node_count && !used[child], "slot type invalid");
        used[child] = 1;
        if (i < s.parameters) {
            char expected[32];
            int len = snprintf(expected, sizeof(expected), "arg%u", i);
            CHECK(role == 0 && text.size == (size_t)len && !memcmp(text.data, expected, text.size),
                  "parameter name/role invalid");
        } else if (i < s.parameters + s.bindings) {
            CHECK(role == 1 && (!previous_binding.data || compare(previous_binding, text) < 0),
                  "binding order invalid");
            previous_binding = text;
        } else
            CHECK(role == 2 && text.size == 6 && !memcmp(text.data, "result", 6),
                  "result name/role invalid");
        uint64_t a = u32(node(&s, child) + 4), size = u64(node(&s, child) + 8), offset = u64(r);
        CHECK(offset == aligned(end, a) && offset <= 32u * 1024u * 1024u &&
                  size <= 32u * 1024u * 1024u - offset,
              "slot offset invalid");
        end = offset + size;
        if (a > max_align)
            max_align = a;
    }
    for (uint32_t i = 0; i < s.node_count; i++)
        CHECK(used[i], "unused schema type");
    CHECK(s.alignment == max_align && s.frame_bytes == aligned(end, max_align) &&
              s.frame_bytes <= 32u * 1024u * 1024u,
          "frame size invalid");
    *out = s;
    return FG_STATUS_OK;
fail:
    if (diagnostic && capacity)
        snprintf(diagnostic, capacity, "%s", reason);
    return FG_STATUS_INVALID;
#undef CHECK
}
FgStatus fg_frame_schema_slot(const FgFrameSchema *schema, uint32_t index, FgSchemaSlot *out) {
    if (!schema || !out || index >= schema->slot_count)
        return FG_STATUS_INVALID;
    const unsigned char *r = schema->bytes.data + schema->slot_offset + (size_t)index * 24;
    uint32_t child = u32(r + 8);
    const unsigned char *n = schema->bytes.data + 64 + (size_t)child * 40;
    *out = (FgSchemaSlot){
        .offset = u64(r),
        .size = u64(n + 8),
        .alignment = u32(n + 4),
        .type_index = child,
        .role = u32(r + 20),
        .kind = n[0],
        .name = {schema->bytes.data + schema->name_offset + u32(r + 12), u32(r + 16)}};
    return FG_STATUS_OK;
}

FgStatus fg_frame_schema_init(const FgFrameSchema *schema, void *frame, size_t frame_bytes,
                              uint64_t steps, uint32_t max_depth) {
    if (!schema || !frame || frame_bytes != schema->frame_bytes || frame_bytes < 16)
        return FG_STATUS_INVALID;
    unsigned char *b = frame;
    memset(b, 0, frame_bytes);
    for (size_t i = 0; i < 8; i++)
        b[i] = (unsigned char)(steps >> (8 * i));
    for (size_t i = 0; i < 4; i++)
        b[8 + i] = (unsigned char)(max_depth >> (8 * i));
    return FG_STATUS_OK;
}

FgStatus fg_frame_schema_type(const FgFrameSchema *s, uint32_t index, FgSchemaType *out) {
    if (!s || !out || index >= s->node_count)
        return FG_STATUS_INVALID;
    const unsigned char *n = s->bytes.data + 64 + (size_t)index * 40;
    *out = (FgSchemaType){.kind = n[0],
                          .is_signed = n[1],
                          .bits = n[2],
                          .alignment = u32(n + 4),
                          .element_type = u32(n + 16),
                          .member_start = u32(n + 20),
                          .member_count = u32(n + 24),
                          .size = u64(n + 8),
                          .length = u64(n + 32)};
    return FG_STATUS_OK;
}
FgStatus fg_frame_schema_member(const FgFrameSchema *s, uint32_t index, FgSchemaMember *out) {
    if (!s || !out || index >= s->member_count)
        return FG_STATUS_INVALID;
    const unsigned char *m = s->bytes.data + s->member_offset + (size_t)index * 24;
    *out = (FgSchemaMember){.value = u64(m),
                            .type_index = u32(m + 8),
                            .name = {s->bytes.data + s->name_offset + u32(m + 12), u32(m + 16)}};
    return FG_STATUS_OK;
}

static FgStatus input_value(const FgFrameSchema *schema, uint32_t index, FgBytes frame,
                            uint64_t offset, uint64_t *budget, unsigned depth) {
    if (!*budget)
        return FG_STATUS_BUDGET;
    --*budget;
    FgSchemaType type;
    if (depth > 32 || fg_frame_schema_type(schema, index, &type) != FG_STATUS_OK ||
        offset > frame.size || type.size > frame.size - offset)
        return FG_STATUS_INVALID;
    const unsigned char *value = frame.data + (size_t)offset;
    switch (type.kind) {
    case 1: /* All fixed-width integer representations are valid. */
    case 3: /* Pointer lifetime/authorization belongs to the model boundary. */
        return FG_STATUS_OK;
    case 2:
        return value[0] <= 1 ? FG_STATUS_OK : FG_STATUS_INVALID;
    case 4: {
        uint64_t bits = 0;
        for (size_t i = 0; i < type.size; i++)
            bits |= (uint64_t)value[i] << (8 * i);
        if (type.is_signed && type.bits < 64 && (bits & (UINT64_C(1) << (type.bits - 1))))
            bits |= UINT64_MAX << type.bits;
        for (uint32_t i = 0; i < type.member_count; i++) {
            if (!*budget)
                return FG_STATUS_BUDGET;
            --*budget;
            FgSchemaMember member;
            if (fg_frame_schema_member(schema, type.member_start + i, &member) != FG_STATUS_OK)
                return FG_STATUS_INVALID;
            if (bits == member.value)
                return FG_STATUS_OK;
        }
        return FG_STATUS_INVALID;
    }
    case 5:
        for (uint32_t i = 0; i < type.member_count; i++) {
            FgSchemaMember member;
            if (fg_frame_schema_member(schema, type.member_start + i, &member) != FG_STATUS_OK)
                return FG_STATUS_INVALID;
            FgStatus status = input_value(schema, member.type_index, frame, offset + member.value,
                                          budget, depth + 1);
            if (status != FG_STATUS_OK)
                return status;
        }
        return FG_STATUS_OK;
    case 6: {
        FgSchemaType element;
        if (fg_frame_schema_type(schema, type.element_type, &element) != FG_STATUS_OK)
            return FG_STATUS_INVALID;
        for (uint64_t i = 0; i < type.length; i++) {
            FgStatus status = input_value(schema, type.element_type, frame,
                                          offset + i * element.size, budget, depth + 1);
            if (status != FG_STATUS_OK)
                return status;
        }
        return FG_STATUS_OK;
    }
    default:
        return FG_STATUS_INVALID;
    }
}
FgStatus fg_frame_schema_validate_inputs(const FgFrameSchema *schema, FgBytes frame,
                                         uint64_t max_visits) {
    if (!schema || !frame.data || frame.size != schema->frame_bytes)
        return FG_STATUS_INVALID;
    for (uint32_t i = 0; i < schema->slot_count; i++) {
        FgSchemaSlot slot;
        if (fg_frame_schema_slot(schema, i, &slot) != FG_STATUS_OK)
            return FG_STATUS_INVALID;
        if (slot.role == 2)
            continue; /* Result is not an input; VM initializes it. */
        FgStatus status = input_value(schema, slot.type_index, frame, slot.offset, &max_visits, 1);
        if (status != FG_STATUS_OK)
            return status;
    }
    return FG_STATUS_OK;
}
