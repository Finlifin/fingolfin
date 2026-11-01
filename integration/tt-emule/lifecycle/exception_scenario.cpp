// Appended inside namespace tt::tt_metal in the pinned emule API test source.
TEST_F(MeshDeviceFixture, Fingolfin_LaunchExceptionRecovery_Observed) {
    auto *device = this->devices_.at(0)->get_devices()[0];
    CoreCoord core = {0, 0};
    Program program = CreateProgram();
    CircularBufferConfig config =
        CircularBufferConfig(3072, {{0, tt::DataFormat::Float16_b}}).set_page_size(0, 1024);
    CreateCircularBuffer(program, core, config);
    uint32_t semaphore = CreateSemaphore(program, core, 0);
    std::string source = R"(
        #include "api/dataflow/dataflow_api.h"
        #include <stdexcept>
        void kernel_main() {
            cb_reserve_back(0, uint32_t(3));
            uint32_t base = get_write_ptr(0);
            auto* first = reinterpret_cast<volatile uint32_t*>(base);
            for (uint32_t i = 0; i < 4; i++) first[i] = 42 + i;
            noc_async_read(get_noc_addr(base), base + 1024, 16);
            noc_async_read_barrier();
            noc_async_write(base + 1024, get_noc_addr(base + 2048), 16);
            noc_async_write_barrier();
            noc_async_writes_flushed();
            auto* last = reinterpret_cast<volatile uint32_t*>(base + 2048);
            for (uint32_t i = 0; i < 4; i++) if (last[i] != 42 + i) __builtin_trap();
            noc_async_read_one_packet(get_noc_addr(base), base + 1024, 16);
            noc_async_read_barrier();
            noc_async_write_one_packet(base + 1024, get_noc_addr(base + 2048), 16);
            noc_async_write_barrier();
            noc_inline_dw_write(get_noc_addr(base + 2048), 7);
            noc_async_write_barrier();
            if (last[0] != 7) __builtin_trap();
            uint32_t sem_address = get_semaphore(get_compile_time_arg_val(0));
            auto* sem = reinterpret_cast<volatile tt_l1_ptr uint32_t*>(sem_address);
            noc_semaphore_set(sem, 0);
            noc_semaphore_inc(get_noc_addr(sem_address), 1);
            noc_async_atomic_barrier();
            noc_semaphore_wait(sem, 1);
            noc_semaphore_wait_min(sem, 1);
            if (*sem != 1) __builtin_trap();
            cb_push_back(0, uint32_t(3));
            cb_wait_front(0, uint32_t(3));
            auto* queued = reinterpret_cast<volatile uint32_t*>(get_read_ptr(0));
            if (queued[0] != 42) __builtin_trap();
            cb_pop_front(0, uint32_t(3));
            if (get_arg_val<uint32_t>(0)) throw std::runtime_error("Fingolfin expected launch failure");
        }
    )";
    auto kernel =
        CreateKernelFromString(program, source, core,
                               DataMovementConfig{.processor = DataMovementProcessor::RISCV_0,
                                                  .noc = NOC::RISCV_0_default,
                                                  .compile_args = {semaphore}});
    SetRuntimeArgs(program, kernel, core, {1});
    EXPECT_THROW(detail::LaunchProgram(device, program), std::exception);
    SetRuntimeArgs(program, kernel, core, {0});
    detail::LaunchProgram(device, program);
    SUCCEED(); // Kernel checks copied words, inline write and semaphore value.
}
