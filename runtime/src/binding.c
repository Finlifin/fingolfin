#include "fingolfin/binding.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
struct FgBinding {
    FgAttachment *attachment;
    FgStateKey identity;
    uint64_t hook, generation;
    size_t native_bytes, frame_bytes;
    FgBindingEncode encode;
    unsigned char *frame;
    pthread_mutex_t execution;
    atomic_size_t routes;
    FgBindingGate gate_config;
    FgGateNode *gate_nodes;
    unsigned char *facts;
};
static int matches(FgBinding *b, const FgObjectKey *o, uint64_t generation) {
    return o && generation == b->generation && o->run == b->identity.run &&
           o->launch == b->identity.launch && o->slot == b->identity.slot &&
           o->generation == b->identity.generation;
}
static FgStatus predicate(void *context, uint32_t index, uint64_t *budget, FgTruth *truth) {
    FgBinding *b = context;
    if (!b->gate_config.predicate)
        return FG_STATUS_INVALID;
    return b->gate_config.predicate(b->facts, b->gate_config.facts.size, index, budget, truth);
}
static int gate(void *context, const FgObjectKey *object, uint64_t generation) {
    FgBinding *b = context;
    if (!matches(b, object, generation))
        return 0;
    if (!b->gate_nodes)
        return 1;
    return fg_gate_evaluate(b->gate_config.plan, &b->identity, &b->gate_config.fact_identity,
                            b->gate_config.stage, b->gate_config.budget, predicate, b)
        .allow;
}
static void retain(void *context) {
    FgBinding *b = context;
    size_t count = atomic_load_explicit(&b->routes, memory_order_relaxed);
    do {
        if (count == SIZE_MAX)
            abort();
    } while (!atomic_compare_exchange_weak_explicit(&b->routes, &count, count + 1,
                                                    memory_order_relaxed, memory_order_relaxed));
}
static void release(void *context) {
    FgBinding *b = context;
    atomic_fetch_sub_explicit(&b->routes, 1, memory_order_release);
}
static int32_t dispatch(void *context, const FgCallHeader *native) {
    FgBinding *b = context;
    if (!native || native->abi_version != FG_ABI_VERSION ||
        native->frame_bytes != b->native_bytes || native->hook_id != b->hook ||
        !matches(b, &native->object, native->config_generation))
        return FG_V2_ERROR_FRAME;
    if (pthread_mutex_trylock(&b->execution))
        return -(int32_t)FG_STATUS_BUSY;
    memset(b->frame, 0, b->frame_bytes);
    const FgFrameSchema *schema = &fg_attachment_entry(b->attachment)->schema;
    FgStatus status = b->encode(native, schema, b->frame, b->frame_bytes);
    FgVmResult result = {0};
    if (status == FG_STATUS_OK)
        status = fg_attachment_run(b->attachment, &b->identity, b->frame, b->frame_bytes, &result);
    pthread_mutex_unlock(&b->execution);
    if (status != FG_STATUS_OK)
        return -(int32_t)status;
    return result.entry_status > INT32_MAX ? -(int32_t)FG_STATUS_VM : (int32_t)result.entry_status;
}
FgStatus fg_binding_create(FgAttachment *attachment, uint64_t hook, uint64_t generation,
                           size_t native_bytes, FgBindingEncode encode, FgBinding **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!attachment || !encode || !generation || native_bytes < sizeof(FgCallHeader) ||
        native_bytes > UINT32_MAX)
        return FG_STATUS_INVALID;
    FgStatus status = fg_attachment_pin(attachment);
    if (status != FG_STATUS_OK)
        return status;
    FgBinding *b = calloc(1, sizeof(*b));
    if (!b) {
        fg_attachment_unpin(attachment);
        return FG_STATUS_NOMEM;
    }
    b->attachment = attachment;
    b->identity = *fg_attachment_identity(attachment);
    b->hook = hook;
    b->generation = generation;
    b->native_bytes = native_bytes;
    b->encode = encode;
    b->frame_bytes = (size_t)fg_attachment_entry(attachment)->schema.frame_bytes;
    atomic_init(&b->routes, 0);
    b->frame = malloc(b->frame_bytes);
    if (!b->frame || pthread_mutex_init(&b->execution, NULL)) {
        free(b->frame);
        free(b);
        fg_attachment_unpin(attachment);
        return FG_STATUS_NOMEM;
    }
    *out = b;
    return FG_STATUS_OK;
}
FgStatus fg_binding_create_gated(FgAttachment *attachment, uint64_t hook, uint64_t generation,
                                 size_t native_bytes, FgBindingEncode encode,
                                 const FgBindingGate *config, FgBinding **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!config || config->version != FG_BINDING_GATE_VERSION ||
        config->struct_bytes != sizeof(*config) || !config->plan.nodes || !config->plan.count ||
        config->plan.count > FG_GATE_MAX_NODES || (unsigned)config->stage > FG_GATE_UPDATE ||
        config->facts.size > FG_BINDING_MAX_FACT_BYTES ||
        (!config->facts.data && config->facts.size))
        return FG_STATUS_INVALID;
    FgBinding *b = NULL;
    FgStatus status = fg_binding_create(attachment, hook, generation, native_bytes, encode, &b);
    if (status != FG_STATUS_OK)
        return status;
    b->gate_nodes = malloc(config->plan.count * sizeof(*b->gate_nodes));
    if (config->facts.size)
        b->facts = malloc(config->facts.size);
    if (!b->gate_nodes || (config->facts.size && !b->facts)) {
        fg_binding_destroy(b);
        return FG_STATUS_NOMEM;
    }
    memcpy(b->gate_nodes, config->plan.nodes, config->plan.count * sizeof(*b->gate_nodes));
    if (config->facts.size)
        memcpy(b->facts, config->facts.data, config->facts.size);
    b->gate_config = *config;
    b->gate_config.plan.nodes = b->gate_nodes;
    b->gate_config.facts.data = b->facts;
    *out = b;
    return FG_STATUS_OK;
}
FgStatus fg_binding_clone(FgBinding *b, uint64_t generation, FgBinding **out) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!b)
        return FG_STATUS_INVALID;
    return b->gate_nodes ? fg_binding_create_gated(b->attachment, b->hook, generation,
                                                   b->native_bytes, b->encode, &b->gate_config, out)
                         : fg_binding_create(b->attachment, b->hook, generation, b->native_bytes,
                                             b->encode, out);
}
FgStatus fg_binding_subscription(FgBinding *b, uint64_t id, FgSubscription *out) {
    if (!b || !out)
        return FG_STATUS_INVALID;
    *out = (FgSubscription){id, b, gate, dispatch, retain, release};
    return FG_STATUS_OK;
}
FgStatus fg_binding_destroy(FgBinding *b) {
    if (!b)
        return FG_STATUS_INVALID;
    if (atomic_load_explicit(&b->routes, memory_order_acquire))
        return FG_STATUS_BUSY;
    fg_attachment_unpin(b->attachment);
    pthread_mutex_destroy(&b->execution);
    free(b->facts);
    free(b->gate_nodes);
    free(b->frame);
    free(b);
    return FG_STATUS_OK;
}
