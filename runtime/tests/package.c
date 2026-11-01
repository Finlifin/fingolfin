#include "fingolfin/package.h"
#include "fingolfin/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef FG_TEST_VM
#include "fingolfin/vm.h"
#endif
static FgPackageLimits limits = {
    1, sizeof(FgPackageLimits), 256, FG_PACKAGE_MAX_BYTES, FG_PACKAGE_MAX_BYTES, NULL, NULL};
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
static void hashes(void) {
    const char *inputs[] = {"", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *expected[] = {"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    for (size_t i = 0; i < 3; i++) {
        unsigned char digest[32];
        char hex[65];
        assert(fg_sha256((FgBytes){(const unsigned char *)inputs[i], strlen(inputs[i])}, digest) ==
               FG_STATUS_OK);
        for (size_t j = 0; j < 32; j++)
            snprintf(hex + 2 * j, 3, "%02x", digest[j]);
        assert(!strcmp(hex, expected[i]));
    }
}
#ifdef FG_TEST_VM
static void execute(const FgPackageEntry *e) {
    FgVmOptions options = {1, sizeof(options), 10000, (size_t)e->schema.frame_bytes,
                           e->helper_mask};
    FgVm *vm = NULL;
    char diagnostic[256];
    assert(fg_vm_load_elf(e->elf.data, e->elf.size, &options, &vm, diagnostic,
                          sizeof(diagnostic)) == FG_STATUS_OK);
    FgCounterConfig counter = {0, 0};
    FgHostConfig config = {.version = 1,
                           .struct_bytes = sizeof(config),
                           .counters = &counter,
                           .counter_count = 1,
                           .output_capacity = 2};
    FgHost *host = NULL;
    assert(fg_host_create(&config, NULL, &host) == FG_STATUS_OK);
    unsigned char frame[32];
    assert(e->schema.frame_bytes == sizeof(frame));
    assert(fg_frame_schema_init(&e->schema, frame, sizeof(frame), 10000, 64) == FG_STATUS_OK);
    FgSchemaSlot amount;
    assert(fg_frame_schema_slot(&e->schema, 1, &amount) == FG_STATUS_OK);
    assert(amount.offset == 24);
    frame[amount.offset] = 37;
    FgVmResult result;
    assert(fg_vm_run(vm, frame, sizeof(frame), host, &result) == FG_STATUS_OK);
    assert(!result.entry_status && !result.vm_status && result.helper_status == FG_STATUS_OK);
    uint64_t value;
    assert(fg_host_counter(host, 0, &value) == FG_STATUS_OK && value == 37);
    FgRecord record;
    assert(fg_host_pop(host, &record) == FG_STATUS_OK && record.tag == 7 && record.value == 37);
    fg_host_destroy(host);
    fg_vm_release(vm);
}
#endif
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    hashes();
    FgPackage *p = NULL;
    char diagnostic[256];
    FgStatus status = fg_package_read(argv[1], &limits, &p, diagnostic, sizeof(diagnostic));
    if (status != FG_STATUS_OK)
        fprintf(stderr, "%s\n", diagnostic);
    assert(status == FG_STATUS_OK && fg_package_count(p) == 1);
    const FgPackageEntry *entry = fg_package_find(p, "program::record");
    assert(entry && entry->helper_mask == 6);
    assert(fg_package_entry(p, 0) == entry && !fg_package_entry(p, 1) &&
           !fg_package_find(p, "absent"));
#ifdef FG_TEST_VM
    execute(entry);
#endif
    FgPackageLimits pinned = limits;
    pinned.expected_program_hash = fg_package_program_hash(p);
    pinned.expected_model_hash = fg_package_model_hash(p);
    FgPackage *copy = NULL;
    assert(fg_package_read(argv[1], &pinned, &copy, diagnostic, sizeof(diagnostic)) ==
           FG_STATUS_OK);
    fg_package_destroy(copy);
    pinned.expected_program_hash =
        "0000000000000000000000000000000000000000000000000000000000000000";
    assert(fg_package_read(argv[1], &pinned, &copy, diagnostic, sizeof(diagnostic)) !=
               FG_STATUS_OK &&
           !copy);
    pinned = limits;
    pinned.max_frame_bytes = 16;
    assert(fg_package_read(argv[1], &pinned, &copy, diagnostic, sizeof(diagnostic)) !=
               FG_STATUS_OK &&
           !copy);
    pinned = limits;
    pinned.max_decoded_bytes = 1;
    assert(fg_package_read(argv[1], &pinned, &copy, diagnostic, sizeof(diagnostic)) !=
               FG_STATUS_OK &&
           !copy);
    size_t size;
    unsigned char *bytes = read_file(argv[1], &size);
    assert(fg_package_load((FgBytes){bytes, size}, &limits, &copy, diagnostic,
                           sizeof(diagnostic)) == FG_STATUS_OK);
    memset(bytes, 0, size);
    free(bytes);
    assert(fg_package_find(copy, "program::record"));
    fg_package_destroy(copy);
    fg_package_destroy(p);
    /* Optional offline corpus: u32 accepted, u32 byte length, JSON bytes. */
    if (argc == 3) {
        FILE *f = fopen(argv[2], "rb");
        assert(f);
        unsigned char head[8];
        size_t cases = 0;
        while (fread(head, 1, 8, f) == 8) {
            uint32_t accepted = head[0], n = 0;
            for (unsigned i = 0; i < 4; i++)
                n |= (uint32_t)head[4 + i] << (8 * i);
            assert(n <= FG_PACKAGE_MAX_BYTES);
            bytes = malloc(n ? n : 1);
            assert(bytes);
            assert(fread(bytes, 1, n, f) == n);
            status =
                fg_package_load((FgBytes){bytes, n}, &limits, &p, diagnostic, sizeof(diagnostic));
            if ((status == FG_STATUS_OK) != !!accepted)
                fprintf(stderr, "case %zu: %s\n", cases, diagnostic);
            assert((status == FG_STATUS_OK) == !!accepted);
            assert(accepted || !p);
            fg_package_destroy(p);
            free(bytes);
            cases++;
        }
        assert(feof(f) && !ferror(f));
        fclose(f);
        printf("package corpus: %zu cases\n", cases);
    }
    puts("C package/hash checks passed");
    return 0;
}
