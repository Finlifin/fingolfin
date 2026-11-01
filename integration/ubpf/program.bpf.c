typedef unsigned long long u64;
typedef unsigned int u32;
struct Frame {
    u64 a;
    u64 b;
    u32 iterations;
    u32 reserved;
};
#ifdef FG_NAMED_HELPER
extern u64 fg_add(u64, u64, u64, u64, u64);
#else
static u64 (*const fg_add)(u64, u64, u64, u64, u64) = (void *)1;
#endif
__attribute__((noinline)) static u64 mix(u64 value) {
    return value * 3 + 1;
}
u64 entry(struct Frame *frame) {
    u64 sum = frame->a;
    u32 limit = frame->iterations < 8 ? frame->iterations : 8;
    for (u32 i = 0; i < limit; ++i)
        sum = fg_add(sum, frame->b, 0, 0, 0);
    return mix(sum);
}
