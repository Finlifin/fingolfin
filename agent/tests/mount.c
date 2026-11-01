#include "fingolfin/mount.h"
#include "record.adapter.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct BatchFactory {
    struct FgNativeFrame native;
    size_t calls;
};
static const FgCallHeader *batch_frame(void *context, const FgLeaseV2 *lease) {
    struct BatchFactory *f = context;
    f->calls++;
    f->native = (struct FgNativeFrame){.header = {FG_ABI_VERSION, sizeof(f->native), lease->hook_id,
                                                  lease->generation, lease->object},
                                       .arg0 = 0,
                                       .arg1 = 5};
    return &f->native.header;
}
struct SwitchWorker {
    FgRuntimeV2 runtime;
    size_t successes;
};
static void *switch_worker(void *context) {
    struct SwitchWorker *w = context;
    FgObjectKey object = {3, 4, 5, 6};
    for (size_t i = 0; i < 2000; i++) {
        struct BatchFactory f = {0};
        FgDispatchReportV2 report;
        int32_t status = fg_v2_observe(&w->runtime, 7, &object, batch_frame, &f, &report);
        assert(!status || status == -(int32_t)FG_STATUS_BUSY);
        w->successes += report.attempted - report.failed;
    }
    return NULL;
}
static void concurrent_switching(FgSession *session, const FgMountOptions *base) {
    FgMountOptions options = *base;
    options.host.output_capacity = 4096;
    FgMount *mount = NULL;
    assert(fg_mount_create(session, &options, &mount, NULL, 0) == FG_STATUS_OK);
    struct SwitchWorker workers[2] = {{fg_mount_runtime(mount), 0}, {fg_mount_runtime(mount), 0}};
    pthread_t threads[2];
    for (size_t i = 0; i < 2; i++)
        assert(!pthread_create(&threads[i], NULL, switch_worker, &workers[i]));
    uint64_t generation = 1;
    for (size_t i = 0; i < 500; i++)
        assert(fg_mount_select(mount, generation, i % 2, &generation) == FG_STATUS_OK);
    for (size_t i = 0; i < 2; i++)
        assert(!pthread_join(threads[i], NULL));
    uint64_t expected = workers[0].successes + workers[1].successes, value;
    assert(fg_mount_counter(mount, 0, &value) == FG_STATUS_OK && value == expected * 5);
    assert(fg_mount_detach(mount) == FG_STATUS_OK);
    FgRecord record;
    uint64_t records = 0;
    while (fg_mount_pop(mount, &record) == FG_STATUS_OK) {
        assert(record.value == 5);
        records++;
    }
    assert(records == expected);
    assert(fg_mount_destroy(mount) == FG_STATUS_OK);
    puts("mount concurrent selection: 500 publications / 4000 observations, "
         "counters and output agree");
}

static void switching(FgSession *session, const FgMountOptions *base) {
    FgMountOptions options[2] = {*base, *base};
    for (size_t i = 0; i < 2; i++) {
        options[i].gate = NULL;
        options[i].host.output_capacity = 128;
    }
    FgMount *mount = NULL;
    assert(fg_mount_create_many(session, options, 2, &mount, NULL, 0) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_mount_runtime(mount);
    FgObjectKey object = {3, 4, 5, 6};
    FgLeaseV2 old, newer;
    assert(!fg_v2_acquire(&runtime, 7, &object, &old) && old.enabled);
    uint64_t generation = 999;
    assert(fg_mount_select(mount, 9, 1, &generation) == FG_STATUS_CONFLICT && generation == 999);
    assert(fg_mount_select(mount, 1, 4, &generation) == FG_STATUS_INVALID && generation == 999);
    assert(fg_mount_select(mount, 1, 1, &generation) == FG_STATUS_OK && generation == 2);
    assert(!fg_v2_acquire(&runtime, 7, &object, &newer) && newer.generation == 2);
    struct BatchFactory f = {0};
    const FgCallHeader *header = batch_frame(&f, &old);
    assert(fg_v2_dispatch(&newer, header) == FG_V2_ERROR_FRAME);
    assert(!fg_v2_dispatch(&old, header) && old.report.attempted == 2);
    header = batch_frame(&f, &newer);
    assert(!fg_v2_dispatch(&newer, header) && newer.report.attempted == 1);
    assert(fg_mount_select(mount, 2, 0, &generation) == FG_STATUS_OK && generation == 3);
    assert(fg_mount_collect(mount) == FG_STATUS_BUSY);
    fg_v2_release(&old);
    fg_v2_release(&newer);
    assert(fg_mount_collect(mount) == FG_STATUS_OK);
    f = (struct BatchFactory){0};
    FgDispatchReportV2 report;
    assert(!fg_v2_observe(&runtime, 7, &object, batch_frame, &f, &report));
    assert(!f.calls && !report.attempted);
    assert(fg_mount_select(mount, 3, 2, &generation) == FG_STATUS_OK && generation == 4);
    assert(!fg_v2_observe(&runtime, 7, &object, batch_frame, &f, &report));
    assert(f.calls == 1 && report.attempted == 1);
    for (size_t i = 0; i < 2; i++) {
        uint64_t value;
        assert(fg_mount_counter_at(mount, i, 0, &value) == FG_STATUS_OK && value == 10);
    }
    /* Every replacement remains leased; bounded retirement rejects new updates.
     */
    FgLeaseV2 held[8];
    for (size_t i = 0; i < 8; i++) {
        assert(fg_mount_select(mount, generation, 1, &generation) == FG_STATUS_OK);
        assert(!fg_v2_acquire(&runtime, 7, &object, &held[i]) && held[i].enabled);
    }
    uint64_t unchanged = generation;
    assert(fg_mount_select(mount, generation, 2, &generation) == FG_STATUS_CAPACITY);
    assert(generation == unchanged && fg_mount_generation(mount) == unchanged);
    for (size_t i = 0; i < 8; i++)
        fg_v2_release(&held[i]);
    assert(fg_mount_collect(mount) == FG_STATUS_OK);
    assert(fg_mount_select(mount, generation, 2, &generation) == FG_STATUS_OK);
    assert(fg_mount_detach(mount) == FG_STATUS_OK);
    assert(fg_mount_select(mount, generation, 1, NULL) == FG_STATUS_CLOSED);
    for (size_t i = 0; i < 2; i++) {
        FgRecord record;
        unsigned count = 0;
        while (fg_mount_pop_at(mount, i, &record) == FG_STATUS_OK) {
            assert(record.value == 5);
            count++;
        }
        assert(count == 2);
    }
    assert(fg_mount_destroy(mount) == FG_STATUS_OK);
    puts("mount selection: generations, old leases, disable/resume state and "
         "bounded retirement passed");
}

static void batches(FgSession *session, const FgMountOptions *base) {
    FgMountOptions options[FG_PORT_MAX_SUBSCRIBERS];
    FgCounterConfig counters[FG_PORT_MAX_SUBSCRIBERS];
    for (size_t i = 0; i < FG_PORT_MAX_SUBSCRIBERS; i++) {
        counters[i] = (FgCounterConfig){0, i};
        options[i] = *base;
        options[i].gate = NULL;
        options[i].attachment.identity.configuration = i + 1;
        options[i].host.counters = &counters[i];
    }
    FgMount *mount = NULL;
    assert(fg_mount_create_many(session, options, 0, &mount, NULL, 0) == FG_STATUS_INVALID &&
           !mount);
    assert(fg_mount_create_many(session, options, 65, &mount, NULL, 0) == FG_STATUS_INVALID &&
           !mount);
    options[1].hook++;
    assert(fg_mount_create_many(session, options, 2, &mount, NULL, 0) == FG_STATUS_INVALID &&
           !mount);
    options[1].hook--;
    options[1].attachment.identity.generation++;
    assert(fg_mount_create_many(session, options, 2, &mount, NULL, 0) == FG_STATUS_INVALID &&
           !mount);
    options[1].attachment.identity.generation--;
    options[1].native_frame_bytes++;
    assert(fg_mount_create_many(session, options, 2, &mount, NULL, 0) == FG_STATUS_INVALID &&
           !mount);
    options[1].native_frame_bytes--;
    /* Fail after the first Attachment/VM/binding was successfully prepared. */
    options[1].function = "missing";
    assert(fg_mount_create_many(session, options, 2, &mount, NULL, 0) == FG_STATUS_NOT_FOUND &&
           !mount);
    options[1].function = base->function;
    assert(fg_mount_create_many(session, options, 64, &mount, NULL, 0) == FG_STATUS_OK);
    assert(fg_mount_count(mount) == 64);
    FgRuntimeV2 runtime = fg_mount_runtime(mount);
    FgObjectKey object = {3, 4, 5, 6};
    FgLeaseV2 lease;
    assert(!fg_v2_acquire(&runtime, 7, &object, &lease) && lease.enabled);
    assert(fg_mount_detach(mount) == FG_STATUS_OK);
    assert(fg_mount_destroy(mount) == FG_STATUS_BUSY);
    struct BatchFactory factory = {0};
    const FgCallHeader *native = batch_frame(&factory, &lease);
    assert(!fg_v2_dispatch(&lease, native));
    assert(lease.report.attempted == 64 && !lease.report.failed && factory.calls == 1);
    fg_v2_release(&lease);
    for (size_t i = 0; i < 64; i++) {
        uint64_t value;
        FgRecord record;
        assert(fg_mount_counter_at(mount, i, 0, &value) == FG_STATUS_OK && value == i + 5);
        assert(fg_mount_pop_at(mount, i, &record) == FG_STATUS_OK && record.value == 5);
        assert(fg_mount_pop_at(mount, i, &record) == FG_STATUS_NOT_FOUND);
    }
    uint64_t value = 1234;
    assert(fg_mount_counter_at(mount, 64, 0, &value) == FG_STATUS_INVALID && value == 1234);
    assert(fg_mount_destroy(mount) == FG_STATUS_OK);
    /* One false gate, one failing helper, one successful subscriber. */
    FgGateNode false_nodes[] = {{.op = FG_GATE_TRUE}, {.op = FG_GATE_NOT, .left = 0}};
    FgBindingGate gate = {.version = 1,
                          .struct_bytes = sizeof(gate),
                          .plan = {false_nodes, 2},
                          .fact_identity = options[0].attachment.identity,
                          .stage = FG_GATE_UPDATE,
                          .budget = 100};
    options[0].gate = &gate;
    counters[1].slot = 99;
    assert(fg_mount_create_many(session, options, 3, &mount, NULL, 0) == FG_STATUS_OK);
    runtime = fg_mount_runtime(mount);
    factory = (struct BatchFactory){0};
    FgDispatchReportV2 report;
    assert(fg_v2_observe(&runtime, 7, &object, batch_frame, &factory, &report) != 0);
    assert(factory.calls == 1 && report.attempted == 2 && report.failed == 1);
    assert(fg_mount_counter_at(mount, 0, 0, &value) == FG_STATUS_OK && value == 0);
    assert(fg_mount_counter_at(mount, 1, 99, &value) == FG_STATUS_OK && value == 1);
    assert(fg_mount_counter_at(mount, 2, 0, &value) == FG_STATUS_OK && value == 7);
    FgRecord record;
    assert(fg_mount_pop_at(mount, 0, &record) == FG_STATUS_NOT_FOUND);
    assert(fg_mount_pop_at(mount, 1, &record) == FG_STATUS_NOT_FOUND);
    assert(fg_mount_pop_at(mount, 2, &record) == FG_STATUS_OK && record.value == 5);
    assert(fg_mount_detach(mount) == FG_STATUS_OK && fg_mount_destroy(mount) == FG_STATUS_OK);
    assert(fg_mount_create_many(session, options, 1, &mount, NULL, 0) == FG_STATUS_OK);
    runtime = fg_mount_runtime(mount);
    factory = (struct BatchFactory){0};
    assert(!fg_v2_observe(&runtime, 7, &object, batch_frame, &factory, &report));
    assert(!factory.calls && !report.attempted);
    assert(fg_mount_detach(mount) == FG_STATUS_OK && fg_mount_destroy(mount) == FG_STATUS_OK);
    puts("batch mount: 64 subscribers, atomic preparation, isolation, "
         "independent gates and partial errors passed");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f && !fseek(f, 0, SEEK_END));
    long length = ftell(f);
    assert(length > 0);
    rewind(f);
    unsigned char *bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, f) == (size_t)length);
    assert(!fclose(f));
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
    FgMountOptions options = {
        .version = 1,
        .struct_bytes = sizeof(options),
        .program_id = reply.program_id,
        .hook = 7,
        .function = "program::record",
        .native_frame_bytes = sizeof(struct FgNativeFrame),
        .encode = fg_generated_encode,
        .attachment = {1, sizeof(FgAttachmentOptions), {1, 2, 3, 4, 5, 6}, 10000, 64, 10000, 6},
        .host = {.version = 1,
                 .struct_bytes = sizeof(FgHostConfig),
                 .counters = &counter,
                 .counter_count = 1,
                 .output_capacity = 4}};
    FgBindingGate gate = {.version = 1,
                          .struct_bytes = sizeof(gate),
                          .plan = fg_generated_gate_plan(),
                          .fact_identity = options.attachment.identity,
                          .stage = FG_GATE_UPDATE,
                          .budget = 1000};
    options.gate = &gate;
    FgMount *mount = NULL;
    options.attachment.identity.program = 99;
    assert(fg_mount_create(session, &options, &mount, NULL, 0) == FG_STATUS_INVALID && !mount);
    options.attachment.identity.program = 1;
    options.function = "missing";
    assert(fg_mount_create(session, &options, &mount, NULL, 0) == FG_STATUS_NOT_FOUND && !mount);
    options.function = "program::record";
    options.attachment.allowed_helpers = 0;
    assert(fg_mount_create(session, &options, &mount, NULL, 0) != FG_STATUS_OK && !mount);
    options.attachment.allowed_helpers = 6;
    options.native_frame_bytes = 0;
    assert(fg_mount_create(session, &options, &mount, NULL, 0) == FG_STATUS_INVALID && !mount);
    options.native_frame_bytes = sizeof(struct FgNativeFrame);
    batches(session, &options);
    switching(session, &options);
    concurrent_switching(session, &options);
    assert(fg_mount_create(session, &options, &mount, NULL, 0) == FG_STATUS_OK);
    assert(!fg_mount_detached_and_drained(mount));
    assert(fg_mount_destroy(mount) == FG_STATUS_BUSY);
    FgRuntimeV2 runtime = fg_mount_runtime(mount);
    FgObjectKey object = {3, 4, 5, 6};
    FgLeaseV2 lease;
    assert(!fg_v2_acquire(&runtime, 7, &object, &lease) && lease.enabled);
    struct FgNativeFrame native = {
        .header = {FG_ABI_VERSION, sizeof(native), 7, lease.generation, object},
        .arg0 = 0,
        .arg1 = 11};
    assert(!fg_v2_dispatch(&lease, &native.header));
    fg_v2_release(&lease);
    assert(!fg_v2_acquire(&runtime, 7, &object, &lease) && lease.enabled);
    /* Registry unload and session destruction do not detach a target mount. */
    request.sequence = 2;
    request.expected_generation = 2;
    request.method = FG_SESSION_UNLOAD;
    request.program_id = 1;
    assert(fg_session_apply(session, &request, &reply, NULL, 0) == FG_STATUS_OK);
    fg_session_destroy(session);
    assert(fg_mount_detach(mount) == FG_STATUS_OK);
    assert(fg_mount_detach(mount) == FG_STATUS_OK);
    assert(!fg_mount_detached_and_drained(mount));
    assert(fg_mount_destroy(mount) == FG_STATUS_BUSY);
    FgLeaseV2 refused;
    assert(fg_v2_acquire(&runtime, 7, &object, &refused) != 0 && !refused.enabled);
    /* Old lease must still execute after detach, with its original ownership. */
    native.arg1 = 17;
    assert(!fg_v2_dispatch(&lease, &native.header));
    uint64_t value;
    assert(fg_mount_counter(mount, 0, &value) == FG_STATUS_OK && value == 28);
    fg_v2_release(&lease);
    assert(fg_mount_detached_and_drained(mount));
    FgRecord record;
    assert(fg_mount_pop(mount, &record) == FG_STATUS_OK && record.value == 11);
    assert(fg_mount_pop(mount, &record) == FG_STATUS_OK && record.value == 17);
    assert(fg_mount_pop(mount, &record) == FG_STATUS_NOT_FOUND);
    assert(fg_mount_destroy(mount) == FG_STATUS_OK);
    assert(fg_mount_detach(NULL) == FG_STATUS_INVALID);
    assert(!fg_mount_detached_and_drained(NULL));
    puts("mount: generated adapter, registry independence, detach with live "
         "lease, output drain and cleanup passed");
    return 0;
}
