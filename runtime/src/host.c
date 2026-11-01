#include "fingolfin/host.h"
#include <stdalign.h>
struct FgHost {
    FgAllocator allocator;
    FgMap *counters, *maps, *histograms;
    FgQueue *output;
};
static int compare32(const void *a, const void *b, void *context) {
    (void)context;
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}
static int compare64(const void *a, const void *b, void *context) {
    (void)context;
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static FgStatus vacant(FgMap *map, uint32_t slot) {
    return fg_map_find(map, &slot, NULL) == FG_STATUS_NOT_FOUND ? FG_STATUS_OK : FG_STATUS_INVALID;
}
void fg_host_destroy(FgHost *h) {
    if (!h)
        return;
    FgAllocator a = h->allocator;
    for (size_t i = 0; i < fg_map_length(h->maps); i++) {
        void *v = NULL;
        (void)fg_map_at(h->maps, i, NULL, &v);
        fg_map_destroy(*(FgMap **)v);
    }
    for (size_t i = 0; i < fg_map_length(h->histograms); i++) {
        void *v = NULL;
        (void)fg_map_at(h->histograms, i, NULL, &v);
        fg_histogram_destroy(*(FgHistogram **)v);
    }
    fg_map_destroy(h->counters);
    fg_map_destroy(h->maps);
    fg_map_destroy(h->histograms);
    fg_queue_destroy(h->output);
    a.deallocate(a.context, h);
}
FgStatus fg_host_create(const FgHostConfig *c, const FgAllocator *allocator, FgHost **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgAllocator a = allocator ? *allocator : fg_allocator_default();
    if (!c || c->version != FG_HOST_CONFIG_VERSION || c->struct_bytes != sizeof(*c) ||
        !a.allocate || !a.deallocate || (c->counter_count && !c->counters) ||
        (c->map_count && !c->maps) || (c->histogram_count && !c->histograms))
        return FG_STATUS_INVALID;
    FgHost *h = a.allocate(a.context, sizeof(*h), alignof(FgHost));
    if (!h)
        return FG_STATUS_NOMEM;
    *h = (FgHost){.allocator = a};
    FgStatus status;
#define TRY(EXPR)                                                                                  \
    do {                                                                                           \
        status = (EXPR);                                                                           \
        if (status != FG_STATUS_OK)                                                                \
            goto fail;                                                                             \
    } while (0)
    TRY(fg_map_create(c->counter_count, sizeof(uint32_t), sizeof(uint64_t), alignof(uint64_t),
                      compare32, NULL, &a, &h->counters));
    TRY(fg_map_create(c->map_count, sizeof(uint32_t), sizeof(FgMap *), alignof(max_align_t),
                      compare32, NULL, &a, &h->maps));
    TRY(fg_map_create(c->histogram_count, sizeof(uint32_t), sizeof(FgHistogram *),
                      alignof(max_align_t), compare32, NULL, &a, &h->histograms));
    TRY(fg_queue_create(c->output_capacity, sizeof(FgRecord), alignof(FgRecord), &a, &h->output));
    for (size_t i = 0; i < c->counter_count; i++) {
        TRY(vacant(h->counters, c->counters[i].slot));
        TRY(fg_map_put(h->counters, &c->counters[i].slot, &c->counters[i].initial, NULL));
    }
    for (size_t i = 0; i < c->map_count; i++) {
        TRY(vacant(h->maps, c->maps[i].slot));
        FgMap *map = NULL;
        TRY(fg_map_create(c->maps[i].capacity, sizeof(uint64_t), sizeof(uint64_t),
                          alignof(uint64_t), compare64, NULL, &a, &map));
        status = fg_map_put(h->maps, &c->maps[i].slot, &map, NULL);
        if (status != FG_STATUS_OK) {
            fg_map_destroy(map);
            goto fail;
        }
    }
    for (size_t i = 0; i < c->histogram_count; i++) {
        const FgHistogramConfig *hc = &c->histograms[i];
        TRY(vacant(h->histograms, hc->slot));
        FgHistogram *histogram = NULL;
        TRY(fg_histogram_create(hc->bounds, hc->bound_count, hc->max_buckets, &a, &histogram));
        status = fg_map_put(h->histograms, &hc->slot, &histogram, NULL);
        if (status != FG_STATUS_OK) {
            fg_histogram_destroy(histogram);
            goto fail;
        }
    }
#undef TRY
    *out = h;
    return FG_STATUS_OK;
fail:
    fg_host_destroy(h);
    return status;
}
FgStatus fg_host_invoke(FgHost *h, uint32_t helper, const uint64_t args[5]) {
    if (!h || !args || helper < 1 || helper > 4 || args[0] > UINT32_MAX)
        return FG_STATUS_INVALID;
    for (size_t i = helper <= 2 ? 2 : 3; i < 5; i++)
        if (args[i])
            return FG_STATUS_INVALID;
    uint32_t slot = (uint32_t)args[0];
    void *v = NULL;
    FgStatus status;
    switch (helper) {
    case 1:
        status = fg_map_find(h->counters, &slot, &v);
        return status == FG_STATUS_OK ? fg_counter_add(v, args[1]) : status;
    case 2: {
        FgRecord record = {.tag = slot, .value = args[1]};
        return fg_queue_push(h->output, &record);
    }
    case 3: {
        status = fg_map_find(h->maps, &slot, &v);
        if (status != FG_STATUS_OK)
            return status;
        FgMap *map = *(FgMap **)v;
        status = fg_map_find(map, &args[1], &v);
        if (status == FG_STATUS_OK)
            return fg_counter_add(v, args[2]);
        if (status != FG_STATUS_NOT_FOUND)
            return status;
        return fg_map_put(map, &args[1], &args[2], NULL);
    }
    case 4: {
        status = fg_map_find(h->histograms, &slot, &v);
        if (status != FG_STATUS_OK)
            return status;
        int64_t sample =
            args[1] <= INT64_MAX ? (int64_t)args[1] : -1 - (int64_t)(UINT64_MAX - args[1]);
        return fg_histogram_add(*(FgHistogram **)v, sample, args[2]);
    }
    default:
        return FG_STATUS_INVALID;
    }
}
FgStatus fg_host_counter(const FgHost *h, uint32_t slot, uint64_t *value) {
    if (!h || !value)
        return FG_STATUS_INVALID;
    void *v = NULL;
    FgStatus status = fg_map_find(h->counters, &slot, &v);
    if (status == FG_STATUS_OK)
        *value = *(uint64_t *)v;
    return status;
}
FgStatus fg_host_map_value(const FgHost *h, uint32_t slot, uint64_t key, uint64_t *value) {
    if (!h || !value)
        return FG_STATUS_INVALID;
    void *v = NULL;
    FgStatus status = fg_map_find(h->maps, &slot, &v);
    if (status != FG_STATUS_OK)
        return status;
    status = fg_map_find(*(FgMap **)v, &key, &v);
    if (status == FG_STATUS_OK)
        *value = *(uint64_t *)v;
    return status;
}
FgStatus fg_host_histogram(const FgHost *h, uint32_t slot, const uint64_t **counts, size_t *count,
                           uint64_t *total) {
    if (!h || !counts || !count || !total)
        return FG_STATUS_INVALID;
    void *v = NULL;
    FgStatus status = fg_map_find(h->histograms, &slot, &v);
    if (status != FG_STATUS_OK)
        return status;
    FgHistogram *histogram = *(FgHistogram **)v;
    *counts = fg_histogram_counts(histogram, count);
    *total = fg_histogram_total(histogram);
    return FG_STATUS_OK;
}
FgStatus fg_host_pop(FgHost *h, FgRecord *record) {
    return h ? fg_queue_pop(h->output, record) : FG_STATUS_INVALID;
}
size_t fg_host_output_length(const FgHost *h) {
    return h ? fg_queue_length(h->output) : 0;
}
