#ifndef FG_ELF_H
#define FG_ELF_H
#include "bytes.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_ELF_MAX_BYTES (32u * 1024u * 1024u)
#define FG_ELF_MAX_SECTIONS 32u
#define FG_ELF_MAX_SYMBOLS 65536u
#define FG_ELF_MAX_NAME 4096u
typedef struct FgElfInfo {
    uint32_t helper_mask;
    uint32_t function_count;
    uint64_t executable_bytes, entry_offset, entry_bytes;
} FgElfInfo;
/* Structural checks for the current generated, relocation-free BPF ELF profile.
 * No allocation. Does not verify instruction safety or authenticate content.
 * Unknown helpers and relocation sections are rejected. Output unchanged on error.
 * When diagnostic != NULL and its capacity >0, it is NUL terminated. */
FgStatus fg_elf_check(FgBytes bytes, FgElfInfo *out, char *diagnostic, size_t diagnostic_bytes);
#ifdef __cplusplus
}
#endif
#endif
