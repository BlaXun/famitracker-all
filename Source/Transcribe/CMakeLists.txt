# Transcription engine for the recorder: library, wav2ftm tool and tests.
# Builds on Linux, macOS and Windows independently of the MFC application:
#   cmake -S Source/Transcribe -B build/transcribe
#   cmake --build build/transcribe --config Release
#   ctest --test-dir build/transcribe -C Release --output-on-failure

cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
project(transcribe CXX)

set(CMAKE_CXX_STANDARD 14)	# FamiTracker itself builds as C++14
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")	# static CRT, like FamiTracker
	add_compile_options(/W3)
else()
	add_compile_options(-Wall -Wextra)
endif()

add_library(transcribe STATIC
	WavReader.cpp
	PitchTracker.cpp
	Notes.cpp
	Hits.cpp
	TextModule.cpp
)
target_include_directories(transcribe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(wav2ftm tools/wav2ftm.cpp)
target_link_libraries(wav2ftm PRIVATE transcribe)

enable_testing()
add_executable(transcribe_tests tests/TranscribeTests.cpp)
target_link_libraries(transcribe_tests PRIVATE transcribe)
add_test(NAME transcribe_tests COMMAND transcribe_tests)
