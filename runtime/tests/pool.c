#include "fingolfin/pool.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Frame {
    uint64_t steps;
    uint32_t depth, error, slot;
    uint64_t amount;
};
_Static_assert(sizeof(struct Frame) == 32, "fixture ABI");
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
static void run(FgVmLease *lease, FgHost *host, uint64_t value) {
    struct Frame frame = {.steps = 10000, .depth = 64, .amount = value};
    FgVmResult result;
    assert(fg_vm_lease_run(lease, &frame, sizeof(frame), host, &result) == FG_STATUS_OK);
    assert(!frame.error && !result.entry_status && !result.vm_status &&
           result.helper_status == FG_STATUS_OK);
    FgRecord record;
    assert(fg_host_pop(host, &record) == FG_STATUS_OK);
    assert(record.tag == 7 && record.value == value);
}
struct Worker {
    FgVmPool *pool;
    uint64_t amount;
    atomic_int *ready, *proceed;
};
static void *worker(void *argument) {
    struct Worker *w = argument;
    FgHost *host = make_host();
    size_t completed = 0;
    for (size_t i = 0; i < 5000; i++) {
        FgVmLease lease = {0};
        /* Eight workers and eight slots: each worker owns at most one slot. */
        FgStatus status = fg_vm_pool_acquire(w->pool, &lease);
        if (w->ready && i == 1) {
            assert(status == FG_STATUS_CLOSED);
            break;
        }
        assert(status == FG_STATUS_OK);
        if (w->ready && i == 0) {
            atomic_fetch_add(w->ready, 1);
            while (!atomic_load(w->proceed))
                sched_yield();
        }
        run(&lease, host, w->amount);
        completed++;
        assert(fg_vm_lease_release(&lease) == FG_STATUS_OK);
    }
    uint64_t value;
    assert(fg_host_counter(host, 0, &value) == FG_STATUS_OK && value == completed * w->amount);
    fg_host_destroy(host);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long n = ftell(file);
    assert(n > 0);
    rewind(file);
    unsigned char *elf = malloc((size_t)n);
    assert(elf);
    assert(fread(elf, 1, (size_t)n, file) == (size_t)n);
    fclose(file);
    FgVmOptions options = {1, sizeof(options), 10000, 32, 6};
    FgVmPool *pool = NULL;
    char diagnostic[256];
    assert(fg_vm_pool_create(elf, (size_t)n, &options, 0, &pool, diagnostic, sizeof(diagnostic)) ==
               FG_STATUS_INVALID &&
           !pool);
    assert(fg_vm_pool_create(elf, (size_t)n, &options, 65, &pool, diagnostic, sizeof(diagnostic)) ==
               FG_STATUS_INVALID &&
           !pool);
    assert(fg_vm_pool_create(elf, 10, &options, 2, &pool, diagnostic, sizeof(diagnostic)) !=
               FG_STATUS_OK &&
           !pool);
    assert(fg_vm_pool_create(elf, (size_t)n, &options, 2, &pool, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    assert(fg_vm_pool_destroy(pool) == FG_STATUS_BUSY);
    FgVmLease a = {0}, b = {0}, c = {0};
    assert(fg_vm_pool_acquire(pool, &a) == FG_STATUS_OK);
    assert(fg_vm_pool_acquire(pool, &a) == FG_STATUS_INVALID);
    assert(fg_vm_pool_acquire(pool, &b) == FG_STATUS_OK && a.slot != b.slot);
    assert(fg_vm_pool_acquire(pool, &c) == FG_STATUS_BUSY && !c.pool);
    /* Defensively reject accidental token copies without releasing the owner. */
    FgVmLease copy = a;
    assert(fg_vm_lease_release(&copy) == FG_STATUS_INVALID);
    struct Frame unchanged = {.steps = 10000, .depth = 64, .amount = 5};
    struct Frame frame = unchanged;
    FgVmResult result = {.entry_status = 123};
    FgHost *host = make_host();
    assert(fg_vm_lease_run(&copy, &frame, sizeof(frame), host, &result) == FG_STATUS_INVALID);
    assert(!memcmp(&frame, &unchanged, sizeof(frame)) && result.entry_status == 123);
    run(&a, host, 7);
    assert(fg_vm_lease_release(&a) == FG_STATUS_OK && !a.pool);
    assert(fg_vm_lease_release(&a) == FG_STATUS_INVALID);
    assert(fg_vm_pool_acquire(pool, &c) == FG_STATUS_OK && c.generation > copy.generation);
    assert(fg_vm_pool_close(pool) == FG_STATUS_OK);
    assert(fg_vm_pool_close(pool) == FG_STATUS_OK);
    assert(!fg_vm_pool_closed_and_drained(pool));
    assert(fg_vm_pool_acquire(pool, &a) == FG_STATUS_CLOSED);
    run(&b, host, 11);
    run(&c, host, 13);
    assert(fg_vm_lease_release(&b) == FG_STATUS_OK);
    assert(fg_vm_pool_destroy(pool) == FG_STATUS_BUSY);
    assert(fg_vm_lease_release(&c) == FG_STATUS_OK);
    assert(fg_vm_pool_closed_and_drained(pool));
    assert(fg_vm_pool_destroy(pool) == FG_STATUS_OK);
    fg_host_destroy(host);
    assert(fg_vm_pool_create(elf, (size_t)n, &options, 8, &pool, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    pthread_t threads[8];
    struct Worker workers[8];
    for (size_t i = 0; i < 8; i++) {
        workers[i] = (struct Worker){.pool = pool, .amount = i + 1};
        assert(!pthread_create(&threads[i], NULL, worker, &workers[i]));
    }
    for (size_t i = 0; i < 8; i++)
        assert(!pthread_join(threads[i], NULL));
    assert(fg_vm_pool_close(pool) == FG_STATUS_OK && fg_vm_pool_closed_and_drained(pool));
    assert(fg_vm_pool_destroy(pool) == FG_STATUS_OK);
    assert(fg_vm_pool_create(elf, (size_t)n, &options, 8, &pool, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    free(elf);
    atomic_int ready = 0, proceed = 0;
    for (size_t i = 0; i < 8; i++) {
        workers[i] = (struct Worker){pool, i + 1, &ready, &proceed};
        assert(!pthread_create(&threads[i], NULL, worker, &workers[i]));
    }
    while (atomic_load(&ready) != 8)
        sched_yield();
    assert(fg_vm_pool_close(pool) == FG_STATUS_OK);
    assert(!fg_vm_pool_closed_and_drained(pool));
    atomic_store(&proceed, 1);
    for (size_t i = 0; i < 8; i++)
        assert(!pthread_join(threads[i], NULL));
    assert(fg_vm_pool_closed_and_drained(pool));
    assert(fg_vm_pool_destroy(pool) == FG_STATUS_OK);
    puts(
        "C VM pool: lifecycle, exhaustion, token ownership and 40000 concurrent executions passed");
    return 0;
}
