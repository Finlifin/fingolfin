#include "fingolfin/schema.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put32(unsigned char *b, size_t at, uint32_t n) {
    for (size_t i = 0; i < 4; i++)
        b[at + i] = (unsigned char)(n >> (8 * i));
}
static void put64(unsigned char *b, size_t at, uint64_t n) {
    for (size_t i = 0; i < 8; i++)
        b[at + i] = (unsigned char)(n >> (8 * i));
}
static void fixture(unsigned char b[132]) {
    memset(b, 0, 132);
    memcpy(b, "FGFRAME\0", 8);
    put32(b, 8, 1);
    put32(b, 12, 132);
    put64(b, 16, 24);
    put32(b, 24, 8);
    put32(b, 28, 1);
    put32(b, 40, 1);
    put32(b, 48, 4);
    put32(b, 56, 1);
    b[64] = 1;
    b[66] = 64;
    put32(b, 68, 8);
    put64(b, 72, 8);
    put64(b, 104, 16);
    put32(b, 120, 4);
    memcpy(b + 128, "arg0", 4);
}
static FgStatus check(FgBytes b, FgFrameSchema *schema) {
    char error[128];
    return fg_frame_schema_check(b, schema, error, sizeof(error));
}
static void basic(void) {
    unsigned char bytes[132];
    fixture(bytes);
    FgFrameSchema schema;
    assert(check((FgBytes){bytes, sizeof(bytes)}, &schema) == FG_STATUS_OK &&
           schema.frame_bytes == 24);
    FgSchemaSlot slot;
    assert(fg_frame_schema_slot(&schema, 0, &slot) == FG_STATUS_OK && slot.offset == 16 &&
           slot.size == 8 && slot.kind == 1 && slot.role == 0);
    assert(fg_frame_schema_slot(&schema, 1, &slot) == FG_STATUS_INVALID);
    FgSchemaType type;
    assert(fg_frame_schema_type(&schema, 0, &type) == FG_STATUS_OK && type.kind == 1 &&
           type.bits == 64 && !type.is_signed && type.size == 8);
    unsigned char frame[24];
    memset(frame, 255, sizeof(frame));
    assert(fg_frame_schema_init(&schema, frame, 23, 1, 2) == FG_STATUS_INVALID && frame[0] == 255);
    assert(fg_frame_schema_init(&schema, frame, 24, 1, 2) == FG_STATUS_OK && frame[0] == 1 &&
           frame[8] == 2 && frame[16] == 0);

    for (size_t size = 0; size < sizeof(bytes); size++)
        assert(check((FgBytes){bytes, size}, &schema) == FG_STATUS_INVALID);
    put64(bytes, 104, 24);
    assert(check((FgBytes){bytes, sizeof(bytes)}, &schema) == FG_STATUS_INVALID);
    fixture(bytes);
    put32(bytes, 112, 1);
    assert(check((FgBytes){bytes, sizeof(bytes)}, &schema) == FG_STATUS_INVALID);
    fixture(bytes);
    put64(bytes, 72, UINT64_MAX);
    assert(check((FgBytes){bytes, sizeof(bytes)}, &schema) == FG_STATUS_INVALID);
    fixture(bytes);
    bytes[128] = 0xff;
    assert(check((FgBytes){bytes, sizeof(bytes)}, &schema) == FG_STATUS_INVALID);
    uint64_t seed = 41;
    unsigned char changed[133];
    for (size_t i = 0; i < 10000; i++) {
        fixture(changed + 1);
        seed = seed * UINT64_C(6364136223846793005) + 1;
        changed[1 + (seed >> 32) % 132] ^= (unsigned char)((seed >> 16) | 1);
        FgStatus status = check((FgBytes){changed + 1, 132}, &schema);
        assert(status == FG_STATUS_OK || status == FG_STATUS_INVALID);
    }
}
static void corpus(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t count = 0;
    for (;;) {
        int expected = fgetc(file);
        if (expected == EOF)
            break;
        assert(expected == 0 || expected == 1);
        unsigned char size_bytes[4];
        assert(fread(size_bytes, 1, 4, file) == 4);
        uint32_t size;
        assert(fg_bytes_u32le((FgBytes){size_bytes, 4}, 0, &size) == FG_STATUS_OK &&
               size <= FG_SCHEMA_MAX_BYTES);
        unsigned char *bytes = malloc((size_t)size + 1);
        assert(bytes);
        assert(fread(bytes + 1, 1, size, file) == size);
        FgFrameSchema view;
        int accepted = check((FgBytes){bytes + 1, size}, &view) == FG_STATUS_OK;
        if (accepted != expected)
            fprintf(stderr, "schema mismatch at case %zu (C=%d expected=%d)\n", count, accepted,
                    expected);
        assert(accepted == expected);
        free(bytes);
        count++;
    }
    assert(!ferror(file));
    fclose(file);
    printf("PASS: %zu Rust/C schema agreement cases\n", count);
}
int main(int argc, char **argv) {
    basic();
    if (argc == 2)
        corpus(argv[1]);
    else
        assert(argc == 1);
    puts("PASS: C frame schema ranges/slots + 10000 unaligned mutations");
}
