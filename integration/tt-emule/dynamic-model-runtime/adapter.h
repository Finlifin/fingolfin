#ifndef FG_DYNAMIC_ADAPTER_H
#define FG_DYNAMIC_ADAPTER_H
#include "fingolfin/facts.h"
#include "fingolfin/mount.h"
typedef struct FgModelEvent {
    uint64_t sequence, a, b, c, device;
    uint32_t hook, phase, x, y, thread;
} FgModelEvent;
typedef struct FgModelAdapter {
    size_t frame_bytes;
    FgBindingEncode encode;
    int (*valid)(const FgModelEvent *);
    void (*fill)(void *, const FgCallHeader *, const FgModelEvent *);
    size_t (*expected)(FgRecord *, const FgModelEvent *);
    int (*matched)(const FgModelEvent *);
} FgModelAdapter;
#define FG_TT_POLICY_MAX_READS 64u
struct FgTtProperties;
FgStatus fg_tt_policy(struct FgTtProperties *, FgStateKey, FgFactValue *, FgBindingGate *, int *);
extern const FgModelAdapter *fg_model_adapters[40];
#endif
