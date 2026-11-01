/* Executable documentation: C Session -> Mount -> SDK -> Host.
 * Assertions deliberately remain enabled in the tutorial build. */
#include "fingolfin/mount.h"
#include "record.adapter.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

struct Input {
    struct FgNativeFrame frame;
    uint64_t amount;
    unsigned calls;
};
static const FgCallHeader *make_frame(void *context, const FgLeaseV2 *lease) {
    struct Input *input = context;
    input->calls++;
    input->frame =
        (struct FgNativeFrame){.header = {FG_ABI_VERSION, sizeof(input->frame), lease->hook_id,
                                          lease->generation, lease->object},
                               .arg0 = 0,
                               .arg1 = input->amount};
    return &input->frame.header;
}
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s record.fgo\n", argv[0]);
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0);
    rewind(file);
    unsigned char *bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(!fclose(file));

    FgSession *session = NULL;
    assert(fg_session_create(42, 1, &session) == FG_STATUS_OK);
    FgSessionRequest request = {.version = 1,
                                .struct_bytes = sizeof(request),
                                .session_id = 42,
                                .sequence = 1,
                                .expected_generation = 1,
                                .method = FG_SESSION_LOAD,
                                .package = {bytes, (size_t)length}};
    FgSessionReply reply;
    assert(fg_session_apply(session, &request, &reply, NULL, 0) == FG_STATUS_OK);
    free(bytes);
    FgCounterConfig counter = {0, 0};
    FgMountOptions options = {.version = FG_MOUNT_OPTIONS_VERSION,
                              .struct_bytes = sizeof(options),
                              .program_id = reply.program_id,
                              .hook = 7,
                              .function = "program::record",
                              .native_frame_bytes = sizeof(struct FgNativeFrame),
                              .encode = fg_generated_encode,
                              .attachment = {.version = FG_ATTACHMENT_OPTIONS_VERSION,
                                             .struct_bytes = sizeof(FgAttachmentOptions),
                                             .identity = {reply.program_id, 2, 3, 4, 5, 6},
                                             .step_budget = 10000,
                                             .depth_budget = 64,
                                             .instruction_limit = 10000,
                                             .allowed_helpers = (1u << 1) | (1u << 2)},
                              .host = {.version = FG_HOST_CONFIG_VERSION,
                                       .struct_bytes = sizeof(FgHostConfig),
                                       .counters = &counter,
                                       .counter_count = 1,
                                       .output_capacity = 4}};
    FgMount *mount = NULL;
    assert(fg_mount_create(session, &options, &mount, NULL, 0) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_mount_runtime(mount);
    FgObjectKey object = {3, 4, 5, 6};
    struct Input input = {0};
    const uint64_t amounts[] = {11, 17};
    for (size_t i = 0; i < 2; i++) {
        input.amount = amounts[i];
        FgDispatchReportV2 report;
        assert(!fg_v2_observe(&runtime, 7, &object, make_frame, &input, &report));
        assert(report.attempted == 1 && report.failed == 0);
    }
    uint64_t value = 0;
    assert(fg_mount_counter(mount, 0, &value) == FG_STATUS_OK && value == 28);
    printf("factory_calls=%u counter[0]=%" PRIu64 "\n", input.calls, value);
    /* Single-threaded example: no other SDK table calls can race destruction. */
    assert(fg_mount_detach(mount) == FG_STATUS_OK);
    assert(fg_mount_detached_and_drained(mount));
    for (size_t i = 0; i < 2; i++) {
        FgRecord record;
        assert(fg_mount_pop(mount, &record) == FG_STATUS_OK);
        assert(record.tag == 7 && record.value == amounts[i]);
        printf("record tag=%u value=%" PRIu64 "\n", record.tag, record.value);
    }
    FgRecord record;
    assert(fg_mount_pop(mount, &record) == FG_STATUS_NOT_FOUND);
    assert(fg_mount_destroy(mount) == FG_STATUS_OK);
    fg_session_destroy(session);
    puts("detached, drained, destroyed");
    return 0;
}
