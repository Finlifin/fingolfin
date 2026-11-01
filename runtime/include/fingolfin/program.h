#ifndef FG_PROGRAM_H
#define FG_PROGRAM_H
#include "package.h"
#include "pool.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FgProgram FgProgram;
typedef struct FgExecutor FgExecutor;
#define FG_EXECUTOR_OPTIONS_VERSION 1u
typedef struct FgExecutorOptions {
    uint32_t version;
    size_t struct_bytes, slots;
    uint32_t instruction_limit, allowed_helpers;
} FgExecutorOptions;
/* Immutable validated development package. New objects hold one reference.
 * retain requires an already-live reference; final release excludes all users.
 * No global registry or implicit active program. Requires package + uBPF. */
FgStatus fg_program_load(FgBytes bytes, const FgPackageLimits *limits, FgProgram **out,
                         char *diagnostic, size_t diagnostic_bytes);
FgStatus fg_program_read(const char *path, const FgPackageLimits *limits, FgProgram **out,
                         char *diagnostic, size_t diagnostic_bytes);
FgStatus fg_program_retain(FgProgram *program);
void fg_program_release(FgProgram *program);
/* Borrowed immutable package; valid while caller holds a program reference. */
const FgPackage *fg_program_package(const FgProgram *program);
/* Select by stable function identity. Executor retains program and owns a pool;
 * different executors never share mutable VM scratch. allowed_helpers is an
 * explicit policy mask, checked against actual package ELF requirements. */
FgStatus fg_executor_create(FgProgram *program, const char *function,
                            const FgExecutorOptions *options, FgExecutor **out, char *diagnostic,
                            size_t diagnostic_bytes);
const FgPackageEntry *fg_executor_entry(const FgExecutor *executor);
/* Exact schema frame size required. Frame initialization/argument encoding and
 * synchronization of a shared Host belong to caller. Nonblocking slot admission;
 * all acquired slots are released on success and execution failure. */
FgStatus fg_executor_run(FgExecutor *executor, void *frame, size_t frame_bytes, FgHost *host,
                         FgVmResult *result);
FgStatus fg_executor_close(FgExecutor *executor);
int fg_executor_closed_and_drained(FgExecutor *executor);
/* Caller must externally exclude all API users before destroy. BUSY preserves
 * executor until closed and drained. Final destruction releases program. */
FgStatus fg_executor_destroy(FgExecutor *executor);
#ifdef __cplusplus
}
#endif
#endif
