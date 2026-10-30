# The Claude Code connection's core (MCP over HTTP, the tools in musical terms)
# with its tests, and famitracker-mcp, the connection for Claude Code in WSL
# (and for MCP clients on another computer). Builds on Linux, macOS and Windows
# without MFC:
#   cmake -S Source/Api -B build/api
#   cmake --build build/api --config Release
#   ctest --test-dir build/api -C Release --output-on-failure
# famitracker-mcp alone, statically linked (as CI builds it for Linux, on Alpine):
#   cmake -S Source/Api -B build/bridge -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-static
#   cmake --build build/bridge --target famitracker-mcp

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
	# Floating point as MSVC does it: no fused multiply-adds (newer GCCs on x86-64-v3 contract a*b+c,
	# which broke near ties between costs, so Linux and Windows chose different voicings and keys)
	add_compile_options(-ffp-contract=off)
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

# The harmony code (keys, chords, bass lines) comes from the transcription
# engine, and the NSF importer (Source/Nsf) studies and imports NSF files
set(TRANSCRIBE ${CMAKE_CURRENT_SOURCE_DIR}/../Transcribe)
set(NSF ${CMAKE_CURRENT_SOURCE_DIR}/../Nsf)
# What famitracker-mcp needs: JSON, HTTP and the bridge (Bridge.h)
add_library(ftbridge STATIC
	Json.cpp
	Http.cpp
	Bridge.cpp
	BridgeSocket.cpp
)
target_include_directories(ftbridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
	target_link_libraries(ftbridge PUBLIC ws2_32 advapi32)
endif()

add_library(ftapi STATIC
	Mcp.cpp
	Network.cpp
	Png.cpp
	Picture.cpp
	Ogg.cpp
	GameAudio.cpp
	Jingles.cpp
	SoundEffects.cpp
	Effects.cpp
	Glossary.cpp
	FdsSounds.cpp
	Wavetables.cpp
	PulseClicks.cpp
	ProblemList.cpp
	Music.cpp
	Sections.cpp
	SongShapes.cpp
	Exercises.cpp
	Keys.cpp
	ChannelMix.cpp
	Vrc6Plus.cpp
	Chords.cpp
	ChordRoles.cpp
	RecorderInput.cpp
	Arrange.cpp
	GrowMelody.cpp
	ChordHelper.cpp
	StepNotes.cpp
	QrCode.cpp
	PhoneTakes.cpp
	DpcmKit.cpp
	Presets.cpp
	SoundLibrary.cpp
	GameSounds.cpp
	SoundIndex.cpp
	References.cpp
	StyleGuide.cpp
	Tools.cpp
	WhatNext.cpp
	NextSteps.cpp
	Listen.cpp
	Assistant.cpp
	AssistantSocket.cpp
	AgentLog.cpp
	${TRANSCRIBE}/WavReader.cpp
	${TRANSCRIBE}/PitchTracker.cpp
	${TRANSCRIBE}/Guess.cpp
	${TRANSCRIBE}/Expression.cpp
	${TRANSCRIBE}/Notes.cpp
	${TRANSCRIBE}/Hits.cpp
	${TRANSCRIBE}/Take.cpp
	${TRANSCRIBE}/Harmony.cpp
	${TRANSCRIBE}/TextModule.cpp
	${TRANSCRIBE}/SoundEffect.cpp
	${TRANSCRIBE}/Tapping.cpp
	${TRANSCRIBE}/Strums.cpp
	${NSF}/Cpu6502.cpp
	${NSF}/NsfFile.cpp
	${NSF}/Chips.cpp
	${NSF}/Player.cpp
	${NSF}/Module.cpp
	${NSF}/Convert.cpp
	${NSF}/Import.cpp
	${NSF}/Simulate.cpp
	${NSF}/Study.cpp
	${NSF}/Tracker.cpp
	${NSF}/MidiFile.cpp
	${NSF}/FtmFile.cpp
	${NSF}/FdsModulator.cpp
)
target_include_directories(ftapi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ftapi PUBLIC ftbridge xiph)

# Claude Code in WSL starts this Windows program, which passes its messages
# to FamiTracker on Windows' 127.0.0.1 (Bridge.h); with --host, a stdio MCP
# client on another computer reaches FamiTracker over the network with it
add_executable(famitracker-mcp tools/famitracker-mcp.cpp)
target_link_libraries(famitracker-mcp PRIVATE ftbridge)

# The tests run FamiTracker's endpoint on a thread to try the bridge over sockets
find_package(Threads REQUIRED)
enable_testing()
# with the tutorials' reader (Source/TutorialScript.cpp, plain C++) for the exercises' scripts and starter songs in
# res/exercises
add_executable(api_tests tests/ApiTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../TutorialScript.cpp)
target_link_libraries(api_tests PRIVATE ftapi Threads::Threads)
target_compile_definitions(api_tests PRIVATE EXERCISES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../res/exercises")
add_test(NAME api_tests COMMAND api_tests)

# The piano roll's chord helper (ChordHelper.h)
add_executable(chord_helper_tests tests/ChordHelperTests.cpp)
target_link_libraries(chord_helper_tests PRIVATE ftapi)
add_test(NAME chord_helper_tests COMMAND chord_helper_tests)

# What chords do in a key, and the chord tones under a row (ChordRoles.h)
add_executable(chord_roles_tests tests/ChordRolesTests.cpp)
target_link_libraries(chord_roles_tests PRIVATE ftapi)
add_test(NAME chord_roles_tests COMMAND chord_roles_tests)

# Melodies written in steps (StepNotes.h)
add_executable(step_notes_tests tests/StepNotesTests.cpp)
target_link_libraries(step_notes_tests PRIVATE ftapi)
add_test(NAME step_notes_tests COMMAND step_notes_tests)

# The phone page (PhoneTakes.h) and its QR code (QrCode.h)
add_executable(phone_tests tests/PhoneTests.cpp)
target_link_libraries(phone_tests PRIVATE ftapi)
target_compile_definitions(phone_tests PRIVATE PHONE_PAGE="${CMAKE_CURRENT_SOURCE_DIR}/../../res/phone.html")
add_test(NAME phone_tests COMMAND phone_tests)

# The glossary (Glossary.h): its entries, links, searches and Hear it examples
add_executable(glossary_tests tests/GlossaryTests.cpp)
target_link_libraries(glossary_tests PRIVATE ftapi)
add_test(NAME glossary_tests COMMAND glossary_tests)

# Listening to a song as it sounds (Listen.h): warbles, tuning, beating, clicks, drones and the mix
add_executable(listen_tests tests/ListenTests.cpp)
target_link_libraries(listen_tests PRIVATE ftapi)
add_test(NAME listen_tests COMMAND listen_tests)

# Help > Exercises' checks (Exercises.h) on songs described by hand, their links and their answers as JSON
add_executable(exercises_tests tests/ExercisesTests.cpp)
target_link_libraries(exercises_tests PRIVATE ftapi)
add_test(NAME exercises_tests COMMAND exercises_tests)

# The Ask panel's model (Assistant.h, AssistantSocket.h): requests, streamed replies, the conversation, and a scripted
# model over a real socket (tests/FakeModel.h, which ApiTests.cpp uses too for the loop with the tools)
add_executable(assistant_tests tests/AssistantTests.cpp)
target_link_libraries(assistant_tests PRIVATE ftapi Threads::Threads)
add_test(NAME assistant_tests COMMAND assistant_tests)
