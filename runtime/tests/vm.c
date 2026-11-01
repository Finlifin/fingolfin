#include "fingolfin/vm.h"
#include "fingolfin/elf.h"
#include "fingolfin/route.h"
#include "fingolfin/schema.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Fixture ABI independently asserted; matching generated headers are saved by prepare script. */
struct RecordFrame {
    uint64_t steps;
    uint32_t depth, error, slot;
    uint64_t amount;
};
struct AggregateFrame {
    uint64_t steps;
    uint32_t depth, error;
    uint64_t key;
    int64_t sample;
    uint64_t weight;
};
_Static_assert(sizeof(struct RecordFrame) == 32 && offsetof(struct RecordFrame, amount) == 24,
               "record ABI");
_Static_assert(sizeof(struct AggregateFrame) == 40 && offsetof(struct AggregateFrame, sample) == 24,
               "aggregate ABI");
static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long n = ftell(f);
    assert(n > 0);
    rewind(f);
    unsigned char *b = malloc((size_t)n);
    assert(b);
    assert(fread(b, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *size = (size_t)n;
    return b;
}
static FgHost *host(uint64_t initial) {
    FgCounterConfig counter = {0, initial};
    FgMapConfig map = {0, 2};
    int64_t bounds[] = {0, 10};
    FgHistogramConfig histogram = {0, bounds, 2, 3};
    FgHostConfig config = {
        FG_HOST_CONFIG_VERSION, sizeof(config), &counter, 1, &map, 1, &histogram, 1, 2};
    FgHost *h = NULL;
    assert(fg_host_create(&config, NULL, &h) == FG_STATUS_OK);
    return h;
}
static FgStatus record(FgVm *vm, FgHost *host, uint32_t slot, uint64_t amount, uint32_t expected) {
    struct RecordFrame f = {.steps = 10000, .depth = 64, .slot = slot, .amount = amount};
    FgVmResult result;
    FgStatus status = fg_vm_run(vm, &f, sizeof(f), host, &result);
    assert(f.error == expected && result.entry_status == expected);
    return status;
}
struct NativeFrame {
    FgCallHeader header;
    uint32_t slot;
    uint64_t amount;
};
struct Adapter {
    FgVm *vm;
    FgHost *host;
    int enabled;
};
static int adapter_gate(void *context, const FgObjectKey *object, uint64_t generation) {
    (void)object;
    (void)generation;
    return ((struct Adapter *)context)->enabled;
}
static int32_t adapter_dispatch(void *context, const FgCallHeader *header) {
    struct Adapter *adapter = context;
    if (header->frame_bytes != sizeof(struct NativeFrame))
        return FG_V2_ERROR_FRAME;
    const struct NativeFrame *native = (const struct NativeFrame *)header;
    struct RecordFrame frame = {
        .steps = 10000, .depth = 64, .slot = native->slot, .amount = native->amount};
    FgVmResult result;
    FgStatus status = fg_vm_run(adapter->vm, &frame, sizeof(frame), adapter->host, &result);
    return status == FG_STATUS_OK ? (int32_t)result.entry_status : -(int32_t)status;
}
struct NativeFactory {
    struct NativeFrame frame;
    size_t builds;
};
static const FgCallHeader *native_factory(void *context, const FgLeaseV2 *lease) {
    struct NativeFactory *factory = context;
    factory->builds++;
    factory->frame =
        (struct NativeFrame){.header = {FG_ABI_VERSION, sizeof(struct NativeFrame), lease->hook_id,
                                        lease->generation, lease->object},
                             .slot = 0,
                             .amount = 4};
    return &factory->frame.header;
}
static void routed_vm(FgVm *vm) {
    FgHost *state = host(0);
    FgObjectKey object = {1, 2, 3, 4};
    FgPort *port;
    assert(fg_port_create(7, &object, 1, NULL, &port) == FG_STATUS_OK);
    struct Adapter reject = {vm, state, 0}, allow = {vm, state, 1};
    FgSubscription sub = {
        .id = 1, .context = &reject, .gate = adapter_gate, .dispatch = adapter_dispatch};
    assert(fg_port_publish(port, 0, &sub, 1, NULL) == FG_STATUS_OK);
    FgRuntimeV2 runtime = fg_port_runtime(port);
    struct NativeFactory factory = {0};
    assert(fg_v2_observe(&runtime, 7, &object, native_factory, &factory, NULL) == 0 &&
           factory.builds == 0);
    uint64_t value;
    assert(fg_host_counter(state, 0, &value) == FG_STATUS_OK && value == 0);
    sub.context = &allow;
    assert(fg_port_publish(port, 1, &sub, 1, NULL) == FG_STATUS_OK);
    assert(fg_v2_observe(&runtime, 7, &object, native_factory, &factory, NULL) == 0 &&
           factory.builds == 1);
    assert(fg_host_counter(state, 0, &value) == FG_STATUS_OK && value == 4);
    FgRecord output;
    assert(fg_host_pop(state, &output) == FG_STATUS_OK && output.value == 4);
    assert(fg_port_close(port) == FG_STATUS_OK && fg_port_closed_and_drained(port));
    assert(fg_port_destroy(port) == FG_STATUS_OK);
    fg_host_destroy(state);
}
static void schema_invocation(FgVm *vm, const char *path) {
    size_t size;
    unsigned char *bytes = read_file(path, &size);
    FgFrameSchema schema;
    char error[128];
    assert(fg_frame_schema_check((FgBytes){bytes, size}, &schema, error, sizeof(error)) ==
           FG_STATUS_OK);
    assert(schema.parameters == 2 && schema.bindings == 0 && !schema.has_result);
    unsigned char *frame = malloc((size_t)schema.frame_bytes);
    assert(frame);
    assert(fg_frame_schema_init(&schema, frame, (size_t)schema.frame_bytes, 10000, 64) ==
           FG_STATUS_OK);
    FgSchemaSlot slot;
    assert(fg_frame_schema_slot(&schema, 0, &slot) == FG_STATUS_OK && slot.kind == 1 &&
           slot.size == 4);
    assert(fg_frame_schema_slot(&schema, 1, &slot) == FG_STATUS_OK && slot.kind == 1 &&
           slot.size == 8);
    uint64_t amount = 37;
    for (size_t i = 0; i < 8; i++)
        frame[slot.offset + i] = (unsigned char)(amount >> (8 * i));
    FgHost *state = host(0);
    FgVmResult result;
    assert(fg_vm_run(vm, frame, (size_t)schema.frame_bytes, state, &result) == FG_STATUS_OK &&
           result.entry_status == 0);
    uint64_t value;
    assert(fg_host_counter(state, 0, &value) == FG_STATUS_OK && value == 37);
    fg_host_destroy(state);
    free(frame);
    free(bytes);
}
int main(int argc, char **argv) {
    assert(argc == 4);
    size_t size;
    unsigned char *elf = read_file(argv[1], &size);
    char error[256];
    FgVm *vm = NULL;
    FgVmOptions options = {FG_VM_OPTIONS_VERSION, sizeof(options), 10000, 40, FG_HELPERS_ALL};
    assert(fg_vm_load_elf(elf, size, &options, &vm, error, sizeof(error)) == FG_STATUS_OK);
    FgElfInfo elf_info;
    assert(fg_elf_check((FgBytes){elf, size}, &elf_info, error, sizeof(error)) == FG_STATUS_OK);
    assert(elf_info.helper_mask == (FG_HELPER_MASK(1) | FG_HELPER_MASK(2)));
    for (size_t end = 0; end < size; end++) {
        FgVm *truncated = (void *)1;
        assert(fg_vm_load_elf(elf, end, &options, &truncated, error, sizeof(error)) !=
                   FG_STATUS_OK &&
               truncated == NULL);
    }
    unsigned char *unaligned = malloc(size + 1);
    assert(unaligned);
    memcpy(unaligned + 1, elf, size);
    FgVm *other_vm = NULL;
    assert(fg_vm_load_elf(unaligned + 1, size, &options, &other_vm, error, sizeof(error)) ==
           FG_STATUS_OK);
    FgHost *unaligned_host = host(0);
    assert(record(other_vm, unaligned_host, 0, 1, 0) == FG_STATUS_OK);
    fg_host_destroy(unaligned_host);
    fg_vm_release(other_vm);
    free(unaligned);
    schema_invocation(vm, argv[3]);
    routed_vm(vm);
    FgHost *a = host(0), *b = host(100), *overflow = host(UINT64_MAX);
    uint64_t value;
    assert(record(vm, a, 0, 3, 0) == FG_STATUS_OK);
    assert(record(vm, b, 0, 9, 0) == FG_STATUS_OK);
    assert(record(vm, a, 0, 5, 0) == FG_STATUS_OK);
    assert(fg_host_counter(a, 0, &value) == FG_STATUS_OK && value == 8);
    assert(fg_host_counter(b, 0, &value) == FG_STATUS_OK && value == 109);
    assert(record(vm, a, 0, 1, 6) == FG_STATUS_CAPACITY);
    assert(fg_host_counter(a, 0, &value) == FG_STATUS_OK && value == 9);
    assert(record(vm, overflow, 0, 1, 6) == FG_STATUS_OVERFLOW);
    assert(record(vm, b, 1, 1, 6) == FG_STATUS_NOT_FOUND);
    FgRecord output;
    assert(fg_host_pop(a, &output) == FG_STATUS_OK && output.tag == 7 && output.value == 3);
    assert(record(vm, a, 0, 2, 0) == FG_STATUS_OK);
    unsigned char bad_frame[41] = {0};
    FgVmResult result;
    assert(fg_vm_run(vm, bad_frame, 41, a, &result) == FG_STATUS_CAPACITY);
    assert(fg_vm_run(vm, bad_frame, 16, a, &result) == FG_STATUS_OK && result.entry_status == 5);
    fg_vm_release(vm);
    options.helper_mask = FG_HELPER_MASK(1);
    assert(fg_vm_load_elf(elf, size, &options, &vm, error, sizeof(error)) != FG_STATUS_OK &&
           vm == NULL);
    options.helper_mask = FG_HELPERS_ALL;
    options.instruction_limit = 1;
    assert(fg_vm_load_elf(elf, size, &options, &vm, error, sizeof(error)) == FG_STATUS_OK);
    struct RecordFrame f = {.steps = 10000, .depth = 64, .slot = 0, .amount = 1};
    assert(fg_vm_run(vm, &f, sizeof(f), b, &result) == FG_STATUS_VM);
    assert(fg_host_counter(b, 0, &value) == FG_STATUS_OK && value == 109);
    fg_vm_release(vm);
    elf[0] = 0;
    assert(fg_vm_load_elf(elf, size, &options, &vm, error, sizeof(error)) != FG_STATUS_OK &&
           vm == NULL);
    free(elf);
    elf = read_file(argv[2], &size);
    options.instruction_limit = 10000;
    assert(fg_vm_load_elf(elf, size, &options, &vm, error, sizeof(error)) == FG_STATUS_OK);
    free(elf);
    struct AggregateFrame aggregate = {
        .steps = 10000, .depth = 64, .key = 7, .sample = INT64_MIN, .weight = 2};
    assert(fg_vm_run(vm, &aggregate, sizeof(aggregate), a, &result) == FG_STATUS_OK &&
           result.entry_status == 0);
    aggregate = (struct AggregateFrame){
        .steps = 10000, .depth = 64, .key = 8, .sample = INT64_MAX, .weight = UINT64_MAX};
    assert(fg_vm_run(vm, &aggregate, sizeof(aggregate), a, &result) == FG_STATUS_OVERFLOW &&
           result.helper_status == FG_STATUS_OVERFLOW && aggregate.error == 6);
    assert(fg_host_map_value(a, 0, 8, &value) == FG_STATUS_OK && value == UINT64_MAX);
    const uint64_t *counts;
    size_t n;
    uint64_t total;
    assert(fg_host_histogram(a, 0, &counts, &n, &total) == FG_STATUS_OK && total == 2 &&
           counts[0] == 2 && counts[2] == 0);
    aggregate =
        (struct AggregateFrame){.steps = 10000, .depth = 64, .key = 9, .sample = 0, .weight = 1};
    assert(fg_vm_run(vm, &aggregate, sizeof(aggregate), a, &result) == FG_STATUS_CAPACITY);
    assert(fg_host_map_value(b, 0, 7, &value) == FG_STATUS_NOT_FOUND);
    fg_vm_release(vm);
    fg_host_destroy(a);
    fg_host_destroy(b);
    fg_host_destroy(overflow);
    puts("PASS: generated ELF -> C VM -> C Host; isolation, allowlist, "
         "frame/capacity/overflow/budget, partial success");
}
