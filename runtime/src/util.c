#include "fingolfin/util.h"
#include <stdalign.h>
#include <stdlib.h>
static void *allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    size_t rounded;
    if (alignment < alignof(max_align_t))
        alignment = alignof(max_align_t);
    if (fg_size_align(size, alignment, &rounded) != FG_STATUS_OK)
        return NULL;
    return aligned_alloc(alignment, rounded);
}
static void deallocate(void *context, void *pointer) {
    (void)context;
    free(pointer);
}
FgAllocator fg_allocator_default(void) {
    return (FgAllocator){NULL, allocate, deallocate};
}
FgStatus fg_size_add(size_t a, size_t b, size_t *out) {
    if (!out)
        return FG_STATUS_INVALID;
    if (a > SIZE_MAX - b)
        return FG_STATUS_OVERFLOW;
    *out = a + b;
    return FG_STATUS_OK;
}
FgStatus fg_size_mul(size_t a, size_t b, size_t *out) {
    if (!out)
        return FG_STATUS_INVALID;
    if (b && a > SIZE_MAX / b)
        return FG_STATUS_OVERFLOW;
    *out = a * b;
    return FG_STATUS_OK;
}
FgStatus fg_size_align(size_t bytes, size_t alignment, size_t *out) {
    if (!out || !alignment || (alignment & (alignment - 1)))
        return FG_STATUS_INVALID;
    size_t sum;
    FgStatus status = fg_size_add(bytes, alignment - 1, &sum);
    if (status != FG_STATUS_OK)
        return status;
    *out = sum & ~(alignment - 1);
    return FG_STATUS_OK;
}
FgStatus fg_counter_add(uint64_t *counter, uint64_t amount) {
    if (!counter)
        return FG_STATUS_INVALID;
    if (*counter > UINT64_MAX - amount)
        return FG_STATUS_OVERFLOW;
    *counter += amount;
    return FG_STATUS_OK;
}
FgStatus fg_budget_charge(uint64_t *remaining, uint64_t amount) {
    if (!remaining)
        return FG_STATUS_INVALID;
    if (*remaining < amount)
        return FG_STATUS_BUDGET;
    *remaining -= amount;
    return FG_STATUS_OK;
}
const char *fg_status_string(FgStatus status) {
    switch (status) {
    case FG_STATUS_OK:
        return "ok";
    case FG_STATUS_INVALID:
        return "invalid argument";
    case FG_STATUS_OVERFLOW:
        return "overflow";
    case FG_STATUS_CAPACITY:
        return "capacity exhausted";
    case FG_STATUS_NOMEM:
        return "allocation failed";
    case FG_STATUS_NOT_FOUND:
        return "not found";
    case FG_STATUS_BUDGET:
        return "budget exhausted";
    case FG_STATUS_VM:
        return "VM failure";
    case FG_STATUS_BUSY:
        return "execution slot busy";
    case FG_STATUS_CLOSED:
        return "port closed";
    case FG_STATUS_CONFLICT:
        return "configuration conflict";
    case FG_STATUS_IO:
        return "I/O failure";
    default:
        return "unknown status";
    }
}
