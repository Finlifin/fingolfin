#include <assert.h>
#include <ebpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <ubpf.h>
static int quiet(FILE *stream, const char *format, ...) {
    (void)stream;
    (void)format;
    return 0;
}
int main(void) {
    struct ubpf_vm *vm = ubpf_create();
    assert(vm);
    assert(!ubpf_set_execution_profile(vm, UBPF_EXECUTION_PROFILE_SAFE));
    assert(!ubpf_set_instruction_limit(vm, 100, NULL));
    ubpf_set_error_print(vm, quiet);
    ubpf_toggle_undefined_behavior_check(vm, true);
    /* input == 0 writes stack, input != 0 skips write then reads same stack.
     * Reusing scratch must never preserve prior initialized shadow bits. */
    struct ebpf_inst code[] = {{.opcode = 0x79, .dst = 2, .src = 1},
                               {.opcode = 0x55, .dst = 2, .offset = 1},
                               {.opcode = 0x7a, .dst = 10, .offset = -8, .imm = 7},
                               {.opcode = 0x79, .dst = 0, .src = 10, .offset = -8},
                               {.opcode = 0x95}};
    char *error = NULL;
    assert(!ubpf_load(vm, code, sizeof(code), &error));
    free(error);
    struct ubpf_safe_scratch *scratch = ubpf_safe_scratch_create();
    assert(scratch);
    for (size_t i = 0; i < 2000; i++) {
        uint64_t input = i % 2, a = 0, b = 0;
        int legacy = ubpf_exec(vm, &input, sizeof(input), &a);
        int reused = ubpf_exec_safe_scratch(vm, &input, sizeof(input), &b, scratch);
        assert(legacy == reused);
        if (input)
            assert(reused < 0);
        else
            assert(!reused && a == 7 && b == 7);
    }
    assert(ubpf_exec_safe_scratch(vm, NULL, 1, NULL, scratch) < 0);
    ubpf_safe_scratch_destroy(scratch);
    ubpf_destroy(vm);
    puts("2000 legacy/scratch comparisons; stale stack initialization rejected");
    return 0;
}
