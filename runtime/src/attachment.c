#include "fingolfin/attachment.h"
#include <pthread.h>
#include <stdlib.h>
struct FgAttachment {
    FgAttachmentOptions options;
    FgExecutor *executor;
    FgHost *host;
    pthread_mutex_t lifecycle, state;
    size_t active, pins;
    int closed;
};
static int same(const FgStateKey *a, const FgStateKey *b) {
    return a->program == b->program && a->configuration == b->configuration && a->run == b->run &&
           a->launch == b->launch && a->slot == b->slot && a->generation == b->generation;
}
FgStatus fg_attachment_create(FgProgram *program, const char *function,
                              const FgAttachmentOptions *options, const FgHostConfig *host,
                              FgAttachment **out, char *diagnostic, size_t diagnostic_bytes) {
    if (diagnostic && diagnostic_bytes)
        diagnostic[0] = 0;
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!options || options->version != FG_ATTACHMENT_OPTIONS_VERSION ||
        options->struct_bytes != sizeof(*options) || !options->step_budget ||
        !options->depth_budget || (!diagnostic && diagnostic_bytes))
        return FG_STATUS_INVALID;
    FgAttachment *a = calloc(1, sizeof(*a));
    if (!a)
        return FG_STATUS_NOMEM;
    a->options = *options;
    if (pthread_mutex_init(&a->lifecycle, NULL)) {
        free(a);
        return FG_STATUS_NOMEM;
    }
    if (pthread_mutex_init(&a->state, NULL)) {
        pthread_mutex_destroy(&a->lifecycle);
        free(a);
        return FG_STATUS_NOMEM;
    }
    FgExecutorOptions execution = {FG_EXECUTOR_OPTIONS_VERSION, sizeof(execution), 1,
                                   options->instruction_limit, options->allowed_helpers};
    FgStatus status = fg_executor_create(program, function, &execution, &a->executor, diagnostic,
                                         diagnostic_bytes);
    if (status == FG_STATUS_OK)
        status = fg_host_create(host, NULL, &a->host);
    if (status != FG_STATUS_OK) {
        if (a->executor) {
            fg_executor_close(a->executor);
            fg_executor_destroy(a->executor);
        }
        pthread_mutex_destroy(&a->state);
        pthread_mutex_destroy(&a->lifecycle);
        free(a);
        return status;
    }
    *out = a;
    return FG_STATUS_OK;
}
FgStatus fg_attachment_pin(FgAttachment *a) {
    if (!a)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&a->lifecycle);
    FgStatus status = a->closed             ? FG_STATUS_CLOSED
                      : a->pins == SIZE_MAX ? FG_STATUS_OVERFLOW
                                            : FG_STATUS_OK;
    if (status == FG_STATUS_OK)
        a->pins++;
    pthread_mutex_unlock(&a->lifecycle);
    return status;
}
FgStatus fg_attachment_unpin(FgAttachment *a) {
    if (!a)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&a->lifecycle);
    FgStatus status = a->pins ? FG_STATUS_OK : FG_STATUS_INVALID;
    if (a->pins)
        a->pins--;
    pthread_mutex_unlock(&a->lifecycle);
    return status;
}
const FgStateKey *fg_attachment_identity(const FgAttachment *a) {
    return a ? &a->options.identity : NULL;
}
const FgPackageEntry *fg_attachment_entry(const FgAttachment *a) {
    return a ? fg_executor_entry(a->executor) : NULL;
}
FgStatus fg_attachment_run(FgAttachment *a, const FgStateKey *identity, void *frame, size_t bytes,
                           FgVmResult *result) {
    if (!a || !identity || !frame || !result ||
        bytes != fg_executor_entry(a->executor)->schema.frame_bytes)
        return FG_STATUS_INVALID;
    if (!same(identity, &a->options.identity))
        return FG_STATUS_CONFLICT;
    pthread_mutex_lock(&a->lifecycle);
    if (a->closed) {
        pthread_mutex_unlock(&a->lifecycle);
        return FG_STATUS_CLOSED;
    }
    if (pthread_mutex_trylock(&a->state)) {
        pthread_mutex_unlock(&a->lifecycle);
        return FG_STATUS_BUSY;
    }
    a->active++;
    pthread_mutex_unlock(&a->lifecycle);
    unsigned char *prefix = frame;
    for (unsigned i = 0; i < 8; i++)
        prefix[i] = (unsigned char)(a->options.step_budget >> (8 * i));
    for (unsigned i = 0; i < 4; i++) {
        prefix[8 + i] = (unsigned char)(a->options.depth_budget >> (8 * i));
        prefix[12 + i] = 0;
    }
    FgStatus status = fg_executor_run(a->executor, frame, bytes, a->host, result);
    pthread_mutex_lock(&a->lifecycle);
    a->active--;
    pthread_mutex_unlock(&a->state);
    pthread_mutex_unlock(&a->lifecycle);
    return status;
}
FgStatus fg_attachment_counter(FgAttachment *a, uint32_t slot, uint64_t *value) {
    if (!a || !value)
        return FG_STATUS_INVALID;
    if (pthread_mutex_trylock(&a->state))
        return FG_STATUS_BUSY;
    FgStatus status = fg_host_counter(a->host, slot, value);
    pthread_mutex_unlock(&a->state);
    return status;
}
FgStatus fg_attachment_map_value(FgAttachment *a, uint32_t slot, uint64_t key, uint64_t *value) {
    if (!a || !value)
        return FG_STATUS_INVALID;
    if (pthread_mutex_trylock(&a->state))
        return FG_STATUS_BUSY;
    FgStatus status = fg_host_map_value(a->host, slot, key, value);
    pthread_mutex_unlock(&a->state);
    return status;
}
FgStatus fg_attachment_pop(FgAttachment *a, FgRecord *record) {
    if (!a || !record)
        return FG_STATUS_INVALID;
    if (pthread_mutex_trylock(&a->state))
        return FG_STATUS_BUSY;
    FgStatus status = fg_host_pop(a->host, record);
    pthread_mutex_unlock(&a->state);
    return status;
}
FgStatus fg_attachment_close(FgAttachment *a) {
    if (!a)
        return FG_STATUS_INVALID;
    pthread_mutex_lock(&a->lifecycle);
    a->closed = 1;
    pthread_mutex_unlock(&a->lifecycle);
    return FG_STATUS_OK;
}
int fg_attachment_closed_and_drained(FgAttachment *a) {
    if (!a)
        return 0;
    pthread_mutex_lock(&a->lifecycle);
    int drained = a->closed && !a->active && !a->pins;
    pthread_mutex_unlock(&a->lifecycle);
    return drained;
}
FgStatus fg_attachment_destroy(FgAttachment *a) {
    if (!a)
        return FG_STATUS_INVALID;
    if (!fg_attachment_closed_and_drained(a))
        return FG_STATUS_BUSY;
    fg_executor_close(a->executor);
    FgStatus status = fg_executor_destroy(a->executor);
    if (status != FG_STATUS_OK)
        return status;
    fg_host_destroy(a->host);
    pthread_mutex_destroy(&a->state);
    pthread_mutex_destroy(&a->lifecycle);
    free(a);
    return FG_STATUS_OK;
}
