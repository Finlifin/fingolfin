#define _POSIX_C_SOURCE 200809L
#include "fingolfin/objects.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define LIMIT 256
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static FgObjects *registry;
static uint64_t active, device, events, begins, ends, errors, stale_checks;
struct Observed {
    FgObjectKey key;
    FgObjectIdentity identity;
};
static struct Observed objects[LIMIT];
static size_t count;
uint64_t fg_tt_launch_begin(uint64_t target_device) {
    pthread_mutex_lock(&mutex);
    uint64_t token = 0;
    if (active || errors) {
        errors++;
        goto done;
    }
    if (!registry) {
        const char *value = getenv("FG_LIFECYCLE_RUN");
        char *end = NULL;
        errno = 0;
        unsigned long long run = value ? strtoull(value, &end, 10) : 0;
        if (!value || value[0] == '-' || errno || !run || !end || *end ||
            fg_objects_create(run, 1, LIMIT, &registry) != FG_STATUS_OK) {
            errors++;
            goto done;
        }
    }
    if (fg_objects_begin(registry, &token) != FG_STATUS_OK) {
        errors++;
        goto done;
    }
    active = token;
    device = target_device;
    begins++;
done:
    pthread_mutex_unlock(&mutex);
    return token;
}
void fg_tt_scope_event(uint32_t hook, uint32_t phase, uint32_t x, uint32_t y, uint64_t a,
                       uint64_t b, uint64_t c) {
    (void)b;
    (void)c;
    pthread_mutex_lock(&mutex);
    events++;
    if (!active || errors || hook < 1 || hook > 39 || phase > 1 || x == UINT32_MAX ||
        y == UINT32_MAX) {
        errors++;
        goto done;
    }
    FgObjectIdentity id = {.device = device,
                           .local = hook <= 6 ? a : 0,
                           .kind = hook <= 6    ? 1u
                                   : hook <= 20 ? 2u
                                                : 3u,
                           .core_x = x,
                           .core_y = y};
    FgObjectKey key;
    FgStatus status = fg_objects_find(registry, active, &id, &key);
    if (status == FG_STATUS_NOT_FOUND) {
        if (count == LIMIT || fg_objects_register(registry, active, &id, &key) != FG_STATUS_OK) {
            errors++;
            goto done;
        }
        /* Every key saved from a completed launch must remain invalid after slot reuse. */
        for (size_t i = 0; i < count; i++)
            if (objects[i].key.launch != active) {
                FgObjectIdentity old;
                if (fg_objects_resolve(registry, &objects[i].key, &old) != FG_STATUS_NOT_FOUND)
                    errors++;
                else
                    stale_checks++;
            }
        objects[count++] = (struct Observed){key, id};
    } else if (status != FG_STATUS_OK) {
        errors++;
        goto done;
    }
    FgObjectIdentity resolved;
    if (fg_objects_resolve(registry, &key, &resolved) != FG_STATUS_OK ||
        resolved.device != device || resolved.kind != id.kind || resolved.local != id.local ||
        resolved.core_x != x || resolved.core_y != y)
        errors++;
done:
    pthread_mutex_unlock(&mutex);
}
void fg_tt_launch_end(uint64_t token) {
    pthread_mutex_lock(&mutex);
    if (!token || token != active || fg_objects_end(registry, token) != FG_STATUS_OK)
        errors++;
    else {
        ends++;
        for (size_t i = 0; i < count; i++)
            if (objects[i].key.launch == token) {
                FgObjectIdentity old;
                if (fg_objects_resolve(registry, &objects[i].key, &old) != FG_STATUS_NOT_FOUND)
                    errors++;
                else
                    stale_checks++;
            }
        active = 0;
    }
    pthread_mutex_unlock(&mutex);
}
__attribute__((destructor)) static void finish(void) {
    if (!begins && !events && !errors)
        return;
    if (active || begins != ends)
        errors++;
    if (registry && fg_objects_destroy(registry) != FG_STATUS_OK)
        errors++;
    const char *path = getenv("FG_LIFECYCLE_REPORT");
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f)
        return;
    fprintf(f,
            "{\"begins\":%llu,\"ends\":%llu,\"events\":%llu,\"errors\":%llu,\"stale_checks\":%llu,"
            "\"objects\":[",
            (unsigned long long)begins, (unsigned long long)ends, (unsigned long long)events,
            (unsigned long long)errors, (unsigned long long)stale_checks);
    for (size_t i = 0; i < count; i++) {
        const struct Observed *o = &objects[i];
        fprintf(f,
                "%s{\"run\":%llu,\"launch\":%llu,\"slot\":%llu,\"generation\":%llu,\"device\":%llu,"
                "\"kind\":%u,\"local\":%llu,\"x\":%u,\"y\":%u}",
                i ? "," : "", (unsigned long long)o->key.run, (unsigned long long)o->key.launch,
                (unsigned long long)o->key.slot, (unsigned long long)o->key.generation,
                (unsigned long long)o->identity.device, o->identity.kind,
                (unsigned long long)o->identity.local, o->identity.core_x, o->identity.core_y);
    }
    fputs("]}\n", f);
    fclose(f);
}
