#ifndef FG_HASH_H
#define FG_HASH_H
#include "bytes.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Control-path SHA-256 using OpenSSL. Output unchanged on failure. */
FgStatus fg_sha256(FgBytes bytes, unsigned char output[32]);
#ifdef __cplusplus
}
#endif
#endif
