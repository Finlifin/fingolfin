#ifndef FG_CONTAINERS_H
#define FG_CONTAINERS_H
#include "util.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Fixed-layout byte copies, no element destructors or hidden synchronization.
 * create copies allocator (NULL selects default). Failure sets *out=NULL.
 * Inputs/outputs must not alias container storage. Views are invalidated by
 * mutation/destroy; keys must not be mutated in place. Comparators impose a
 * total order and do not mutate/reenter the container. Mutation allocates nothing. */
typedef struct FgVec FgVec;
FgStatus fg_vec_create(size_t capacity, size_t element_size, size_t alignment,
                       const FgAllocator *allocator, FgVec **out);
void fg_vec_destroy(FgVec *vec);
size_t fg_vec_length(const FgVec *vec);
void *fg_vec_at(FgVec *vec, size_t index);
FgStatus fg_vec_push(FgVec *vec, const void *value);
FgStatus fg_vec_pop(FgVec *vec, void *out);
void fg_vec_clear(FgVec *vec);

typedef int (*FgCompare)(const void *a, const void *b, void *context);
typedef struct FgMap FgMap;
FgStatus fg_map_create(size_t capacity, size_t key_size, size_t value_size, size_t alignment,
                       FgCompare compare, void *context, const FgAllocator *allocator, FgMap **out);
void fg_map_destroy(FgMap *map);
size_t fg_map_length(const FgMap *map);
/* put replaces existing values even at capacity. inserted is optional. */
FgStatus fg_map_put(FgMap *map, const void *key, const void *value, int *inserted);
FgStatus fg_map_find(FgMap *map, const void *key, void **value);
FgStatus fg_map_remove(FgMap *map, const void *key, void *old_value);
FgStatus fg_map_at(FgMap *map, size_t index, const void **key, void **value);
void fg_map_clear(FgMap *map);
/* A set is a map with zero-sized values; put/find/remove use NULL value slots. */
typedef FgMap FgSet;
FgStatus fg_set_create(size_t capacity, size_t key_size, size_t alignment, FgCompare compare,
                       void *context, const FgAllocator *allocator, FgSet **out);

typedef struct FgQueue FgQueue;
FgStatus fg_queue_create(size_t capacity, size_t element_size, size_t alignment,
                         const FgAllocator *allocator, FgQueue **out);
void fg_queue_destroy(FgQueue *queue);
size_t fg_queue_length(const FgQueue *queue);
FgStatus fg_queue_push(FgQueue *queue, const void *value);
FgStatus fg_queue_pop(FgQueue *queue, void *out);
/* displaced/overwritten are optional. Zero capacity rejects without mutation. */
FgStatus fg_queue_overwrite(FgQueue *queue, const void *value, void *displaced, int *overwritten);
const void *fg_queue_at(const FgQueue *queue, size_t index);
void fg_queue_clear(FgQueue *queue);

typedef struct FgHistogram FgHistogram;
FgStatus fg_histogram_create(const int64_t *bounds, size_t bound_count, size_t max_buckets,
                             const FgAllocator *allocator, FgHistogram **out);
void fg_histogram_destroy(FgHistogram *histogram);
FgStatus fg_histogram_add(FgHistogram *histogram, int64_t sample, uint64_t weight);
uint64_t fg_histogram_total(const FgHistogram *histogram);
const uint64_t *fg_histogram_counts(const FgHistogram *histogram, size_t *count);
void fg_histogram_clear(FgHistogram *histogram);
#ifdef __cplusplus
}
#endif
#endif
