#include "frame.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ubpf.h>
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long length = ftell(f);
    assert(length > 0);
    rewind(f);
    void *elf = malloc((size_t)length);
    assert(elf);
    assert(fread(elf, 1, (size_t)length, f) == (size_t)length);
    fclose(f);
    struct ubpf_vm *vm = ubpf_create();
    assert(vm);
    assert(!ubpf_set_execution_profile(vm, UBPF_EXECUTION_PROFILE_SAFE));
    assert(!ubpf_set_instruction_limit(vm, 10000, NULL));
    char *message = NULL;
    int loaded = ubpf_load_elf_ex(vm, elf, (size_t)length, "entry", &message);
    if (loaded)
        fprintf(stderr, "%s\n", message);
    free(message);
    assert(!loaded);
    free(elf);
    uint64_t a, b, steps;
    unsigned depth, short_frame;
    _Static_assert(sizeof(((struct FgBpfFrame *)0)->arg0) == 8, "arg0 width");
    _Static_assert(sizeof(((struct FgBpfFrame *)0)->arg1) == 8, "arg1 width");
    while (scanf("%" SCNx64 " %" SCNx64 " %" SCNu64 " %u %u", &a, &b, &steps, &depth,
                 &short_frame) == 5) {
        struct FgBpfFrame frame = {0};
        frame.steps = steps;
        frame.max_depth = depth;
        memcpy(&frame.arg0, &a, 8);
        memcpy(&frame.arg1, &b, 8);
        uint64_t result = 0;
        assert(!ubpf_exec(vm, &frame, sizeof(frame) - (short_frame ? 1 : 0), &result));
        printf("%" PRIu64 " %u %" PRIx64 "\n", result, frame.error, (uint64_t)frame.result);
    }
    ubpf_destroy(vm);
    return 0;
}
