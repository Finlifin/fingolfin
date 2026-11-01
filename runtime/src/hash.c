#include "fingolfin/hash.h"
#include <openssl/evp.h>
#include <string.h>
FgStatus fg_sha256(FgBytes bytes, unsigned char output[32]) {
    if (!output || (!bytes.data && bytes.size))
        return FG_STATUS_INVALID;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if (!EVP_Digest(bytes.data, bytes.size, digest, &size, EVP_sha256(), NULL) || size != 32)
        return FG_STATUS_INVALID;
    memcpy(output, digest, 32);
    return FG_STATUS_OK;
}
