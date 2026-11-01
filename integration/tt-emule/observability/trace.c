#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define LIMIT 65536
struct Event {
    uint32_t hook, phase, x, y, thread;
    uint64_t a, b, c, ns;
};
static struct Event events[LIMIT];
static size_t count, dropped;
static uint32_t next_thread;
static _Thread_local uint32_t thread;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
void fg_tt_scope_event(uint32_t hook, uint32_t phase, uint32_t x, uint32_t y, uint64_t a,
                       uint64_t b, uint64_t c) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return;
    pthread_mutex_lock(&mutex);
    if (!thread)
        thread = ++next_thread;
    if (count < LIMIT)
        events[count++] = (struct Event){
            hook, phase,  x,
            y,    thread, a,
            b,    c,      (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec};
    else
        dropped++;
    pthread_mutex_unlock(&mutex);
}
__attribute__((destructor)) static void finish(void) {
    if (!count)
        return;
    const char *path = getenv("FG_TRACE_REPORT");
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "{\"dropped\":%zu,\"events\":[", dropped);
    for (size_t i = 0; i < count; i++) {
        const struct Event *e = &events[i];
        fprintf(f,
                "%s{\"sequence\":%zu,\"hook\":%u,\"phase\":%u,\"x\":%u,\"y\":%u,"
                "\"thread\":%u,\"ns\":\"%llu\",\"args\":[\"%llu\",\"%llu\",\"%llu\"]}",
                i ? "," : "", i + 1, e->hook, e->phase, e->x, e->y, e->thread,
                (unsigned long long)e->ns, (unsigned long long)e->a, (unsigned long long)e->b,
                (unsigned long long)e->c);
    }
    fputs("]}\n", f);
    fclose(f);
}
