// C++ model wrapper compatibility; the library itself remains C-only.
#include "fingolfin/route.h"
#include <cassert>
#include <cstddef>
#include <type_traits>
static_assert(std::is_standard_layout<FgLeaseV2>::value, "lease must use C layout");
static_assert(offsetof(FgLeaseV2, generation) == 8, "generation offset");
static int32_t callback(void *context, const FgCallHeader *frame) {
    auto *calls = static_cast<unsigned *>(context);
    assert(frame->config_generation == 1);
    ++*calls;
    return 0;
}
int main() {
    FgPort *port = nullptr;
    FgObjectKey object{1, 2, 3, 4};
    assert(fg_port_create(7, &object, 1, nullptr, &port) == FG_STATUS_OK);
    unsigned calls = 0;
    FgSubscription sub{};
    sub.id = 1;
    sub.context = &calls;
    sub.dispatch = callback;
    assert(fg_port_publish(port, 0, &sub, 1, nullptr) == FG_STATUS_OK);
    auto runtime = fg_port_runtime(port);
    FgLeaseV2 lease{};
    assert(fg_v2_acquire(&runtime, 7, &object, &lease) == 0);
    FgCallHeader header{FG_ABI_VERSION, sizeof(FgCallHeader), 7, lease.generation, object};
    assert(fg_v2_dispatch(&lease, &header) == 0 && calls == 1);
    fg_v2_release(&lease);
    assert(fg_port_close(port) == FG_STATUS_OK);
    assert(fg_port_destroy(port) == FG_STATUS_OK);
}
