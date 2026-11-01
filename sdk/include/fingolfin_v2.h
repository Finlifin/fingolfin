#ifndef FINGOLFIN_SDK_V2_H
#define FINGOLFIN_SDK_V2_H
#include "fingolfin.h"
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FG_SDK_V2 2u
#define FG_V2_ERROR_ABI (-100)
#define FG_V2_ERROR_FRAME (-101)
#define FG_V2_ERROR_LEASE (-102)
#define FG_V2_ERROR_DISABLED (-103)
typedef struct FgDispatchReportV2 {
    uint32_t attempted, failed;
    int32_t first_error;
} FgDispatchReportV2;
typedef struct FgLeaseV2 FgLeaseV2;
/* Stack-owned linear token: do not copy, forge, mutate, or concurrently use it.
 * generation/hook/object/enabled/report are readable. Other fields are private
 * provider storage. release is mandatory after each successful acquire. */
struct FgLeaseV2 {
    uint32_t abi_version, struct_bytes;
    uint64_t generation, hook_id;
    FgObjectKey object;
    uint8_t enabled;
    FgDispatchReportV2 report;
    void *owner, *route;
    uint64_t selected;
    uint8_t dispatched;
    int32_t (*dispatch)(FgLeaseV2 *, const FgCallHeader *);
    void (*release)(FgLeaseV2 *);
};
typedef struct FgRuntimeV2 {
    uint32_t abi_version, struct_bytes;
    void *context;
    int32_t (*acquire)(void *, uint64_t, const FgObjectKey *, FgLeaseV2 *);
} FgRuntimeV2;
static inline int32_t fg_v2_acquire(const FgRuntimeV2 *runtime, uint64_t hook,
                                    const FgObjectKey *object, FgLeaseV2 *lease) {
    if (!lease)
        return FG_V2_ERROR_LEASE;
    /* lease must not already own an outstanding acquire. */
    memset(lease, 0, sizeof(*lease));
    if (!runtime || runtime->abi_version != FG_SDK_V2 || runtime->struct_bytes < sizeof(*runtime) ||
        !runtime->acquire || !object)
        return FG_V2_ERROR_ABI;
    return runtime->acquire(runtime->context, hook, object, lease);
}
static inline int32_t fg_v2_dispatch(FgLeaseV2 *lease, const FgCallHeader *frame) {
    if (!lease || lease->abi_version != FG_SDK_V2 || lease->struct_bytes != sizeof(*lease) ||
        !lease->dispatch || !lease->release)
        return FG_V2_ERROR_LEASE;
    if (!lease->enabled)
        return FG_V2_ERROR_DISABLED;
    if (!frame || frame->abi_version != FG_ABI_VERSION || frame->frame_bytes < sizeof(*frame) ||
        frame->hook_id != lease->hook_id || frame->config_generation != lease->generation ||
        frame->object.run != lease->object.run || frame->object.launch != lease->object.launch ||
        frame->object.slot != lease->object.slot ||
        frame->object.generation != lease->object.generation)
        return FG_V2_ERROR_FRAME;
    return lease->dispatch(lease, frame);
}
static inline void fg_v2_release(FgLeaseV2 *lease) {
    if (lease && lease->abi_version == FG_SDK_V2 && lease->struct_bytes == sizeof(*lease) &&
        lease->release)
        lease->release(lease);
}
/* Factory storage must stay readable until observe returns. Factory runs once,
 * only for an enabled lease, and receives its exact generation/object identity.
 * The runtime table/context must outlive this call. No exceptions/longjmp may
 * cross callbacks; use explicit acquire/release for custom control flow. */
typedef const FgCallHeader *(*FgFrameFactoryV2)(void *, const FgLeaseV2 *);
static inline int32_t fg_v2_observe(const FgRuntimeV2 *runtime, uint64_t hook,
                                    const FgObjectKey *object, FgFrameFactoryV2 factory,
                                    void *context, FgDispatchReportV2 *report) {
    FgDispatchReportV2 empty = {0, 0, 0};
    if (report)
        *report = empty;
    if (!factory)
        return FG_V2_ERROR_FRAME;
    FgLeaseV2 lease;
    int32_t result = fg_v2_acquire(runtime, hook, object, &lease);
    if (result)
        return result;
    if (lease.enabled)
        result = fg_v2_dispatch(&lease, factory(context, &lease));
    if (report)
        *report = lease.report;
    fg_v2_release(&lease);
    return result;
}
#ifdef __cplusplus
}
#endif
#endif
