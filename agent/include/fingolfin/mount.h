#ifndef FG_MOUNT_H
#define FG_MOUNT_H
#include "fingolfin/binding.h"
#include "fingolfin/session.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FgMount FgMount;
#define FG_MOUNT_OPTIONS_VERSION 1u
typedef struct FgMountOptions {
    uint32_t version;
    size_t struct_bytes;
    uint64_t program_id, hook;
    const char *function;
    size_t native_frame_bytes;
    FgBindingEncode encode;
    FgAttachmentOptions attachment;
    FgHostConfig host;
    const FgBindingGate *gate;
} FgMountOptions;
/* Target-side control primitive for one entry on a dedicated owned Port.
 * Serialized controller: session must remain live during create; options and
 * source buffers are copied by the underlying owners. Encoder/predicate code
 * must remain loaded until destroy. attachment.identity.program must equal
 * program_id; configuration/object identities are assigned by the controller.
 * The caller/model adapter supplies the trusted hook mapping, not remote code.
 * Publishes only after all resources exist; failure leaves *out NULL.
 * The mount independently retains Program through Attachment, so registry
 * unload or session destroy does NOT detach it. Owner must
 * detach/drain/destroy. Requires FG_WITH_PACKAGE + FG_WITH_UBPF. */
FgStatus fg_mount_create(FgSession *session, const FgMountOptions *options, FgMount **out,
                         char *diagnostic, size_t diagnostic_bytes);
/* Atomic initial publication of 1..FG_PORT_MAX_SUBSCRIBERS entries on ONE
 * owned Port. All entries must share hook, ObjectKey and native frame size;
 * trusted adapters must also agree on the native layout. Each gets independent
 * Attachment/Host state, even when Program or configuration identity repeats.
 * Input order is subscriber order; one SDK factory produces the common frame.
 * Gates select subscribers independently. Dispatch errors do not roll back
 * other subscribers or previously successful helpers. Failure publishes none.
 * No mutation of an existing mount, cross-Port transaction or cross-entry
 * shared state is implied. Single-entry create delegates to this API. */
FgStatus fg_mount_create_many(FgSession *session, const FgMountOptions *options, size_t count,
                              FgMount **out, char *diagnostic, size_t diagnostic_bytes);
size_t fg_mount_count(const FgMount *mount);
/* Serialized control operations; SDK calls may run concurrently. Enable bits
 * refer to configured input indices. Atomic generation-checked replacement,
 * preserving Attachment state and copied gate facts. Zero disables all without
 * terminal detach. Does not add/remove configured entries or refresh facts.
 * Bounded to eight outstanding replacement route snapshots plus the initial
 * binding templates; CAPACITY preserves active route until old leases release.
 * Collection only frees retired snapshots; BUSY means some remain leased. */
FgStatus fg_mount_select(FgMount *mount, uint64_t expected_generation, uint64_t enabled,
                         uint64_t *published_generation);
FgStatus fg_mount_collect(FgMount *mount);
uint64_t fg_mount_generation(FgMount *mount);
/* Borrowed SDK table. Model owner must keep mount alive throughout ALL table
 * calls, including disabled/failed acquire. Normal SDK lease rules apply. */
FgRuntimeV2 fg_mount_runtime(FgMount *mount);
/* Terminal, idempotent stop of new acquisitions; previously acquired leases
 * remain runnable. Does not block, close Attachment early, or discard output.
 */
FgStatus fg_mount_detach(FgMount *mount);
int fg_mount_detached_and_drained(FgMount *mount);
FgStatus fg_mount_counter(FgMount *mount, uint32_t slot, uint64_t *value);
FgStatus fg_mount_pop(FgMount *mount, FgRecord *record);
/* Index refers to input order at create. Legacy counter/pop access index 0. */
FgStatus fg_mount_counter_at(FgMount *mount, size_t index, uint32_t slot, uint64_t *value);
FgStatus fg_mount_pop_at(FgMount *mount, size_t index, FgRecord *record);
/* Requires detach + all leases drained AND external exclusion of all runtime
 * table/control calls. BUSY preserves mount. Like Attachment destroy, no
 * implicit output flush: caller drains records first or explicitly accepts
 * loss. */
FgStatus fg_mount_destroy(FgMount *mount);
#ifdef __cplusplus
}
#endif
#endif
