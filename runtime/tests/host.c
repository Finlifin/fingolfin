#include "fingolfin/host.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct Tracker {
    size_t calls, frees, fail;
} Tracker;
static void *allocate(void *ctx, size_t size, size_t align) {
    Tracker *t = ctx;
    if (t->calls++ == t->fail)
        return NULL;
    FgAllocator a = fg_allocator_default();
    return a.allocate(NULL, size, align);
}
static void deallocate(void *ctx, void *p) {
    Tracker *t = ctx;
    t->frees++;
    free(p);
}
static FgStatus call(FgHost *host, uint32_t id, uint64_t a, uint64_t b, uint64_t c) {
    uint64_t args[5] = {a, b, c, 0, 0};
    return fg_host_invoke(host, id, args);
}
int main(void) {
    FgCounterConfig counters[] = {{0, 0}, {1, UINT64_MAX}};
    FgMapConfig maps[] = {{0, 2}};
    int64_t bounds[] = {0, 10};
    FgHistogramConfig histograms[] = {{0, bounds, 2, 3}};
    FgHostConfig config = {
        FG_HOST_CONFIG_VERSION, sizeof(FgHostConfig), counters, 2, maps, 1, histograms, 1, 2};
    Tracker tracker = {0, 0, SIZE_MAX};
    FgAllocator allocator = {&tracker, allocate, deallocate};
    FgHost *host = NULL;
    assert(fg_host_create(&config, &allocator, &host) == FG_STATUS_OK);
    size_t allocations = tracker.calls;
    tracker.fail = allocations;
    uint64_t value = 0, total;
    size_t count;
    const uint64_t *counts;
    for (size_t i = 0; i < 10000; i++) {
        assert(call(host, 1, 0, 1, 0) == FG_STATUS_OK);
        assert(call(host, 2, 7, 9, 0) == FG_STATUS_OK);
        FgRecord record;
        assert(fg_host_pop(host, &record) == FG_STATUS_OK && record.tag == 7 && record.value == 9);
        assert(call(host, 3, 0, 1, 1) == FG_STATUS_OK);
        assert(call(host, 4, 0, (uint64_t)-1, 1) == FG_STATUS_OK);
    }
    assert(tracker.calls == allocations);
    assert(fg_host_counter(host, 0, &value) == FG_STATUS_OK && value == 10000);
    assert(fg_host_map_value(host, 0, 1, &value) == FG_STATUS_OK && value == 10000);
    assert(fg_host_histogram(host, 0, &counts, &count, &total) == FG_STATUS_OK && count == 3 &&
           counts[0] == 10000 && total == 10000);
    assert(call(host, 1, 1, 1, 0) == FG_STATUS_OVERFLOW);
    assert(fg_host_counter(host, 1, &value) == FG_STATUS_OK && value == UINT64_MAX);
    assert(call(host, 3, 0, 2, UINT64_MAX) == FG_STATUS_OK);
    assert(call(host, 3, 0, 2, 1) == FG_STATUS_OVERFLOW);
    assert(call(host, 3, 0, 3, 1) == FG_STATUS_CAPACITY);
    assert(call(host, 4, 0, 10, UINT64_MAX) == FG_STATUS_OVERFLOW && counts[2] == 0);
    assert(call(host, 2, 1, 1, 0) == FG_STATUS_OK);
    assert(call(host, 2, 1, 2, 0) == FG_STATUS_OK);
    assert(call(host, 2, 1, 3, 0) == FG_STATUS_CAPACITY);
    assert(call(host, 1, 9, 1, 0) == FG_STATUS_NOT_FOUND);
    assert(call(host, 4, 9, 0, 1) == FG_STATUS_NOT_FOUND);
    assert(call(host, 5, 0, 0, 0) == FG_STATUS_INVALID);
    assert(call(host, 1, UINT64_MAX, 0, 0) == FG_STATUS_INVALID);
    uint64_t invalid[] = {0, 1, 0, 0, 1};
    assert(fg_host_invoke(host, 1, invalid) == FG_STATUS_INVALID);
    assert(call(host, 1, 0, 1, 1) == FG_STATUS_INVALID);
    FgHost *other = NULL;
    assert(fg_host_create(&config, NULL, &other) == FG_STATUS_OK);
    assert(fg_host_counter(other, 0, &value) == FG_STATUS_OK && value == 0);
    fg_host_destroy(other);
    fg_host_destroy(host);
    assert(tracker.frees == allocations);
    for (size_t fail = 0; fail < allocations; fail++) {
        tracker = (Tracker){0, 0, fail};
        host = (void *)1;
        assert(fg_host_create(&config, &allocator, &host) == FG_STATUS_NOMEM && host == NULL);
        assert(tracker.frees == fail);
    }
    counters[1].slot = 0;
    assert(fg_host_create(&config, NULL, &host) == FG_STATUS_INVALID && host == NULL);
    counters[1].slot = 1;
    bounds[1] = 0;
    assert(fg_host_create(&config, NULL, &host) == FG_STATUS_INVALID && host == NULL);
    puts("PASS: C Host four helpers, isolation/errors, all allocation failures, 10000 "
         "allocation-free calls each");
}
