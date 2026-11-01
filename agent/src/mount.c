#include "fingolfin/mount.h"
#include <stdlib.h>
struct FgMount {
    FgPort *port;
    size_t count;
    FgAttachment *attachments[FG_PORT_MAX_SUBSCRIBERS];
    FgBinding *bindings[FG_PORT_MAX_SUBSCRIBERS];
    FgBinding *versions[8][FG_PORT_MAX_SUBSCRIBERS];
    unsigned char used[8];
    int current, detached;
};
static int valid(const FgMountOptions *o, const FgMountOptions *first) {
    const FgStateKey *a = &o->attachment.identity, *b = &first->attachment.identity;
    return o->version == FG_MOUNT_OPTIONS_VERSION && o->struct_bytes == sizeof(*o) &&
           o->program_id && a->program == o->program_id && o->function && o->encode &&
           o->hook == first->hook && o->native_frame_bytes == first->native_frame_bytes &&
           a->run == b->run && a->launch == b->launch && a->slot == b->slot &&
           a->generation == b->generation;
}
FgStatus fg_mount_create_many(FgSession *session, const FgMountOptions *options, size_t count,
                              FgMount **out, char *diagnostic, size_t diagnostic_bytes) {
    if (diagnostic && diagnostic_bytes)
        diagnostic[0] = 0;
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!session || !options || !count || count > FG_PORT_MAX_SUBSCRIBERS)
        return FG_STATUS_INVALID;
    for (size_t i = 0; i < count; i++)
        if (!valid(&options[i], options))
            return FG_STATUS_INVALID;
    FgMount *m = calloc(1, sizeof(*m));
    if (!m)
        return FG_STATUS_NOMEM;
    m->count = count;
    m->current = -1;
    const FgStateKey *identity = &options->attachment.identity;
    FgObjectKey object = {identity->run, identity->launch, identity->slot, identity->generation};
    FgStatus status = fg_port_create(options->hook, &object, count, NULL, &m->port);
    if (status != FG_STATUS_OK)
        goto fail;
    FgSubscription subscriptions[FG_PORT_MAX_SUBSCRIBERS];
    for (size_t i = 0; i < count; i++) {
        const FgMountOptions *o = &options[i];
        FgProgram *program = NULL;
        status = fg_session_program(session, o->program_id, &program);
        if (status != FG_STATUS_OK)
            goto fail;
        status = fg_attachment_create(program, o->function, &o->attachment, &o->host,
                                      &m->attachments[i], diagnostic, diagnostic_bytes);
        fg_program_release(program);
        if (status != FG_STATUS_OK)
            goto fail;
        if (o->gate)
            status = fg_binding_create_gated(m->attachments[i], o->hook, 1, o->native_frame_bytes,
                                             o->encode, o->gate, &m->bindings[i]);
        else
            status = fg_binding_create(m->attachments[i], o->hook, 1, o->native_frame_bytes,
                                       o->encode, &m->bindings[i]);
        if (status != FG_STATUS_OK)
            goto fail;
        status = fg_binding_subscription(m->bindings[i], i + 1, &subscriptions[i]);
        if (status != FG_STATUS_OK)
            goto fail;
    }
    status = fg_port_publish(m->port, 0, subscriptions, count, NULL);
    if (status != FG_STATUS_OK)
        goto fail;
    *out = m;
    return FG_STATUS_OK;
fail:
    /* Nothing escaped; all callbacks and candidate-route references have
     * returned. */
    if (m->port)
        fg_port_close(m->port);
    for (size_t i = 0; i < count; i++) {
        if (m->bindings[i])
            fg_binding_destroy(m->bindings[i]);
        if (m->attachments[i]) {
            fg_attachment_close(m->attachments[i]);
            fg_attachment_destroy(m->attachments[i]);
        }
    }
    if (m->port)
        fg_port_destroy(m->port);
    free(m);
    return status;
}
FgStatus fg_mount_create(FgSession *session, const FgMountOptions *options, FgMount **out,
                         char *diagnostic, size_t diagnostic_bytes) {
    return fg_mount_create_many(session, options, 1, out, diagnostic, diagnostic_bytes);
}
size_t fg_mount_count(const FgMount *m) {
    return m ? m->count : 0;
}
FgRuntimeV2 fg_mount_runtime(FgMount *m) {
    return m ? fg_port_runtime(m->port) : (FgRuntimeV2){0};
}
FgStatus fg_mount_detach(FgMount *m) {
    if (!m)
        return FG_STATUS_INVALID;
    FgStatus status = fg_port_close(m->port);
    if (status == FG_STATUS_OK)
        m->detached = 1;
    return status;
}
uint64_t fg_mount_generation(FgMount *m) {
    return m ? fg_port_generation(m->port) : 0;
}
FgStatus fg_mount_collect(FgMount *m) {
    if (!m)
        return FG_STATUS_INVALID;
    int pending = 0;
    for (int row = 0; row < 8; row++) {
        if (!m->used[row] || row == m->current)
            continue;
        int busy = 0;
        for (size_t i = 0; i < m->count; i++) {
            if (!m->versions[row][i])
                continue;
            FgStatus status = fg_binding_destroy(m->versions[row][i]);
            if (status == FG_STATUS_BUSY)
                busy = 1;
            else if (status != FG_STATUS_OK)
                return status;
            else
                m->versions[row][i] = NULL;
        }
        m->used[row] = (unsigned char)busy;
        pending |= busy;
    }
    return pending ? FG_STATUS_BUSY : FG_STATUS_OK;
}
FgStatus fg_mount_select(FgMount *m, uint64_t expected, uint64_t enabled, uint64_t *published) {
    if (!m || (m->count < 64 && (enabled >> m->count)))
        return FG_STATUS_INVALID;
    if (m->detached)
        return FG_STATUS_CLOSED;
    if (fg_port_generation(m->port) != expected)
        return FG_STATUS_CONFLICT;
    if (expected == UINT64_MAX)
        return FG_STATUS_OVERFLOW;
    FgStatus status = fg_mount_collect(m);
    if (status != FG_STATUS_OK && status != FG_STATUS_BUSY)
        return status;
    int row = 0;
    while (row < 8 && m->used[row])
        row++;
    if (row == 8)
        return FG_STATUS_CAPACITY;
    FgSubscription subscriptions[FG_PORT_MAX_SUBSCRIBERS];
    size_t count = 0;
    for (size_t i = 0; i < m->count; i++) {
        if (!(enabled & (UINT64_C(1) << i)))
            continue;
        status = fg_binding_clone(m->bindings[i], expected + 1, &m->versions[row][i]);
        if (status != FG_STATUS_OK)
            goto fail;
        status = fg_binding_subscription(m->versions[row][i], i + 1, &subscriptions[count++]);
        if (status != FG_STATUS_OK)
            goto fail;
    }
    status = fg_port_publish(m->port, expected, subscriptions, count, published);
    if (status != FG_STATUS_OK)
        goto fail;
    m->used[row] = 1;
    m->current = row;
    return FG_STATUS_OK;
fail:
    for (size_t i = 0; i < m->count; i++) {
        if (m->versions[row][i]) {
            fg_binding_destroy(m->versions[row][i]);
            m->versions[row][i] = NULL;
        }
    }
    return status;
}
int fg_mount_detached_and_drained(FgMount *m) {
    return m && fg_port_closed_and_drained(m->port);
}
FgStatus fg_mount_counter_at(FgMount *m, size_t index, uint32_t slot, uint64_t *value) {
    return m && index < m->count && m->attachments[index]
               ? fg_attachment_counter(m->attachments[index], slot, value)
               : FG_STATUS_INVALID;
}
FgStatus fg_mount_pop_at(FgMount *m, size_t index, FgRecord *record) {
    return m && index < m->count && m->attachments[index]
               ? fg_attachment_pop(m->attachments[index], record)
               : FG_STATUS_INVALID;
}
FgStatus fg_mount_counter(FgMount *m, uint32_t slot, uint64_t *value) {
    return fg_mount_counter_at(m, 0, slot, value);
}
FgStatus fg_mount_pop(FgMount *m, FgRecord *record) {
    return fg_mount_pop_at(m, 0, record);
}
FgStatus fg_mount_destroy(FgMount *m) {
    if (!m)
        return FG_STATUS_INVALID;
    if (!fg_mount_detached_and_drained(m))
        return FG_STATUS_BUSY;
    m->current = -1;
    FgStatus collected = fg_mount_collect(m);
    if (collected != FG_STATUS_OK)
        return collected;
    for (size_t i = 0; i < m->count; i++) {
        FgStatus status;
        if (m->bindings[i]) {
            status = fg_binding_destroy(m->bindings[i]);
            if (status != FG_STATUS_OK)
                return status;
            m->bindings[i] = NULL;
        }
        if (m->attachments[i]) {
            fg_attachment_close(m->attachments[i]);
            status = fg_attachment_destroy(m->attachments[i]);
            if (status != FG_STATUS_OK)
                return status;
            m->attachments[i] = NULL;
        }
    }
    fg_port_destroy(m->port);
    free(m);
    return FG_STATUS_OK;
}
