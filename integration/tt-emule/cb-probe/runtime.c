#define _POSIX_C_SOURCE 200809L
#include "fingolfin.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ubpf.h>

struct Frame {
    FgCallHeader header;
    uint32_t cb, pages, x, y;
};
_Static_assert(sizeof(struct Frame) == 72, "frame layout");
static struct ubpf_vm *vm;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t seen, frames, executions, errors, push_pages, pop_pages;
static int mode; /* 0=observe, 1=disabled, 2=nonmatching object */
static void initialize(void) {
    const char *setting = getenv("FG_CB_MODE");
    mode = setting && !strcmp(setting, "off") ? 1 : setting && !strcmp(setting, "miss") ? 2 : 0;
    const char *path = getenv("FG_CB_BPF");
    if (!path) {
        ++errors;
        return;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        ++errors;
        return;
    }
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        ++errors;
        return;
    }
    long length = ftell(file);
    rewind(file);
    if (length <= 0) {
        fclose(file);
        ++errors;
        return;
    }
    void *elf = malloc((size_t)length);
    if (!elf || fread(elf, 1, (size_t)length, file) != (size_t)length) {
        free(elf);
        fclose(file);
        ++errors;
        return;
    }
    fclose(file);
    vm = ubpf_create();
    char *message = NULL;
    if (!vm || ubpf_set_execution_profile(vm, UBPF_EXECUTION_PROFILE_SAFE) ||
        ubpf_set_instruction_limit(vm, 1000, NULL) ||
        ubpf_load_elf_ex(vm, elf, (size_t)length, "entry", &message)) {
        fprintf(stderr, "Fingolfin CB probe load failed: %s\n", message ? message : "VM setup");
        ++errors;
        if (vm)
            ubpf_destroy(vm);
        vm = NULL;
    }
    free(message);
    free(elf);
}
static uint8_t enabled(void *context, uint64_t hook, const FgObjectKey *key) {
    (void)context;
    return vm && mode != 1 && (hook == 1 || hook == 2) && key->slot == (mode == 2 ? 16u : 0u);
}
static int32_t dispatch(void *context, const FgCallHeader *header) {
    (void)context;
    if (header->frame_bytes != sizeof(struct Frame)) {
        ++errors;
        return FG_ERROR_FRAME;
    }
    uint64_t result = 0;
    ++executions;
    if (ubpf_exec(vm, (void *)header, sizeof(struct Frame), &result)) {
        ++errors;
        return FG_ERROR_FRAME;
    }
    if (header->hook_id == 1)
        push_pages += result;
    else if (header->hook_id == 2)
        pop_pages += result;
    else
        ++errors;
    return FG_OK;
}
static const FgCallHeader *make_frame(struct Frame *frame, uint32_t hook, const FgObjectKey *key,
                                      uint32_t cb, uint32_t pages, uint32_t x, uint32_t y) {
    ++frames;
    *frame = (struct Frame){{FG_ABI_VERSION, sizeof(*frame), hook, 1, *key}, cb, pages, x, y};
    return &frame->header;
}
/* Technical spike: one launch per process; no claim of lifecycle management. */
void fg_tt_cb_event(uint32_t hook, uint32_t cb, uint32_t pages, uint32_t x, uint32_t y) {
    pthread_mutex_lock(&mutex);
    pthread_once(&once, initialize);
    ++seen;
    FgObjectKey key = {1, 1, ((uint64_t)x << 32) | ((uint64_t)y << 16) | cb, 1};
    FgRuntimeV1 runtime = {FG_ABI_VERSION, sizeof(runtime), NULL, enabled, dispatch};
    struct Frame frame;
    FG_OBSERVE(&runtime, hook, &key, make_frame(&frame, hook, &key, cb, pages, x, y));
    pthread_mutex_unlock(&mutex);
}
__attribute__((destructor)) static void finish(void) {
    /* LD_PRELOAD propagates to JIT compiler subprocesses: only the process that
     * observed real CB events may write evidence. */
    if (!seen)
        return;
    const char *path = getenv("FG_CB_REPORT");
    if (!path)
        return;
    FILE *file = fopen(path, "w");
    if (!file)
        return;
    fprintf(file,
            "{\"seen\":%llu,\"frames\":%llu,\"vm\":%llu,\"errors\":%llu,\"push_pages\":%llu,\"pop_"
            "pages\":%llu}\n",
            (unsigned long long)seen, (unsigned long long)frames, (unsigned long long)executions,
            (unsigned long long)errors, (unsigned long long)push_pages,
            (unsigned long long)pop_pages);
    fclose(file);
    if (vm)
        ubpf_destroy(vm);
}
