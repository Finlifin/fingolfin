#include "fingolfin/elf.h"
#include <stdio.h>
#include <string.h>
typedef struct Section {
    uint32_t type, link, info;
    uint64_t flags, offset, size, alignment, entry_size;
    FgBytes data;
} Section;
static uint16_t u16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64(const unsigned char *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32);
}
static int range(FgBytes b, uint64_t offset, uint64_t size, FgBytes *out) {
    return offset <= SIZE_MAX && size <= SIZE_MAX &&
           fg_bytes_sub(b, (size_t)offset, (size_t)size, out) == FG_STATUS_OK;
}
static int name(FgBytes strings, uint32_t offset, FgBytes *out) {
    if (offset >= strings.size)
        return 0;
    size_t available = strings.size - offset;
    if (available > FG_ELF_MAX_NAME)
        available = FG_ELF_MAX_NAME;
    const unsigned char *start = strings.data + offset, *end = memchr(start, 0, available);
    if (!end)
        return 0;
    *out = (FgBytes){start, (size_t)(end - start)};
    return 1;
}
static int named(FgBytes value, const char *expected) {
    return value.size == strlen(expected) && !memcmp(value.data, expected, value.size);
}
FgStatus fg_elf_check(FgBytes bytes, FgElfInfo *out, char *diagnostic, size_t diagnostic_bytes) {
    const char *reason = "invalid ELF";
    if (diagnostic && diagnostic_bytes)
        diagnostic[0] = 0;
#define REQUIRE(CONDITION, MESSAGE)                                                                \
    do {                                                                                           \
        if (!(CONDITION)) {                                                                        \
            reason = MESSAGE;                                                                      \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)
    REQUIRE(out && (!diagnostic_bytes || diagnostic), "invalid output buffer");
    REQUIRE(bytes.data && bytes.size >= 64 && bytes.size <= FG_ELF_MAX_BYTES,
            "ELF size outside limits");
    const unsigned char *h = bytes.data;
    REQUIRE(!memcmp(h, "\177ELF\002\001\001", 7) && h[7] == 0 && h[8] == 0 && u16(h + 16) == 1 &&
                u16(h + 18) == 247 && u32(h + 20) == 1 && u16(h + 52) == 64 && u16(h + 58) == 64,
            "unsupported ELF target or header");
    REQUIRE(u64(h + 24) == 0 && u64(h + 32) == 0 && u16(h + 54) == 0 && u16(h + 56) == 0 &&
                u32(h + 48) == 0,
            "unexpected executable header fields");
    uint16_t count = u16(h + 60), names_index = u16(h + 62);
    REQUIRE(count > 0 && count <= FG_ELF_MAX_SECTIONS && names_index < count,
            "invalid section count or string index");
    FgBytes table;
    uint64_t table_offset = u64(h + 40);
    REQUIRE(table_offset >= 64 && table_offset % 8 == 0 &&
                range(bytes, table_offset, (uint64_t)count * 64, &table),
            "invalid section table range");
    Section sections[FG_ELF_MAX_SECTIONS] = {0};
    size_t first_strings = SIZE_MAX, symtab = SIZE_MAX;
    FgElfInfo info = {0};
    for (size_t i = 0; i < count; i++) {
        const unsigned char *s = table.data + i * 64;
        Section *section = &sections[i];
        *section = (Section){.type = u32(s + 4),
                             .flags = u64(s + 8),
                             .offset = u64(s + 24),
                             .size = u64(s + 32),
                             .link = u32(s + 40),
                             .info = u32(s + 44),
                             .alignment = u64(s + 48),
                             .entry_size = u64(s + 56)};
        REQUIRE(!section->alignment || !(section->alignment & (section->alignment - 1)),
                "section alignment is not power of two");
        REQUIRE(!section->alignment || section->offset % section->alignment == 0,
                "unaligned section offset");
        REQUIRE(section->type != 4 && section->type != 9,
                "relocations require a future loader profile");
        REQUIRE(section->type != 8 || section->size == 0, "allocated NOBITS data is unsupported");
        REQUIRE(range(bytes, section->offset, section->size, &section->data),
                "section outside file");
        if (i == 0) {
            for (size_t j = 0; j < 64; j++)
                REQUIRE(s[j] == 0, "nonzero null section");
            continue;
        }
        if (section->size) {
            REQUIRE(section->offset >= 64, "section overlaps ELF header");
            uint64_t end = section->offset + section->size;
            REQUIRE(end <= table_offset || section->offset >= table_offset + (uint64_t)count * 64,
                    "section overlaps section table");
            for (size_t j = 1; j < i; j++)
                if (sections[j].size)
                    REQUIRE(end <= sections[j].offset ||
                                section->offset >= sections[j].offset + sections[j].size,
                            "overlapping section data");
        }
        if (section->type == 3) {
            REQUIRE(section->size && section->data.data[0] == 0 &&
                        section->data.data[section->size - 1] == 0,
                    "invalid string table");
            if (first_strings == SIZE_MAX)
                first_strings = i;
        }
        if (section->type == 2) {
            REQUIRE(symtab == SIZE_MAX, "multiple symbol tables unsupported");
            symtab = i;
        }
        if (section->flags & 4) {
            REQUIRE(section->type == 1 && section->flags == 6 && section->offset % 8 == 0 &&
                        section->size % 8 == 0,
                    "invalid executable section");
            info.executable_bytes += section->size;
            for (size_t pc = 0; pc < section->data.size;) {
                const unsigned char *insn = section->data.data + pc;
                if (insn[0] == 0x85 && (insn[1] >> 4) == 0) {
                    uint32_t id = u32(insn + 4);
                    REQUIRE(id >= 1 && id <= 4, "unknown helper ID");
                    info.helper_mask |= UINT32_C(1) << id;
                }
                size_t width = insn[0] == 0x18 ? 16 : 8;
                REQUIRE(width <= section->data.size - pc, "truncated wide instruction");
                if (width == 16)
                    REQUIRE(insn[8] == 0 && insn[9] == 0 && insn[10] == 0 && insn[11] == 0,
                            "invalid wide instruction continuation");
                pc += width;
            }
        }
    }
    REQUIRE(first_strings != SIZE_MAX && names_index == first_strings &&
                sections[names_index].type == 3,
            "loader requires shared first string table");
    REQUIRE(symtab != SIZE_MAX, "missing symbol table");
    Section *symbols = &sections[symtab];
    REQUIRE(symbols->link == first_strings && symbols->entry_size == 24 &&
                symbols->offset % 8 == 0 && symbols->size >= 24 && symbols->size % 24 == 0 &&
                symbols->size / 24 <= FG_ELF_MAX_SYMBOLS && symbols->info <= symbols->size / 24,
            "invalid symbol table layout");
    FgBytes strings = sections[first_strings].data;
    for (size_t i = 0; i < count; i++) {
        FgBytes section_name;
        REQUIRE(name(strings, u32(table.data + i * 64), &section_name), "invalid section name");
    }
    size_t entries = 0;
    uint64_t function_bytes = 0;
    for (size_t i = 0; i < symbols->data.size / 24; i++) {
        const unsigned char *s = symbols->data.data + i * 24;
        FgBytes symbol_name;
        REQUIRE(name(strings, u32(s), &symbol_name), "invalid symbol name");
        uint16_t index = u16(s + 6);
        unsigned type = s[4] & 15, binding = s[4] >> 4;
        uint64_t offset = u64(s + 8), size = u64(s + 16);
        if (i == 0) {
            for (size_t j = 0; j < 24; j++)
                REQUIRE(s[j] == 0, "nonzero null symbol");
            continue;
        }
        REQUIRE(index != 0, "undefined symbol");
        REQUIRE(binding <= 2 && (s[5] & ~3u) == 0, "invalid symbol binding/visibility");
        if (index == 0xfff1) {
            REQUIRE(type == 4 && binding == 0 && offset == 0 && size == 0,
                    "unsupported absolute symbol");
            continue;
        }
        REQUIRE(index < count, "symbol section outside table");
        Section *section = &sections[index];
        REQUIRE(offset <= section->size && size <= section->size - offset,
                "symbol outside section");
        if (type == 2) {
            REQUIRE(section->type == 1 && section->flags == 6 && size && offset % 8 == 0 &&
                        size % 8 == 0,
                    "invalid function range");
            REQUIRE(offset == 0 || section->data.data[offset - 8] != 0x18,
                    "function begins inside wide instruction");
            REQUIRE(section->data.data[offset + size - 8] != 0x18,
                    "function ends inside wide instruction");
            function_bytes += size;
            info.function_count++;
            REQUIRE(function_bytes <= FG_ELF_MAX_BYTES && info.function_count <= 4096,
                    "linked code exceeds limits");
        }
        if (named(symbol_name, "entry")) {
            REQUIRE(type == 2 && binding == 1 && s[5] == 0, "entry must be global function");
            entries++;
            info.entry_offset = section->offset + offset;
            info.entry_bytes = size;
        }
    }
    REQUIRE(entries == 1, "expected exactly one global entry function");
    *out = info;
    return FG_STATUS_OK;
fail:
    if (diagnostic && diagnostic_bytes)
        snprintf(diagnostic, diagnostic_bytes, "%s", reason);
    return FG_STATUS_INVALID;
#undef REQUIRE
}
