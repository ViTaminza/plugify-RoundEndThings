# RoundEndThings — Plugify port of the Metamod plugin RoundEndThings (ABKAM): bhop + speed/gravity
# boost in the last seconds before the round ends, with per-player opt-outs stored in permissions
# cookies.
#
#   plugify-RoundEndThings  the plugin shared library (entry "bin/RoundEndThings")
#   roundend_core           the engine-independent settings/cookie helpers (+ its tests)

set(ROUNDENDTHINGS_VERSION "1.0.1")

# --- engine-independent core --------------------------------------------------
add_library(roundend_core STATIC src/roundend_core.cpp)
target_include_directories(roundend_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(roundend_core SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/sdk/include")
target_compile_options(roundend_core PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(roundend_core PUBLIC _GLIBCXX_USE_CXX11_ABI=1)

# --- plugin -------------------------------------------------------------------
add_library(plugify-RoundEndThings SHARED
        src/plugin.cpp
        # __s2sdk_* / __permissions_* binding globals, filled by the plugify module from the
        # plugins declared as dependencies.
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:permissions_bindings>
)
target_include_directories(plugify-RoundEndThings PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated pisexcompat/plg/s2sdk/permissions headers must not fail -Werror.
target_include_directories(plugify-RoundEndThings SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-RoundEndThings PRIVATE
        roundend_core
        pisexcompat
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
add_executable(roundend_core_tests
        tests/roundend_core_tests.cpp
        src/roundend_core.cpp
)
target_include_directories(roundend_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(roundend_core_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
target_compile_options(roundend_core_tests PRIVATE -Wall -Wextra -Werror)
set_target_properties(roundend_core_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
