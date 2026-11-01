#include "fingolfin.h"
#ifdef __cplusplus
#define FG_EXPORT extern "C"
#define FG_ALIGNOF(T) alignof(T)
#else
#define FG_EXPORT
#define FG_ALIGNOF(T) _Alignof(T)
#endif

FG_EXPORT uint64_t fg_probe_layout(uint32_t field) {
    switch (field) {
    case 0:
        return sizeof(FgObjectKey);
    case 1:
        return FG_ALIGNOF(FgObjectKey);
    case 2:
        return sizeof(FgCallHeader);
    case 3:
        return FG_ALIGNOF(FgCallHeader);
    case 4:
        return offsetof(FgCallHeader, object);
    case 5:
        return sizeof(FgRuntimeV1);
    case 6:
        return offsetof(FgRuntimeV1, context);
    case 7:
        return offsetof(FgRuntimeV1, enabled);
    case 8:
        return offsetof(FgRuntimeV1, dispatch);
    default:
        return UINT64_MAX;
    }
}
FG_EXPORT FgObjectKey fg_probe_roundtrip(FgObjectKey key, uint32_t increment) {
    key.slot += increment;
    key.generation += 1;
    return key;
}
typedef FgObjectKey (*KeyCallback)(FgObjectKey, uint32_t);
FG_EXPORT FgObjectKey fg_probe_callback(FgObjectKey key, KeyCallback callback) {
    return callback(key, 7);
}
static unsigned rt_count, key_count, hook_count, frame_count;
static const FgRuntimeV1 *get_runtime(const FgRuntimeV1 *r) {
    ++rt_count;
    return r;
}
static const FgObjectKey *get_key(const FgObjectKey *key) {
    ++key_count;
    return key;
}
static uint64_t get_hook(void) {
    ++hook_count;
    return 42;
}
static const FgCallHeader *get_frame(const FgObjectKey *key) {
    static FgCallHeader result;
    FgCallHeader frame = {FG_ABI_VERSION, sizeof(FgCallHeader), 42, 3, *key};
    ++frame_count;
    result = frame;
    return &result;
}
FG_EXPORT int32_t fg_probe_guard(const FgRuntimeV1 *runtime, const FgObjectKey *key) {
    rt_count = key_count = hook_count = frame_count = 0;
    FG_OBSERVE(get_runtime(runtime), get_hook(), get_key(key), get_frame(key));
    if (rt_count != 1 || key_count != 1 || hook_count != 1)
        return -100;
    return (int32_t)frame_count;
}
FG_EXPORT int32_t fg_probe_dispatch(const FgRuntimeV1 *runtime, const FgCallHeader *frame) {
    return fg_dispatch(runtime, frame);
}

FG_EXPORT int32_t fg_probe_dispatch_at(const FgRuntimeV1 *runtime, const FgObjectKey *object,
                                       const FgCallHeader *frame) {
    return fg_dispatch_at(runtime, 42, object, frame);
}
