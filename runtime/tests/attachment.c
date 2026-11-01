#include "fingolfin/attachment.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
struct Frame {
    uint64_t steps;
    uint32_t depth, error, slot;
    uint64_t amount;
};
static atomic_int hold, entered, proceed;
FgStatus __real_fg_executor_run(FgExecutor *, void *, size_t, FgHost *, FgVmResult *);
FgStatus __wrap_fg_executor_run(FgExecutor *e, void *frame, size_t bytes, FgHost *host,
                                FgVmResult *result) {
    if (atomic_load(&hold)) {
        atomic_store(&entered, 1);
        while (!atomic_load(&proceed))
            sched_yield();
    }
    return __real_fg_executor_run(e, frame, bytes, host, result);
}
struct Worker {
    FgAttachment *a;
    FgStateKey key;
    size_t attempts, successes;
};
static void *worker(void *context) {
    struct Worker *w = context;
    for (size_t i = 0; i < w->attempts; i++) {
        struct Frame frame = {.steps = 0, .depth = 0, .error = 999, .amount = 1}, original = frame;
        FgVmResult result = {.entry_status = 123};
        FgStatus status = fg_attachment_run(w->a, &w->key, &frame, sizeof(frame), &result);
        if (status == FG_STATUS_OK) {
            assert(!result.entry_status && !frame.error);
            w->successes++;
        } else {
            assert(status == FG_STATUS_BUSY);
            assert(!memcmp(&original, &frame, sizeof(frame)) && result.entry_status == 123);
        }
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
    FgStateKey key = {1, 2, 3, 4, 5, 6};
    FgAttachmentOptions options = {1, sizeof(options), key, 10000, 64, 10000, 6};
    FgCounterConfig counter = {0, 0};
    FgHostConfig host = {.version = 1,
                         .struct_bytes = sizeof(host),
                         .counters = &counter,
                         .counter_count = 1,
                         .output_capacity = 40001};
    FgAttachment *a = NULL, *b = NULL;
    host.version = 0;
    assert(fg_attachment_create(program, "program::record", &options, &host, &a, diagnostic,
                                sizeof(diagnostic)) == FG_STATUS_INVALID &&
           !a);
    host.version = 1;
    assert(fg_attachment_create(program, "program::record", &options, &host, &a, diagnostic,
                                sizeof(diagnostic)) == FG_STATUS_OK);
    options.identity.generation++;
    assert(fg_attachment_create(program, "program::record", &options, &host, &b, diagnostic,
                                sizeof(diagnostic)) == FG_STATUS_OK);
    fg_program_release(program);
    assert(fg_attachment_entry(a)->schema.frame_bytes == sizeof(struct Frame));
    assert(fg_attachment_destroy(a) == FG_STATUS_BUSY);
    struct Frame frame = {.amount = 1}, original = frame;
    FgVmResult result = {.entry_status = 123};
    FgStateKey wrong = key;
    for (unsigned i = 0; i < 6; i++) {
        wrong = key;
        switch (i) {
        case 0:
            wrong.program++;
            break;
        case 1:
            wrong.configuration++;
            break;
        case 2:
            wrong.run++;
            break;
        case 3:
            wrong.launch++;
            break;
        case 4:
            wrong.slot++;
            break;
        default:
            wrong.generation++;
            break;
        }
        assert(fg_attachment_run(a, &wrong, &frame, sizeof(frame), &result) == FG_STATUS_CONFLICT);
        assert(!memcmp(&frame, &original, sizeof(frame)) && result.entry_status == 123);
    }
    frame.slot = 99;
    assert(fg_attachment_run(a, &key, &frame, sizeof(frame), &result) == FG_STATUS_NOT_FOUND);
    pthread_t threads[4];
    struct Worker workers[4];
    size_t successes = 0;
    for (size_t i = 0; i < 4; i++) {
        workers[i] = (struct Worker){a, key, 10000, 0};
        assert(!pthread_create(&threads[i], NULL, worker, &workers[i]));
    }
    for (size_t i = 0; i < 4; i++) {
        assert(!pthread_join(threads[i], NULL));
        successes += workers[i].successes;
    }
    assert(successes > 0);
    uint64_t value;
    assert(fg_attachment_counter(a, 0, &value) == FG_STATUS_OK && value == successes);
    assert(fg_attachment_counter(b, 0, &value) == FG_STATUS_OK && value == 0);
    /* Deterministically pause an admitted call before VM entry. Close must not
     * revoke it, destroy must wait, and state readers must report BUSY. */
    atomic_store(&hold, 1);
    struct Worker pending = {a, key, 1, 0};
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, worker, &pending));
    while (!atomic_load(&entered))
        sched_yield();
    frame = original;
    result.entry_status = 123;
    assert(fg_attachment_run(a, &key, &frame, sizeof(frame), &result) == FG_STATUS_BUSY);
    assert(!memcmp(&frame, &original, sizeof(frame)) && result.entry_status == 123);
    value = 999;
    assert(fg_attachment_counter(a, 0, &value) == FG_STATUS_BUSY && value == 999);
    assert(fg_attachment_close(a) == FG_STATUS_OK && !fg_attachment_closed_and_drained(a));
    assert(fg_attachment_destroy(a) == FG_STATUS_BUSY);
    frame = original;
    result.entry_status = 123;
    assert(fg_attachment_run(a, &key, &frame, sizeof(frame), &result) == FG_STATUS_CLOSED);
    assert(!memcmp(&frame, &original, sizeof(frame)) && result.entry_status == 123);
    atomic_store(&proceed, 1);
    assert(!pthread_join(thread, NULL));
    assert(pending.successes == 1);
    atomic_store(&hold, 0);
    assert(fg_attachment_closed_and_drained(a));
    FgRecord record;
    size_t records = 0;
    while (fg_attachment_pop(a, &record) == FG_STATUS_OK) {
        assert(record.tag == 7 && record.value == 1);
        records++;
    }
    assert(records == successes + 1);
    assert(fg_attachment_destroy(a) == FG_STATUS_OK);
    struct Worker independent = {b, options.identity, 3, 0};
    worker(&independent);
    assert(independent.successes == 3);
    assert(fg_attachment_counter(b, 0, &value) == FG_STATUS_OK && value == 3);
    assert(fg_attachment_close(b) == FG_STATUS_OK && fg_attachment_destroy(b) == FG_STATUS_OK);
    printf("Attachment: 40000 concurrent attempts, %zu successes; identity, budget, isolation and "
           "close/drain passed\n",
           successes);
    return 0;
}
