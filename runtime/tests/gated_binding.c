#include "record.adapter.h"
#include <assert.h>
#include <stdio.h>
struct Factory {
    struct FgNativeFrame frame;
    size_t calls;
};
static const FgCallHeader *factory(void *context, const FgLeaseV2 *lease) {
    struct Factory *f = context;
    f->calls++;
    f->frame = (struct FgNativeFrame){.header = {FG_ABI_VERSION, sizeof(f->frame), lease->hook_id,
                                                 lease->generation, lease->object},
                                      .arg0 = 0,
                                      .arg1 = 1};
    return &f->frame.header;
}
static FgStatus predicate(const void *facts, size_t bytes, uint32_t index, uint64_t *budget,
                          FgTruth *truth) {
    if (bytes != 1 || index)
        return FG_STATUS_INVALID;
    if (!*budget)
        return FG_STATUS_BUDGET;
    (*budget)--;
    *truth = (FgTruth) * (const unsigned char *)facts;
    return FG_STATUS_OK;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FgPackageLimits limits = {
        1, sizeof(limits), 256, FG_PACKAGE_MAX_BYTES, FG_PACKAGE_MAX_BYTES, NULL, NULL};
    FgProgram *program = NULL;
    char diagnostic[256];
    assert(fg_program_read(argv[1], &limits, &program, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    FgAttachmentOptions options = {1, sizeof(options), {1, 2, 3, 4, 5, 6}, 10000, 64, 10000, 6};
    FgCounterConfig counter = {0, 0};
    FgHostConfig host = {.version = 1,
                         .struct_bytes = sizeof(host),
                         .counters = &counter,
                         .counter_count = 1,
                         .output_capacity = 32};
    FgAttachment *a = NULL;
    assert(fg_attachment_create(program, "program::record", &options, &host, &a, diagnostic,
                                sizeof(diagnostic)) == FG_STATUS_OK);
    fg_program_release(program);
    FgObjectKey object = {3, 4, 5, 6};
    FgPort *port = NULL;
    assert(fg_port_create(7, &object, 2, NULL, &port) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_port_runtime(port);
    FgBinding *bindings[8] = {0};
    FgLeaseV2 held = {0};
    struct Factory f = {0};
    for (size_t i = 0; i < 8; i++) {
        unsigned char truth = i == 1 ? FG_TRUTH_TRUE : i == 2 ? FG_TRUTH_UNKNOWN : FG_TRUTH_FALSE;
        FgGateNode node = {.op = FG_GATE_TEST,
                           .stage = i == 4 ? FG_GATE_INVOCATION : FG_GATE_UPDATE};
        FgBindingGate config = {1,
                                sizeof(config),
                                {&node, 1},
                                options.identity,
                                FG_GATE_UPDATE,
                                i == 5 ? 0 : 10,
                                {&truth, 1},
                                i == 6 ? NULL : predicate};
        if (i == 3)
            config.fact_identity.configuration++;
        if (i == 7)
            node.op = 999;
        assert(fg_binding_create_gated(a, 7, i + 1, sizeof(struct FgNativeFrame),
                                       fg_generated_encode, &config, &bindings[i]) == FG_STATUS_OK);
        /* Caller buffers are immediately mutable/releasable; published copies
         * must retain both original predicate topology and original facts. */
        truth = FG_TRUTH_TRUE;
        node.op = FG_GATE_TRUE;
        FgSubscription sub;
        assert(fg_binding_subscription(bindings[i], i + 1, &sub) == FG_STATUS_OK);
        uint64_t generation;
        assert(fg_port_publish(port, i, &sub, 1, &generation) == FG_STATUS_OK &&
               generation == i + 1);
        FgDispatchReportV2 report;
        size_t before = f.calls;
        assert(!fg_v2_observe(&runtime, 7, &object, factory, &f, &report));
        assert(f.calls == before + (i != 0));
        assert(report.attempted == (i != 0) && !report.failed);
        if (i == 1)
            assert(!fg_v2_acquire(&runtime, 7, &object, &held) && held.enabled);
    }
    assert(f.calls == 7);
    assert(fg_binding_destroy(bindings[1]) == FG_STATUS_BUSY);
    assert(fg_port_close(port) == FG_STATUS_OK && !fg_port_closed_and_drained(port));
    assert(!fg_v2_dispatch(&held, factory(&f, &held)));
    fg_v2_release(&held);
    assert(fg_port_closed_and_drained(port) && fg_port_destroy(port) == FG_STATUS_OK);
    for (size_t i = 0; i < 8; i++)
        assert(fg_binding_destroy(bindings[i]) == FG_STATUS_OK);
    uint64_t value;
    assert(fg_attachment_counter(a, 0, &value) == FG_STATUS_OK && value == 8);
    assert(fg_attachment_close(a) == FG_STATUS_OK && fg_attachment_destroy(a) == FG_STATUS_OK);
    puts("gated binding: false skips factory/VM; unknown/stale/budget/invalid allow; copied facts "
         "and old leases passed");
    return 0;
}
