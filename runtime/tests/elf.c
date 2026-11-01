#include "fingolfin/elf.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SIZE 416u
static void put16(unsigned char *b, size_t offset, uint16_t value) {
    b[offset] = (unsigned char)value;
    b[offset + 1] = (unsigned char)(value >> 8);
}
static void put32(unsigned char *b, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; i++)
        b[offset + i] = (unsigned char)(value >> (8 * i));
}
static void put64(unsigned char *b, size_t offset, uint64_t value) {
    for (size_t i = 0; i < 8; i++)
        b[offset + i] = (unsigned char)(value >> (8 * i));
}
static void fixture(unsigned char b[SIZE]) {
    memset(b, 0, SIZE);
    memcpy(b, "\177ELF\002\001\001", 7);
    put16(b, 16, 1);
    put16(b, 18, 247);
    put32(b, 20, 1);
    put64(b, 40, 160);
    put16(b, 52, 64);
    put16(b, 58, 64);
    put16(b, 60, 4);
    put16(b, 62, 1);
    b[64] = 0xb7;
    b[72] = 0x95;
    memcpy(b + 80, "\0.text\0.strtab\0.symtab\0entry\0", 29);
    put32(b, 136, 23);
    b[140] = 0x12;
    put16(b, 142, 2);
    put64(b, 152, 16);
    put32(b, 224, 7);
    put32(b, 228, 3);
    put64(b, 248, 80);
    put64(b, 256, 29);
    put64(b, 272, 1);
    put32(b, 288, 1);
    put32(b, 292, 1);
    put64(b, 296, 6);
    put64(b, 312, 64);
    put64(b, 320, 16);
    put64(b, 336, 8);
    put32(b, 352, 15);
    put32(b, 356, 2);
    put64(b, 376, 112);
    put64(b, 384, 48);
    put32(b, 392, 1);
    put32(b, 396, 1);
    put64(b, 400, 8);
    put64(b, 408, 24);
}
static FgStatus check(const unsigned char *b, size_t size, FgElfInfo *info) {
    char diagnostic[64];
    return fg_elf_check((FgBytes){b, size}, info, diagnostic, sizeof(diagnostic));
}
static void bytes(void) {
    const unsigned char b[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    FgBytes all = {b, sizeof(b)}, part = {NULL, 0};
    uint64_t value = 0;
    assert(fg_bytes_u64le(all, 1, &value) == FG_STATUS_OK && value == UINT64_C(0x0807060504030201));
    assert(fg_bytes_u64le(all, 2, &value) == FG_STATUS_INVALID &&
           value == UINT64_C(0x0807060504030201));
    assert(fg_bytes_sub(all, SIZE_MAX, 2, &part) == FG_STATUS_INVALID && part.data == NULL);
    assert(fg_bytes_sub((FgBytes){NULL, 0}, 0, 0, &part) == FG_STATUS_OK && part.size == 0);
    assert(fg_bytes_sub((FgBytes){NULL, 1}, 0, 0, &part) == FG_STATUS_INVALID);
    uint16_t v16;
    uint32_t v32;
    assert(fg_bytes_u16le(all, 1, &v16) == FG_STATUS_OK && v16 == 513);
    assert(fg_bytes_u32le(all, 1, &v32) == FG_STATUS_OK && v32 == UINT32_C(0x04030201));
}
static void bad_fields(void) {
    unsigned char b[SIZE];
    FgElfInfo info = {0};
    fixture(b);
    assert(check(b, SIZE, &info) == FG_STATUS_OK && info.entry_offset == 64 &&
           info.entry_bytes == 16 && info.function_count == 1 && info.helper_mask == 0);
    for (size_t size = 0; size < SIZE; size++)
        assert(check(b, size, &info) == FG_STATUS_INVALID);
    const size_t fields64[] = {40, 248, 256, 312, 320, 376, 384, 144, 152};
    for (size_t i = 0; i < sizeof(fields64) / sizeof(*fields64); i++) {
        fixture(b);
        put64(b, fields64[i], UINT64_MAX);
        assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    }
    fixture(b);
    put16(b, 18, 62);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    put16(b, 142, 4);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    put16(b, 142, 0);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    put32(b, 136, 1000);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    put32(b, 392, 2);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    put32(b, 356, 9);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    put64(b, 336, 3);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    put64(b, 296, 2);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    b[108] = 'x';
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    put64(b, 312, 80);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    b[64] = 0x85;
    put32(b, 68, 4);
    assert(check(b, SIZE, &info) == FG_STATUS_OK && info.helper_mask == (1u << 4));
    put32(b, 68, 99);
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    b[72] = 0x18;
    assert(check(b, SIZE, &info) == FG_STATUS_INVALID);
    fixture(b);
    b[64] = 0x18;
    b[72] = 0;
    put32(b, 76, 0x85);
    assert(check(b, SIZE, &info) == FG_STATUS_OK && info.helper_mask == 0);
    char diagnostic[1] = {'x'};
    fixture(b);
    b[0] = 0;
    FgElfInfo before = info;
    assert(fg_elf_check((FgBytes){b, SIZE}, &info, diagnostic, 1) == FG_STATUS_INVALID &&
           diagnostic[0] == 0);
    assert(!memcmp(&before, &info, sizeof(info)));
}
static void mutations(void) {
    unsigned char original[SIZE], changed[SIZE + 1];
    fixture(original);
    uint64_t seed = 13;
    FgElfInfo info;
    for (size_t i = 0; i < 30000; i++) {
        memcpy(changed + 1, original, SIZE);
        seed = seed * UINT64_C(6364136223846793005) + 1;
        size_t offset = (size_t)(seed >> 32) % SIZE;
        changed[1 + offset] ^= (unsigned char)((seed >> 16) | 1);
        FgStatus status = check(changed + 1, SIZE, &info);
        assert(status == FG_STATUS_OK || status == FG_STATUS_INVALID);
    }
    assert(check(original, SIZE, &info) == FG_STATUS_OK);
    puts("PASS: ELF structure/truncation/helpers + 30000 unaligned deterministic mutations");
}
int main(int argc, char **argv) {
    bytes();
    bad_fields();
    mutations();
    for (int i = 1; i < argc; i++) {
        FILE *file = fopen(argv[i], "rb");
        assert(file);
        assert(!fseek(file, 0, SEEK_END));
        long size = ftell(file);
        assert(size > 0 && size <= FG_ELF_MAX_BYTES);
        rewind(file);
        unsigned char *data = malloc((size_t)size);
        assert(data);
        assert(fread(data, 1, (size_t)size, file) == (size_t)size);
        fclose(file);
        FgElfInfo info;
        char diagnostic[256];
        FgStatus status =
            fg_elf_check((FgBytes){data, (size_t)size}, &info, diagnostic, sizeof(diagnostic));
        if (status != FG_STATUS_OK)
            fprintf(stderr, "%s: %s\n", argv[i], diagnostic);
        assert(status == FG_STATUS_OK);
        free(data);
    }
    if (argc > 1)
        printf("PASS: %d external generated ELF files\n", argc - 1);
}
