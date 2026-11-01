#ifndef FG_UTIL_H
#define FG_UTIL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum FgStatus {
    FG_STATUS_OK = 0,
    FG_STATUS_INVALID,
    FG_STATUS_OVERFLOW,
    FG_STATUS_CAPACITY,
    FG_STATUS_NOMEM,
    FG_STATUS_NOT_FOUND,
    FG_STATUS_BUDGET,
    FG_STATUS_VM,
    FG_STATUS_BUSY,
    FG_STATUS_CLOSED,
    FG_STATUS_CONFLICT,
    FG_STATUS_IO
} FgStatus;
/* Allocator must return suitably aligned storage or NULL. Callback/context must
 * outlive every object using it; callbacks must not reenter that object. */
typedef struct FgAllocator {
    void *context;
    void *(*allocate)(void *context, size_t bytes, size_t alignment);
    void (*deallocate)(void *context, void *pointer);
} FgAllocator;
FgAllocator fg_allocator_default(void);
FgStatus fg_size_add(size_t a, size_t b, size_t *out);
FgStatus fg_size_mul(size_t a, size_t b, size_t *out);
FgStatus fg_size_align(size_t bytes, size_t alignment, size_t *out);
/* Failure leaves output/counter/budget unchanged. */
FgStatus fg_counter_add(uint64_t *counter, uint64_t amount);
FgStatus fg_budget_charge(uint64_t *remaining, uint64_t amount);
const char *fg_status_string(FgStatus status);
#ifdef __cplusplus
}
#endif
#endif
