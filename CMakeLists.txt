cmake_minimum_required(VERSION 3.25)

# TRoss 303 — Max for Live TB-303 voice and sequencer.
# Builds the tross303~ external (.mxo on macOS, .mxe64 on Windows) into ./externals,
# and optionally the engine tests (-DTROSS_TESTS=ON, any platform).

set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(TROSS_TESTS "Build the engine tests instead of the Max external" OFF)

set(LEMUS_CORE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lemus/machines/tb303/core")
set(TROSS_ENGINE_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/engine/Synth.cpp"
	"${LEMUS_CORE}/voice.cpp"
	"${LEMUS_CORE}/oversampler.cpp")
set(TROSS_ENGINE_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/engine" "${LEMUS_CORE}")

if(TROSS_TESTS)
	project(TRoss303Tests C CXX)
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	add_executable(engine_test tests/engine_test.cpp ${TROSS_ENGINE_SOURCES})
	target_include_directories(engine_test PRIVATE ${TROSS_ENGINE_INCLUDES})
	return()
endif()

set(C74_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/externals")
add_subdirectory(third_party/max-sdk-base)

project(TRoss303 C CXX)

add_subdirectory(source/tross303~)
