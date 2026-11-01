#include "fingolfin/containers.h"
#include <stdalign.h>
#include <string.h>
struct FgVec {
    FgAllocator allocator;
    unsigned char *data;
    size_t capacity, length, size, stride;
};
static int valid_allocator(FgAllocator a) {
    return a.allocate && a.deallocate;
}
FgStatus fg_vec_create(size_t capacity, size_t size, size_t alignment, const FgAllocator *allocator,
                       FgVec **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgAllocator a = allocator ? *allocator : fg_allocator_default();
    if (!size || !valid_allocator(a))
        return FG_STATUS_INVALID;
    size_t stride, bytes;
    FgStatus status = fg_size_align(size, alignment, &stride);
    if (status != FG_STATUS_OK)
        return status;
    status = fg_size_mul(capacity, stride, &bytes);
    if (status != FG_STATUS_OK)
        return status;
    FgVec *v = a.allocate(a.context, sizeof(*v), alignof(FgVec));
    if (!v)
        return FG_STATUS_NOMEM;
    *v = (FgVec){a, NULL, capacity, 0, size, stride};
    if (bytes) {
        v->data = a.allocate(a.context, bytes, alignment);
        if (!v->data) {
            a.deallocate(a.context, v);
            return FG_STATUS_NOMEM;
        }
    }
    *out = v;
    return FG_STATUS_OK;
}
void fg_vec_destroy(FgVec *v) {
    if (v) {
        FgAllocator a = v->allocator;
        if (v->data)
            a.deallocate(a.context, v->data);
        a.deallocate(a.context, v);
    }
}
size_t fg_vec_length(const FgVec *v) {
    return v ? v->length : 0;
}
void *fg_vec_at(FgVec *v, size_t i) {
    return v && i < v->length ? v->data + i * v->stride : NULL;
}
FgStatus fg_vec_push(FgVec *v, const void *value) {
    if (!v || !value)
        return FG_STATUS_INVALID;
    if (v->length == v->capacity)
        return FG_STATUS_CAPACITY;
    memcpy(v->data + v->length * v->stride, value, v->size);
    v->length++;
    return FG_STATUS_OK;
}
FgStatus fg_vec_pop(FgVec *v, void *out) {
    if (!v)
        return FG_STATUS_INVALID;
    if (!v->length)
        return FG_STATUS_NOT_FOUND;
    v->length--;
    if (out)
        memcpy(out, v->data + v->length * v->stride, v->size);
    return FG_STATUS_OK;
}
void fg_vec_clear(FgVec *v) {
    if (v)
        v->length = 0;
}
struct FgMap {
    FgAllocator allocator;
    FgVec *records;
    size_t key_size, value_size, value_offset;
    FgCompare compare;
    void *context;
};
FgStatus fg_map_create(size_t capacity, size_t key_size, size_t value_size, size_t alignment,
                       FgCompare compare, void *context, const FgAllocator *allocator,
                       FgMap **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgAllocator a = allocator ? *allocator : fg_allocator_default();
    if (!key_size || !compare || !valid_allocator(a))
        return FG_STATUS_INVALID;
    size_t offset, size;
    FgStatus status = fg_size_align(key_size, alignment, &offset);
    if (status != FG_STATUS_OK)
        return status;
    status = fg_size_add(offset, value_size, &size);
    if (status != FG_STATUS_OK)
        return status;
    FgMap *m = a.allocate(a.context, sizeof(*m), alignof(FgMap));
    if (!m)
        return FG_STATUS_NOMEM;
    *m = (FgMap){a, NULL, key_size, value_size, offset, compare, context};
    status = fg_vec_create(capacity, size, alignment, &a, &m->records);
    if (status != FG_STATUS_OK) {
        a.deallocate(a.context, m);
        return status;
    }
    *out = m;
    return FG_STATUS_OK;
}
void fg_map_destroy(FgMap *m) {
    if (m) {
        FgAllocator a = m->allocator;
        fg_vec_destroy(m->records);
        a.deallocate(a.context, m);
    }
}
size_t fg_map_length(const FgMap *m) {
    return m ? m->records->length : 0;
}
static size_t search(FgMap *m, const void *key, int *found) {
    FgVec *v = m->records;
    size_t lo = 0, hi = v->length;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (m->compare(v->data + mid * v->stride, key, m->context) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    *found = lo < v->length && m->compare(v->data + lo * v->stride, key, m->context) == 0;
    return lo;
}
FgStatus fg_map_put(FgMap *m, const void *key, const void *value, int *inserted) {
    if (!m || !key || (m->value_size && !value))
        return FG_STATUS_INVALID;
    int found;
    size_t i = search(m, key, &found);
    FgVec *v = m->records;
    if (!found && v->length == v->capacity)
        return FG_STATUS_CAPACITY;
    unsigned char *record = v->data + i * v->stride;
    if (!found) {
        memmove(record + v->stride, record, (v->length - i) * v->stride);
        memset(record, 0, v->stride);
        memcpy(record, key, m->key_size);
        v->length++;
    }
    if (m->value_size)
        memcpy(record + m->value_offset, value, m->value_size);
    if (inserted)
        *inserted = !found;
    return FG_STATUS_OK;
}
FgStatus fg_map_find(FgMap *m, const void *key, void **value) {
    if (value)
        *value = NULL;
    if (!m || !key)
        return FG_STATUS_INVALID;
    int found;
    size_t i = search(m, key, &found);
    if (!found)
        return FG_STATUS_NOT_FOUND;
    if (value && m->value_size)
        *value = m->records->data + i * m->records->stride + m->value_offset;
    return FG_STATUS_OK;
}
FgStatus fg_map_remove(FgMap *m, const void *key, void *old_value) {
    if (!m || !key)
        return FG_STATUS_INVALID;
    int found;
    size_t i = search(m, key, &found);
    if (!found)
        return FG_STATUS_NOT_FOUND;
    FgVec *v = m->records;
    unsigned char *record = v->data + i * v->stride;
    if (old_value && m->value_size)
        memcpy(old_value, record + m->value_offset, m->value_size);
    memmove(record, record + v->stride, (v->length - i - 1) * v->stride);
    v->length--;
    return FG_STATUS_OK;
}
FgStatus fg_map_at(FgMap *m, size_t i, const void **key, void **value) {
    if (key)
        *key = NULL;
    if (value)
        *value = NULL;
    if (!m)
        return FG_STATUS_INVALID;
    unsigned char *record = fg_vec_at(m->records, i);
    if (!record)
        return FG_STATUS_NOT_FOUND;
    if (key)
        *key = record;
    if (value && m->value_size)
        *value = record + m->value_offset;
    return FG_STATUS_OK;
}
void fg_map_clear(FgMap *m) {
    if (m)
        fg_vec_clear(m->records);
}
FgStatus fg_set_create(size_t capacity, size_t key_size, size_t alignment, FgCompare compare,
                       void *context, const FgAllocator *a, FgSet **out) {
    return fg_map_create(capacity, key_size, 0, alignment, compare, context, a, out);
}
struct FgQueue {
    FgAllocator allocator;
    FgVec *storage;
    size_t head, length;
};
FgStatus fg_queue_create(size_t capacity, size_t size, size_t alignment,
                         const FgAllocator *allocator, FgQueue **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgAllocator a = allocator ? *allocator : fg_allocator_default();
    if (!valid_allocator(a))
        return FG_STATUS_INVALID;
    FgQueue *q = a.allocate(a.context, sizeof(*q), alignof(FgQueue));
    if (!q)
        return FG_STATUS_NOMEM;
    *q = (FgQueue){a, NULL, 0, 0};
    FgStatus status = fg_vec_create(capacity, size, alignment, &a, &q->storage);
    if (status != FG_STATUS_OK) {
        a.deallocate(a.context, q);
        return status;
    }
    *out = q;
    return FG_STATUS_OK;
}
void fg_queue_destroy(FgQueue *q) {
    if (q) {
        FgAllocator a = q->allocator;
        fg_vec_destroy(q->storage);
        a.deallocate(a.context, q);
    }
}
size_t fg_queue_length(const FgQueue *q) {
    return q ? q->length : 0;
}
static size_t ring_index(const FgQueue *q, size_t i) {
    size_t tail = q->storage->capacity - q->head;
    return i < tail ? q->head + i : i - tail;
}
const void *fg_queue_at(const FgQueue *q, size_t i) {
    return q && i < q->length ? q->storage->data + ring_index(q, i) * q->storage->stride : NULL;
}
FgStatus fg_queue_push(FgQueue *q, const void *value) {
    if (!q || !value)
        return FG_STATUS_INVALID;
    if (q->length == q->storage->capacity)
        return FG_STATUS_CAPACITY;
    memcpy(q->storage->data + ring_index(q, q->length) * q->storage->stride, value,
           q->storage->size);
    q->length++;
    return FG_STATUS_OK;
}
FgStatus fg_queue_pop(FgQueue *q, void *out) {
    if (!q)
        return FG_STATUS_INVALID;
    if (!q->length)
        return FG_STATUS_NOT_FOUND;
    if (out)
        memcpy(out, fg_queue_at(q, 0), q->storage->size);
    q->head = (q->head + 1 == q->storage->capacity) ? 0 : q->head + 1;
    q->length--;
    return FG_STATUS_OK;
}
FgStatus fg_queue_overwrite(FgQueue *q, const void *value, void *displaced, int *overwritten) {
    if (!q || !value)
        return FG_STATUS_INVALID;
    if (!q->storage->capacity)
        return FG_STATUS_CAPACITY;
    int full = q->length == q->storage->capacity;
    if (full) {
        FgStatus status = fg_queue_pop(q, displaced);
        if (status != FG_STATUS_OK)
            return status;
    }
    FgStatus status = fg_queue_push(q, value);
    if (status == FG_STATUS_OK && overwritten)
        *overwritten = full;
    return status;
}
void fg_queue_clear(FgQueue *q) {
    if (q) {
        q->head = 0;
        q->length = 0;
    }
}
struct FgHistogram {
    FgAllocator allocator;
    int64_t *bounds;
    uint64_t *counts;
    size_t n;
    uint64_t total;
};
FgStatus fg_histogram_create(const int64_t *bounds, size_t n, size_t max,
                             const FgAllocator *allocator, FgHistogram **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgAllocator a = allocator ? *allocator : fg_allocator_default();
    if (!valid_allocator(a) || (n && !bounds))
        return FG_STATUS_INVALID;
    size_t count, bytes;
    FgStatus status = fg_size_add(n, 1, &count);
    if (status != FG_STATUS_OK)
        return status;
    if (count > max)
        return FG_STATUS_CAPACITY;
    status = fg_size_mul(count, sizeof(uint64_t), &bytes);
    if (status != FG_STATUS_OK)
        return status;
    for (size_t i = 1; i < n; i++)
        if (bounds[i - 1] >= bounds[i])
            return FG_STATUS_INVALID;
    FgHistogram *h = a.allocate(a.context, sizeof(*h), alignof(FgHistogram));
    if (!h)
        return FG_STATUS_NOMEM;
    *h = (FgHistogram){a, NULL, NULL, n, 0};
    if (n) {
        h->bounds = a.allocate(a.context, n * sizeof(int64_t), alignof(int64_t));
        if (!h->bounds) {
            fg_histogram_destroy(h);
            return FG_STATUS_NOMEM;
        }
        memcpy(h->bounds, bounds, n * sizeof(int64_t));
    }
    h->counts = a.allocate(a.context, bytes, alignof(uint64_t));
    if (!h->counts) {
        fg_histogram_destroy(h);
        return FG_STATUS_NOMEM;
    }
    memset(h->counts, 0, bytes);
    *out = h;
    return FG_STATUS_OK;
}
void fg_histogram_destroy(FgHistogram *h) {
    if (h) {
        FgAllocator a = h->allocator;
        if (h->bounds)
            a.deallocate(a.context, h->bounds);
        if (h->counts)
            a.deallocate(a.context, h->counts);
        a.deallocate(a.context, h);
    }
}
FgStatus fg_histogram_add(FgHistogram *h, int64_t sample, uint64_t weight) {
    if (!h)
        return FG_STATUS_INVALID;
    size_t lo = 0, hi = h->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (h->bounds[mid] <= sample)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (h->total > UINT64_MAX - weight || h->counts[lo] > UINT64_MAX - weight)
        return FG_STATUS_OVERFLOW;
    h->counts[lo] += weight;
    h->total += weight;
    return FG_STATUS_OK;
}
uint64_t fg_histogram_total(const FgHistogram *h) {
    return h ? h->total : 0;
}
const uint64_t *fg_histogram_counts(const FgHistogram *h, size_t *count) {
    if (count)
        *count = h ? h->n + 1 : 0;
    return h ? h->counts : NULL;
}
void fg_histogram_clear(FgHistogram *h) {
    if (h) {
        memset(h->counts, 0, (h->n + 1) * sizeof(uint64_t));
        h->total = 0;
    }
}
