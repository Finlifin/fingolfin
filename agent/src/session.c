#include "fingolfin/session.h"
#include <stdlib.h>

struct Slot {
    uint64_t id;
    FgProgram *program;
};
struct FgSession {
    uint64_t id, sequence, generation, next_program;
    size_t capacity, count;
    int closed;
    struct Slot slots[FG_SESSION_MAX_PROGRAMS];
};
FgStatus fg_session_create(uint64_t id, size_t capacity, FgSession **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!id || !capacity || capacity > FG_SESSION_MAX_PROGRAMS)
        return FG_STATUS_INVALID;
    FgSession *session = calloc(1, sizeof(*session));
    if (!session)
        return FG_STATUS_NOMEM;
    session->id = id;
    session->capacity = capacity;
    session->generation = 1;
    session->next_program = 1;
    *out = session;
    return FG_STATUS_OK;
}
static void clear(FgSession *session) {
    for (size_t i = 0; i < session->capacity; i++) {
        fg_program_release(session->slots[i].program);
        session->slots[i] = (struct Slot){0};
    }
    session->count = 0;
}
void fg_session_destroy(FgSession *session) {
    if (session) {
        clear(session);
        free(session);
    }
}
FgStatus fg_session_program(FgSession *session, uint64_t id, FgProgram **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!session || !id)
        return FG_STATUS_INVALID;
    if (session->closed)
        return FG_STATUS_CLOSED;
    for (size_t i = 0; i < session->capacity; i++) {
        if (session->slots[i].id == id) {
            FgStatus status = fg_program_retain(session->slots[i].program);
            if (status == FG_STATUS_OK)
                *out = session->slots[i].program;
            return status;
        }
    }
    return FG_STATUS_NOT_FOUND;
}
FgStatus fg_session_next(FgSession *s, uint64_t after_id, uint64_t *id, FgProgram **out) {
    if (id)
        *id = 0;
    if (out)
        *out = NULL;
    if (!s || !id || !out)
        return FG_STATUS_INVALID;
    if (s->closed)
        return FG_STATUS_CLOSED;
    struct Slot *next = NULL;
    for (size_t i = 0; i < s->capacity; i++) {
        struct Slot *slot = &s->slots[i];
        if (slot->program && slot->id > after_id && (!next || slot->id < next->id))
            next = slot;
    }
    if (!next)
        return FG_STATUS_NOT_FOUND;
    FgStatus status = fg_program_retain(next->program);
    if (status == FG_STATUS_OK) {
        *id = next->id;
        *out = next->program;
    }
    return status;
}
static FgStatus apply(FgSession *s, const FgSessionRequest *r, uint64_t *id, char *diagnostic,
                      size_t diagnostic_bytes) {
    if (!r || r->version != FG_SESSION_VERSION || r->struct_bytes != sizeof(*r) || !r->sequence ||
        r->method < FG_SESSION_STATUS || r->method > FG_SESSION_LIST)
        return FG_STATUS_INVALID;
    if (s->closed)
        return FG_STATUS_CLOSED;
    if (r->session_id != s->id || r->sequence <= s->sequence)
        return FG_STATUS_CONFLICT;
    s->sequence = r->sequence;
    if (r->method == FG_SESSION_STATUS || r->method == FG_SESSION_LIST)
        return FG_STATUS_OK;
    if (r->expected_generation != s->generation)
        return FG_STATUS_CONFLICT;
    if (s->generation == UINT64_MAX)
        return FG_STATUS_OVERFLOW;
    if (r->method == FG_SESSION_CLOSE) {
        clear(s);
        s->closed = 1;
    } else if (r->method == FG_SESSION_UNLOAD) {
        size_t i = 0;
        for (; i < s->capacity; i++)
            if (s->slots[i].id == r->program_id && r->program_id)
                break;
        if (i == s->capacity)
            return FG_STATUS_NOT_FOUND;
        fg_program_release(s->slots[i].program);
        s->slots[i] = (struct Slot){0};
        s->count--;
    } else {
        if (s->count == s->capacity)
            return FG_STATUS_CAPACITY;
        if (!s->next_program)
            return FG_STATUS_OVERFLOW;
        FgPackageLimits limits = {
            FG_PACKAGE_LIMITS_VERSION, sizeof(limits),       256,
            FG_PACKAGE_MAX_BYTES,      FG_PACKAGE_MAX_BYTES, r->expected_program_hash,
            r->expected_model_hash};
        FgProgram *program = NULL;
        FgStatus status =
            fg_program_load(r->package, &limits, &program, diagnostic, diagnostic_bytes);
        if (status != FG_STATUS_OK)
            return status;
        size_t i = 0;
        while (s->slots[i].program)
            i++;
        *id = s->next_program++;
        s->slots[i] = (struct Slot){*id, program};
        s->count++;
    }
    s->generation++;
    return FG_STATUS_OK;
}
FgStatus fg_session_apply(FgSession *s, const FgSessionRequest *request, FgSessionReply *reply,
                          char *diagnostic, size_t diagnostic_bytes) {
    if (diagnostic && diagnostic_bytes)
        diagnostic[0] = 0;
    if (!s || !reply)
        return FG_STATUS_INVALID;
    uint64_t id = 0;
    FgStatus status = apply(s, request, &id, diagnostic, diagnostic_bytes);
    *reply = (FgSessionReply){s->id, s->sequence, s->generation, id, s->count, s->closed};
    return status;
}
