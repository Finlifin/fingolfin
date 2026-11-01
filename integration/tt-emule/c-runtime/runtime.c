#define _POSIX_C_SOURCE 200809L
#include "fingolfin/mount.h"
#include "record.adapter.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static FgMount *mounts[2];
static FgRuntimeV2 runtimes[2];
static uint64_t seen, frames, dispatches, errors;
enum Mode { ON, OFF, MISS, DUAL, TOGGLE };
static enum Mode mode;
static void initialize(void) {
    const char *setting = getenv("FG_CB_MODE");
    mode = setting && !strcmp(setting, "off")      ? OFF
           : setting && !strcmp(setting, "miss")   ? MISS
           : setting && !strcmp(setting, "dual")   ? DUAL
           : setting && !strcmp(setting, "toggle") ? TOGGLE
                                                   : ON;
    const char *path = getenv("FG_CB_PACKAGE");
    if (!path) {
        errors++;
        return;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        errors++;
        return;
    }
    unsigned char *data = malloc(FG_PACKAGE_MAX_BYTES + 1u);
    if (!data) {
        fclose(file);
        errors++;
        return;
    }
    size_t bytes = fread(data, 1, FG_PACKAGE_MAX_BYTES + 1u, file);
    int failed = ferror(file) || bytes > FG_PACKAGE_MAX_BYTES;
    if (fclose(file))
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
    if (status != FG_STATUS_OK) {
        fg_session_destroy(session);
        errors++;
        return;
    }
    FgCounterConfig counters[] = {{0, 0}, {1, 0}};
    FgMountOptions options[2] = {
        {.version = 1,
         .struct_bytes = sizeof(FgMountOptions),
         .program_id = reply.program_id,
         .function = "program::record",
         .native_frame_bytes = sizeof(struct FgNativeFrame),
         .encode = fg_generated_encode,
         .attachment = {1, sizeof(FgAttachmentOptions), {1, 1, 1, 1, 0, 1}, 10000, 64, 10000, 6},
         .host = {.version = 1,
                  .struct_bytes = sizeof(FgHostConfig),
                  .counters = counters,
                  .counter_count = 2,
                  .output_capacity = 1024}}};
    FgBindingGate gate = {.version = 1,
                          .struct_bytes = sizeof(gate),
                          .plan = fg_generated_gate_plan(),
                          .fact_identity = options[0].attachment.identity,
                          .stage = FG_GATE_UPDATE,
                          .budget = 10000,
                          .predicate = fg_generated_predicate};
    options[0].gate = &gate;
    options[1] = options[0];
    for (size_t i = 0; i < 2; i++) {
        options[0].hook = options[1].hook = i + 1;
        status = fg_mount_create_many(session, options, mode == DUAL ? 2 : 1, &mounts[i], NULL, 0);
        if (status != FG_STATUS_OK)
            break;
        runtimes[i] = fg_mount_runtime(mounts[i]);
        if (mode == OFF) {
            status = fg_mount_select(mounts[i], 1, 0, NULL);
            if (status != FG_STATUS_OK)
                break;
        }
    }
    /* Target mounts own their Program references independently of the registry.
     */
    fg_session_destroy(session);
    if (status != FG_STATUS_OK)
        errors++;
}

struct Factory {
    struct FgNativeFrame frame;
    uint32_t slot, pages;
};
static const FgCallHeader *factory(void *context, const FgLeaseV2 *lease) {
    struct Factory *f = context;
    frames++;
    f->frame = (struct FgNativeFrame){.header = {FG_ABI_VERSION, sizeof(f->frame), lease->hook_id,
                                                 lease->generation, lease->object},
                                      .arg0 = f->slot,
                                      .arg1 = f->pages};
    return &f->frame.header;
}
/* Real CB event boundary; this fixture observes one process launch and
 * core0/CB0. Model ownership/lifecycle and zero-overhead dispatch remain
 * separate work. */
void fg_tt_cb_event(uint32_t hook, uint32_t cb, uint32_t pages, uint32_t x, uint32_t y) {
    pthread_mutex_lock(&mutex);
    pthread_once(&once, initialize);
    seen++;
    /* Deterministic target-side policy changes at known fixture event boundaries.
       This is not a remote command channel or a model property mutation. */
    if (!errors && mode == TOGGLE && (seen == 2 || seen == 4)) {
        for (size_t i = 0; i < 2; i++) {
            if (fg_mount_select(mounts[i], fg_mount_generation(mounts[i]), seen == 4 ? 1 : 0,
                                NULL) != FG_STATUS_OK)
                errors++;
        }
    }
    if (!errors && hook >= 1 && hook <= 2 && !x && !y && cb == (mode == MISS ? 16u : 0u)) {
        FgObjectKey object = {1, 1, 0, 1};
        struct Factory f = {.slot = hook - 1, .pages = pages};
        FgDispatchReportV2 report;
        int32_t status = fg_v2_observe(&runtimes[hook - 1], hook, &object, factory, &f, &report);
        dispatches += report.attempted;
        if (status || report.failed)
            errors++;
    }
    pthread_mutex_unlock(&mutex);
}
__attribute__((destructor)) static void finish(void) {
    if (!seen)
        return; /* JIT compiler subprocesses must not overwrite evidence. */
    uint64_t push = 0, pop = 0, records = 0, record_push = 0, record_pop = 0;
    uint64_t generations[2] = {0};
    for (size_t i = 0; i < 2; i++) {
        if (!mounts[i])
            continue;
        generations[i] = fg_mount_generation(mounts[i]);
        if (fg_mount_detach(mounts[i]) != FG_STATUS_OK || !fg_mount_detached_and_drained(mounts[i]))
            errors++;
        for (size_t j = 0; j < fg_mount_count(mounts[i]); j++) {
            uint64_t local_push = 0, local_pop = 0;
            if (fg_mount_counter_at(mounts[i], j, 0, &local_push) != FG_STATUS_OK ||
                fg_mount_counter_at(mounts[i], j, 1, &local_pop) != FG_STATUS_OK)
                errors++;
            push += local_push;
            pop += local_pop;
            FgRecord record;
            FgStatus status;
            while ((status = fg_mount_pop_at(mounts[i], j, &record)) == FG_STATUS_OK) {
                records++;
                if (record.tag == 0)
                    record_push += record.value;
                else if (record.tag == 1)
                    record_pop += record.value;
                else
                    errors++;
            }
            if (status != FG_STATUS_NOT_FOUND)
                errors++;
        }
        if (fg_mount_destroy(mounts[i]) != FG_STATUS_OK)
            errors++;
    }
    if (record_push != push || record_pop != pop)
        errors++;
    const char *path = getenv("FG_CB_REPORT");
    if (!path)
        return;
    FILE *file = fopen(path, "w");
    if (!file)
        return;
    fprintf(file,
            "{\"seen\":%llu,\"frames\":%llu,\"dispatches\":%llu,\"errors\":%llu,"
            "\"push_pages\":%llu,\"pop_pages\":%llu,\"records\":%llu,\"route_"
            "generations\":[%llu,%llu]}\n",
            (unsigned long long)seen, (unsigned long long)frames, (unsigned long long)dispatches,
            (unsigned long long)errors, (unsigned long long)push, (unsigned long long)pop,
            (unsigned long long)records, (unsigned long long)generations[0],
            (unsigned long long)generations[1]);
    fclose(file);
}
