#include "fingolfin/program.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
struct FgProgram {
    atomic_size_t references;
    FgPackage *package;
};
struct FgExecutor {
    FgProgram *program;
    const FgPackageEntry *entry;
    FgVmPool *pool;
};
static FgStatus publish(FgPackage *package, FgProgram **out) {
    FgProgram *program = malloc(sizeof(*program));
    if (!program) {
        fg_package_destroy(package);
        return FG_STATUS_NOMEM;
    }
    atomic_init(&program->references, 1);
    program->package = package;
    *out = program;
    return FG_STATUS_OK;
}
FgStatus fg_program_load(FgBytes bytes, const FgPackageLimits *limits, FgProgram **out,
                         char *diagnostic, size_t diagnostic_bytes) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgPackage *package = NULL;
    FgStatus status = fg_package_load(bytes, limits, &package, diagnostic, diagnostic_bytes);
    return status == FG_STATUS_OK ? publish(package, out) : status;
}
FgStatus fg_program_read(const char *path, const FgPackageLimits *limits, FgProgram **out,
                         char *diagnostic, size_t diagnostic_bytes) {
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    FgPackage *package = NULL;
    FgStatus status = fg_package_read(path, limits, &package, diagnostic, diagnostic_bytes);
    return status == FG_STATUS_OK ? publish(package, out) : status;
}
FgStatus fg_program_retain(FgProgram *program) {
    if (!program)
        return FG_STATUS_INVALID;
    size_t count = atomic_load_explicit(&program->references, memory_order_relaxed);
    do {
        if (count == SIZE_MAX)
            return FG_STATUS_OVERFLOW;
    } while (!atomic_compare_exchange_weak_explicit(&program->references, &count, count + 1,
                                                    memory_order_relaxed, memory_order_relaxed));
    return FG_STATUS_OK;
}
void fg_program_release(FgProgram *program) {
    if (program && atomic_fetch_sub_explicit(&program->references, 1, memory_order_acq_rel) == 1) {
        fg_package_destroy(program->package);
        free(program);
    }
}
const FgPackage *fg_program_package(const FgProgram *program) {
    return program ? program->package : NULL;
}
FgStatus fg_executor_create(FgProgram *program, const char *function,
                            const FgExecutorOptions *options, FgExecutor **out, char *diagnostic,
                            size_t diagnostic_bytes) {
    if (diagnostic && diagnostic_bytes)
        diagnostic[0] = 0;
    if (!out)
        return FG_STATUS_INVALID;
    *out = NULL;
    if (!program || !function || !options || options->version != FG_EXECUTOR_OPTIONS_VERSION ||
        options->struct_bytes != sizeof(*options) || !options->slots ||
        options->slots > FG_VM_POOL_MAX_SLOTS || !options->instruction_limit ||
        (options->allowed_helpers & ~FG_HELPERS_ALL) || (!diagnostic && diagnostic_bytes))
        return FG_STATUS_INVALID;
    const FgPackageEntry *entry = fg_package_find(program->package, function);
    if (!entry)
        return FG_STATUS_NOT_FOUND;
    if (entry->helper_mask & ~options->allowed_helpers) {
        if (diagnostic && diagnostic_bytes)
            snprintf(diagnostic, diagnostic_bytes, "entry helper requirements exceed policy");
        return FG_STATUS_INVALID;
    }
    FgExecutor *executor = calloc(1, sizeof(*executor));
    if (!executor)
        return FG_STATUS_NOMEM;
    FgStatus status = fg_program_retain(program);
    if (status != FG_STATUS_OK) {
        free(executor);
        return status;
    }
    executor->program = program;
    executor->entry = entry;
    FgVmOptions vm = {FG_VM_OPTIONS_VERSION, sizeof(vm), options->instruction_limit,
                      (size_t)entry->schema.frame_bytes, entry->helper_mask};
    status = fg_vm_pool_create(entry->elf.data, entry->elf.size, &vm, options->slots,
                               &executor->pool, diagnostic, diagnostic_bytes);
    if (status != FG_STATUS_OK) {
        fg_program_release(program);
        free(executor);
        return status;
    }
    *out = executor;
    return FG_STATUS_OK;
}
const FgPackageEntry *fg_executor_entry(const FgExecutor *executor) {
    return executor ? executor->entry : NULL;
}
FgStatus fg_executor_run(FgExecutor *executor, void *frame, size_t frame_bytes, FgHost *host,
                         FgVmResult *result) {
    if (!executor || !frame || !host || !result ||
        frame_bytes != executor->entry->schema.frame_bytes)
        return FG_STATUS_INVALID;
    FgStatus valid = fg_frame_schema_validate_inputs(
        &executor->entry->schema, (FgBytes){frame, frame_bytes}, FG_FRAME_INPUT_CHECK_BUDGET);
    if (valid != FG_STATUS_OK)
        return valid;
    FgVmLease lease = {0};
    FgStatus status = fg_vm_pool_acquire(executor->pool, &lease);
    if (status != FG_STATUS_OK)
        return status;
    status = fg_vm_lease_run(&lease, frame, frame_bytes, host, result);
    FgStatus released = fg_vm_lease_release(&lease);
    return released == FG_STATUS_OK ? status : released;
}
FgStatus fg_executor_close(FgExecutor *executor) {
    return executor ? fg_vm_pool_close(executor->pool) : FG_STATUS_INVALID;
}
int fg_executor_closed_and_drained(FgExecutor *executor) {
    return executor && fg_vm_pool_closed_and_drained(executor->pool);
}
FgStatus fg_executor_destroy(FgExecutor *executor) {
    if (!executor)
        return FG_STATUS_INVALID;
    FgStatus status = fg_vm_pool_destroy(executor->pool);
    if (status != FG_STATUS_OK)
        return status;
    fg_program_release(executor->program);
    free(executor);
    return FG_STATUS_OK;
}
