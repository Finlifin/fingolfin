#define _POSIX_C_SOURCE 200809L
#include "fingolfin/mount.h"
#include "push.adapter.h"
#include "policy.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef FG_MODEL_HOOK
#define FG_MODEL_HOOK 2
#endif
#ifndef FG_MODEL_HAS_COUNT
#define FG_MODEL_HAS_COUNT 1
#endif
#ifndef FG_MODEL_CALLBACK
#define FG_MODEL_CALLBACK fg_tt_scope_event
#endif
#ifndef FG_MODEL_PACKAGE_ENV
#define FG_MODEL_PACKAGE_ENV "FG_MODEL_PACKAGE"
#endif
#ifndef FG_MODEL_REPORT_ENV
#define FG_MODEL_REPORT_ENV "FG_MODEL_REPORT"
#endif
/* This adapter owns one process-scoped core0/compute0 fixture binding. */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static FgMount *mount;
static FgRuntimeV2 runtime;
static uint64_t seen, selected, dispatches, errors, sequence_sum;
static uint64_t expected[3], expected_records;
static uint64_t expected_sequences[8192];
static uint32_t expected_phases[8192];
static uint32_t next_thread;
static _Thread_local uint32_t thread;
static int initialized;
static void initialize(void) {
    initialized = 1;
    const char *path = getenv(FG_MODEL_PACKAGE_ENV);
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) {
        errors++;
        return;
    }
    unsigned char *data = malloc(FG_PACKAGE_MAX_BYTES + 1u);
    if (!data) {
        fclose(f);
        errors++;
        return;
    }
    size_t bytes = fread(data, 1, FG_PACKAGE_MAX_BYTES + 1u, f);
    int failed = ferror(f) || bytes > FG_PACKAGE_MAX_BYTES;
    if (fclose(f))
        failed = 1;
    FgSession *session = NULL;
    FgStatus status = failed ? FG_STATUS_IO : fg_session_create(1, 1, &session);
    FgSessionRequest request = {.version = 1,
                                .struct_bytes = sizeof(request),
                                .session_id = 1,
                                .sequence = 1,
                                .expected_generation = 1,
                                .method = FG_SESSION_LOAD,
                                .package = {data, bytes}};
    FgSessionReply reply;
    if (status == FG_STATUS_OK)
        status = fg_session_apply(session, &request, &reply, NULL, 0);
    free(data);
    if (status == FG_STATUS_OK) {
        FgCounterConfig counters[] = {{0, 0}, {1, 0}, {2, 0}};
        FgMountOptions options = {
            .version = 1,
            .struct_bytes = sizeof(options),
            .program_id = reply.program_id,
            .function = "program::observe",
            .hook = FG_MODEL_HOOK,
            .native_frame_bytes = sizeof(struct FgNativeFrame),
            .encode = fg_generated_encode,
            .attachment = {1, sizeof(FgAttachmentOptions), {1, 1, 1, 1, 0, 1}, 10000, 64, 10000, 6},
            .host = {.version = 1,
                     .struct_bytes = sizeof(FgHostConfig),
                     .counters = counters,
                     .counter_count = 3,
                     .output_capacity = 8192}};
        FgBindingGate gate = {.version = 1,
                              .struct_bytes = sizeof(gate),
                              .plan = fg_generated_gate_plan(),
                              .fact_identity = options.attachment.identity,
                              .stage = FG_GATE_UPDATE,
                              .budget = 10000,
                              .predicate = fg_generated_predicate};
        options.gate = &gate;
        status = fg_mount_create_many(session, &options, 1, &mount, NULL, 0);
        if (status == FG_STATUS_OK)
            runtime = fg_mount_runtime(mount);
    }
    fg_session_destroy(session);
    if (status != FG_STATUS_OK)
        errors++;
}
struct Factory {
    struct FgNativeFrame frame;
};
static const FgCallHeader *factory(void *context, const FgLeaseV2 *lease) {
    struct Factory *f = context;
    f->frame.header = (FgCallHeader){FG_ABI_VERSION, sizeof(f->frame), lease->hook_id,
                                     lease->generation, lease->object};
    return &f->frame.header;
}
void FG_MODEL_CALLBACK(uint32_t hook, uint32_t phase, uint32_t x, uint32_t y, uint64_t a,
                       uint64_t b, uint64_t c) {

    pthread_mutex_lock(&mutex);
    seen++;
    if (!thread)
        thread = ++next_thread;
    /* Generated policy maps named arguments to their captured slots. */
    if (hook == FG_MODEL_HOOK && x == 0 && y == 0) {
        if (!initialized)
            initialize();
        if (phase > 1 || !fg_model_valid(a, b, c) || expected_records + FG_MODEL_FIELDS + 1 > 8192)
            errors++;
        else if (!errors) {
            expected_sequences[expected_records] = seen;
            expected_phases[expected_records++] = phase;
            uint32_t values[3] = {0};
            fg_model_values(values, a, b, c);
            for (size_t i = 0; i != FG_MODEL_FIELDS; i++) {
                expected_sequences[expected_records] = seen;
                expected_phases[expected_records++] = values[i];
            }
            selected++;
            expected[phase]++;
            expected[2]++;
            sequence_sum += seen * (FG_MODEL_FIELDS + 1);
            struct Factory f = {0};
            f.frame.arg0.fg_73657175656e6365 = seen;
            f.frame.arg0.fg_746872656164 = thread;
            f.frame.arg0.fg_7068617365 = phase;
            f.frame.arg1.fg_78 = x;
            f.frame.arg1.fg_79 = y;
            fg_model_fill(&f.frame, a, b, c);
            FgObjectKey object = {1, 1, 0, 1};
            FgDispatchReportV2 report = {0};
            int32_t status = fg_v2_observe(&runtime, FG_MODEL_HOOK, &object, factory, &f, &report);
            dispatches += report.attempted;
            if (status || report.failed || report.attempted != 1)
                errors++;
        }
    }
    pthread_mutex_unlock(&mutex);
}
__attribute__((destructor)) static void finish(void) {
    if (!seen)
        return;
    uint64_t counters[3] = {0}, records = 0, sums = 0;
    if (mount) {
        if (fg_mount_detach(mount) != FG_STATUS_OK || !fg_mount_detached_and_drained(mount))
            errors++;
        for (size_t i = 0; i < 3; i++) {
            if (fg_mount_counter_at(mount, 0, i, &counters[i]) != FG_STATUS_OK ||
                counters[i] != expected[i])
                errors++;
        }
        FgRecord record;
        FgStatus status;
        while ((status = fg_mount_pop_at(mount, 0, &record)) == FG_STATUS_OK) {
            if (records >= expected_records || record.value != expected_sequences[records] ||
                record.tag != expected_phases[records])
                errors++;
            records++;
            sums += record.value;
        }
        if (status != FG_STATUS_NOT_FOUND || records != expected_records || sums != sequence_sum)
            errors++;
        if (fg_mount_destroy(mount) != FG_STATUS_OK)
            errors++;
    }
    const char *path = getenv(FG_MODEL_REPORT_ENV);
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f)
        return;
    fprintf(f,
            "{\"seen\":%llu,\"selected\":%llu,\"dispatches\":%llu,\"errors\":%llu,"
            "\"enter\":%llu,\"exit\":%llu,\"matched_args\":%llu,\"records\":%llu,\"sequence_sum\":%"
            "llu}\n",
            (unsigned long long)seen, (unsigned long long)selected, (unsigned long long)dispatches,
            (unsigned long long)errors, (unsigned long long)counters[0],
            (unsigned long long)counters[1], (unsigned long long)counters[2],
            (unsigned long long)records, (unsigned long long)sums);
    fclose(f);
}
