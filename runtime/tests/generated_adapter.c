#include "record.adapter.h"
#include <assert.h>
#include <stdio.h>
int main(int argc, char **argv) {
    assert(argc == 2);
    FgPackageLimits limits = {
        1, sizeof(limits), 256, FG_PACKAGE_MAX_BYTES, FG_PACKAGE_MAX_BYTES, NULL, NULL};
    FgProgram *p = NULL;
    char diagnostic[256];
    assert(fg_program_read(argv[1], &limits, &p, diagnostic, sizeof(diagnostic)) == FG_STATUS_OK);
    FgAttachmentOptions options = {1, sizeof(options), {1, 2, 3, 4, 5, 6}, 10000, 64, 10000, 6};
    FgCounterConfig counter = {0, 0};
    FgHostConfig host = {.version = 1,
                         .struct_bytes = sizeof(host),
                         .counters = &counter,
                         .counter_count = 1,
                         .output_capacity = 1};
    FgAttachment *a = NULL;
    assert(fg_attachment_create(p, "program::record", &options, &host, &a, diagnostic,
                                sizeof(diagnostic)) == FG_STATUS_OK);
    fg_program_release(p);
    FgObjectKey object = {3, 4, 5, 6};
    FgPort *port = NULL;
    assert(fg_port_create(7, &object, 1, NULL, &port) == FG_STATUS_OK);
    FgBinding *binding = NULL;
    FgBindingGate gate = {.version = FG_BINDING_GATE_VERSION,
                          .struct_bytes = sizeof(gate),
                          .plan = fg_generated_gate_plan(),
                          .fact_identity = options.identity,
                          .stage = FG_GATE_UPDATE,
                          .budget = 1000};
    assert(fg_binding_create_gated(a, 7, 1, sizeof(struct FgNativeFrame), fg_generated_encode,
                                   &gate, &binding) == FG_STATUS_OK);
    FgSubscription sub;
    assert(fg_binding_subscription(binding, 1, &sub) == FG_STATUS_OK);
    uint64_t generation;
    assert(fg_port_publish(port, 0, &sub, 1, &generation) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_port_runtime(port);
    FgLeaseV2 lease;
    assert(!fg_v2_acquire(&runtime, 7, &object, &lease) && lease.enabled);
    struct FgNativeFrame native = {
        .header = {FG_ABI_VERSION, sizeof(native), 7, lease.generation, object},
        .arg0 = 0,
        .arg1 = 37};
    struct FgBpfFrame output;
    memset(&output, 0xa5, sizeof(output));
    struct FgBpfFrame unchanged = output;
    FgFrameSchema wrong = fg_attachment_entry(a)->schema;
    wrong.bytes.size--;
    assert(fg_generated_encode(&native.header, &wrong, &output, sizeof(output)) ==
           FG_STATUS_INVALID);
    assert(!memcmp(&output, &unchanged, sizeof(output)));
    assert(!fg_v2_dispatch(&lease, &native.header));
    fg_v2_release(&lease);
    uint64_t value;
    assert(fg_attachment_counter(a, 0, &value) == FG_STATUS_OK && value == 37);
    FgRecord record;
    assert(fg_attachment_pop(a, &record) == FG_STATUS_OK && record.tag == 7 && record.value == 37);
    assert(fg_port_close(port) == FG_STATUS_OK && fg_port_closed_and_drained(port));
    assert(fg_port_destroy(port) == FG_STATUS_OK);
    assert(fg_binding_destroy(binding) == FG_STATUS_OK);
    assert(fg_attachment_close(a) == FG_STATUS_OK && fg_attachment_destroy(a) == FG_STATUS_OK);
    puts("compiler-generated native frame/encoder -> SDK route -> Attachment -> C VM passed");
    return 0;
}
