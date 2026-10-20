# The Claude Code connection's core (MCP over HTTP, the tools in musical terms)
# with its tests, and famitracker-mcp, the connection for Claude Code in WSL.
# Builds on Linux, macOS and Windows without MFC:
#   cmake -S Source/Api -B build/api
#   cmake --build build/api --config Release
#   ctest --test-dir build/api -C Release --output-on-failure

cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
project(ftapi C CXX)

set(CMAKE_CXX_STANDARD 14)	# FamiTracker itself builds as C++14
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	add_compile_options(/W3)
else()
	add_compile_options(-Wall -Wextra)
endif()

# libogg and libvorbis (BSD licensed, Source/libogg and Source/libvorbis as
# released), for the OGG export; their own warnings are left to them
set(XIPH ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(xiph STATIC
	${XIPH}/libogg/src/bitwise.c
	${XIPH}/libogg/src/framing.c
	${XIPH}/libvorbis/lib/analysis.c
	${XIPH}/libvorbis/lib/bitrate.c
	${XIPH}/libvorbis/lib/block.c
	${XIPH}/libvorbis/lib/codebook.c
	${XIPH}/libvorbis/lib/envelope.c
	${XIPH}/libvorbis/lib/floor0.c
	${XIPH}/libvorbis/lib/floor1.c
	${XIPH}/libvorbis/lib/info.c
	${XIPH}/libvorbis/lib/lookup.c
	${XIPH}/libvorbis/lib/lpc.c
	${XIPH}/libvorbis/lib/lsp.c
	${XIPH}/libvorbis/lib/mapping0.c
	${XIPH}/libvorbis/lib/mdct.c
	${XIPH}/libvorbis/lib/psy.c
	${XIPH}/libvorbis/lib/registry.c
	${XIPH}/libvorbis/lib/res0.c
	${XIPH}/libvorbis/lib/sharedbook.c
	${XIPH}/libvorbis/lib/smallft.c
	${XIPH}/libvorbis/lib/synthesis.c
	${XIPH}/libvorbis/lib/vorbisenc.c
	${XIPH}/libvorbis/lib/vorbisfile.c
	${XIPH}/libvorbis/lib/window.c
)
target_include_directories(xiph PUBLIC ${XIPH}/libogg/include ${XIPH}/libvorbis/include PRIVATE ${XIPH}/libvorbis/lib)
if(MSVC)
	target_compile_options(xiph PRIVATE /w)
	target_compile_definitions(xiph PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(xiph PRIVATE -w)
	target_link_libraries(xiph PUBLIC m)
endif()

# The harmony code (keys, chords, bass lines) comes from the transcription engine
set(TRANSCRIBE ${CMAKE_CURRENT_SOURCE_DIR}/../Transcribe)
add_library(ftapi STATIC
	Json.cpp
	Http.cpp
	Mcp.cpp
	Bridge.cpp
	BridgeSocket.cpp
	Png.cpp
	Picture.cpp
	Ogg.cpp
	GameAudio.cpp
	Jingles.cpp
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
target_link_libraries(ftapi PUBLIC xiph)
if(WIN32)
	target_link_libraries(ftapi PUBLIC ws2_32 advapi32)
endif()

# Claude Code in WSL starts this Windows program, which passes its messages
# to FamiTracker on Windows' 127.0.0.1 (Bridge.h)
add_executable(famitracker-mcp tools/famitracker-mcp.cpp)
target_link_libraries(famitracker-mcp PRIVATE ftapi)

# The tests run FamiTracker's endpoint on a thread to try the bridge over sockets
find_package(Threads REQUIRED)
enable_testing()
add_executable(api_tests tests/ApiTests.cpp)
target_link_libraries(api_tests PRIVATE ftapi Threads::Threads)
add_test(NAME api_tests COMMAND api_tests)
