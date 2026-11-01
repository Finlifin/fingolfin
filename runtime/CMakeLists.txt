cmake_minimum_required(VERSION 3.20)
project(
  fingolfin_runtime
  VERSION 0.1.0
  LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(FG_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
add_library(
  fingolfin STATIC
  src/objects.c
  src/util.c
  src/containers.c
  src/state.c
  src/host.c
  src/route.c
  src/bytes.c
  src/elf.c
  src/schema.c
  src/gate.c
  src/facts.c)
find_package(Threads REQUIRED)
target_link_libraries(fingolfin PUBLIC Threads::Threads)
target_include_directories(fingolfin PUBLIC include ../sdk/include)
target_compile_options(fingolfin PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(FG_SANITIZE)
  target_compile_options(fingolfin PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(fingolfin PUBLIC -fsanitize=address,undefined)
endif()
include(CTest)
if(BUILD_TESTING)
  add_executable(objects_test tests/objects.c)
  target_link_libraries(objects_test PRIVATE fingolfin)
  target_compile_options(objects_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME objects COMMAND objects_test)
  add_executable(facts_test tests/facts.c)
  target_link_libraries(facts_test PRIVATE fingolfin)
  target_compile_options(facts_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME facts COMMAND facts_test)
  add_executable(gate_test tests/gate.c)
  target_link_libraries(gate_test PRIVATE fingolfin)
  target_compile_options(gate_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME gate COMMAND gate_test)
  add_executable(containers_test tests/containers.c)
  target_link_libraries(containers_test PRIVATE fingolfin)
  target_compile_options(containers_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME containers COMMAND containers_test)
  add_executable(host_test tests/host.c)
  target_link_libraries(host_test PRIVATE fingolfin)
  target_compile_options(host_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME host COMMAND host_test)
  add_executable(quiescence_test tests/quiescence.c)
  target_link_libraries(quiescence_test PRIVATE fingolfin)
  target_compile_options(quiescence_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME quiescence COMMAND quiescence_test)
  add_executable(route_test tests/route.c)
  target_link_libraries(route_test PRIVATE fingolfin)
  target_compile_options(route_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME route COMMAND route_test)
  add_executable(elf_test tests/elf.c)
  target_link_libraries(elf_test PRIVATE fingolfin)
  target_compile_options(elf_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME elf COMMAND elf_test)
  add_executable(schema_test tests/schema.c)
  target_link_libraries(schema_test PRIVATE fingolfin)
  target_compile_options(schema_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_test(NAME schema COMMAND schema_test)
  set(FG_SCHEMA_CORPUS
      ""
      CACHE FILEPATH "Optional Rust/C agreement corpus")
  if(FG_SCHEMA_CORPUS)
    add_test(NAME schema_agreement COMMAND schema_test "${FG_SCHEMA_CORPUS}")
  endif()
endif()

option(FG_WITH_UBPF "Build pinned safe interpreter support" OFF)
option(FG_UBPF_SCRATCH "Use patched preallocated safe interpreter scratch" OFF)
if(FG_UBPF_SCRATCH AND NOT FG_WITH_UBPF)
  message(FATAL_ERROR "FG_UBPF_SCRATCH requires FG_WITH_UBPF")
endif()
set(FG_UBPF_ROOT
    ""
    CACHE PATH "Pinned uBPF checkout")
set(FG_UBPF_BUILD
    ""
    CACHE PATH "uBPF build containing lib/libubpf.a")
set(FG_TEST_ELF_DIR
    ""
    CACHE PATH "Generated record.o and aggregate.o test artifacts")
if(FG_WITH_UBPF)
  execute_process(
    COMMAND git -C "${FG_UBPF_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE UBPF_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE UBPF_GIT_STATUS)
  if(NOT UBPF_GIT_STATUS EQUAL 0 OR NOT UBPF_REV STREQUAL
                                    "8d2dd3580ab80f9b4a79a9d2fa2420aa38fc7521")
    message(FATAL_ERROR "uBPF checkout must match pinned revision")
  endif()
  if(NOT EXISTS "${FG_UBPF_BUILD}/lib/libubpf.a")
    message(FATAL_ERROR "Build pinned uBPF before enabling FG_WITH_UBPF")
  endif()
  target_sources(fingolfin PRIVATE src/vm.c src/pool.c)
  if(FG_UBPF_SCRATCH)
    target_compile_definitions(fingolfin PRIVATE FG_UBPF_SCRATCH=1)
  endif()
  target_include_directories(fingolfin PRIVATE "${FG_UBPF_ROOT}/vm/inc" "${FG_UBPF_BUILD}/vm")
  target_link_libraries(fingolfin PUBLIC "${FG_UBPF_BUILD}/lib/libubpf.a" m)
  if(BUILD_TESTING)
    if(NOT EXISTS "${FG_TEST_ELF_DIR}/record.o"
       OR NOT EXISTS "${FG_TEST_ELF_DIR}/aggregate.o"
       OR NOT EXISTS "${FG_TEST_ELF_DIR}/record.schema")
      message(
        FATAL_ERROR "FG_TEST_ELF_DIR must contain generated record.o, aggregate.o and record.schema"
      )
    endif()
    if(FG_UBPF_SCRATCH)
      add_executable(scratch_test tests/scratch.c)
      target_include_directories(scratch_test PRIVATE "${FG_UBPF_ROOT}/vm/inc" "${FG_UBPF_ROOT}/vm"
                                                      "${FG_UBPF_BUILD}/vm")
      target_link_libraries(scratch_test PRIVATE fingolfin)
      target_compile_options(scratch_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
      add_test(NAME scratch COMMAND scratch_test)
    endif()
    if(FG_UBPF_SCRATCH AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
      add_executable(vm_alloc_test tests/vm_alloc.c)
      target_link_libraries(vm_alloc_test PRIVATE fingolfin)
      target_compile_options(vm_alloc_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
      target_link_options(
        vm_alloc_test
        PRIVATE
        -Wl,--wrap=malloc
        -Wl,--wrap=calloc
        -Wl,--wrap=realloc
        -Wl,--wrap=aligned_alloc
        -Wl,--wrap=free)
      add_test(NAME vm_alloc COMMAND vm_alloc_test "${FG_TEST_ELF_DIR}/record.o")
    endif()
    add_executable(pool_test tests/pool.c)
    target_link_libraries(pool_test PRIVATE fingolfin)
    target_compile_options(pool_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME pool COMMAND pool_test "${FG_TEST_ELF_DIR}/record.o")
    add_executable(vm_test tests/vm.c)
    target_link_libraries(vm_test PRIVATE fingolfin)
    target_compile_options(vm_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME vm COMMAND vm_test "${FG_TEST_ELF_DIR}/record.o" "${FG_TEST_ELF_DIR}/aggregate.o"
                             "${FG_TEST_ELF_DIR}/record.schema")
  endif()
endif()

option(FG_WITH_PACKAGE "Build v3 package loader (json-c and OpenSSL)" OFF)
if(FG_WITH_PACKAGE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c>=0.15)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  target_sources(fingolfin PRIVATE src/hash.c src/package.c)
  target_link_libraries(fingolfin PUBLIC PkgConfig::JSONC OpenSSL::Crypto)
  add_executable(fgctl ../cli/fgctl.c ../cli/session.c)
  target_link_libraries(fgctl PRIVATE fingolfin)
  target_compile_options(fgctl PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(BUILD_TESTING)
    set(FG_TEST_PACKAGE
        ""
        CACHE FILEPATH "Generated v3 record.fgo fixture")
    set(FG_PACKAGE_CORPUS
        ""
        CACHE FILEPATH "Optional package validation corpus")
    if(NOT EXISTS "${FG_TEST_PACKAGE}")
      message(FATAL_ERROR "FG_TEST_PACKAGE must point to generated v3 record.fgo")
    endif()
    add_executable(package_test tests/package.c)
    target_link_libraries(package_test PRIVATE fingolfin)
    target_compile_options(package_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    if(FG_WITH_UBPF)
      target_compile_definitions(package_test PRIVATE FG_TEST_VM=1)
    endif()
    add_test(NAME package COMMAND package_test "${FG_TEST_PACKAGE}")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME cli_inspect
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../cli/test_inspect.py"
                     "$<TARGET_FILE:fgctl>" "${FG_TEST_PACKAGE}")
    if(FG_PACKAGE_CORPUS)
      add_test(NAME package_corpus COMMAND package_test "${FG_TEST_PACKAGE}" "${FG_PACKAGE_CORPUS}")
    endif()
  endif()
endif()

if(FG_WITH_PACKAGE AND FG_WITH_UBPF)
  target_sources(fingolfin PRIVATE src/program.c src/attachment.c src/binding.c)
  add_library(fingolfin_agent STATIC ../agent/src/session.c ../agent/src/mount.c)
  target_include_directories(fingolfin_agent PUBLIC ../agent/include)
  target_link_libraries(fingolfin_agent PUBLIC fingolfin)
  target_compile_options(fingolfin_agent PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(fg-agent ../agent/src/main.c)
  target_link_libraries(fg-agent PRIVATE fingolfin_agent)
  target_compile_options(fg-agent PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(BUILD_TESTING)
    add_executable(session_test ../agent/tests/session.c)
    target_link_libraries(session_test PRIVATE fingolfin_agent)
    target_compile_options(session_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME session COMMAND session_test "${FG_TEST_PACKAGE}")
    add_test(NAME agent_stdio
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../agent/tests/stdio.py"
                     "$<TARGET_FILE:fg-agent>" "${FG_TEST_PACKAGE}")
    add_test(NAME cli_session
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../cli/test_session.py"
                     "$<TARGET_FILE:fgctl>" "$<TARGET_FILE:fg-agent>" "${FG_TEST_PACKAGE}")
    set(FG_TEST_ADAPTER_DIR
        ""
        CACHE PATH "Optional generated record.adapter.h directory")
    if(FG_TEST_ADAPTER_DIR)
      add_executable(mount_test ../agent/tests/mount.c)
      target_include_directories(mount_test PRIVATE "${FG_TEST_ADAPTER_DIR}")
      target_link_libraries(mount_test PRIVATE fingolfin_agent)
      target_compile_options(mount_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
      add_test(NAME mount COMMAND mount_test "${FG_TEST_PACKAGE}")
      add_executable(gated_binding_test tests/gated_binding.c)
      target_include_directories(gated_binding_test PRIVATE "${FG_TEST_ADAPTER_DIR}")
      target_link_libraries(gated_binding_test PRIVATE fingolfin)
      target_compile_options(gated_binding_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
      add_test(NAME gated_binding COMMAND gated_binding_test "${FG_TEST_PACKAGE}")
      add_executable(generated_adapter_test tests/generated_adapter.c)
      target_include_directories(generated_adapter_test PRIVATE "${FG_TEST_ADAPTER_DIR}")
      target_link_libraries(generated_adapter_test PRIVATE fingolfin)
      target_compile_options(generated_adapter_test PRIVATE -Wall -Wextra -Wpedantic -Werror
                                                            -UNDEBUG)
      add_test(NAME generated_adapter COMMAND generated_adapter_test "${FG_TEST_PACKAGE}")
    endif()
    add_executable(binding_test tests/binding.c)
    target_link_libraries(binding_test PRIVATE fingolfin)
    target_compile_options(binding_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME binding COMMAND binding_test "${FG_TEST_PACKAGE}")
    add_executable(attachment_test tests/attachment.c)
    target_link_libraries(attachment_test PRIVATE fingolfin)
    target_link_options(attachment_test PRIVATE -Wl,--wrap=fg_executor_run)
    target_compile_options(attachment_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME attachment COMMAND attachment_test "${FG_TEST_PACKAGE}")
    add_executable(program_test tests/program.c)
    target_link_libraries(program_test PRIVATE fingolfin)
    target_compile_options(program_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME program COMMAND program_test "${FG_TEST_PACKAGE}")
  endif()
endif()
