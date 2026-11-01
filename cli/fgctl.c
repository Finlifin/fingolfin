#include "fingolfin/package.h"
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>

/* json-c transfers ownership only on successful insertion. */
static int add(struct json_object *object, const char *key, struct json_object *value) {
    if (!value)
        return 0;
    if (json_object_object_add(object, key, value)) {
        json_object_put(value);
        return 0;
    }
    return 1;
}

static struct json_object *describe(const FgPackage *package) {
    struct json_object *root = json_object_new_object();
    struct json_object *entries = json_object_new_array();
    if (!root || !entries)
        goto fail;
    if (!add(root, "format_version", json_object_new_int(1)) ||
        !add(root, "program_source_hash",
             json_object_new_string(fg_package_program_hash(package))) ||
        !add(root, "model_source_hash", json_object_new_string(fg_package_model_hash(package))))
        goto fail;
    for (size_t i = 0; i < fg_package_count(package); i++) {
        const FgPackageEntry *entry = fg_package_entry(package, i);
        struct json_object *item = json_object_new_object();
        if (!item)
            goto fail;
        if (!add(item, "function", json_object_new_string(entry->function)) ||
            !add(item, "hook", json_object_new_string(entry->hook ? entry->hook : "")) ||
            !add(item, "frame_bytes", json_object_new_uint64(entry->schema.frame_bytes)) ||
            !add(item, "alignment", json_object_new_int64(entry->schema.alignment)) ||
            !add(item, "parameters", json_object_new_int64(entry->schema.parameters)) ||
            !add(item, "bindings", json_object_new_int64(entry->schema.bindings)) ||
            !add(item, "has_result", json_object_new_boolean(entry->schema.has_result)) ||
            !add(item, "helper_mask", json_object_new_int64(entry->helper_mask)) ||
            !add(item, "elf_bytes", json_object_new_uint64(entry->elf.size)) ||
            json_object_array_add(entries, item)) {
            json_object_put(item);
            goto fail;
        }
    }
    /* add consumes entries even on insertion failure. */
    if (!add(root, "entries", entries)) {
        entries = NULL;
        goto fail;
    }
    return root;
fail:
    json_object_put(entries);
    json_object_put(root);
    return NULL;
}

int fgctl_session(int argc, char **argv);
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "session"))
        return fgctl_session(argc, argv);
    const char *usage = "usage: fgctl inspect PACKAGE [--program-hash SHA256] "
                        "[--model-hash SHA256]\n"
                        "       fgctl session SESSION_ID -- EXECUTABLE [ARGS...]\n";
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        return fputs(usage, stdout) < 0 || fflush(stdout) ? 1 : 0;
    }
    if (argc < 3 || strcmp(argv[1], "inspect")) {
        fputs(usage, stderr);
        return 2;
    }
    FgPackageLimits limits = {FG_PACKAGE_LIMITS_VERSION, sizeof(limits), 256, FG_PACKAGE_MAX_BYTES,
                              FG_PACKAGE_MAX_BYTES,      NULL,           NULL};
    for (int i = 3; i < argc; i += 2) {
        const char **pin = NULL;
        if (!strcmp(argv[i], "--program-hash"))
            pin = &limits.expected_program_hash;
        else if (!strcmp(argv[i], "--model-hash"))
            pin = &limits.expected_model_hash;
        if (!pin || *pin || i + 1 >= argc) {
            fputs(usage, stderr);
            return 2;
        }
        *pin = argv[i + 1];
    }
    FgPackage *package = NULL;
    char diagnostic[512] = {0};
    FgStatus status = fg_package_read(argv[2], &limits, &package, diagnostic, sizeof(diagnostic));
    if (status != FG_STATUS_OK) {
        fprintf(stderr, "fgctl: %s: %s\n", fg_status_string(status), diagnostic);
        return 1;
    }
    struct json_object *result = describe(package);
    fg_package_destroy(package);
    if (!result) {
        fputs("fgctl: out of memory\n", stderr);
        return 1;
    }
    const char *encoded = json_object_to_json_string_ext(result, JSON_C_TO_STRING_PRETTY);
    int failed = !encoded || puts(encoded) < 0 || fflush(stdout);
    json_object_put(result);
    return failed ? 1 : 0;
}
