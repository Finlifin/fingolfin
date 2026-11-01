#ifndef FG_HOST_H
#define FG_HOST_H
#include "containers.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_HOST_CONFIG_VERSION 1u
typedef struct FgCounterConfig {
    uint32_t slot;
    uint64_t initial;
} FgCounterConfig;
typedef struct FgMapConfig {
    uint32_t slot;
    size_t capacity;
} FgMapConfig;
typedef struct FgHistogramConfig {
    uint32_t slot;
    const int64_t *bounds;
    size_t bound_count, max_buckets;
} FgHistogramConfig;
typedef struct FgHostConfig {
    uint32_t version;
    size_t struct_bytes;
    const FgCounterConfig *counters;
    size_t counter_count;
    const FgMapConfig *maps;
    size_t map_count;
    const FgHistogramConfig *histograms;
    size_t histogram_count;
    size_t output_capacity;
} FgHostConfig;
typedef struct FgRecord {
    uint32_t tag;
    uint64_t value;
} FgRecord;
typedef struct FgHost FgHost;
/* Copies configuration and allocates all storage. Same-kind duplicate slots are
 * invalid. Caller supplies synchronization and attachment/object ownership. */
FgStatus fg_host_create(const FgHostConfig *config, const FgAllocator *allocator, FgHost **out);
void fg_host_destroy(FgHost *host);
/* IDs 1 counter_add, 2 emit, 3 map_add, 4 histogram_add; unused words must be 0.
 * Each failed operation preserves its container. Earlier helpers are not rolled back. */
FgStatus fg_host_invoke(FgHost *host, uint32_t helper, const uint64_t args[5]);
FgStatus fg_host_counter(const FgHost *host, uint32_t slot, uint64_t *value);
FgStatus fg_host_map_value(const FgHost *host, uint32_t slot, uint64_t key, uint64_t *value);
FgStatus fg_host_histogram(const FgHost *host, uint32_t slot, const uint64_t **counts,
                           size_t *count, uint64_t *total);
FgStatus fg_host_pop(FgHost *host, FgRecord *record);
size_t fg_host_output_length(const FgHost *host);
#ifdef __cplusplus
}
#endif
#endif
