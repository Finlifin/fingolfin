#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ubpf.h>
struct Frame {
    uint64_t a, b;
    uint32_t iterations, reserved;
};
static unsigned helper_calls;
static uint64_t add(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e) {
    (void)c;
    (void)d;
    (void)e;
    ++helper_calls;
    return a + b;
}
static void *read_elf(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    assert(size > 0);
    *length = (size_t)size;
    rewind(f);
    void *bytes = malloc(*length);
    assert(bytes);
    assert(fread(bytes, 1, *length, f) == *length);
    fclose(f);
    return bytes;
}
static void load(struct ubpf_vm *vm, void *elf, size_t length) {
    char *message = NULL;
    int status = ubpf_load_elf_ex(vm, elf, length, "entry", &message);
    if (status)
        fprintf(stderr, "ELF load: %s\n", message);
    free(message);
    assert(status == 0);
}
int main(int argc, char **argv) {
    assert(argc == 4);
    size_t length, bad_length;
    void *elf = read_elf(argv[1], &length), *bad = read_elf(argv[2], &bad_length);
    size_t named_length;
    void *named = read_elf(argv[3], &named_length);
    struct ubpf_vm *named_vm = ubpf_create();
    assert(named_vm);
    assert(ubpf_register(named_vm, 1, "fg_add", add) == 0);
    char *named_error = NULL;
    assert(ubpf_load_elf_ex(named_vm, named, named_length, "entry", &named_error) != 0);
    fprintf(stderr, "KNOWN LIMITATION (Clang 23 named helper): %s\n", named_error);
    free(named_error);
    free(named);
    ubpf_destroy(named_vm);
    struct ubpf_vm *vm = ubpf_create();
    assert(vm);
    char *message = NULL;
    assert(ubpf_load_elf_ex(vm, elf, length, "entry", &message) != 0);
    free(message);
    ubpf_destroy(vm);
    vm = ubpf_create();
    assert(vm);
    assert(ubpf_register(vm, 1, "fg_add", add) == 0);
    load(vm, elf, length);
    assert(ubpf_set_instruction_limit(vm, 1000, NULL) == 0);
    ubpf_jit_fn jit = ubpf_compile(vm, &message);
    if (!jit)
        fprintf(stderr, "JIT: %s\n", message);
    assert(jit);
    free(message);
    for (uint32_t n = 0; n < 32; ++n) {
        struct Frame frame = {11, 7, n, 0};
        uint64_t result = 0;
        unsigned count = n < 8 ? n : 8;
        uint64_t expected = (11 + 7 * count) * 3 + 1;
        helper_calls = 0;
        assert(ubpf_exec(vm, &frame, sizeof(frame), &result) == 0);
        assert(result == expected && helper_calls == count);
        helper_calls = 0;
        assert(jit(&frame, sizeof(frame)) == expected && helper_calls == count);
    }
    struct Frame frame = {11, 7, 4, 0};
    uint64_t result = 0;
    assert(ubpf_set_instruction_limit(vm, 1, NULL) == 0);
    assert(ubpf_exec(vm, &frame, sizeof(frame), &result) != 0);
    ubpf_destroy(vm);
    vm = ubpf_create();
    assert(vm);
    load(vm, bad, bad_length);
    assert(ubpf_exec(vm, &frame, sizeof(frame), &result) != 0);
    ubpf_destroy(vm);
    vm = ubpf_create();
    assert(vm);
    assert(ubpf_set_execution_profile(vm, UBPF_EXECUTION_PROFILE_SAFE) == 0);
    load(vm, bad, bad_length);
    assert(ubpf_exec(vm, &frame, sizeof(frame), &result) != 0);
    message = NULL;
    assert(ubpf_compile(vm, &message) == NULL);
    free(message);
    ubpf_destroy(vm);
    free(elf);
    free(bad);
    puts(
        "PASS: ELF loading/helper IDs, bounded loops, local calls, 32 interpreter/JIT comparisons, "
        "missing helper rejection, interpreter budget/OOB rejection, safe-profile OOB/JIT refusal");
    return 0;
}
