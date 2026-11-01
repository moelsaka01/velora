cmake_minimum_required(VERSION 3.25)

# vcpkg.json is the single source of truth for the version.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" VELORA_VCPKG_JSON)
string(JSON VELORA_VERSION GET "${VELORA_VCPKG_JSON}" "version")
if(NOT VELORA_VERSION MATCHES "^([0-9]+)[.]([0-9]+)[.]([0-9]+)$")
  message(FATAL_ERROR "vcpkg.json version '${VELORA_VERSION}' must be MAJOR.MINOR.PATCH")
endif()
set(VELORA_VERSION_MAJOR ${CMAKE_MATCH_1})
set(VELORA_VERSION_MINOR ${CMAKE_MATCH_2})
set(VELORA_VERSION_PATCH ${CMAKE_MATCH_3})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")

project(Velora VERSION ${VELORA_VERSION} LANGUAGES CXX)

configure_file(src/core/version.h.in "${CMAKE_BINARY_DIR}/generated/core/version.h" @ONLY)
add_library(velora_version INTERFACE)
target_include_directories(velora_version INTERFACE "${CMAKE_BINARY_DIR}/generated")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
endif()

enable_testing()

add_executable(smoke_test tests/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE velora_version)
add_test(NAME smoke_test COMMAND smoke_test)

# Shared contracts (header-only) and their wire schema.
file(READ ipc/v1.json VELORA_IPC_JSON)
string(JSON VELORA_IPC_VERSION GET "${VELORA_IPC_JSON}" "version")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ipc/v1.json")
add_library(velora_contracts INTERFACE)
target_include_directories(velora_contracts INTERFACE src)

add_executable(contracts_test tests/contracts_test.cpp)
target_link_libraries(contracts_test PRIVATE velora_contracts)
target_compile_definitions(contracts_test PRIVATE VELORA_IPC_JSON_VERSION=${VELORA_IPC_VERSION})
add_test(NAME contracts_test COMMAND contracts_test)

# Design tokens (header-only).
add_library(velora_tokens INTERFACE)
target_include_directories(velora_tokens INTERFACE src)
add_executable(tokens_test tests/tokens_test.cpp)
target_link_libraries(tokens_test PRIVATE velora_tokens)
add_test(NAME tokens_test COMMAND tokens_test)

# core/: JSON, state store, IPC pipes, logging, runtime paths (Windows-only static lib).
add_library(velora_core STATIC
  src/core/json.cpp src/core/text.cpp src/core/runtime.cpp src/core/log.cpp
  src/core/state_store.cpp src/core/ipc_pipe.cpp)
target_include_directories(velora_core PUBLIC src)
target_compile_definitions(velora_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(velora_core PUBLIC velora_contracts advapi32 shell32 ole32 user32)

add_executable(core_test tests/core_test.cpp)
target_link_libraries(core_test PRIVATE velora_core)
add_test(NAME core_test COMMAND core_test)

# Binaries (empty skeletons). Resident: shell, watchdog. On-demand: files, settings.
function(velora_add_app target source)
  add_executable(${target} WIN32 ${source})
  target_link_libraries(${target} PRIVATE velora_version velora_contracts velora_tokens)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endfunction()

velora_add_app(velora-shell    src/main.cpp)
velora_add_app(velora-watchdog watchdog/main.cpp)
velora_add_app(velora-files    apps/files/main.cpp)
velora_add_app(velora-settings apps/settings/main.cpp)
