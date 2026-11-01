#include "fingolfin/session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f && !fseek(f, 0, SEEK_END));
    long length = ftell(f);
    assert(length > 0);
    rewind(f);
    unsigned char *data = malloc((size_t)length);
    assert(data && fread(data, 1, (size_t)length, f) == (size_t)length);
    assert(!fclose(f));
    FgSession *s = NULL;
    assert(fg_session_create(0, 1, &s) == FG_STATUS_INVALID && !s);
    assert(fg_session_create(1, 0, &s) == FG_STATUS_INVALID && !s);
    assert(fg_session_create(1, 65, &s) == FG_STATUS_INVALID && !s);
    assert(fg_session_create(42, 1, &s) == FG_STATUS_OK);
    FgSessionRequest r = {.version = 1,
                          .struct_bytes = sizeof(r),
                          .session_id = 42,
                          .sequence = 1,
                          .expected_generation = 1,
                          .method = FG_SESSION_LOAD,
                          .package = {data, (size_t)length}};
    FgSessionReply reply;
    char diagnostic[256];
    r.version = 2;
    assert(fg_session_apply(s, &r, &reply, diagnostic, sizeof(diagnostic)) == FG_STATUS_INVALID);
    assert(!reply.last_sequence && reply.generation == 1 && !reply.program_count);
    r.version = 1;
    r.session_id = 41;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_CONFLICT);
    assert(!reply.last_sequence);
    r.session_id = 42;
    r.expected_program_hash = "bad";
    assert(fg_session_apply(s, &r, &reply, diagnostic, sizeof(diagnostic)) != FG_STATUS_OK);
    assert(reply.last_sequence == 1 && reply.generation == 1 && !reply.program_count);
    r.expected_program_hash = NULL;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_CONFLICT);
    r.sequence++;
    r.package.size /= 2;
    assert(fg_session_apply(s, &r, &reply, diagnostic, sizeof(diagnostic)) != FG_STATUS_OK);
    assert(reply.last_sequence == 2 && reply.generation == 1 && !reply.program_count);
    r.package.size = (size_t)length;
    r.sequence++;
    assert(fg_session_apply(s, &r, &reply, diagnostic, sizeof(diagnostic)) == FG_STATUS_OK);
    assert(reply.program_id == 1 && reply.generation == 2 && reply.program_count == 1);
    FgProgram *retained = NULL;
    assert(fg_session_program(s, 1, &retained) == FG_STATUS_OK);
    const FgPackage *package = fg_program_package(retained);
    char hash[65];
    strcpy(hash, fg_package_program_hash(package));
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_CONFLICT);
    r.sequence++;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_CONFLICT);
    assert(reply.last_sequence == 4 && reply.generation == 2);
    r.sequence++;
    r.expected_generation = 2;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_CAPACITY);
    r.sequence++;
    r.method = FG_SESSION_UNLOAD;
    r.program_id = 999;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_NOT_FOUND);
    r.sequence++;
    r.program_id = 1;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_OK);
    assert(reply.generation == 3 && !reply.program_count);
    assert(!strcmp(fg_package_program_hash(package), hash));
    FgProgram *missing = NULL;
    assert(fg_session_program(s, 1, &missing) == FG_STATUS_NOT_FOUND && !missing);
    r.sequence++;
    r.expected_generation = 3;
    r.method = FG_SESSION_LOAD;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_OK);
    assert(reply.program_id == 2 && reply.generation == 4);
    /* Loading snapshots input; keep both independently retained objects alive
       across close and destroy, even after the input buffer is overwritten. */
    FgProgram *second = NULL;
    uint64_t enumerated = 0;
    assert(fg_session_next(s, 0, &enumerated, &second) == FG_STATUS_OK);
    assert(enumerated == 2);
    assert(fg_session_next(s, 2, &enumerated, &missing) == FG_STATUS_NOT_FOUND);
    assert(!enumerated && !missing);
    assert(fg_session_next(s, UINT64_MAX, &enumerated, &missing) == FG_STATUS_NOT_FOUND);
    assert(!enumerated && !missing);
    assert(fg_session_next(s, 0, NULL, &missing) == FG_STATUS_INVALID && !missing);
    memset(data, 0, (size_t)length);
    free(data);
    r.package = (FgBytes){0};
    r.sequence++;
    r.method = FG_SESSION_STATUS;
    r.expected_generation = 0;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_OK);
    assert(reply.generation == 4 && reply.program_count == 1);
    r.sequence++;
    r.method = FG_SESSION_CLOSE;
    r.expected_generation = 4;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_OK);
    assert(reply.closed && reply.generation == 5 && !reply.program_count);
    assert(fg_session_next(s, 0, &enumerated, &missing) == FG_STATUS_CLOSED);
    assert(!enumerated && !missing);
    assert(fg_session_program(s, 2, &missing) == FG_STATUS_CLOSED && !missing);
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_CLOSED);
    fg_session_destroy(s);
    assert(!strcmp(fg_package_program_hash(fg_program_package(second)), hash));
    assert(!strcmp(fg_package_program_hash(package), hash));
    fg_program_release(second);
    fg_program_release(retained);
    /* Independent sessions may reuse local IDs but reject old session envelopes.
     */
    assert(fg_session_create(43, 1, &s) == FG_STATUS_OK);
    r.method = FG_SESSION_STATUS;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_CONFLICT);
    assert(!reply.last_sequence && reply.generation == 1);
    r.session_id = 43;
    r.sequence = UINT64_MAX;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_OK);
    r.sequence = 1;
    assert(fg_session_apply(s, &r, &reply, NULL, 0) == FG_STATUS_CONFLICT);
    fg_session_destroy(s);
    puts("session: load/unload, generation, replay, capacity, pins, ownership "
         "and close passed");
    return 0;
}
