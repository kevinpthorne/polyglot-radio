cmake_minimum_required(VERSION 3.15)
project(polyglot_native C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(
    include
    external
)

# Core source files
set(SOURCES
    src/dart_api_dl.c
    src/audio_hal.cpp
    src/sentry_coordinator.cpp
    src/native_bridge.cpp
    src/modems/sstv_engine.cpp
    src/modems/rattlegram.cpp
    src/modems/aprs_bell202.cpp
    src/modems/eas_same.cpp
    src/modems/cw_morse.cpp
    src/modems/psk31.cpp
    src/modems/feld_hell.cpp
    src/modems/olivia.cpp
    src/modems/wefax.cpp
    src/modems/ft8.cpp
    src/modems/ultrasound.cpp
)

# Shared Library for Flutter dart:ffi
add_library(polyglot_native SHARED ${SOURCES})

if (APPLE)
    find_library(COREAUDIO_LIB CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX_LIB AudioToolbox REQUIRED)
    find_library(CORESERVICES_LIB CoreServices REQUIRED)
    target_link_libraries(polyglot_native PRIVATE
        ${COREAUDIO_LIB}
        ${AUDIOTOOLBOX_LIB}
        ${CORESERVICES_LIB}
    )
elseif (ANDROID)
    target_link_libraries(polyglot_native PRIVATE
        OpenSLES
        log
        m
    )
elseif (UNIX AND NOT ANDROID)
    target_link_libraries(polyglot_native PRIVATE pthread dl m)
endif()

# Build Test Binaries (Desktop / Host Only)
if (NOT ANDROID)
    enable_testing()

    add_executable(dsp_tests tests/test_dsp_math.cpp)
    target_link_libraries(dsp_tests PRIVATE polyglot_native)

    add_executable(sentry_tests tests/test_sentry_correlator.cpp)
    target_link_libraries(sentry_tests PRIVATE polyglot_native)

    add_executable(loopback_tests tests/test_modem_loopbacks.cpp)
    target_link_libraries(loopback_tests PRIVATE polyglot_native)

    add_test(NAME dsp_tests COMMAND dsp_tests)
    add_test(NAME sentry_tests COMMAND sentry_tests)
    add_test(NAME loopback_tests COMMAND loopback_tests)
endif()
