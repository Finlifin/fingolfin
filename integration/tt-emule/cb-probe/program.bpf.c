typedef unsigned long long u64;
typedef unsigned int u32;
struct Object {
    u64 run, launch, slot, generation;
};
struct Header {
    u32 version, bytes;
    u64 hook, generation;
    struct Object object;
};
struct Frame {
    struct Header header;
    u32 cb, pages, x, y;
};
_Static_assert(sizeof(struct Frame) == 72, "frame layout");
u64 entry(const struct Frame *frame) {
    if (frame->header.version != 1 || frame->header.bytes != sizeof(*frame))
        return 0;
    if (frame->cb != 0 || frame->x != 0 || frame->y != 0)
        return 0;
    return frame->pages;
}
