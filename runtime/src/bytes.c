#include "fingolfin/bytes.h"
FgStatus fg_bytes_sub(FgBytes bytes, size_t offset, size_t length, FgBytes *out) {
    if (!out || (!bytes.data && bytes.size))
        return FG_STATUS_INVALID;
    if (offset > bytes.size || length > bytes.size - offset)
        return FG_STATUS_INVALID;
    *out = (FgBytes){bytes.data ? bytes.data + offset : NULL, length};
    return FG_STATUS_OK;
}
#define READ(NAME, TYPE, WIDTH)                                                                    \
    FgStatus NAME(FgBytes bytes, size_t offset, TYPE *out) {                                       \
        if (!out)                                                                                  \
            return FG_STATUS_INVALID;                                                              \
        FgBytes part;                                                                              \
        FgStatus status = fg_bytes_sub(bytes, offset, WIDTH, &part);                               \
        if (status != FG_STATUS_OK)                                                                \
            return status;                                                                         \
        TYPE value = 0;                                                                            \
        for (size_t i = 0; i < WIDTH; i++)                                                         \
            value |= (TYPE)((TYPE)part.data[i] << (8 * i));                                        \
        *out = value;                                                                              \
        return FG_STATUS_OK;                                                                       \
    }
READ(fg_bytes_u16le, uint16_t, 2)
READ(fg_bytes_u32le, uint32_t, 4)
READ(fg_bytes_u64le, uint64_t, 8)
