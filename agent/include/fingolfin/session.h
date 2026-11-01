#ifndef FG_SESSION_H
#define FG_SESSION_H
#include "fingolfin/program.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FG_SESSION_VERSION 1u
#define FG_SESSION_MAX_PROGRAMS 64u
typedef struct FgSession FgSession;
typedef enum FgSessionMethod {
    FG_SESSION_STATUS = 1,
    FG_SESSION_LOAD,
    FG_SESSION_UNLOAD,
    FG_SESSION_CLOSE,
    FG_SESSION_LIST
} FgSessionMethod;
typedef struct FgSessionRequest {
    uint32_t version;
    size_t struct_bytes;
    uint64_t session_id, sequence, expected_generation;
    FgSessionMethod method;
    uint64_t program_id;
    FgBytes package;
    const char *expected_program_hash, *expected_model_hash;
} FgSessionRequest;
typedef struct FgSessionReply {
    uint64_t session_id, last_sequence, generation, program_id;
    size_t program_count;
    int closed;
} FgSessionReply;
/* Single controller owns a session; externally serialize ALL calls, including
 * reads/destroy. Caller supplies a nonzero session identity unique across
 * reconnects. This is an identity guard, not authentication. Initial
 * generation=1. Registry capacity is fixed; IDs are never reused within a
 * session. */
FgStatus fg_session_create(uint64_t session_id, size_t capacity, FgSession **out);
/* Version/size/session/method/sequence envelope failures do not consume
 * sequence. Accepted envelopes require strictly increasing nonzero sequence,
 * consumed even on stale generation, capacity or package errors. Replays
 * explicitly CONFLICT; no automatic replay of responses. STATUS/LIST ignore
 * expected_generation; mutations require exact generation and increment it only
 * on success. LOAD copies package through the C Program loader before
 * publication. Reply always snapshots current state when session/reply are
 * valid; program_id is nonzero only for successful LOAD. CLOSE releases
 * registry references, then rejects requests with CLOSED. Loading is not
 * attaching: no hooks execute until a separate controller binds. */
FgStatus fg_session_apply(FgSession *session, const FgSessionRequest *request,
                          FgSessionReply *reply, char *diagnostic, size_t diagnostic_bytes);
/* Acquires an independent Program reference. Unload/close/destroy cannot
 * invalidate it; caller must release. IDs are scoped to session_id. */
FgStatus fg_session_program(FgSession *session, uint64_t program_id, FgProgram **out);
/* Ascending-ID enumeration, excluding after_id (use 0 to start). Acquires one
 * independent reference; NOT_FOUND ends enumeration. Outputs reset on failure.
 * Externally exclude mutations for the entire traversal to obtain one snapshot.
 */
FgStatus fg_session_next(FgSession *session, uint64_t after_id, uint64_t *program_id,
                         FgProgram **out);
void fg_session_destroy(FgSession *session);
#ifdef __cplusplus
}
#endif
#endif
