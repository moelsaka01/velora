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
