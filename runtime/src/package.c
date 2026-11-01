#include "fingolfin/package.h"
#include "fingolfin/hash.h"
#include <json-c/json.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct FgPackage {
    struct json_object *root;
    size_t count;
    FgPackageEntry *entries;
    const char *program_hash, *model_hash;
};
static const char *manifest_keys[] = {
    "version",           "profile",          "target",        "program_source_hash",
    "model_source_hash", "compiler_version", "clang_version", "clang_flags",
    "entries",           "typed_ir"};
static const char *entry_keys[] = {
    "function", "hook",     "dependencies", "helpers",           "signature",    "signature_hash",
    "elf",      "elf_hash", "frame_schema", "frame_schema_hash", "frame_header", "frame_hash"};
static struct json_object *get(struct json_object *o, const char *key) {
    struct json_object *value = NULL;
    json_object_object_get_ex(o, key, &value);
    return value;
}
static int exact_fields(struct json_object *o, const char *const *keys, size_t count) {
    if (!json_object_is_type(o, json_type_object) || (size_t)json_object_object_length(o) != count)
        return 0;
    for (size_t i = 0; i < count; i++) {
        struct json_object *unused;
        if (!json_object_object_get_ex(o, keys[i], &unused))
            return 0;
    }
    return 1;
}
static const char *string(struct json_object *o) {
    return json_object_is_type(o, json_type_string) ? json_object_get_string(o) : NULL;
}
static int equal_string(struct json_object *o, const char *s) {
    const char *text = string(o);
    return text && !strcmp(text, s);
}
static int hex(const char *s) {
    if (!s || strlen(s) != 64)
        return 0;
    for (size_t i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return 1;
}
static int hash_equal(const unsigned char digest[32], const char *text) {
    static const char digits[] = "0123456789abcdef";
    if (!hex(text))
        return 0;
    for (size_t i = 0; i < 32; i++)
        if (text[2 * i] != digits[digest[i] >> 4] || text[2 * i + 1] != digits[digest[i] & 15])
            return 0;
    return 1;
}
static int hash_bytes(FgBytes bytes, struct json_object *expected) {
    unsigned char digest[32];
    return fg_sha256(bytes, digest) == FG_STATUS_OK && hash_equal(digest, string(expected));
}
static int whitespace(unsigned char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}
static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static int unicode(const unsigned char *p) {
    int value = 0;
    for (size_t j = 0; j < 4; j++) {
        int v = hex_digit(p[j]);
        if (v < 0)
            return -1;
        value = value * 16 + v;
    }
    return value;
}
/* Bound DOM work before parsing; reject integer saturation and Unicode escapes
 * whose interpretation differs between parsers. Colons count raw object keys. */
static int preflight(FgBytes bytes, size_t *keys) {
    size_t tokens = 0, depth = 0;
    *keys = 0;
    for (size_t i = 0; i < bytes.size;) {
        unsigned char c = bytes.data[i++];
        if (whitespace(c) || c == ',')
            continue;
        if (c == ':') {
            (*keys)++;
            continue;
        }
        if (c == '}' || c == ']') {
            if (!depth)
                return 0;
            depth--;
            continue;
        }
        if (++tokens > FG_PACKAGE_MAX_TOKENS)
            return 0;
        if (c == '{' || c == '[') {
            if (++depth > 96)
                return 0;
            continue;
        }
        if (c == '"') {
            int closed = 0;
            while (i < bytes.size) {
                c = bytes.data[i++];
                if (c == '"') {
                    closed = 1;
                    break;
                }
                if (c < 32)
                    return 0;
                if (c == '\\') {
                    if (i == bytes.size)
                        return 0;
                    c = bytes.data[i++];
                    if (c == 'u') {
                        if (bytes.size - i < 4)
                            return 0;
                        int value = unicode(bytes.data + i);
                        i += 4;
                        if (value <= 0)
                            return 0;
                        if (value >= 0xd800 && value <= 0xdbff) {
                            if (bytes.size - i < 6 || bytes.data[i] != '\\' ||
                                bytes.data[i + 1] != 'u')
                                return 0;
                            int low = unicode(bytes.data + i + 2);
                            if (low < 0xdc00 || low > 0xdfff)
                                return 0;
                            i += 6;
                        } else if (value >= 0xdc00 && value <= 0xdfff)
                            return 0;
                    }
                }
            }
            if (!closed)
                return 0;
            continue;
        }
        size_t start = i - 1;
        while (i < bytes.size && !whitespace(bytes.data[i]) && bytes.data[i] != ',' &&
               bytes.data[i] != '}' && bytes.data[i] != ']' && bytes.data[i] != ':')
            i++;
        if (c == '-' || (c >= '0' && c <= '9')) {
            int negative = c == '-';
            size_t at = start + (size_t)negative;
            if (at == i)
                return 0;
            uint64_t value = 0, limit = negative ? UINT64_C(9223372036854775808) : UINT64_MAX;
            for (; at < i; at++) {
                unsigned digit = bytes.data[at] - '0';
                if (digit > 9 || value > (limit - digit) / 10)
                    return 0;
                value = value * 10 + digit;
            }
        }
    }
    return depth == 0;
}
static int write_hash(EVP_MD_CTX *ctx, const void *data, size_t size) {
    return EVP_DigestUpdate(ctx, data, size) == 1;
}
static int write_string(EVP_MD_CTX *ctx, const char *s, size_t len) {
    static const char hexes[] = "0123456789abcdef";
    if (!write_hash(ctx, "\"", 1))
        return 0;
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 32 && c != '"' && c != '\\')
            continue;
        if (!write_hash(ctx, s + start, i - start))
            return 0;
        const char *escape = NULL;
        switch (c) {
        case '"':
            escape = "\\\"";
            break;
        case '\\':
            escape = "\\\\";
            break;
        case '\b':
            escape = "\\b";
            break;
        case '\f':
            escape = "\\f";
            break;
        case '\n':
            escape = "\\n";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\t':
            escape = "\\t";
            break;
        default:
            break;
        }
        if (escape) {
            if (!write_hash(ctx, escape, 2))
                return 0;
        } else {
            char unicode_escape[] = {'\\', 'u', '0', '0', hexes[c >> 4], hexes[c & 15]};
            if (!write_hash(ctx, unicode_escape, 6))
                return 0;
        }
        start = i + 1;
    }
    return write_hash(ctx, s + start, len - start) && write_hash(ctx, "\"", 1);
}
static int compare_keys(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
/* mode 1=Manifest struct, 2=sorted entry map, 3=Entry struct, 0=serde_json::Value. */
static int canonical(EVP_MD_CTX *ctx, struct json_object *o, unsigned mode, unsigned depth,
                     size_t *key_count) {
    if (depth > 96)
        return 0;
    enum json_type type = json_object_get_type(o);
    if (mode && type != json_type_object)
        return 0;
    if (type == json_type_object) {
        const char *const *keys = NULL;
        const char **sorted = NULL;
        size_t count = (size_t)json_object_object_length(o);
        if (mode == 1) {
            keys = manifest_keys;
            if (!exact_fields(o, keys, 10))
                return 0;
        } else if (mode == 3) {
            keys = entry_keys;
            if (!exact_fields(o, keys, 12))
                return 0;
        } else if (count) {
            sorted = malloc(count * sizeof(*sorted));
            if (!sorted)
                return 0;
            size_t n = 0;
            json_object_object_foreach(o, key, value) {
                (void)value;
                sorted[n++] = key;
            }
            qsort(sorted, count, sizeof(*sorted), compare_keys);
            keys = sorted;
        }
        *key_count += count;
        int ok = write_hash(ctx, "{", 1);
        for (size_t i = 0; ok && i < count; i++) {
            unsigned child = mode == 2 ? 3 : (mode == 1 && !strcmp(keys[i], "entries") ? 2 : 0);
            ok = (!i || write_hash(ctx, ",", 1)) && write_string(ctx, keys[i], strlen(keys[i])) &&
                 write_hash(ctx, ":", 1) &&
                 canonical(ctx, get(o, keys[i]), child, depth + 1, key_count);
        }
        free(sorted);
        return ok && write_hash(ctx, "}", 1);
    }
    if (type == json_type_array) {
        if (!write_hash(ctx, "[", 1))
            return 0;
        size_t count = json_object_array_length(o);
        for (size_t i = 0; i < count; i++)
            if ((i && !write_hash(ctx, ",", 1)) ||
                !canonical(ctx, json_object_array_get_idx(o, i), 0, depth + 1, key_count))
                return 0;
        return write_hash(ctx, "]", 1);
    }
    if (type == json_type_string)
        return write_string(ctx, json_object_get_string(o), (size_t)json_object_get_string_len(o));
    if (type == json_type_double)
        return 0;
    const char *value =
        json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
    return value && write_hash(ctx, value, strlen(value));
}
static int hash_json(struct json_object *o, unsigned mode, const char *expected, size_t *keys) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
        return 0;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned size = 0;
    int ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) && canonical(ctx, o, mode, 0, keys) &&
             EVP_DigestFinal_ex(ctx, digest, &size) && size == 32 && hash_equal(digest, expected);
    EVP_MD_CTX_free(ctx);
    return ok;
}
static int integer(struct json_object *o, uint64_t *out) {
    if (!json_object_is_type(o, json_type_int) || json_object_get_int64(o) < 0)
        return 0;
    *out = json_object_get_uint64(o);
    return 1;
}
static int strings(struct json_object *o) {
    if (!json_object_is_type(o, json_type_array))
        return 0;
    for (size_t i = 0; i < json_object_array_length(o); i++)
        if (!string(json_object_array_get_idx(o, i)))
            return 0;
    return 1;
}
static int byte_array(struct json_object *array, size_t *budget, FgBytes *out) {
    if (!json_object_is_type(array, json_type_array))
        return 0;
    size_t size = json_object_array_length(array);
    if (!size || size > *budget)
        return 0;
    unsigned char *data = malloc(size);
    if (!data)
        return 0;
    for (size_t i = 0; i < size; i++) {
        uint64_t n;
        if (!integer(json_object_array_get_idx(array, i), &n) || n > 255) {
            free(data);
            return 0;
        }
        data[i] = (unsigned char)n;
    }
    *out = (FgBytes){data, size};
    *budget -= size;
    return 1;
}
static int type_matches(const FgFrameSchema *s, uint32_t index, struct json_object *type) {
    FgSchemaType node;
    if (fg_frame_schema_type(s, index, &node) != FG_STATUS_OK)
        return 0;
    if (equal_string(type, "Bool"))
        return node.kind == 2;
    if (!json_object_is_type(type, json_type_object) || json_object_object_length(type) != 1)
        return 0;
    struct json_object *inner;
    if (json_object_object_get_ex(type, "Integer", &inner)) {
        const char *keys[] = {"bits", "signed"};
        uint64_t bits;
        return exact_fields(inner, keys, 2) && integer(get(inner, "bits"), &bits) &&
               json_object_is_type(get(inner, "signed"), json_type_boolean) && node.kind == 1 &&
               node.bits == bits && node.is_signed == json_object_get_boolean(get(inner, "signed"));
    }
    if (json_object_object_get_ex(type, "Struct", &inner))
        return node.kind == 5 && string(inner) && *string(inner);
    if (json_object_object_get_ex(type, "Enum", &inner))
        return node.kind == 4 && string(inner) && *string(inner);
    if (json_object_object_get_ex(type, "Pointer", &inner))
        return node.kind == 3 && (string(inner) || json_object_is_type(inner, json_type_object));
    if (json_object_object_get_ex(type, "Array", &inner)) {
        uint64_t length;
        return node.kind == 6 && json_object_is_type(inner, json_type_array) &&
               json_object_array_length(inner) == 2 &&
               integer(json_object_array_get_idx(inner, 1), &length) && length == node.length &&
               type_matches(s, node.element_type, json_object_array_get_idx(inner, 0));
    }
    return 0;
}
static int signature_matches(FgPackageEntry *entry, struct json_object *signature) {
    const char *keys[] = {"parameters", "bindings", "returns", "hook"};
    if (!exact_fields(signature, keys, 4))
        return 0;
    struct json_object *parameters = get(signature, "parameters"),
                       *bindings = get(signature, "bindings"), *returns = get(signature, "returns"),
                       *hook = get(signature, "hook");
    if (!json_object_is_type(parameters, json_type_array) ||
        json_object_array_length(parameters) != entry->schema.parameters ||
        !json_object_is_type(bindings, json_type_object) ||
        (size_t)json_object_object_length(bindings) != entry->schema.bindings ||
        !!returns != entry->schema.has_result)
        return 0;
    if (entry->hook ? !equal_string(hook, entry->hook) : hook != NULL)
        return 0;
    for (uint32_t i = 0; i < entry->schema.slot_count; i++) {
        FgSchemaSlot slot;
        if (fg_frame_schema_slot(&entry->schema, i, &slot) != FG_STATUS_OK)
            return 0;
        struct json_object *type = NULL;
        if (slot.role == 0)
            type = json_object_array_get_idx(parameters, i);
        else if (slot.role == 2)
            type = returns;
        else {
            char name_buffer[4097];
            memcpy(name_buffer, slot.name.data, slot.name.size);
            name_buffer[slot.name.size] = 0;
            if (!json_object_object_get_ex(bindings, name_buffer, &type))
                return 0;
        }
        if (!type_matches(&entry->schema, slot.type_index, type))
            return 0;
    }
    return 1;
}
void fg_package_destroy(FgPackage *p) {
    if (p) {
        for (size_t i = 0; i < p->count; i++) {
            free((void *)p->entries[i].elf.data);
            free((void *)p->entries[i].frame_schema.data);
        }
        free(p->entries);
        json_object_put(p->root);
        free(p);
    }
}
FgStatus fg_package_load(FgBytes bytes, const FgPackageLimits *limits, FgPackage **out,
                         char *diagnostic, size_t diagnostic_bytes) {
    if (diagnostic && diagnostic_bytes)
        diagnostic[0] = 0;
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgPackage *p = NULL;
    struct json_tokener *tokener = NULL;
    struct json_object *root = NULL;
    const char *reason = "invalid package";
    FgStatus status = FG_STATUS_INVALID;
#define CHECK(COND, WHY)                                                                           \
    do {                                                                                           \
        if (!(COND)) {                                                                             \
            reason = WHY;                                                                          \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)
    CHECK(bytes.data && bytes.size && bytes.size <= FG_PACKAGE_MAX_BYTES &&
              (!diagnostic_bytes || diagnostic),
          "package size/output invalid");
    CHECK(limits && limits->version == FG_PACKAGE_LIMITS_VERSION &&
              limits->struct_bytes == sizeof(*limits) && limits->max_entries &&
              limits->max_entries <= 256 && limits->max_decoded_bytes &&
              limits->max_decoded_bytes <= FG_PACKAGE_MAX_BYTES && limits->max_frame_bytes &&
              limits->max_frame_bytes <= FG_PACKAGE_MAX_BYTES,
          "package limits invalid");
    CHECK((!limits->expected_model_hash || hex(limits->expected_model_hash)) &&
              (!limits->expected_program_hash || hex(limits->expected_program_hash)),
          "identity pin invalid");
    size_t raw_keys = 0;
    CHECK(preflight(bytes, &raw_keys), "JSON lexical limits/integers/Unicode invalid");
    tokener = json_tokener_new_ex(128);
    CHECK(tokener, "JSON parser allocation failed");
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    root = json_tokener_parse_ex(tokener, (const char *)bytes.data, (int)bytes.size);
    CHECK(json_tokener_get_error(tokener) == json_tokener_success, "JSON parse failed");
    size_t end = json_tokener_get_parse_end(tokener);
    while (end < bytes.size && whitespace(bytes.data[end]))
        end++;
    CHECK(end == bytes.size, "trailing JSON data");
    json_tokener_free(tokener);
    tokener = NULL;
    const char *root_keys[] = {"magic", "manifest", "integrity"};
    CHECK(exact_fields(root, root_keys, 3) && equal_string(get(root, "magic"), "FINGOLFIN-FGO"),
          "package envelope invalid");
    struct json_object *m = get(root, "manifest");
    CHECK(exact_fields(m, manifest_keys, 10), "manifest fields invalid");
    uint64_t version;
    CHECK(integer(get(m, "version"), &version) && version == 3 &&
              equal_string(get(m, "profile"), "development-function-frames") &&
              equal_string(get(m, "target"), "bpfel-v3"),
          "unsupported package version/profile/target");
    const char *program_hash = string(get(m, "program_source_hash")),
               *model_hash = string(get(m, "model_source_hash"));
    CHECK(hex(program_hash) && hex(model_hash), "source identities invalid");
    CHECK(
        (!limits->expected_program_hash || !strcmp(program_hash, limits->expected_program_hash)) &&
            (!limits->expected_model_hash || !strcmp(model_hash, limits->expected_model_hash)),
        "source identity pin mismatch");
    CHECK(string(get(m, "compiler_version")) && string(get(m, "clang_version")) &&
              strings(get(m, "clang_flags")) &&
              json_object_is_type(get(m, "typed_ir"), json_type_object),
          "toolchain/IR metadata invalid");
    struct json_object *entries = get(m, "entries");
    CHECK(json_object_is_type(entries, json_type_object), "entries must be object");
    size_t count = (size_t)json_object_object_length(entries);
    CHECK(count && count <= limits->max_entries, "entry limit exceeded");
    size_t keys = 0;
    CHECK(hash_json(m, 1, string(get(root, "integrity")), &keys), "package integrity mismatch");
    CHECK(keys + 3 == raw_keys, "duplicate JSON object keys");
    p = calloc(1, sizeof(*p));
    CHECK(p, "package allocation failed");
    p->root = root;
    root = NULL;
    p->program_hash = program_hash;
    p->model_hash = model_hash;
    p->entries = calloc(count, sizeof(*p->entries));
    CHECK(p->entries, "entry allocation failed");
    p->count = count;
    size_t index = 0, budget = limits->max_decoded_bytes;
    json_object_object_foreach(entries, id, e) {
        FgPackageEntry *entry = &p->entries[index++];
        entry->function = string(get(e, "function"));
        entry->hook = string(get(e, "hook"));
        CHECK(entry->function && !strcmp(entry->function, id) && *id &&
                  (!get(e, "hook") || entry->hook) && strings(get(e, "dependencies")),
              "entry identity/dependencies invalid");
        CHECK(byte_array(get(e, "elf"), &budget, &entry->elf) &&
                  byte_array(get(e, "frame_schema"), &budget, &entry->frame_schema),
              "entry bytes/allocation budget invalid");
        CHECK(hash_bytes(entry->elf, get(e, "elf_hash")) &&
                  hash_bytes(entry->frame_schema, get(e, "frame_schema_hash")),
              "entry binary hash mismatch");
        struct json_object *header = get(e, "frame_header");
        CHECK(string(header) && hash_bytes((FgBytes){(const unsigned char *)string(header),
                                                     (size_t)json_object_get_string_len(header)},
                                           get(e, "frame_hash")),
              "frame header hash mismatch");
        size_t signature_keys = 0;
        CHECK(hash_json(get(e, "signature"), 0, string(get(e, "signature_hash")), &signature_keys),
              "signature hash mismatch");
        FgElfInfo elf_info;
        CHECK(fg_elf_check(entry->elf, &elf_info, NULL, 0) == FG_STATUS_OK, "invalid entry ELF");
        CHECK(fg_frame_schema_check(entry->frame_schema, &entry->schema, NULL, 0) == FG_STATUS_OK &&
                  entry->schema.frame_bytes <= limits->max_frame_bytes,
              "invalid or oversized frame schema");
        CHECK(signature_matches(entry, get(e, "signature")), "signature/schema mismatch");
        struct json_object *helpers = get(e, "helpers");
        CHECK(json_object_is_type(helpers, json_type_array) &&
                  json_object_array_length(helpers) <= 4,
              "helper list invalid");
        uint64_t previous = 0;
        for (size_t i = 0; i < json_object_array_length(helpers); i++) {
            uint64_t helper;
            CHECK(integer(json_object_array_get_idx(helpers, i), &helper) && helper > previous &&
                      helper <= 4,
                  "helper list unordered/unsupported");
            entry->helper_mask |= UINT32_C(1) << helper;
            previous = helper;
        }
        CHECK(entry->helper_mask == elf_info.helper_mask, "helper list differs from ELF");
    }
    *out = p;
    return FG_STATUS_OK;
fail:
    if (diagnostic && diagnostic_bytes)
        snprintf(diagnostic, diagnostic_bytes, "%s", reason);
    if (tokener)
        json_tokener_free(tokener);
    if (root)
        json_object_put(root);
    fg_package_destroy(p);
    return status;
#undef CHECK
}
FgStatus fg_package_read(const char *path, const FgPackageLimits *limits, FgPackage **out,
                         char *diagnostic, size_t diagnostic_bytes) {
    if (diagnostic && diagnostic_bytes)
        diagnostic[0] = 0;
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!path)
        return FG_STATUS_INVALID;
    FILE *file = fopen(path, "rb");
    if (!file)
        return FG_STATUS_IO;
    FgStatus status = FG_STATUS_IO;
    unsigned char *bytes = NULL;
    if (fseek(file, 0, SEEK_END))
        goto done;
    long size = ftell(file);
    if (size <= 0 || size > FG_PACKAGE_MAX_BYTES) {
        status = FG_STATUS_INVALID;
        goto done;
    }
    rewind(file);
    bytes = malloc((size_t)size);
    if (!bytes) {
        status = FG_STATUS_NOMEM;
        goto done;
    }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size || fgetc(file) != EOF || ferror(file))
        goto done;
    status =
        fg_package_load((FgBytes){bytes, (size_t)size}, limits, out, diagnostic, diagnostic_bytes);
done:
    free(bytes);
    fclose(file);
    return status;
}
size_t fg_package_count(const FgPackage *p) {
    return p ? p->count : 0;
}
const FgPackageEntry *fg_package_entry(const FgPackage *p, size_t i) {
    return p && i < p->count ? &p->entries[i] : NULL;
}
const FgPackageEntry *fg_package_find(const FgPackage *p, const char *function) {
    if (p && function)
        for (size_t i = 0; i < p->count; i++)
            if (!strcmp(p->entries[i].function, function))
                return &p->entries[i];
    return NULL;
}
const char *fg_package_program_hash(const FgPackage *p) {
    return p ? p->program_hash : NULL;
}
const char *fg_package_model_hash(const FgPackage *p) {
    return p ? p->model_hash : NULL;
}
