#include "fingolfin/program.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
struct Frame {
    uint64_t steps;
    uint32_t depth, error, slot;
    uint64_t amount;
};
static FgHost *make_host(void) {
    FgCounterConfig counter = {0, 0};
    FgHostConfig config = {.version = 1,
                           .struct_bytes = sizeof(config),
                           .counters = &counter,
                           .counter_count = 1,
                           .output_capacity = 1};
    FgHost *host = NULL;
    assert(fg_host_create(&config, NULL, &host) == FG_STATUS_OK);
    return host;
}
struct Worker {
    FgExecutor *executor;
    uint64_t amount;
};
static void *worker(void *context) {
    struct Worker *w = context;
    FgHost *host = make_host();
    for (size_t i = 0; i < 1000; i++) {
        struct Frame frame;
        const FgPackageEntry *entry = fg_executor_entry(w->executor);
        assert(fg_frame_schema_init(&entry->schema, &frame, sizeof(frame), 10000, 64) ==
               FG_STATUS_OK);
        frame.amount = w->amount;
        frame.slot = 99;
        FgVmResult result;
        assert(fg_executor_run(w->executor, &frame, sizeof(frame), host, &result) ==
               FG_STATUS_NOT_FOUND);
        assert(fg_frame_schema_init(&entry->schema, &frame, sizeof(frame), 10000, 64) ==
               FG_STATUS_OK);
        frame.amount = w->amount;
        assert(fg_executor_run(w->executor, &frame, sizeof(frame), host, &result) == FG_STATUS_OK &&
               !result.entry_status);
        FgRecord record;
        assert(fg_host_pop(host, &record) == FG_STATUS_OK && record.tag == 7 &&
               record.value == w->amount);
    }
    uint64_t value;
    assert(fg_host_counter(host, 0, &value) == FG_STATUS_OK && value == 1000 * w->amount);
    fg_host_destroy(host);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    char diagnostic[256];
    FgPackageLimits limits = {
        1, sizeof(limits), 256, FG_PACKAGE_MAX_BYTES, FG_PACKAGE_MAX_BYTES, NULL, NULL};
    FgProgram *program = NULL;
    assert(fg_program_load((FgBytes){NULL, 0}, &limits, &program, diagnostic, sizeof(diagnostic)) ==
               FG_STATUS_INVALID &&
           !program);
    assert(fg_program_read(argv[1], &limits, &program, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    assert(fg_package_count(fg_program_package(program)) == 1);
    assert(fg_program_retain(program) == FG_STATUS_OK);
    fg_program_release(program);
    FgExecutorOptions options = {1, sizeof(options), 4, 10000, 6};
    FgExecutor *executor = NULL, *second = NULL, *limited = NULL;
    assert(fg_executor_create(program, "missing", &options, &executor, diagnostic,
                              sizeof(diagnostic)) == FG_STATUS_NOT_FOUND &&
           !executor);
    options.allowed_helpers = 2;
    assert(fg_executor_create(program, "program::record", &options, &executor, diagnostic,
                              sizeof(diagnostic)) == FG_STATUS_INVALID &&
           !executor);
    options.allowed_helpers = 6;
    assert(fg_executor_create(program, "program::record", &options, &executor, diagnostic,
                              sizeof(diagnostic)) == FG_STATUS_OK);
    options.slots = 1;
    assert(fg_executor_create(program, "program::record", &options, &second, diagnostic,
                              sizeof(diagnostic)) == FG_STATUS_OK);
    options.instruction_limit = 1;
    assert(fg_executor_create(program, "program::record", &options, &limited, diagnostic,
                              sizeof(diagnostic)) == FG_STATUS_OK);
    fg_program_release(program);
    program = NULL; /* All executors keep the package alive. */
    assert(fg_executor_destroy(executor) == FG_STATUS_BUSY);
    FgHost *host = make_host();
    struct Frame frame = {.steps = 10000, .depth = 64, .amount = 1};
    FgVmResult result = {.entry_status = 123};
    assert(fg_executor_run(executor, &frame, sizeof(frame) - 1, host, &result) ==
               FG_STATUS_INVALID &&
           result.entry_status == 123);
    for (size_t i = 0; i < 20; i++)
        assert(fg_executor_run(limited, &frame, sizeof(frame), host, &result) == FG_STATUS_VM);
    assert(fg_executor_close(limited) == FG_STATUS_OK && fg_executor_closed_and_drained(limited));
    assert(fg_executor_destroy(limited) == FG_STATUS_OK);
    fg_host_destroy(host);
    pthread_t threads[4];
    struct Worker workers[4];
    for (size_t i = 0; i < 4; i++) {
        workers[i] = (struct Worker){executor, i + 1};
        assert(!pthread_create(&threads[i], NULL, worker, &workers[i]));
    }
    for (size_t i = 0; i < 4; i++)
        assert(!pthread_join(threads[i], NULL));
    assert(fg_executor_close(executor) == FG_STATUS_OK && fg_executor_closed_and_drained(executor));
    host = make_host();
    result.entry_status = 123;
    assert(fg_executor_run(executor, &frame, sizeof(frame), host, &result) == FG_STATUS_CLOSED &&
           result.entry_status == 123);
    fg_host_destroy(host);
    assert(fg_executor_destroy(executor) == FG_STATUS_OK);
    struct Worker other = {second, 17};
    worker(&other);
    assert(fg_executor_close(second) == FG_STATUS_OK &&
           fg_executor_destroy(second) == FG_STATUS_OK);
    puts("C Program: ownership, helper policy, error recovery, 10000 calls and executor isolation "
         "passed");
    return 0;
}
