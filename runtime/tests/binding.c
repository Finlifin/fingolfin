#include "fingolfin/binding.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
struct Native {
    FgCallHeader header;
    uint32_t slot;
    uint64_t amount;
};
static FgStatus encode(const FgCallHeader *header, const FgFrameSchema *schema, void *frame,
                       size_t bytes) {
    const struct Native *native = (const struct Native *)header;
    if (bytes != 32 || schema->parameters != 2 || native->amount == 999)
        return FG_STATUS_INVALID;
    FgSchemaSlot slot, amount;
    if (fg_frame_schema_slot(schema, 0, &slot) != FG_STATUS_OK ||
        fg_frame_schema_slot(schema, 1, &amount) != FG_STATUS_OK || slot.offset != 16 ||
        amount.offset != 24)
        return FG_STATUS_INVALID;
    memcpy((unsigned char *)frame + 16, &native->slot, 4);
    memcpy((unsigned char *)frame + 24, &native->amount, 8);
    return FG_STATUS_OK;
}
static struct Native frame(const FgLeaseV2 *lease, uint64_t amount) {
    return (struct Native){.header = {FG_ABI_VERSION, sizeof(struct Native), lease->hook_id,
                                      lease->generation, lease->object},
                           .amount = amount};
}
struct Worker {
    const FgRuntimeV2 *runtime;
    FgObjectKey object;
    size_t successes;
};
static void *worker(void *context) {
    struct Worker *w = context;
    for (size_t i = 0; i < 1000; i++) {
        FgLeaseV2 lease;
        assert(!fg_v2_acquire(w->runtime, 7, &w->object, &lease) && lease.enabled);
        struct Native native = frame(&lease, 1);
        int32_t status = fg_v2_dispatch(&lease, &native.header);
        assert(!status || status == -(int32_t)FG_STATUS_BUSY);
        if (!status)
            w->successes++;
        fg_v2_release(&lease);
    }
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FgPackageLimits limits = {
        1, sizeof(limits), 256, FG_PACKAGE_MAX_BYTES, FG_PACKAGE_MAX_BYTES, NULL, NULL};
    FgProgram *program = NULL;
    char diagnostic[256];
    assert(fg_program_read(argv[1], &limits, &program, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    FgAttachmentOptions options = {1, sizeof(options), {10, 20, 30, 40, 50, 60}, 10000, 64, 10000,
                                   6};
    FgCounterConfig counter = {0, 0};
    FgHostConfig host = {.version = 1,
                         .struct_bytes = sizeof(host),
                         .counters = &counter,
                         .counter_count = 1,
                         .output_capacity = 5000};
    FgAttachment *attachment = NULL;
    assert(fg_attachment_create(program, "program::record", &options, &host, &attachment,
                                diagnostic, sizeof(diagnostic)) == FG_STATUS_OK);
    fg_program_release(program);
    FgObjectKey object = {30, 40, 50, 60};
    FgPort *port = NULL;
    assert(fg_port_create(7, &object, 2, NULL, &port) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_port_runtime(port);
    FgBinding *old = NULL, *newer = NULL, *failed = NULL;
    assert(fg_binding_create(attachment, 7, 1, sizeof(struct Native), encode, &old) ==
           FG_STATUS_OK);
    assert(fg_binding_create(attachment, 7, 2, sizeof(struct Native), encode, &newer) ==
           FG_STATUS_OK);
    assert(fg_binding_create(attachment, 7, 2, sizeof(struct Native), encode, &failed) ==
           FG_STATUS_OK);
    FgSubscription sub;
    uint64_t generation;
    assert(fg_binding_subscription(old, 1, &sub) == FG_STATUS_OK);
    assert(fg_port_publish(port, 0, &sub, 1, &generation) == FG_STATUS_OK && generation == 1);
    assert(fg_binding_destroy(old) == FG_STATUS_BUSY);
    FgLeaseV2 invalid;
    assert(!fg_v2_acquire(&runtime, 7, &object, &invalid) && invalid.enabled);
    struct Native rejected = frame(&invalid, 999);
    assert(fg_v2_dispatch(&invalid, &rejected.header) == -(int32_t)FG_STATUS_INVALID);
    assert(invalid.report.failed == 1);
    fg_v2_release(&invalid);
    assert(!fg_v2_acquire(&runtime, 7, &object, &invalid));
    rejected = frame(&invalid, 1);
    rejected.header.frame_bytes--;
    assert(fg_v2_dispatch(&invalid, &rejected.header) == FG_V2_ERROR_FRAME);
    fg_v2_release(&invalid);
    pthread_t threads[4];
    struct Worker workers[4];
    size_t successes = 0;
    for (size_t i = 0; i < 4; i++) {
        workers[i] = (struct Worker){&runtime, object, 0};
        assert(!pthread_create(&threads[i], NULL, worker, &workers[i]));
    }
    for (size_t i = 0; i < 4; i++) {
        assert(!pthread_join(threads[i], NULL));
        successes += workers[i].successes;
    }
    assert(successes > 0);
    FgLeaseV2 old_lease;
    assert(!fg_v2_acquire(&runtime, 7, &object, &old_lease) && old_lease.enabled);
    assert(fg_binding_subscription(failed, 3, &sub) == FG_STATUS_OK);
    assert(fg_port_publish(port, 99, &sub, 1, &generation) == FG_STATUS_CONFLICT);
    assert(fg_binding_destroy(failed) == FG_STATUS_OK); /* failed publication dropped its pin */
    assert(fg_binding_subscription(newer, 2, &sub) == FG_STATUS_OK);
    assert(fg_port_publish(port, 1, &sub, 1, &generation) == FG_STATUS_OK && generation == 2);
    FgLeaseV2 new_lease;
    assert(!fg_v2_acquire(&runtime, 7, &object, &new_lease) && new_lease.enabled);
    assert(fg_port_publish(port, 2, NULL, 0, &generation) == FG_STATUS_OK && generation == 3);
    assert(fg_port_close(port) == FG_STATUS_OK && !fg_port_closed_and_drained(port));
    assert(fg_binding_destroy(old) == FG_STATUS_BUSY &&
           fg_binding_destroy(newer) == FG_STATUS_BUSY);
    struct Native old_frame = frame(&old_lease, 3), new_frame = frame(&new_lease, 5);
    assert(!fg_v2_dispatch(&old_lease, &old_frame.header));
    assert(!fg_v2_dispatch(&new_lease, &new_frame.header));
    assert(old_lease.report.attempted == 1 && !old_lease.report.failed);
    uint64_t value;
    assert(fg_attachment_counter(attachment, 0, &value) == FG_STATUS_OK && value == successes + 8);
    fg_v2_release(&old_lease);
    assert(fg_binding_destroy(old) == FG_STATUS_OK);
    assert(fg_attachment_close(attachment) == FG_STATUS_OK &&
           !fg_attachment_closed_and_drained(attachment));
    assert(fg_attachment_destroy(attachment) == FG_STATUS_BUSY);
    fg_v2_release(&new_lease);
    assert(fg_binding_destroy(newer) == FG_STATUS_OK);
    assert(fg_port_closed_and_drained(port) && fg_port_destroy(port) == FG_STATUS_OK);
    assert(fg_attachment_closed_and_drained(attachment));
    FgRecord record;
    for (size_t i = 0; i < successes; i++)
        assert(fg_attachment_pop(attachment, &record) == FG_STATUS_OK && record.value == 1);
    assert(fg_attachment_pop(attachment, &record) == FG_STATUS_OK && record.value == 3);
    assert(fg_attachment_pop(attachment, &record) == FG_STATUS_OK && record.value == 5);
    assert(fg_attachment_destroy(attachment) == FG_STATUS_OK);
    puts("SDK route -> binding -> Attachment -> C VM: old generations, failed publication, frame "
         "validation and lifetime pins passed");
    return 0;
}
