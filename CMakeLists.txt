cmake_minimum_required(VERSION 3.20)
project(SnowRunnerHeadTracking VERSION 0.2.0 LANGUAGES C CXX)

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
    src/legacy_config/legacy_config.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/window_centering.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
    src/builds/gdk_offsets.cpp
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

    # The frozen reader's value guards - headers only, nothing to link.
    sr_add_test(config_sanitize)

    # The camera composition, compiled straight from the production translation
    # unit: axis signs, camera-local rotation, body-frame lean, and the numbers
    # the running game was measured producing.
    sr_add_test(camera_transform src/camera_transform.cpp)

    # The gate's classification, compiled from the production translation unit.
    sr_add_test(game_state src/game_state.cpp)
    target_link_libraries(sr_game_state_tests PRIVATE cameraunlock cameraunlock_hooks
                          cameraunlock_discovery)

    # Where the log and the config land. The mod calls cameraunlock::os for this
    # rather than carrying its own copy, so this locks the contract the bootstrap
    # depends on. The running test EXE stands in for the game.
    sr_add_test(exe_paths)
    target_link_libraries(sr_exe_paths_tests PRIVATE cameraunlock)

    # The committed config against the table's fresh render, the fleet's
    # default hotkeys, the mode cycle's and the yaw toggle's saves, and Fov's
    # range. `sr_config_tests --render-config <path>` is pixi run render-config.
    sr_add_test(config src/config.cpp src/legacy_config/legacy_config.cpp)
    target_link_libraries(sr_config_tests PRIVATE cameraunlock)
    target_compile_definitions(sr_config_tests PRIVATE
        SR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # The hotkey half of the frozen loader: which INI values reach a binding and
    # which leave the action on the key it already had.
    sr_add_test(config_hotkeys src/legacy_config/legacy_config.cpp)
    target_link_libraries(sr_config_hotkeys_tests PRIVATE cameraunlock)

    # And the rest of the frozen loader's boundary: which bool, float and port
    # values reach the Config, and which are refused and leave it as it was.
    sr_add_test(config_values src/legacy_config/legacy_config.cpp)
    target_link_libraries(sr_config_values_tests PRIVATE cameraunlock)

    # The canonical config conversion's differential test: v0.2.0's reader and
    # startup code (the oracle, compiled from byte copies in
    # tests/config_differential/oracle) against the frozen reader in
    # src/legacy_config/, and that against the config owner's migration into
    # CameraUnlock.ini.
    set(SR_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    add_executable(sr_config_differential
        ${SR_DIFFERENTIAL_DIR}/differential_tests.cpp
        ${SR_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(sr_config_differential PRIVATE
        src
        ${SR_DIFFERENTIAL_DIR}
        cameraunlock-core/cpp/include
    )
    target_compile_definitions(sr_config_differential PRIVATE
        SR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(sr_config_differential PRIVATE cameraunlock)
    set_target_properties(sr_config_differential PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(sr_config_differential PRIVATE /WX /EHsc /bigobj)
    endif()
    add_test(NAME config_differential COMMAND sr_config_differential)
    set_property(GLOBAL APPEND PROPERTY SR_TEST_TARGETS sr_config_differential)
    # Core's canonical config lint over the committed file and every file the
    # differential test migrated, which it writes beside itself under migrated/.
    add_test(NAME config_lint COMMAND node
             ${SR_DIFFERENTIAL_DIR}/lint-migrated.mjs
             $<TARGET_FILE_DIR:sr_config_differential>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential)

    # What the differential test compiles is pinned by hash, line endings aside:
    # the oracle's byte copies must stay v0.2.0's (git show v0.2.0:src/<file>),
    # the core sources both readers compile must stay the ones v0.2.0 was built
    # with (core c2d7914), and the frozen reader is never edited.
    function(sr_require_hash path expected)
        file(READ "${path}" text)
        string(REPLACE "\r" "" text "${text}")
        string(SHA256 actual "${text}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                                "${expected}). The config differential test compares against "
                                "exactly this file; see CMakeLists.txt.")
        endif()
    endfunction()
    set(SR_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp)
    foreach(pinned
            "${SR_DIFFERENTIAL_DIR}/oracle/src/config.cpp|9690d7912cc4bb1862909e1d5db721fd443333be6461d13b15214995999a7026"
            "${SR_DIFFERENTIAL_DIR}/oracle/src/config.h|368442ff6794c16ec69931f3a4d02ac8e479d0a31c6729345275bb2acca655e4"
            "${SR_DIFFERENTIAL_DIR}/oracle/src/config_sanitize.h|5e3b534ec106296d6f44834865be34e5ecf0cb302196ee556f45decba9cb0385"
            "${SR_DIFFERENTIAL_DIR}/oracle/src/logging.h|23e5e250e5c9f7bcb70ab077761dd37538eac339835b7afce39514209f2df9ed"
            "${SR_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
            "${SR_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
            "${SR_CORE_DIR}/include/cameraunlock/protocol/port_utils.h|91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"
            "${SR_CORE_DIR}/include/cameraunlock/math/smoothing_utils.h|fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"
            "${SR_CORE_DIR}/include/cameraunlock/math/angle_utils.h|d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"
            "${SR_CORE_DIR}/include/cameraunlock/data/position_settings.h|b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"
            "${SR_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
            "${SR_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.h|a90d98edb49f136b97dee89e4ebd946af3579c4bb869ec1419e99097d8e24634"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|2115aca7cf23b73059304549e3340aeeee7dd4310caeec31082a6cbcd15bc6fd"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/config_sanitize.h|3a8ba55a860caa11cbbf0c15a34779c504cd01490de3c1098d5397f7375925a9")
        string(REPLACE "|" ";" pair "${pinned}")
        list(GET pair 0 pinned_path)
        list(GET pair 1 pinned_hash)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
        sr_require_hash("${pinned_path}" "${pinned_hash}")
    endforeach()

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
