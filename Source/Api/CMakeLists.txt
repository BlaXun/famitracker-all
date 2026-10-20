# The Claude Code connection's core (MCP over HTTP, the tools in musical terms)
# with its tests. Builds on Linux, macOS and Windows without MFC:
#   cmake -S Source/Api -B build/api
#   cmake --build build/api --config Release
#   ctest --test-dir build/api -C Release --output-on-failure

cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
project(ftapi CXX)

set(CMAKE_CXX_STANDARD 14)	# FamiTracker itself builds as C++14
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	add_compile_options(/W3)
else()
	add_compile_options(-Wall -Wextra)
endif()

# The harmony code (keys, chords, bass lines) comes from the transcription engine
set(TRANSCRIBE ${CMAKE_CURRENT_SOURCE_DIR}/../Transcribe)
add_library(ftapi STATIC
	Json.cpp
	Http.cpp
	Mcp.cpp
	Music.cpp
	Presets.cpp
	Tools.cpp
	${TRANSCRIBE}/WavReader.cpp
	${TRANSCRIBE}/PitchTracker.cpp
	${TRANSCRIBE}/Guess.cpp
	${TRANSCRIBE}/Expression.cpp
	${TRANSCRIBE}/Notes.cpp
	${TRANSCRIBE}/Hits.cpp
	${TRANSCRIBE}/Take.cpp
	${TRANSCRIBE}/Harmony.cpp
	${TRANSCRIBE}/TextModule.cpp
)
target_include_directories(ftapi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_executable(api_tests tests/ApiTests.cpp)
target_link_libraries(api_tests PRIVATE ftapi)
add_test(NAME api_tests COMMAND api_tests)
