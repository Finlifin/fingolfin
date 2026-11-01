#ifndef FINGOLFIN_SDK_H
#define FINGOLFIN_SDK_H
#include <stddef.h>
#include <stdint.h>
#if !defined(__linux__) || !defined(__x86_64__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Fingolfin SDK v1 requires Linux x86-64 little-endian"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FG_ABI_VERSION 1u
#define FG_OK 0
#define FG_ERROR_ABI (-1)
#define FG_ERROR_FRAME (-2)

/* Native SDK v1 targets Linux x86-64 little-endian. All handles are identities,
 * not host addresses. Reused slots must receive a new generation. */
typedef struct FgObjectKey {
    uint64_t run;
    uint64_t launch;
    uint64_t slot;
    uint64_t generation;
} FgObjectKey;

typedef struct FgCallHeader {
    uint32_t abi_version;
    uint32_t frame_bytes;
    uint64_t hook_id;
    uint64_t config_generation;
    FgObjectKey object;
} FgCallHeader;

typedef uint8_t (*FgEnabledFn)(void *context, uint64_t hook_id, const FgObjectKey *object);
typedef int32_t (*FgDispatchFn)(void *context, const FgCallHeader *frame);

typedef struct FgRuntimeV1 {
    uint32_t abi_version;
    uint32_t struct_bytes;
    void *context;
    FgEnabledFn enabled;
    FgDispatchFn dispatch;
} FgRuntimeV1;

/* Tables are immutable while published. The host owns their lifetime, including
 * all in-flight callbacks; this SDK does not provide reclamation by itself. */
static inline int fg_runtime_valid(const FgRuntimeV1 *runtime) {
    return runtime && runtime->abi_version == FG_ABI_VERSION &&
           runtime->struct_bytes >= sizeof(FgRuntimeV1) && runtime->enabled && runtime->dispatch;
}
static inline int fg_is_enabled(const FgRuntimeV1 *runtime, uint64_t hook_id,
                                const FgObjectKey *object) {
    return fg_runtime_valid(runtime) && object &&
           runtime->enabled(runtime->context, hook_id, object) == 1;
}
static inline int32_t fg_dispatch(const FgRuntimeV1 *runtime, const FgCallHeader *frame) {
    if (!fg_runtime_valid(runtime))
        return FG_ERROR_ABI;
    if (!frame || frame->abi_version != FG_ABI_VERSION || frame->frame_bytes < sizeof(FgCallHeader))
        return FG_ERROR_FRAME;
    return runtime->dispatch(runtime->context, frame);
}

/* Ensure the constructed frame belongs to the same route that was gated. */
static inline int32_t fg_dispatch_at(const FgRuntimeV1 *runtime, uint64_t hook_id,
                                     const FgObjectKey *object, const FgCallHeader *frame) {
    if (!object || !frame || frame->hook_id != hook_id || frame->object.run != object->run ||
        frame->object.launch != object->launch || frame->object.slot != object->slot ||
        frame->object.generation != object->generation)
        return FG_ERROR_FRAME;
    return fg_dispatch(runtime, frame);
}

/* frame_expression must yield a readable frame pointer valid until dispatch
 * returns. It is evaluated at most once and only after the enabled callback.
 * The callback must validate the hook-specific frame size and payload. */
#define FG_OBSERVE(runtime_expression, hook_expression, object_expression, frame_expression)       \
    do {                                                                                           \
        const FgRuntimeV1 *fg_sdk_runtime_ = (runtime_expression);                                 \
        const uint64_t fg_sdk_hook_ = (hook_expression);                                           \
        const FgObjectKey *fg_sdk_object_ = (object_expression);                                   \
        if (fg_is_enabled(fg_sdk_runtime_, fg_sdk_hook_, fg_sdk_object_)) {                        \
            (void)fg_dispatch_at(fg_sdk_runtime_, fg_sdk_hook_, fg_sdk_object_,                    \
                                 (frame_expression));                                              \
        }                                                                                          \
    } while (0)

#ifdef __cplusplus
}
#endif
#endif
