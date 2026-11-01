TEST_F(MeshDeviceFixture, Fingolfin_MulticastTwoReceivers_Observed) {
    auto *device = this->devices_.at(0)->get_devices()[0];
    CoreRange cores({0, 0}, {2, 0});
    Program program = CreateProgram();
    CreateCircularBuffer(
        program, cores,
        CircularBufferConfig(2048, {{0, tt::DataFormat::Float16_b}}).set_page_size(0, 2048));
    uint32_t ready = CreateSemaphore(program, cores, 0);
    uint32_t done = CreateSemaphore(program, cores, 0);
    auto sender = device->worker_core_from_logical_core({0, 0});
    auto first = device->worker_core_from_logical_core({1, 0});
    auto last = device->worker_core_from_logical_core({2, 0});
    std::string kernel = R"(
        #include "api/dataflow/dataflow_api.h"
        void kernel_main() {
            const uint32_t role = get_compile_time_arg_val(0);
            uint32_t ready_addr = get_semaphore(get_compile_time_arg_val(1));
            uint32_t done_addr = get_semaphore(get_compile_time_arg_val(2));
            auto* ready = reinterpret_cast<volatile tt_l1_ptr uint32_t*>(ready_addr);
            auto* done = reinterpret_cast<volatile tt_l1_ptr uint32_t*>(done_addr);
            const uint32_t sx=get_compile_time_arg_val(3), sy=get_compile_time_arg_val(4);
            const uint32_t x1=get_compile_time_arg_val(5), y1=get_compile_time_arg_val(6);
            const uint32_t x2=get_compile_time_arg_val(7), y2=get_compile_time_arg_val(8);
            cb_reserve_back(0, uint32_t(1));
            uint32_t base = get_write_ptr(0);
            auto* words = reinterpret_cast<volatile uint32_t*>(base);
            for (uint32_t i=0; i<512; i++) words[i] = role==0 ? 0x12340000+i : 0;
            if (role==0) {
                noc_semaphore_wait(ready, 2);
                noc_async_write_multicast(base, get_noc_multicast_addr(x1,y1,x2,y2,base), 2048, 2);
                noc_async_write_barrier();
                noc_semaphore_inc(get_noc_addr(x1,y1,done_addr), 1);
                noc_semaphore_inc(get_noc_addr(x2,y2,done_addr), 1);
                noc_async_atomic_barrier();
                // Wait for both consumers to verify their own L1, not just send completion.
                noc_semaphore_wait(ready, 4);
            } else {
                noc_semaphore_inc(get_noc_addr(sx,sy,ready_addr), 1);
                noc_async_atomic_barrier();
                noc_semaphore_wait(done, 1);
                for (uint32_t i=0; i<512; i++)
                    if (words[i] != 0x12340000+i) __builtin_trap();
                noc_semaphore_inc(get_noc_addr(sx,sy,ready_addr), 1);
                noc_async_atomic_barrier();
            }
            cb_push_back(0, uint32_t(1)); cb_wait_front(0, uint32_t(1)); cb_pop_front(0, uint32_t(1));
        }
    )";
    for (uint32_t role = 0; role < 3; role++) {
        CreateKernelFromString(
            program, kernel, CoreCoord{role, 0},
            DataMovementConfig{.processor = DataMovementProcessor::RISCV_0,
                               .noc = NOC::RISCV_0_default,
                               .compile_args = {role, ready, done, sender.x, sender.y, first.x,
                                                first.y, last.x, last.y}});
    }
    detail::LaunchProgram(device, program);
    SUCCEED(); // Both receiving cores checked every word before acknowledging.
}
