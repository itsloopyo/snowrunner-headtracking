cmake_minimum_required(VERSION 3.20)
project(SnowRunnerHeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP /W4)
endif()

# HOOKS carries hooks/hook_manager.cpp, the MinHook wrapper both the camera
# hook and the Present hook place their detours through, and pulls in the
# MinHook copy vendored in cameraunlock-core. DISCOVERY carries the RTTI vtable
# walk, which is how this mod reaches the Saber3D camera classes: the engine
# ships full MSVC RTTI, so the classes resolve by name at runtime rather than
# by an RVA that every patch moves.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_DISCOVERY ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/config.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/hotkey_names.cpp
    src/window_centering.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(SnowRunnerHeadTracking SHARED ${MOD_SOURCES})

target_include_directories(SnowRunnerHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(SnowRunnerHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_discovery
    ws2_32
    psapi
)

# Emit a linker map so a crash offset (asi+0xNNNN) resolves to a symbol.
if(MSVC)
    target_link_options(SnowRunnerHeadTracking PRIVATE /MAP /MAPINFO:EXPORTS)
endif()

# Output as .asi for Ultimate ASI Loader.
set_target_properties(SnowRunnerHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Opt-in unit tests. OFF by default so the normal build and the release packager
# never produce a test binary next to the .asi. Enabled by scripts/run-tests.ps1
# (`pixi run test`), which builds the sr_tests target.
option(SR_BUILD_TESTS "Build SnowRunnerHeadTracking unit tests" OFF)
if(SR_BUILD_TESTS)
    enable_testing()

    function(sr_add_test test_name)
        set(target "sr_${test_name}_tests")
        add_executable(${target} tests/${test_name}_tests.cpp ${ARGN})
        target_include_directories(${target} PRIVATE
            src
            cameraunlock-core/cpp/include
        )
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        # /W4 and /permissive- are already on globally; tests additionally treat
        # warnings as errors.
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        add_test(NAME ${test_name} COMMAND ${target})
        set_property(GLOBAL APPEND PROPERTY SR_TEST_TARGETS ${target})
    endfunction()

    # Pure boundary logic - headers only, nothing to link.
    sr_add_test(config_sanitize)

    # The camera composition, compiled straight from the production translation
    # unit: axis signs, camera-local rotation, body-frame lean, and the numbers
    # the running game was measured producing.
    sr_add_test(camera_transform src/camera_transform.cpp)

    # The gate's classification, compiled from the production translation unit.
    sr_add_test(game_state src/game_state.cpp)
    target_link_libraries(sr_game_state_tests PRIVATE cameraunlock cameraunlock_hooks
                          cameraunlock_discovery)

    # Where the log and the INI land. The mod calls cameraunlock::os for this
    # rather than carrying its own copy, so this locks the contract the bootstrap
    # depends on. The running test EXE stands in for the game.
    sr_add_test(exe_paths)
    target_link_libraries(sr_exe_paths_tests PRIVATE cameraunlock)

    # The one log line that reports which keys the config landed on. What a key
    # is called is the keyboard layout's business; that a name is always produced
    # is this mod's.
    sr_add_test(hotkey_names src/hotkey_names.cpp)

    # Exercises the real INI generator and loader, so it needs the core's
    # IniReader and the log sink config.cpp writes its diagnostics to.
    sr_add_test(config_defaults src/config.cpp)
    target_link_libraries(sr_config_defaults_tests PRIVATE cameraunlock)
    target_compile_definitions(sr_config_defaults_tests PRIVATE
        SR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # The hotkey half of the same loader: which INI values reach a binding and
    # which leave the action on the key it already had.
    sr_add_test(config_hotkeys src/config.cpp)
    target_link_libraries(sr_config_hotkeys_tests PRIVATE cameraunlock)

    # And the rest of the loader's boundary: which bool, float and port values
    # reach the Config, and which are refused and leave it as it was.
    sr_add_test(config_values src/config.cpp)
    target_link_libraries(sr_config_values_tests PRIVATE cameraunlock)

    # Reclaiming the tracker port after another game gives it up. Drives the
    # real receiver against a real socket and measures the recovery, because
    # the retry interval in the header is not what the player waits: the
    # supervisor's own tick is on top of it.
    sr_add_test(udp_recovery)
    target_link_libraries(sr_udp_recovery_tests PRIVATE cameraunlock ws2_32)

    # Built from what sr_add_test registered, so a new test cannot be added to
    # ctest and left out of the target run-tests.ps1 builds.
    get_property(sr_test_targets GLOBAL PROPERTY SR_TEST_TARGETS)
    add_custom_target(sr_tests DEPENDS ${sr_test_targets})
endif()
