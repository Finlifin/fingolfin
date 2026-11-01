#define _POSIX_C_SOURCE 200809L
#include "adapter.h"
#include "fingolfin/objects.h"
#include "properties.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef FG_DYNAMIC_LIMIT
#define FG_DYNAMIC_LIMIT 512
#endif
#define LIMIT FG_DYNAMIC_LIMIT
#define HISTORY_LIMIT 512
/* One root, up to 64 devices, and one core/leaf per binding plus a failed event. */
#define OBJECT_LIMIT (65 + 2 * (LIMIT + 1))
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static FgObjects *registry;
static FgSession *session;
static uint64_t programs[40], session_sequence, session_generation = 1;
static uint64_t launch, device, begins, ends, seen, errors, dispatches, records, stale_routes,
    detached_routes;
static uint64_t skipped_events, discarded_records, history_dropped;
static int launch_failed, property_gate;
static uint64_t filtered;
static void fail(void) {
    errors++;
    launch_failed = 1;
}
static uint64_t devices[64];
static size_t device_count;
static FgObjectKey root_key;
static uint64_t tree_checks, tree_invalidations, property_checks, stale_properties;
static uint32_t next_thread;
static _Thread_local uint32_t thread;
struct Binding {
    FgMount *mount;
    FgObjectKey key, ancestors[3];
    FgObjectIdentity identity;
    uint32_t hook;
    int allowed;
    uint64_t counters[3], dispatches, records, matched_context, filtered;
};
static struct Binding bindings[LIMIT], history[HISTORY_LIMIT];
static size_t count, history_count;
static int same_key(FgObjectKey a, FgObjectKey b) {
    return a.run == b.run && a.launch == b.launch && a.slot == b.slot &&
           a.generation == b.generation;
}
/* Resolve parents from the registry, rather than trusting cached ancestry. */
static int check_tree(const struct Binding *b) {
    FgObjectKey child = b->key, parent;
    FgObjectIdentity id;
    for (size_t i = 0; i < 3; i++) {
        if (fg_objects_parent(registry, &child, &parent) != FG_STATUS_OK ||
            !same_key(parent, b->ancestors[i]) ||
            fg_objects_resolve(registry, &parent, &id) != FG_STATUS_OK)
            return 0;
        if (id.kind != (i == 0   ? 6u
                        : i == 1 ? 5u
                                 : 4u) ||
            id.local || id.device != (i == 2 ? 0 : b->identity.device) ||
            id.core_x != (i == 0 ? b->identity.core_x : 0) ||
            id.core_y != (i == 0 ? b->identity.core_y : 0))
            return 0;
        child = parent;
    }
    return same_key(child, root_key) &&
           fg_objects_parent(registry, &child, &parent) == FG_STATUS_NOT_FOUND;
}
static int check_properties(const struct Binding *b, int stale) {
    const char *id[] = {"id"}, *x[] = {"x"}, *y[] = {"y"};
    const char *leaf = b->identity.kind == 1   ? "tt_emule.device.core.cb"
                       : b->identity.kind == 2 ? "tt_emule.device.core.noc"
                                               : "tt_emule.device.core.compute";
    FgPropertyRead reads[] = {{{"tt_emule", NULL, 0}, FG_GATE_OBJECT},
                              {{"tt_emule", id, 1}, FG_GATE_OBJECT},
                              {{"tt_emule.device", NULL, 0}, FG_GATE_OBJECT},
                              {{"tt_emule.device", id, 1}, FG_GATE_OBJECT},
                              {{"tt_emule.device.core", NULL, 0}, FG_GATE_OBJECT},
                              {{"tt_emule.device.core", x, 1}, FG_GATE_OBJECT},
                              {{"tt_emule.device.core", y, 1}, FG_GATE_OBJECT},
                              {{leaf, NULL, 0}, FG_GATE_OBJECT},
                              {{leaf, id, 1}, FG_GATE_OBJECT}};
    FgTtProperties context = {registry, b->key};
    FgFactValue facts[9];
    uint64_t budget = 1024;
    if (fg_facts_collect(reads, 9, FG_GATE_OBJECT, fg_tt_property, &context, &budget, facts, 9) !=
        FG_STATUS_OK)
        return 0;
    const uint64_t values[] = {
        0, b->key.run,       0, b->identity.device, 0, b->identity.core_x, b->identity.core_y,
        0, b->identity.local};
    for (size_t i = 0; i < 9; i++) {
        uint32_t kind = stale                                  ? FG_FACT_UNKNOWN
                        : i == 0 || i == 2 || i == 4 || i == 7 ? FG_FACT_RECORD
                                                               : FG_FACT_INTEGER;
        if (facts[i].kind != kind || facts[i].integer.high ||
            facts[i].integer.low != (stale ? 0 : values[i]))
            return 0;
    }
    return 1;
}
static FgStatus ensure_child(const FgObjectIdentity *id, const FgObjectKey *parent,
                             FgObjectKey *out) {
    FgStatus status = fg_objects_find(registry, launch, id, out);
    if (status == FG_STATUS_NOT_FOUND)
        return fg_objects_register_child(registry, launch, id, parent, out);
    if (status == FG_STATUS_OK) {
        FgObjectKey actual;
        if (fg_objects_parent(registry, out, &actual) != FG_STATUS_OK || !same_key(actual, *parent))
            return FG_STATUS_CONFLICT;
    }
    return status;
}
static uint64_t load_program(uint32_t hook) {
    if (programs[hook])
        return programs[hook];
    const char *directory = getenv("FG_DYNAMIC_PACKAGES");
    char path[4096];
    if (!directory ||
        snprintf(path, sizeof(path), "%s/%u/push.fgo", directory, hook) >= (int)sizeof(path))
        return 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    unsigned char *data = malloc(FG_PACKAGE_MAX_BYTES + 1u);
    if (!data) {
        fclose(f);
        return 0;
    }
    size_t bytes = fread(data, 1, FG_PACKAGE_MAX_BYTES + 1u, f);
    int failed = ferror(f) || bytes > FG_PACKAGE_MAX_BYTES;
    if (fclose(f))
        failed = 1;
    FgSessionRequest request = {.version = 1,
                                .struct_bytes = sizeof(request),
                                .session_id = 1,
                                .sequence = ++session_sequence,
                                .expected_generation = session_generation,
                                .method = FG_SESSION_LOAD,
                                .package = {data, bytes}};
    FgSessionReply reply;
    FgStatus status = failed ? FG_STATUS_IO : fg_session_apply(session, &request, &reply, NULL, 0);
    free(data);
    if (status != FG_STATUS_OK)
        return 0;
    session_generation = reply.generation;
    programs[hook] = reply.program_id;
    return reply.program_id;
}
struct Factory {
    union {
        max_align_t alignment;
        unsigned char bytes[256];
    } frame;
    const FgModelAdapter *adapter;
    const FgModelEvent *event;
    unsigned calls;
};
static const FgCallHeader *factory(void *context, const FgLeaseV2 *lease) {
    struct Factory *f = context;
    f->calls++;
    FgCallHeader h = {FG_ABI_VERSION, (uint32_t)f->adapter->frame_bytes, lease->hook_id,
                      lease->generation, lease->object};
    f->adapter->fill(f->frame.bytes, &h, f->event);
    return (const FgCallHeader *)f->frame.bytes;
}
static void rejected(FgRuntimeV2 *runtime, struct Binding *b, FgObjectKey key,
                     const FgModelEvent *event, int detached) {
    struct Factory f = {.adapter = fg_model_adapters[b->hook], .event = event};
    FgDispatchReportV2 report = {0};
    int32_t status = fg_v2_observe(runtime, b->hook, &key, factory, &f, &report);
    if (status != -(int32_t)(detached ? FG_STATUS_CLOSED : FG_STATUS_INVALID) || f.calls ||
        report.attempted)
        fail();
    else if (detached)
        detached_routes++;
    else
        stale_routes++;
}
uint64_t fg_tt_launch_begin_many(const uint64_t *targets, size_t n) {
    pthread_mutex_lock(&mutex);
    uint64_t token = 0;
    if (!targets || !n || n > 64) {
        fail();
        goto done;
    }
    for (size_t i = 0; i < n; i++) {
        if (targets[i] == UINT64_MAX) {
            fail();
            goto done;
        }
        for (size_t j = 0; j < i; j++)
            if (targets[i] == targets[j]) {
                fail();
                goto done;
            }
    }
    if (launch || count) {
        fail();
        goto done;
    }
    if (!registry) {
        const char *mode = getenv("FG_DYNAMIC_PROPERTY_GATE");
        property_gate = mode && !strcmp(mode, "1");
        /* Explicit process-scoped fixture run domain, not a global run allocator. */
        if (fg_objects_create(71, 1, OBJECT_LIMIT, &registry) != FG_STATUS_OK) {
            fail();
            goto done;
        }
        if (fg_session_create(1, 39, &session) != FG_STATUS_OK) {
            if (fg_objects_destroy(registry) != FG_STATUS_OK)
                fail();
            registry = NULL;
            fail();
            goto done;
        }
    }
    if (fg_objects_begin(registry, &token) != FG_STATUS_OK) {
        fail();
        goto done;
    }
    FgObjectIdentity root = {.kind = 4};
    if (fg_objects_register(registry, token, &root, &root_key) != FG_STATUS_OK) {
        if (fg_objects_end(registry, token) != FG_STATUS_OK)
            fail();
        token = 0;
        fail();
        goto done;
    }
    launch = token;
    device = targets[0];
    device_count = n;
    memcpy(devices, targets, n * sizeof(*targets));
    launch_failed = 0;
    begins++;
done:
    pthread_mutex_unlock(&mutex);
    return token;
}
uint64_t fg_tt_launch_begin(uint64_t target_device) {
    return fg_tt_launch_begin_many(&target_device, 1);
}
static void observe(uint32_t hook, uint32_t phase, uint64_t source_device, uint32_t x, uint32_t y,
                    uint64_t a, uint64_t b, uint64_t c, int legacy) {
    pthread_mutex_lock(&mutex);
    seen++;
    if (!thread)
        thread = ++next_thread;
    FgModelEvent event = {.sequence = seen,
                          .a = a,
                          .b = b,
                          .c = c,
                          .hook = hook,
                          .phase = phase,
                          .x = x,
                          .y = y,
                          .thread = thread};
    if (launch && launch_failed) {
        skipped_events++;
        goto done;
    }
    if (!launch || hook < 1 || hook > 39 || phase > 1 || x == UINT32_MAX || y == UINT32_MAX) {
        fail();
        goto done;
    }
    if (legacy && device_count == 1)
        source_device = device;
    else if (legacy) {
        fail();
        goto done;
    }
    int known = 0;
    for (size_t i = 0; i < device_count; i++)
        if (devices[i] == source_device)
            known = 1;
    if (!known) {
        fail();
        goto done;
    }
    event.device = source_device;
    const FgModelAdapter *adapter = fg_model_adapters[hook];
    if (!adapter->valid(&event) || adapter->frame_bytes > 256) {
        fail();
        goto done;
    }
    FgObjectIdentity id = {.device = source_device,
                           .local = hook <= 6 ? a : 0,
                           .kind = hook <= 6    ? 1u
                                   : hook <= 20 ? 2u
                                                : 3u,
                           .core_x = x,
                           .core_y = y};
    FgObjectKey key, ancestors[3];
    ancestors[2] = root_key;
    FgObjectIdentity parent_id = {.device = source_device, .kind = 5};
    if (ensure_child(&parent_id, &root_key, &ancestors[1]) != FG_STATUS_OK) {
        fail();
        goto done;
    }
    parent_id.kind = 6;
    parent_id.core_x = x;
    parent_id.core_y = y;
    if (ensure_child(&parent_id, &ancestors[1], &ancestors[0]) != FG_STATUS_OK) {
        fail();
        goto done;
    }
    FgStatus status = ensure_child(&id, &ancestors[0], &key);
    if (status != FG_STATUS_OK) {
        fail();
        goto done;
    }
    struct Binding *binding = NULL;
    for (size_t i = 0; i < count; i++)
        if (bindings[i].hook == hook && same_key(bindings[i].key, key))
            binding = &bindings[i];
    if (!binding) {
        uint64_t program = load_program(hook);
        if (!program || count == LIMIT) {
            fail();
            goto done;
        }
        binding = &bindings[count];
        *binding = (struct Binding){.key = key, .identity = id, .hook = hook, .allowed = 1};
        memcpy(binding->ancestors, ancestors, sizeof(ancestors));
        if (!check_tree(binding) || !check_properties(binding, 0)) {
            fail();
            goto done;
        }
        property_checks++;
        FgCounterConfig counters[] = {{0, 0}, {1, 0}, {2, 0}};
        FgMountOptions options = {
            .version = 1,
            .struct_bytes = sizeof(options),
            .program_id = program,
            .hook = hook,
            .function = "program::observe",
            .native_frame_bytes = adapter->frame_bytes,
            .encode = adapter->encode,
            .attachment = {1,
                           sizeof(FgAttachmentOptions),
                           {program, 1, key.run, key.launch, key.slot, key.generation},
                           10000,
                           64,
                           10000,
                           6},
            .host = {.version = 1,
                     .struct_bytes = sizeof(FgHostConfig),
                     .counters = counters,
                     .counter_count = 3,
                     .output_capacity = 32}};
        FgFactValue facts[FG_TT_POLICY_MAX_READS];
        FgBindingGate gate;
        if (property_gate && hook <= 6) {
            FgTtProperties context = {registry, key};
            if (fg_tt_policy(&context, options.attachment.identity, facts, &gate,
                             &binding->allowed) != FG_STATUS_OK) {
                fail();
                goto done;
            }
            options.gate = &gate;
        }
        if (fg_mount_create(session, &options, &binding->mount, NULL, 0) != FG_STATUS_OK) {
            fail();
            goto done;
        }
        count++;
        FgRuntimeV2 rt = fg_mount_runtime(binding->mount);
        for (size_t i = 0; i < history_count; i++)
            if (history[i].hook == hook && history[i].key.slot == key.slot)
                rejected(&rt, binding, history[i].key, &event, 0);
    }
    struct Factory f = {.adapter = adapter, .event = &event};
    FgDispatchReportV2 report = {0};
    FgRuntimeV2 rt = fg_mount_runtime(binding->mount);
    int32_t result = fg_v2_observe(&rt, hook, &key, factory, &f, &report);
    if (!binding->allowed) {
        if (result || report.failed || report.attempted || f.calls)
            fail();
        else {
            filtered++;
            binding->filtered++;
        }
        goto done;
    }
    if (result || report.failed || report.attempted != 1 || f.calls != 1) {
        fail();
        goto done;
    }
    binding->dispatches++;
    dispatches++;
    binding->counters[phase]++;
    binding->counters[2] += adapter->matched(&event) != 0;
    FgRecord expected[32], actual;
    size_t n = adapter->expected(expected, &event);
    if (n > 32) {
        fail();
        goto done;
    }
    for (size_t i = 0; i < n; i++) {
        if (fg_mount_pop(binding->mount, &actual) != FG_STATUS_OK ||
            actual.tag != expected[i].tag || actual.value != expected[i].value)
            fail();
        else {
            binding->records++;
            records++;
        }
    }
    if (fg_mount_pop(binding->mount, &actual) != FG_STATUS_NOT_FOUND)
        fail();
done:
    pthread_mutex_unlock(&mutex);
}
void fg_tt_scope_event(uint32_t h, uint32_t p, uint32_t x, uint32_t y, uint64_t a, uint64_t b,
                       uint64_t c) {
    observe(h, p, 0, x, y, a, b, c, 1);
}
void fg_tt_scope_event_v2(uint32_t h, uint32_t p, uint64_t d, uint32_t x, uint32_t y, uint64_t a,
                          uint64_t b, uint64_t c) {
    observe(h, p, d, x, y, a, b, c, 0);
}
void fg_tt_launch_end(uint64_t token) {
    pthread_mutex_lock(&mutex);
    if (!token || token != launch) {
        fail();
        goto done;
    }
    /* Stop every route before collecting. On BUSY retain ownership and keys:
       a later end(token) can retry after outstanding leases drain. */
    int ready = 1;
    for (size_t i = 0; i < count; i++)
        if (bindings[i].mount) {
            if (fg_mount_detach(bindings[i].mount) != FG_STATUS_OK ||
                !fg_mount_detached_and_drained(bindings[i].mount))
                ready = 0;
        }
    if (!ready) {
        fail();
        goto done;
    }
    for (size_t i = 0; i < count; i++) {
        struct Binding *b = &bindings[i];
        if (!b->mount)
            continue;
        for (size_t slot = 0; slot < 3; slot++) {
            uint64_t value = 0;
            if (fg_mount_counter(b->mount, (uint32_t)slot, &value) != FG_STATUS_OK ||
                value != b->counters[slot])
                fail();
            if (slot == 2)
                b->matched_context = value;
        }
        FgRecord remaining;
        FgStatus drain;
        while ((drain = fg_mount_pop(b->mount, &remaining)) == FG_STATUS_OK)
            discarded_records++;
        if (drain != FG_STATUS_NOT_FOUND) {
            fail();
            goto done;
        }
        FgRuntimeV2 rt = fg_mount_runtime(b->mount);
        FgModelEvent unused = {0};
        rejected(&rt, b, b->key, &unused, 1);
        if (fg_mount_destroy(b->mount) != FG_STATUS_OK) {
            fail();
            goto done;
        }
        b->mount = NULL;
        if (!check_tree(b))
            fail();
        else
            tree_checks++;
        if (history_count < HISTORY_LIMIT)
            history[history_count++] = *b;
        else
            history_dropped++;
    }
    if (fg_objects_end(registry, token) != FG_STATUS_OK)
        fail();
    for (size_t i = 0; i < count; i++) {
        if (!check_properties(&bindings[i], 1))
            fail();
        else
            stale_properties++;
        FgObjectIdentity old;
        if (fg_objects_resolve(registry, &bindings[i].key, &old) != FG_STATUS_NOT_FOUND)
            fail();
        for (size_t j = 0; j < 3; j++) {
            if (fg_objects_resolve(registry, &bindings[i].ancestors[j], &old) !=
                FG_STATUS_NOT_FOUND)
                fail();
            else
                tree_invalidations++;
        }
    }
    FgObjectIdentity old_root;
    if (fg_objects_resolve(registry, &root_key, &old_root) != FG_STATUS_NOT_FOUND)
        fail();
    count = 0;
    launch = 0;
    ends++;
done:
    pthread_mutex_unlock(&mutex);
}
#ifdef FG_DYNAMIC_TESTING
/* Fault-harness access only; absent from the shipped observer. */
static FgLeaseV2 held_lease;
static int held;
int fg_dynamic_test_hold(void) {
    pthread_mutex_lock(&mutex);
    int result = -1;
    if (!held && count && bindings[0].mount) {
        FgRuntimeV2 rt = fg_mount_runtime(bindings[0].mount);
        result = fg_v2_acquire(&rt, bindings[0].hook, &bindings[0].key, &held_lease);
        held = !result && held_lease.enabled;
        if (!held)
            result = -1;
    }
    pthread_mutex_unlock(&mutex);
    return result;
}
int fg_dynamic_test_retained(void) {
    pthread_mutex_lock(&mutex);
    FgObjectIdentity identity;
    int result = launch && count && bindings[0].mount &&
                 fg_objects_resolve(registry, &bindings[0].key, &identity) == FG_STATUS_OK &&
                 check_tree(&bindings[0]);
    pthread_mutex_unlock(&mutex);
    return result;
}
void fg_dynamic_test_release(void) {
    pthread_mutex_lock(&mutex);
    if (held) {
        fg_v2_release(&held_lease);
        held = 0;
    }
    pthread_mutex_unlock(&mutex);
}
#endif
__attribute__((destructor)) static void finish(void) {
    if (!seen && !begins && !errors)
        return;
    if (launch || count || begins != ends)
        fail();
    fg_session_destroy(session);
    if (registry && fg_objects_destroy(registry) != FG_STATUS_OK)
        fail();
    const char *path = getenv("FG_DYNAMIC_REPORT");
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f)
        return;
    fprintf(f,
            "{\"property_gate\":%d,\"filtered\":%llu,\"begins\":%llu,\"ends\":%llu,\"events\":%llu,"
            "\"dispatches\":%llu,\"records\":%llu,\"errors\":%llu,\"stale_routes\":%llu,\"detached_"
            "routes\":%llu,\"skipped_events\":%llu,\"discarded_records\":%llu,\"history_dropped\":%"
            "llu,\"active_launch\":%llu,\"live_mounts\":%zu,\"property_checks\":%llu,\"stale_"
            "properties\":%llu,\"tree_checks\":%llu,\"tree_invalidations\":%llu,\"bindings\":[",
            property_gate, (unsigned long long)filtered, (unsigned long long)begins,
            (unsigned long long)ends, (unsigned long long)seen, (unsigned long long)dispatches,
            (unsigned long long)records, (unsigned long long)errors,
            (unsigned long long)stale_routes, (unsigned long long)detached_routes,
            (unsigned long long)skipped_events, (unsigned long long)discarded_records,
            (unsigned long long)history_dropped, (unsigned long long)launch, count,
            (unsigned long long)property_checks, (unsigned long long)stale_properties,
            (unsigned long long)tree_checks, (unsigned long long)tree_invalidations);
    for (size_t i = 0; i < history_count; i++) {
        struct Binding *b = &history[i];
        fprintf(f,
                "%s{\"hook\":%u,\"run\":%llu,\"launch\":%llu,\"slot\":%llu,\"generation\":%llu,"
                "\"device\":%llu,\"x\":%u,\"y\":%u,\"local\":%llu,\"dispatches\":%llu,\"records\":%"
                "llu,\"matched_context\":%llu,\"filtered\":%llu,\"ancestors\":[",
                i ? "," : "", b->hook, (unsigned long long)b->key.run,
                (unsigned long long)b->key.launch, (unsigned long long)b->key.slot,
                (unsigned long long)b->key.generation, (unsigned long long)b->identity.device,
                b->identity.core_x, b->identity.core_y, (unsigned long long)b->identity.local,
                (unsigned long long)b->dispatches, (unsigned long long)b->records,
                (unsigned long long)b->matched_context, (unsigned long long)b->filtered);
        for (size_t j = 0; j < 3; j++) {
            FgObjectKey k = b->ancestors[j];
            fprintf(f, "%s{\"run\":%llu,\"launch\":%llu,\"slot\":%llu,\"generation\":%llu}",
                    j ? "," : "", (unsigned long long)k.run, (unsigned long long)k.launch,
                    (unsigned long long)k.slot, (unsigned long long)k.generation);
        }
        fputs("]}", f);
    }
    fputs("]}\n", f);
    fclose(f);
}
