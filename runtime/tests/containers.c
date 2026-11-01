#include "fingolfin/containers.h"
#include "fingolfin/state.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Tracker {
    size_t allocations, frees, fail;
} Tracker;
static void *tracked_allocate(void *context, size_t size, size_t alignment) {
    Tracker *t = context;
    size_t call = t->allocations++;
    if (call == t->fail)
        return NULL;
    FgAllocator a = fg_allocator_default();
    return a.allocate(NULL, size, alignment);
}
static void tracked_free(void *context, void *pointer) {
    Tracker *t = context;
    assert(pointer);
    t->frees++;
    free(pointer);
}
static FgAllocator allocator(Tracker *t) {
    return (FgAllocator){t, tracked_allocate, tracked_free};
}
static int compare(const void *a, const void *b, void *context) {
    (void)context;
    uint64_t x, y;
    memcpy(&x, a, sizeof(x));
    memcpy(&y, b, sizeof(y));
    return (x > y) - (x < y);
}
static void utility(void) {
    size_t out = 7;
    assert(fg_size_add(SIZE_MAX, 1, &out) == FG_STATUS_OVERFLOW && out == 7);
    assert(fg_size_mul(SIZE_MAX, 2, &out) == FG_STATUS_OVERFLOW && out == 7);
    assert(fg_size_align(7, 3, &out) == FG_STATUS_INVALID && out == 7);
    assert(fg_size_align(SIZE_MAX, 8, &out) == FG_STATUS_OVERFLOW && out == 7);
    assert(fg_size_align(9, 8, &out) == FG_STATUS_OK && out == 16);
    uint64_t value = UINT64_MAX;
    assert(fg_counter_add(&value, 1) == FG_STATUS_OVERFLOW && value == UINT64_MAX);
    value = 3;
    assert(fg_budget_charge(&value, 4) == FG_STATUS_BUDGET && value == 3);
    assert(fg_budget_charge(&value, 3) == FG_STATUS_OK && value == 0);
}
static void vectors(void) {
    FgVec *v = NULL;
    assert(fg_vec_create(2, 3, 64, NULL, &v) == FG_STATUS_OK);
    const unsigned char a[] = {1, 2, 3}, b[] = {4, 5, 6};
    unsigned char out[3];
    assert(fg_vec_push(v, a) == FG_STATUS_OK);
    assert(fg_vec_push(v, b) == FG_STATUS_OK);
    assert((uintptr_t)fg_vec_at(v, 0) % 64 == 0 && (uintptr_t)fg_vec_at(v, 1) % 64 == 0);
    assert(fg_vec_push(v, a) == FG_STATUS_CAPACITY);
    assert(fg_vec_length(v) == 2);
    assert(fg_vec_pop(v, out) == FG_STATUS_OK && !memcmp(out, b, 3));
    fg_vec_clear(v);
    assert(fg_vec_pop(v, out) == FG_STATUS_NOT_FOUND);
    fg_vec_destroy(v);
    assert(fg_vec_create(0, 8, 8, NULL, &v) == FG_STATUS_OK);
    assert(fg_vec_push(v, a) == FG_STATUS_CAPACITY);
    fg_vec_destroy(v);
    assert(fg_vec_create(SIZE_MAX, 8, 8, NULL, &v) == FG_STATUS_OVERFLOW && v == NULL);
}
static void maps(void) {
    FgMap *m = NULL;
    assert(fg_map_create(3, 8, 8, 8, compare, NULL, NULL, &m) == FG_STATUS_OK);
    uint64_t keys[] = {9, 2, 6}, value = 11;
    int inserted = -1;
    for (size_t i = 0; i < 3; i++)
        assert(fg_map_put(m, &keys[i], &value, &inserted) == FG_STATUS_OK && inserted);
    const uint64_t expected[] = {2, 6, 9};
    for (size_t i = 0; i < 3; i++) {
        const void *k;
        void *v;
        assert(fg_map_at(m, i, &k, &v) == FG_STATUS_OK);
        assert(*(const uint64_t *)k == expected[i] && *(uint64_t *)v == 11);
    }
    uint64_t missing = 7;
    assert(fg_map_put(m, &missing, &value, NULL) == FG_STATUS_CAPACITY);
    value = 99;
    assert(fg_map_put(m, &keys[0], &value, &inserted) == FG_STATUS_OK && !inserted);
    void *view = NULL;
    assert(fg_map_find(m, &keys[0], &view) == FG_STATUS_OK && *(uint64_t *)view == 99);
    assert(fg_map_remove(m, &keys[1], &value) == FG_STATUS_OK && value == 11);
    assert(fg_map_find(m, &keys[1], &view) == FG_STATUS_NOT_FOUND && view == NULL);
    assert(fg_map_put(m, &missing, &value, NULL) == FG_STATUS_OK);
    fg_map_clear(m);
    assert(fg_map_length(m) == 0);
    fg_map_destroy(m);
    FgSet *set = NULL;
    assert(fg_set_create(1, 8, 8, compare, NULL, NULL, &set) == FG_STATUS_OK);
    assert(fg_map_put(set, &missing, NULL, &inserted) == FG_STATUS_OK && inserted);
    assert(fg_map_put(set, &missing, NULL, &inserted) == FG_STATUS_OK && !inserted);
    assert(fg_map_put(set, &keys[0], NULL, NULL) == FG_STATUS_CAPACITY);
    fg_map_destroy(set);
}
static void queues(void) {
    FgQueue *q = NULL;
    assert(fg_queue_create(2, 8, 8, NULL, &q) == FG_STATUS_OK);
    uint64_t value = 1, out = 0;
    assert(fg_queue_push(q, &value) == FG_STATUS_OK);
    value = 2;
    assert(fg_queue_push(q, &value) == FG_STATUS_OK);
    value = 3;
    assert(fg_queue_push(q, &value) == FG_STATUS_CAPACITY);
    int displaced = 0;
    assert(fg_queue_overwrite(q, &value, &out, &displaced) == FG_STATUS_OK && displaced &&
           out == 1);
    assert(*(const uint64_t *)fg_queue_at(q, 0) == 2 && *(const uint64_t *)fg_queue_at(q, 1) == 3);
    assert(fg_queue_pop(q, &out) == FG_STATUS_OK && out == 2);
    assert(fg_queue_pop(q, &out) == FG_STATUS_OK && out == 3);
    assert(fg_queue_pop(q, &out) == FG_STATUS_NOT_FOUND);
    fg_queue_destroy(q);
    assert(fg_queue_create(0, 8, 8, NULL, &q) == FG_STATUS_OK);
    assert(fg_queue_overwrite(q, &value, NULL, NULL) == FG_STATUS_CAPACITY);
    fg_queue_destroy(q);
}
static void histograms(void) {
    int64_t bounds[] = {0, 10};
    FgHistogram *h = NULL;
    assert(fg_histogram_create(bounds, 2, 3, NULL, &h) == FG_STATUS_OK);
    assert(fg_histogram_add(h, INT64_MIN, 2) == FG_STATUS_OK);
    assert(fg_histogram_add(h, 0, 3) == FG_STATUS_OK);
    assert(fg_histogram_add(h, 10, 4) == FG_STATUS_OK);
    assert(fg_histogram_add(h, INT64_MAX, 5) == FG_STATUS_OK);
    size_t n;
    const uint64_t *counts = fg_histogram_counts(h, &n);
    assert(n == 3 && counts[0] == 2 && counts[1] == 3 && counts[2] == 9 &&
           fg_histogram_total(h) == 14);
    assert(fg_histogram_add(h, 1, UINT64_MAX) == FG_STATUS_OVERFLOW && counts[1] == 3 &&
           fg_histogram_total(h) == 14);
    fg_histogram_clear(h);
    assert(fg_histogram_add(h, -1, UINT64_MAX) == FG_STATUS_OK);
    assert(fg_histogram_add(h, 10, 1) == FG_STATUS_OVERFLOW && counts[2] == 0);
    fg_histogram_destroy(h);
    bounds[1] = 0;
    assert(fg_histogram_create(bounds, 2, 3, NULL, &h) == FG_STATUS_INVALID && h == NULL);
    assert(fg_histogram_create(NULL, 0, 1, NULL, &h) == FG_STATUS_OK);
    fg_histogram_destroy(h);
}
static void allocation_failures(void) {
    /* Every allocation site must release all previously successful allocations. */
    for (size_t fail = 0; fail < 3; fail++) {
        Tracker t = {0, 0, fail};
        FgAllocator a = allocator(&t);
        FgMap *m = (void *)1;
        assert(fg_map_create(3, 8, 8, 8, compare, NULL, &a, &m) == FG_STATUS_NOMEM && m == NULL);
        assert(t.frees == fail);
        t = (Tracker){0, 0, fail};
        FgQueue *q = (void *)1;
        assert(fg_queue_create(3, 8, 8, &a, &q) == FG_STATUS_NOMEM && q == NULL);
        assert(t.frees == fail);
        t = (Tracker){0, 0, fail};
        FgHistogram *h = (void *)1;
        int64_t bound = 0;
        assert(fg_histogram_create(&bound, 1, 2, &a, &h) == FG_STATUS_NOMEM && h == NULL);
        assert(t.frees == fail);
    }
}
static void hot_path(void) {
    Tracker t = {0, 0, SIZE_MAX};
    FgAllocator a = allocator(&t);
    FgVec *v;
    FgMap *m;
    FgQueue *q;
    FgHistogram *h;
    assert(fg_vec_create(2, 8, 8, &a, &v) == FG_STATUS_OK);
    assert(fg_map_create(2, 8, 8, 8, compare, NULL, &a, &m) == FG_STATUS_OK);
    assert(fg_queue_create(2, 8, 8, &a, &q) == FG_STATUS_OK);
    int64_t bound = 0;
    assert(fg_histogram_create(&bound, 1, 2, &a, &h) == FG_STATUS_OK);
    size_t allocations = t.allocations;
    t.fail = allocations; /* Any later allocation fails. */
    for (uint64_t i = 0; i < 10000; i++) {
        assert(fg_vec_push(v, &i) == FG_STATUS_OK);
        assert(fg_vec_pop(v, NULL) == FG_STATUS_OK);
        assert(fg_map_put(m, &i, &i, NULL) == FG_STATUS_OK);
        assert(fg_map_remove(m, &i, NULL) == FG_STATUS_OK);
        assert(fg_queue_push(q, &i) == FG_STATUS_OK);
        assert(fg_queue_pop(q, NULL) == FG_STATUS_OK);
        assert(fg_histogram_add(h, (int64_t)i, 1) == FG_STATUS_OK);
    }
    assert(t.allocations == allocations && fg_histogram_total(h) == 10000);
    fg_vec_destroy(v);
    fg_map_destroy(m);
    fg_queue_destroy(q);
    fg_histogram_destroy(h);
    assert(t.frees == allocations);
}
static void states(void) {
    Tracker tracker = {0, 0, SIZE_MAX};
    FgAllocator a = allocator(&tracker);
    FgStateStore *store = NULL;
    assert(fg_state_create(5, sizeof(uint64_t), alignof(uint64_t), &a, &store) == FG_STATUS_OK);
    FgStateKey keys[] = {{1, 1, 1, 1, 1, 1},
                         {1, 2, 1, 1, 1, 1},
                         {2, 1, 1, 1, 1, 1},
                         {1, 1, 1, 1, 1, 2},
                         {1, 1, 2, 1, 1, 1}};
    size_t allocations = tracker.allocations;
    for (uint64_t i = 0; i < 5; ++i)
        assert(fg_state_put(store, &keys[i], &i, NULL) == FG_STATUS_OK);
    for (uint64_t i = 0; i < 5; ++i) {
        void *value;
        assert(fg_state_find(store, &keys[i], &value) == FG_STATUS_OK);
        assert(*(uint64_t *)value == i);
    }
    FgStateKey extra = {9, 9, 9, 9, 9, 9};
    uint64_t value = 100;
    assert(fg_state_put(store, &extra, &value, NULL) == FG_STATUS_CAPACITY);
    assert(fg_state_put(store, &keys[0], &value, NULL) == FG_STATUS_OK);
    assert(fg_state_remove_object(store, 1, 1, 1, 1) == 3);
    assert(fg_state_remove_configuration(store, 1, 1) == 2);
    assert(fg_state_length(store) == 0);
    for (uint64_t i = 0; i < 5; ++i)
        assert(fg_state_put(store, &keys[i], &i, NULL) == FG_STATUS_OK);
    assert(fg_state_remove_launch(store, 1, 1) == 4);
    assert(fg_state_remove_program(store, 1) == 1);
    assert(tracker.allocations == allocations);
    fg_state_destroy(store);
    assert(tracker.frees == allocations);
    for (size_t fail = 0; fail < 4; ++fail) {
        tracker = (Tracker){0, 0, fail};
        store = (void *)1;
        assert(fg_state_create(5, 8, 8, &a, &store) == FG_STATUS_NOMEM && store == NULL);
        assert(tracker.frees == fail);
    }
}
static void differential(void) {
    FgMap *map;
    FgQueue *queue;
    assert(fg_map_create(16, 8, 8, 8, compare, NULL, NULL, &map) == FG_STATUS_OK);
    assert(fg_queue_create(7, 8, 8, NULL, &queue) == FG_STATUS_OK);
    uint64_t seed = 17, values[64] = {0}, expected_queue[7] = {0};
    unsigned char present[64] = {0};
    size_t count = 0, queued = 0;
    for (size_t step = 0; step < 20000; ++step) {
        seed = seed * UINT64_C(6364136223846793005) + 1;
        uint64_t key = (seed >> 32) % 64, value = seed;
        if (seed & 1) {
            FgStatus status = fg_map_put(map, &key, &value, NULL);
            if (present[key] || count < 16) {
                assert(status == FG_STATUS_OK);
                if (!present[key]) {
                    present[key] = 1;
                    ++count;
                }
                values[key] = value;
            } else
                assert(status == FG_STATUS_CAPACITY);
        } else {
            uint64_t old = 0;
            FgStatus status = fg_map_remove(map, &key, &old);
            if (present[key]) {
                assert(status == FG_STATUS_OK && old == values[key]);
                present[key] = 0;
                --count;
            } else
                assert(status == FG_STATUS_NOT_FOUND);
        }
        assert(fg_map_length(map) == count);
        size_t index = 0;
        for (uint64_t k = 0; k < 64; ++k)
            if (present[k]) {
                const void *actual_key;
                void *actual_value;
                assert(fg_map_at(map, index++, &actual_key, &actual_value) == FG_STATUS_OK);
                assert(*(const uint64_t *)actual_key == k &&
                       *(uint64_t *)actual_value == values[k]);
            }
        if ((seed >> 16) & 1) {
            uint64_t old = 0;
            int displaced = -1;
            assert(fg_queue_overwrite(queue, &value, &old, &displaced) == FG_STATUS_OK);
            if (queued == 7) {
                assert(displaced && old == expected_queue[0]);
                memmove(expected_queue, expected_queue + 1, 6 * sizeof(uint64_t));
                --queued;
            } else
                assert(!displaced);
            expected_queue[queued++] = value;
        } else {
            uint64_t old = 0;
            FgStatus status = fg_queue_pop(queue, &old);
            if (queued) {
                assert(status == FG_STATUS_OK && old == expected_queue[0]);
                --queued;
                memmove(expected_queue, expected_queue + 1, queued * sizeof(uint64_t));
            } else
                assert(status == FG_STATUS_NOT_FOUND);
        }
        assert(fg_queue_length(queue) == queued);
        for (size_t j = 0; j < queued; ++j)
            assert(*(const uint64_t *)fg_queue_at(queue, j) == expected_queue[j]);
    }
    fg_map_destroy(map);
    fg_queue_destroy(queue);
}
int main(void) {
    utility();
    vectors();
    maps();
    queues();
    histograms();
    allocation_failures();
    hot_path();
    states();
    differential();
    puts("PASS: C util/containers, failure cleanup, 10000 allocation-free iterations");
    return 0;
}
