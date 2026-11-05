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
  src/core/state_store.cpp src/core/ipc_pipe.cpp src/core/settings_file.cpp)
target_include_directories(velora_core PUBLIC src)
target_compile_definitions(velora_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(velora_core PUBLIC velora_contracts advapi32 shell32 ole32 user32)

add_executable(core_test tests/core_test.cpp)
target_link_libraries(core_test PRIVATE velora_core)
add_test(NAME core_test COMMAND core_test)

# switch/: enter/exit, appbar, taskbar helpers, restore (shared by shell and watchdog).
add_library(velora_switch STATIC
  src/switch/taskbar.cpp src/switch/restore.cpp src/switch/appbar.cpp src/switch/switch_manager.cpp)
target_link_libraries(velora_switch PUBLIC velora_core velora_tokens shcore velora_wallpaper velora_theming)

# theming/: window theming through documented DWM attributes (no injection), restored on exit and by recovery. Owner: B.
# Defined before velora_switch because recovery (switch/restore.cpp) restores themed windows from the backup file.
add_library(velora_theming STATIC src/theming/theme_policy.cpp src/theming/window_theme.cpp)
target_link_libraries(velora_theming PUBLIC velora_core dwmapi)
add_executable(theming_test tests/theming_test.cpp)
target_link_libraries(theming_test PRIVATE velora_theming)
add_test(NAME theming_test COMMAND theming_test)

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
target_link_libraries(velora-shell    PRIVATE velora_switch velora_menubar velora_windows velora_ui velora_apps velora_dock velora_wallpaper velora_theming velora_overview)
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
target_link_libraries(velora-shell-testing PRIVATE velora_menubar velora_windows velora_ui velora_apps velora_dock velora_wallpaper velora_overview)
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
# Velora's own app icons: assets/icons/*.svg are embedded as string literals at configure time.
file(GLOB VELORA_SVGS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/icons/*.svg)
set(VELORA_SVG_CPP "namespace velora::ui {\n")
foreach(svg ${VELORA_SVGS})
  get_filename_component(svg_name ${svg} NAME_WE)
  string(REPLACE "-" "_" svg_id ${svg_name})
  file(READ ${svg} svg_text)
  string(APPEND VELORA_SVG_CPP "extern const char* const kSvg_${svg_id} = R\"SVG(${svg_text})SVG\";\n")
endforeach()
string(APPEND VELORA_SVG_CPP "}  // namespace velora::ui\n")
file(WRITE ${CMAKE_BINARY_DIR}/generated/builtin_icons_data.cpp "${VELORA_SVG_CPP}")
add_library(velora_ui STATIC src/ui/ui_common.cpp src/ui/window_chrome.cpp src/ui/icon_tile.cpp src/ui/builtin_icons.cpp
            ${CMAKE_BINARY_DIR}/generated/builtin_icons_data.cpp)
target_link_libraries(velora_ui PUBLIC velora_render velora_core dwrite version shell32 ole32 windowscodecs psapi d3d11 dxgi d2d1 shlwapi)
add_executable(chrome_test tests/chrome_test.cpp)
target_link_libraries(chrome_test PRIVATE velora_ui)
add_test(NAME chrome_test COMMAND chrome_test)
# control/: system controls behind Control Center (documented APIs only). Owner: C.
add_library(velora_control_core STATIC src/control/audio.cpp src/control/wifi.cpp src/control/radios.cpp src/control/brightness.cpp
            src/control/personalize.cpp)
target_link_libraries(velora_control_core PUBLIC velora_core ole32 oleaut32 uuid wlanapi dxva2 wbemuuid windowsapp)
add_library(velora_control STATIC src/control/control_center.cpp)
target_link_libraries(velora_control PUBLIC velora_control_core velora_ui velora_render velora_backdrop)
add_executable(control_center_window_test tests/control_center_window_test.cpp)
target_link_libraries(control_center_window_test PRIVATE velora_control)
add_test(NAME control_center_window_test COMMAND control_center_window_test)
add_executable(control_test tests/control_test.cpp)
target_link_libraries(control_test PRIVATE velora_control_core)
add_test(NAME control_test COMMAND control_test)
# overview/: Mission Control-style overview (DWM thumbnails). Layout and hot corner are pure (overview_test).
add_library(velora_overview STATIC src/overview/overview.cpp)
target_link_libraries(velora_overview PUBLIC velora_ui velora_render velora_windows velora_backdrop dwmapi)
add_executable(overview_window_test tests/overview_window_test.cpp)
target_link_libraries(overview_window_test PRIVATE velora_overview)
add_test(NAME overview_window_test COMMAND overview_window_test)
add_executable(overview_test tests/overview_test.cpp)
target_include_directories(overview_test PRIVATE src)
add_test(NAME overview_test COMMAND overview_test)
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
target_link_libraries(velora_menubar PUBLIC velora_ui velora_backdrop velora_switch velora_windows velora_version powrprof velora_appmenu)
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
add_library(velora_apps STATIC src/apps/app_index.cpp src/apps/apps_grid.cpp src/apps/files_launch.cpp)
target_link_libraries(velora_apps PUBLIC velora_ui velora_backdrop velora_core shell32 ole32 propsys)
add_executable(apps_test tests/apps_test.cpp)
target_link_libraries(apps_test PRIVATE velora_apps)
add_test(NAME apps_test COMMAND apps_test)
add_executable(icon_tile_test tests/icon_tile_test.cpp)
target_link_libraries(icon_tile_test PRIVATE velora_ui)
add_test(NAME icon_tile_test COMMAND icon_tile_test)
add_executable(icons_test tests/icons_test.cpp)
target_link_libraries(icons_test PRIVATE velora_apps)
add_test(NAME icons_test COMMAND icons_test)

# dock/: the Dock pill in the bottom appbar. Owner B.
add_library(velora_dock STATIC src/dock/dock.cpp src/dock/dock_model.cpp)
target_link_libraries(velora_dock PUBLIC velora_apps velora_backdrop velora_dock_layout velora_ui velora_switch velora_windows velora_core velora_wallpaper windowscodecs)
add_executable(dock_test tests/dock_test.cpp)
target_link_libraries(dock_test PRIVATE velora_dock)
add_test(NAME dock_test COMMAND dock_test)
add_executable(builtin_icons_test tests/builtin_icons_test.cpp)
target_link_libraries(builtin_icons_test PRIVATE velora_ui)
add_test(NAME builtin_icons_test COMMAND builtin_icons_test)

# files/: model and file operations of velora-files (IFileOperation, Recycle Bin, shell data objects). Owner: C.
add_library(velora_files_core STATIC apps/files/file_ops.cpp apps/files/fs_model.cpp)
target_include_directories(velora_files_core PUBLIC apps)
target_link_libraries(velora_files_core PUBLIC velora_core shell32 ole32 shlwapi bcrypt)
add_executable(files_test tests/files_test.cpp)
target_link_libraries(files_test PRIVATE velora_files_core)
add_test(NAME files_test COMMAND files_test)
set_tests_properties(files_test PROPERTIES TIMEOUT 300)

# The Files app itself (window, drawing, input). Needs the UI layer (Direct2D host, fonts, icon decoding) and the menu popup.
add_library(velora_files_ui STATIC apps/files/files_window.cpp)
target_link_libraries(velora_files_ui PUBLIC velora_files_core velora_ui velora_render velora_menubar dwmapi)
add_executable(files_layout_test tests/files_layout_test.cpp)
target_include_directories(files_layout_test PRIVATE apps)
target_link_libraries(files_layout_test PRIVATE velora_tokens)
add_test(NAME files_layout_test COMMAND files_layout_test)
target_sources(velora-files PRIVATE apps/files/files.rc apps/files/files.manifest)
target_link_libraries(velora-files PRIVATE velora_files_ui)

# settings/: the Settings app (store + pages are pure and unit-tested; the window is tested in CI). Owner: C.
add_library(velora_settings_core STATIC apps/settings/settings_model.cpp)
target_include_directories(velora_settings_core PUBLIC apps)
target_link_libraries(velora_settings_core PUBLIC velora_core)
add_executable(settings_model_test tests/settings_model_test.cpp)
target_link_libraries(settings_model_test PRIVATE velora_settings_core velora_theming)
add_test(NAME settings_model_test COMMAND settings_model_test)
add_library(velora_settings_ui STATIC apps/settings/settings_window.cpp)
target_link_libraries(velora_settings_ui PUBLIC velora_settings_core velora_ui velora_render velora_menubar dwmapi)
target_sources(velora-settings PRIVATE apps/settings/settings.rc apps/settings/settings.manifest)
target_link_libraries(velora-settings PRIVATE velora_settings_ui)
add_executable(settings_window_test tests/settings_window_test.cpp)
target_link_libraries(settings_window_test PRIVATE velora_settings_ui)
add_test(NAME settings_window_test COMMAND settings_window_test)
add_executable(files_window_test tests/files_window_test.cpp)
target_link_libraries(files_window_test PRIVATE velora_files_ui)
add_test(NAME files_window_test COMMAND files_window_test)
set_tests_properties(files_window_test PROPERTIES TIMEOUT 180)

# search/: Spotlight-style panel, Windows Search (OLE DB) files, hotkey. Owner: C.
add_library(velora_search_core STATIC src/search/search_core.cpp)
target_link_libraries(velora_search_core PUBLIC velora_tokens)
target_include_directories(velora_search_core PUBLIC src)
add_library(velora_search STATIC src/search/win_search.cpp src/search/search_panel.cpp src/search/search_hotkey.cpp)
target_link_libraries(velora_search PUBLIC velora_search_core velora_apps velora_backdrop velora_ui velora_render velora_core oledb advapi32)
add_executable(search_core_test tests/search_core_test.cpp)
target_link_libraries(search_core_test PRIVATE velora_search_core)
add_test(NAME search_core_test COMMAND search_core_test)
add_executable(search_test tests/search_test.cpp)
target_link_libraries(search_test PRIVATE velora_search)
add_test(NAME search_test COMMAND search_test)
set_tests_properties(search_test PROPERTIES TIMEOUT 180)
target_link_libraries(velora-shell PRIVATE velora_search)
target_link_libraries(velora-shell-testing PRIVATE velora_search)

# menubar/: global app menus (tier 1 HMENU, tier 2 UI Automation). Owner: B (with C on Day 4).
add_library(velora_appmenu STATIC src/menubar/app_menu.cpp src/menubar/app_menu_provider.cpp)
target_link_libraries(velora_appmenu PUBLIC velora_core oleaut32 ole32 uiautomationcore)
target_include_directories(velora_appmenu PUBLIC src)
add_executable(menu_fixture WIN32 tests/fixtures/menu_fixture.cpp)
target_include_directories(menu_fixture PRIVATE src)
target_compile_definitions(menu_fixture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_executable(app_menu_test tests/app_menu_test.cpp)
target_link_libraries(app_menu_test PRIVATE velora_appmenu)
add_dependencies(app_menu_test menu_fixture)
add_test(NAME app_menu_test COMMAND app_menu_test $<TARGET_FILE:menu_fixture>)
set_tests_properties(app_menu_test PROPERTIES TIMEOUT 400)
add_executable(menubar_appmenu_test tests/menubar_appmenu_test.cpp)
target_link_libraries(menubar_appmenu_test PRIVATE velora_menubar)
add_dependencies(menubar_appmenu_test menu_fixture)
add_test(NAME menubar_appmenu_test COMMAND menubar_appmenu_test $<TARGET_FILE:menu_fixture>)
set_tests_properties(menubar_appmenu_test PROPERTIES TIMEOUT 120)
