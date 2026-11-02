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
enable_language(RC)

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

# switch/: enter/exit, appbar, taskbar helpers, restore (shared by shell and watchdog).
add_library(velora_switch STATIC
  src/switch/taskbar.cpp src/switch/restore.cpp src/switch/appbar.cpp src/switch/switch_manager.cpp)
target_link_libraries(velora_switch PUBLIC velora_core velora_tokens shcore velora_wallpaper)

# wallpaper/: procedural Velora wallpaper per monitor, restore on exit / recovery. Owner B. Defined before
# velora_switch because recovery (switch/restore.cpp) restores the original wallpaper.
add_library(velora_wallpaper STATIC src/wallpaper/wallpaper.cpp)
target_link_libraries(velora_wallpaper PUBLIC velora_core ole32 shell32)
add_executable(wallpaper_test tests/wallpaper_test.cpp)
target_link_libraries(wallpaper_test PRIVATE velora_wallpaper)
add_test(NAME wallpaper_test COMMAND wallpaper_test)

# Binaries. Resident: shell, watchdog. On-demand: files, settings.
function(velora_add_app target source)
  add_executable(${target} WIN32 ${source})
  target_link_libraries(${target} PRIVATE velora_version velora_contracts velora_tokens)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endfunction()

velora_add_app(velora-shell    src/main.cpp)
velora_add_app(velora-watchdog watchdog/main.cpp)
velora_add_app(velora-files    apps/files/main.cpp)
velora_add_app(velora-settings apps/settings/main.cpp)
target_link_libraries(velora-shell    PRIVATE velora_switch velora_menubar velora_windows velora_ui velora_apps velora_dock velora_wallpaper)
target_link_libraries(velora-watchdog PRIVATE velora_switch)
target_sources(velora-shell PRIVATE src/shell.rc)  # the Velora app icon (Dock)

# Test-only builds of the shell and watchdog (VELORA_TESTING compiles the test hooks in:
# hello-rejecting flag file, login-check timing overrides, debug_hang). They are placed in
# build/testbin/<config>/ under the release file names, because the shell starts the
# watchdog by name from its own directory. Release binaries never contain the hooks.
velora_add_app(velora-shell-testing    src/main.cpp)
velora_add_app(velora-watchdog-testing watchdog/main.cpp)
foreach(t velora-shell-testing velora-watchdog-testing)
  target_link_libraries(${t} PRIVATE velora_switch)
  target_compile_definitions(${t} PRIVATE VELORA_TESTING)
endforeach()
target_sources(velora-shell-testing PRIVATE src/shell.rc)
target_link_libraries(velora-shell-testing PRIVATE velora_menubar velora_windows velora_ui velora_apps velora_dock velora_wallpaper)
set_target_properties(velora-shell-testing PROPERTIES OUTPUT_NAME velora-shell RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/testbin")
set_target_properties(velora-watchdog-testing PROPERTIES OUTPUT_NAME velora-watchdog RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/testbin")

# Day-2 safety gate (hermetic: fake taskbar windows, private data dir). See tests/switch_gate_test.cpp.
# Runs against the *-testing binaries.
add_executable(switch_gate_test tests/switch_gate_test.cpp)
target_link_libraries(switch_gate_test PRIVATE velora_switch)
add_dependencies(switch_gate_test velora-shell-testing velora-watchdog-testing)
add_test(NAME switch_gate
  COMMAND switch_gate_test $<TARGET_FILE:velora-shell-testing> $<TARGET_FILE:velora-watchdog-testing> --out ${CMAKE_BINARY_DIR}/switch_gate_results.json)
set_tests_properties(switch_gate PROPERTIES TIMEOUT 900)

# Release binaries must not contain the test hooks; the testing binaries must (so the check can fail).
add_executable(release_hooks_test tests/release_hooks_test.cpp)
add_dependencies(release_hooks_test velora-shell velora-watchdog velora-shell-testing velora-watchdog-testing)
add_test(NAME release_hooks
  COMMAND release_hooks_test
    $<TARGET_FILE:velora-watchdog> $<TARGET_FILE:velora-shell>
    $<TARGET_FILE:velora-watchdog-testing> $<TARGET_FILE:velora-shell-testing>)


# windows/: top-level window tracking (SetWinEventHook, out-of-context).
add_library(velora_windows STATIC src/windows/window_tracker.cpp)
target_link_libraries(velora_windows PUBLIC velora_core dwmapi shell32 user32)
add_executable(window_tracker_test tests/window_tracker_test.cpp)
target_link_libraries(window_tracker_test PRIVATE velora_windows)
add_test(NAME window_tracker_test COMMAND window_tracker_test)

# render/: animation engine (header-only) and the D2D/DirectComposition host.
add_executable(animation_test tests/animation_test.cpp)
target_link_libraries(animation_test PRIVATE velora_tokens)
add_test(NAME animation_test COMMAND animation_test)

add_library(velora_render STATIC src/render/render_host.cpp)
target_link_libraries(velora_render PUBLIC velora_core velora_tokens d3d11 dxgi d2d1 dcomp dwmapi user32)
add_executable(render_host_test tests/render_host_test.cpp)
target_link_libraries(render_host_test PRIVATE velora_render)
add_test(NAME render_host_test COMMAND render_host_test)

# ui/: shared helpers for the shell UI (theme, DirectWrite, icon store). Owner B.
add_library(velora_ui STATIC src/ui/ui_common.cpp)
target_link_libraries(velora_ui PUBLIC velora_render velora_core dwrite version shell32 ole32 windowscodecs)
# The Dock's pure geometry and blur maths (no windows.h), shared with the wallpaper backdrop.
add_library(velora_dock_layout STATIC src/dock/dock_layout.cpp)
target_link_libraries(velora_dock_layout PUBLIC velora_tokens)
# Blurred-wallpaper material for the Dock shelf and the Apps panel.
add_library(velora_backdrop STATIC src/ui/backdrop.cpp)
target_link_libraries(velora_backdrop PUBLIC velora_ui velora_wallpaper velora_dock_layout windowscodecs)
add_executable(backdrop_test tests/backdrop_test.cpp)
target_link_libraries(backdrop_test PRIVATE velora_backdrop)
add_test(NAME backdrop_test COMMAND backdrop_test)
add_executable(velora_icon_test tests/velora_icon_test.cpp src/shell.rc)
target_link_libraries(velora_icon_test PRIVATE velora_ui)
add_test(NAME velora_icon_test COMMAND velora_icon_test)
add_executable(font_test tests/font_test.cpp src/shell.rc)
target_link_libraries(font_test PRIVATE velora_ui)
add_test(NAME font_test COMMAND font_test)

# menubar/: global menu bar drawn into the top appbar. Owner B.
add_library(velora_menubar STATIC src/menubar/menubar.cpp src/menubar/menu_layout.cpp src/menubar/menu_popup.cpp)
target_link_libraries(velora_menubar PUBLIC velora_ui velora_switch velora_windows velora_version powrprof)
add_executable(menubar_test tests/menubar_test.cpp)
target_link_libraries(menubar_test PRIVATE velora_menubar)
add_test(NAME menubar_test COMMAND menubar_test)
add_executable(menu_layout_test tests/menu_layout_test.cpp)
target_link_libraries(menu_layout_test PRIVATE velora_menubar)
add_test(NAME menu_layout_test COMMAND menu_layout_test)

add_executable(debounce_test tests/debounce_test.cpp)
target_link_libraries(debounce_test PRIVATE velora_contracts)
add_test(NAME debounce_test COMMAND debounce_test)

# apps/: installed-app index (AppsFolder + Start-menu shortcuts, cached) and the full-screen grid. Owner B.
add_library(velora_apps STATIC src/apps/app_index.cpp src/apps/apps_grid.cpp)
target_link_libraries(velora_apps PUBLIC velora_ui velora_backdrop velora_core shell32 ole32 propsys)
add_executable(apps_test tests/apps_test.cpp)
target_link_libraries(apps_test PRIVATE velora_apps)
add_test(NAME apps_test COMMAND apps_test)
add_executable(icons_test tests/icons_test.cpp)
target_link_libraries(icons_test PRIVATE velora_apps)
add_test(NAME icons_test COMMAND icons_test)

# dock/: the Dock pill in the bottom appbar. Owner B.
add_library(velora_dock STATIC src/dock/dock.cpp src/dock/dock_model.cpp)
target_link_libraries(velora_dock PUBLIC velora_apps velora_backdrop velora_dock_layout velora_ui velora_switch velora_windows velora_core velora_wallpaper windowscodecs)
add_executable(dock_test tests/dock_test.cpp)
target_link_libraries(dock_test PRIVATE velora_dock)
add_test(NAME dock_test COMMAND dock_test)
