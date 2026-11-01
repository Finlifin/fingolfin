#ifndef FG_PACKAGE_H
#define FG_PACKAGE_H
#include "elf.h"
#include "schema.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_PACKAGE_MAX_BYTES (32u * 1024u * 1024u)
#define FG_PACKAGE_MAX_TOKENS 1000000u
#define FG_PACKAGE_LIMITS_VERSION 1u
typedef struct FgPackageLimits {
    uint32_t version;
    size_t struct_bytes, max_entries, max_decoded_bytes, max_frame_bytes;
    const char *expected_program_hash, *expected_model_hash;
} FgPackageLimits;
typedef struct FgPackage FgPackage;
typedef struct FgPackageEntry {
    const char *function, *hook;
    FgBytes elf, frame_schema;
    FgFrameSchema schema;
    uint32_t helper_mask;
} FgPackageEntry;
/* Control-path API, requires FG_WITH_PACKAGE (json-c and OpenSSL).
 * Load owns a snapshot; input/limits/pin strings may be released after return.
 * Hashes detect corruption, not authorship. Missing pins accept any source identity.
 * Unknown versions/profiles/fields, duplicate JSON keys, floats and overflowing
 * JSON integers are rejected. Entire package validates before publication. */
FgStatus fg_package_load(FgBytes bytes, const FgPackageLimits *limits, FgPackage **out,
                         char *diagnostic, size_t diagnostic_bytes);
FgStatus fg_package_read(const char *path, const FgPackageLimits *limits, FgPackage **out,
                         char *diagnostic, size_t diagnostic_bytes);
void fg_package_destroy(FgPackage *package);
size_t fg_package_count(const FgPackage *package);
/* Entries, strings and schema views are borrowed until package destruction.
 * Read-only access is concurrent-safe; destruction requires all readers drained.
 * Entry enumeration follows input order; use find for stable identity lookup. */
const FgPackageEntry *fg_package_entry(const FgPackage *package, size_t index);
const FgPackageEntry *fg_package_find(const FgPackage *package, const char *function);
const char *fg_package_program_hash(const FgPackage *package);
const char *fg_package_model_hash(const FgPackage *package);
#ifdef __cplusplus
}
#endif
#endif
