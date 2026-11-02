# RoundEndThings — Plugify port of the Metamod plugin RoundEndThings (ABKAM): bhop + speed/gravity
# boost at round end, with per-player opt-outs stored in the official permissions cookies.
#
# Pure s2sdk (docs/PLUGIN_STANDARD.md §7): no pisexcompat, no guard library, no wrapper library and
# the optional hudmenu API dependency (the player's own !menu choice, not a wrapper library). The map
# gate (PITFALLS B1/B2), the slot/controller/handle guards (C2/C1),
# the map-gated raw schema offsets (C6/C8), the update-driven cadence (deliberately no s2sdk timer,
# see src/plugin.cpp), the command ownership (C11), the event hooks and their symmetric teardown
# (C12/A7), the KV1 reader and the menu lifetime all live in this plugin's own sources.
#
#   plugify-RoundEndThings  the plugin shared library (entry "bin/RoundEndThings")
#   roundend_core           the engine-independent settings/menu/cookie helpers (+ its tests)
#   roundend_plugin_tests   the plugin TU with recording __s2sdk_*/__permissions_* fakes: round
#                           events, map gate, schema writes, commands, permissions cookies, slot
#                           reuse, absent bindings, menu lifetime and `plg reload` teardown
#
# The bindings the plugify module fills (__s2sdk_*, __permissions_*) have to be compiled into every
# plugin. The root CMakeLists builds them as the object libraries `s2sdk_bindings` /
# `permissions_bindings`; the fallbacks below (named differently on purpose, so they can never
# collide with the root targets) also make this directory configurable on its own.

set(ROUNDENDTHINGS_VERSION "1.1.0")

# A whole-tree configure inherits the root project's C++23/PIC settings before this directory is
# added; a configure of this directory alone (an isolated build) has to establish them itself.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(NOT CMAKE_CXX_STANDARD)
        set(CMAKE_CXX_STANDARD 23)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)
    endif()
    if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    endif()
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
endif()

# --- SDK paths ------------------------------------------------------------------
# Whole-tree configure: <repo>/sdk/include. Standalone configure of this directory only:
# <repo>/plugins/RoundEndThings/../../sdk/include.
if(EXISTS "${CMAKE_SOURCE_DIR}/sdk/include/s2sdk/s2sdk.cpp")
    set(ROUNDEND_SDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/sdk/include")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include/s2sdk/s2sdk.cpp")
    get_filename_component(ROUNDEND_SDK_INCLUDE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include" ABSOLUTE)
else()
    message(FATAL_ERROR "RoundEndThings needs the generated s2sdk bindings (sdk/include/s2sdk/s2sdk.cpp)")
endif()

if(TARGET plugify_sdk)
    set(ROUNDEND_PLUGIFY_SDK plugify_sdk)
else()
    add_library(roundendthings_plugify_sdk INTERFACE)
    target_include_directories(roundendthings_plugify_sdk INTERFACE "${ROUNDEND_SDK_INCLUDE_DIR}")
    target_compile_definitions(roundendthings_plugify_sdk INTERFACE _GLIBCXX_USE_CXX11_ABI=1)
    set(ROUNDEND_PLUGIFY_SDK roundendthings_plugify_sdk)
endif()

if(TARGET s2sdk_bindings)
    set(ROUNDEND_S2SDK_BINDINGS "$<TARGET_OBJECTS:s2sdk_bindings>")
else()
    add_library(roundendthings_s2sdk_bindings OBJECT
            "${ROUNDEND_SDK_INCLUDE_DIR}/s2sdk/s2sdk.cpp")
    target_link_libraries(roundendthings_s2sdk_bindings PUBLIC ${ROUNDEND_PLUGIFY_SDK})
    target_compile_definitions(roundendthings_s2sdk_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
    set(ROUNDEND_S2SDK_BINDINGS "$<TARGET_OBJECTS:roundendthings_s2sdk_bindings>")
endif()

if(TARGET hudmenu_bindings)
    set(ROUNDEND_HUDMENU_BINDINGS "$<TARGET_OBJECTS:hudmenu_bindings>")
elseif(EXISTS "${ROUNDEND_SDK_INCLUDE_DIR}/hudmenu/hudmenu.cpp")
    add_library(roundendthings_hudmenu_bindings OBJECT
            "${ROUNDEND_SDK_INCLUDE_DIR}/hudmenu/hudmenu.cpp")
    target_link_libraries(roundendthings_hudmenu_bindings PUBLIC ${ROUNDEND_PLUGIFY_SDK})
    target_compile_definitions(roundendthings_hudmenu_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
    set(ROUNDEND_HUDMENU_BINDINGS "$<TARGET_OBJECTS:roundendthings_hudmenu_bindings>")
else()
    message(FATAL_ERROR "RoundEndThings needs the generated hudmenu bindings (sdk/include/hudmenu/hudmenu.cpp)")
endif()

if(TARGET permissions_bindings)
    set(ROUNDEND_PERMISSIONS_BINDINGS "$<TARGET_OBJECTS:permissions_bindings>")
elseif(EXISTS "${ROUNDEND_SDK_INCLUDE_DIR}/permissions/permissions.cpp")
    add_library(roundendthings_permissions_bindings OBJECT
            "${ROUNDEND_SDK_INCLUDE_DIR}/permissions/permissions.cpp")
    target_link_libraries(roundendthings_permissions_bindings PUBLIC ${ROUNDEND_PLUGIFY_SDK})
    target_compile_definitions(roundendthings_permissions_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
    set(ROUNDEND_PERMISSIONS_BINDINGS "$<TARGET_OBJECTS:roundendthings_permissions_bindings>")
else()
    message(FATAL_ERROR "RoundEndThings needs the generated permissions bindings (sdk/include/permissions/permissions.cpp)")
endif()

# --- engine-independent core --------------------------------------------------
add_library(roundend_core STATIC src/roundend_core.cpp)
target_include_directories(roundend_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(roundend_core SYSTEM PUBLIC "${ROUNDEND_SDK_INCLUDE_DIR}")
target_compile_options(roundend_core PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(roundend_core PUBLIC _GLIBCXX_USE_CXX11_ABI=1)

# --- plugin -------------------------------------------------------------------
add_library(plugify-RoundEndThings SHARED
        src/plugin.cpp
        # __s2sdk_* / __permissions_* binding globals, filled by the plugify module from the
        # plugins declared as dependencies.
        ${ROUNDEND_S2SDK_BINDINGS}
        ${ROUNDEND_PERMISSIONS_BINDINGS}
        ${ROUNDEND_HUDMENU_BINDINGS}
)
target_include_directories(plugify-RoundEndThings PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated plg/s2sdk/permissions headers must not fail -Werror.
target_include_directories(plugify-RoundEndThings SYSTEM PRIVATE "${ROUNDEND_SDK_INCLUDE_DIR}")
target_link_libraries(plugify-RoundEndThings PRIVATE
        roundend_core
        ${ROUNDEND_PLUGIFY_SDK}
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-RoundEndThings PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-RoundEndThings PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="RoundEndThings"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-RoundEndThings PROPERTIES OUTPUT_NAME RoundEndThings)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-RoundEndThings PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/RoundEndThings.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/RoundEndThings.pplugin"
)

# --- tests --------------------------------------------------------------------
# The top-level CMakeLists declares the PLUGIFY_PORT_TESTS option *after* the plugin directories, so
# on a first configure it is not defined here yet: default it to the same ON. An explicit
# -DPLUGIFY_PORT_TESTS=OFF (the exported standalone build) is already in the cache and still wins.
if(NOT DEFINED PLUGIFY_PORT_TESTS)
    set(PLUGIFY_PORT_TESTS ON)
endif()
if(PLUGIFY_PORT_TESTS)
# roundend_core_tests: the engine-independent part - the original's menu labels and cookie readers,
# the settings.ini parser (the production file from tests/data), the candidate order of the
# unchanged original path and the first-existing/malformed-authoritative rule. No engine, no s2sdk.
add_executable(roundend_core_tests
        tests/roundend_core_tests.cpp
        src/roundend_core.cpp
)
target_include_directories(roundend_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(roundend_core_tests SYSTEM PRIVATE "${ROUNDEND_SDK_INCLUDE_DIR}")
target_compile_options(roundend_core_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(roundend_core_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        ROUNDEND_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
)
set_target_properties(roundend_core_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

# roundend_plugin_tests: src/plugin.cpp is compiled into this TU (tests/roundend_plugin_tests.cpp
# includes ../src/plugin.cpp) and every __s2sdk_*/__permissions_* binding slot is a recording fake.
# It drives the plugin through its own Plugify_* entry points: round_end/round_start, the map gate,
# the raw schema writes, mm_ret/!ret and their contexts, the cookie reconcile and slot reuse, the
# menu lifecycle, absent bindings and the reload teardown. Written into build/tests so ../test.sh
# runs it with the rest (*_tests convention).
add_executable(roundend_plugin_tests
        tests/roundend_plugin_tests.cpp
        ${ROUNDEND_S2SDK_BINDINGS}
        ${ROUNDEND_PERMISSIONS_BINDINGS}
        ${ROUNDEND_HUDMENU_BINDINGS}
)
target_include_directories(roundend_plugin_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(roundend_plugin_tests SYSTEM PRIVATE "${ROUNDEND_SDK_INCLUDE_DIR}")
target_link_libraries(roundend_plugin_tests PRIVATE
        roundend_core
        ${ROUNDEND_PLUGIFY_SDK}
        -static-libstdc++ -static-libgcc
)
target_compile_options(roundend_plugin_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(roundend_plugin_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="RoundEndThings"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
        ROUNDEND_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
)
set_target_properties(roundend_plugin_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
endif()
