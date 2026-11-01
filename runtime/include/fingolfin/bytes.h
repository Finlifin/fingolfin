#ifndef FG_BYTES_H
#define FG_BYTES_H
#include "util.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FgBytes {
    const unsigned char *data;
    size_t size;
} FgBytes;
/* No ownership, allocation, or unaligned native loads. Output unchanged on error.
 * A nonempty span must refer to readable storage for its entire size. */
FgStatus fg_bytes_sub(FgBytes bytes, size_t offset, size_t length, FgBytes *out);
FgStatus fg_bytes_u16le(FgBytes bytes, size_t offset, uint16_t *out);
FgStatus fg_bytes_u32le(FgBytes bytes, size_t offset, uint32_t *out);
FgStatus fg_bytes_u64le(FgBytes bytes, size_t offset, uint64_t *out);
#ifdef __cplusplus
}
#endif
#endif
