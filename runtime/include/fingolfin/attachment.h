#ifndef FG_ATTACHMENT_H
#define FG_ATTACHMENT_H
#include "program.h"
#include "state.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_ATTACHMENT_OPTIONS_VERSION 1u
typedef struct FgAttachment FgAttachment;
typedef struct FgAttachmentOptions {
    uint32_t version;
    size_t struct_bytes;
    FgStateKey identity;
    uint64_t step_budget;
    uint32_t depth_budget, instruction_limit, allowed_helpers;
} FgAttachmentOptions;
/* Development function attachment: owns isolated Host and single-slot executor.
 * Explicit identity is controller-assigned; it is not an authenticated package ID.
 * Copies config; does not retain input pointers. Program is retained internally. */
FgStatus fg_attachment_create(FgProgram *program, const char *function,
                              const FgAttachmentOptions *options, const FgHostConfig *host,
                              FgAttachment **out, char *diagnostic, size_t diagnostic_bytes);
/* Lifetime pins prevent destruction but do not bypass close. Pin requires an
 * existing live owner; unpin exactly once. Drain includes outstanding pins. */
FgStatus fg_attachment_pin(FgAttachment *attachment);
FgStatus fg_attachment_unpin(FgAttachment *attachment);
const FgStateKey *fg_attachment_identity(const FgAttachment *attachment);
const FgPackageEntry *fg_attachment_entry(const FgAttachment *attachment);
/* Caller encodes arguments in exact schema frame. On admission, replaces frame
 * steps/depth/error prefix with configured budgets/zero. Identity mismatch,
 * closed or busy admission leaves frame/result untouched. Same attachment runs
 * serially; concurrent/reentrant run returns BUSY rather than waiting on Host. */
FgStatus fg_attachment_run(FgAttachment *attachment, const FgStateKey *identity, void *frame,
                           size_t frame_bytes, FgVmResult *result);
/* Control reads share the Host lock; BUSY during execution. Output reads remain
 * available after close so the collector can drain committed records. */
FgStatus fg_attachment_counter(FgAttachment *attachment, uint32_t slot, uint64_t *value);
FgStatus fg_attachment_map_value(FgAttachment *attachment, uint32_t slot, uint64_t key,
                                 uint64_t *value);
FgStatus fg_attachment_pop(FgAttachment *attachment, FgRecord *record);
FgStatus fg_attachment_close(FgAttachment *attachment);
int fg_attachment_closed_and_drained(FgAttachment *attachment);
/* No implicit output flush. Requires external exclusion of all API users.
 * BUSY preserves object until closed and no admitted execution remains. */
FgStatus fg_attachment_destroy(FgAttachment *attachment);
#ifdef __cplusplus
}
#endif
#endif
