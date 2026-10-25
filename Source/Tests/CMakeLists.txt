# Tests for application code that can run outside Windows: the clip library
# (Source/Clips.cpp), the piano roll's note editing (Source/NoteEdit.cpp),
# switching the expansion chip (Source/ChipSwitch.cpp), the drum grid
# (Source/DrumGrid.cpp), reading tutorials (Source/TutorialScript.cpp,
# with the built-in ones in res/tutorials and the example songs' walkthroughs
# in res/examples) and the sound chips' emulation
# with its stereo mixer (Source/APU), against working
# stand-ins for the MFC and document pieces they use (standins/). Linux and
# macOS:
#   cmake -S Source/Tests -B build/tests
#   cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure

cmake_minimum_required(VERSION 3.15)
project(fttests C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# The sources include "stdafx.h" from their own folder first, so they are
# compiled from copies next to the stand-ins
set(STAGE ${CMAKE_CURRENT_BINARY_DIR}/stage)
foreach(file Clips.cpp Clips.h NoteEdit.cpp NoteEdit.h ChipSwitch.cpp ChipSwitch.h DrumGrid.cpp DrumGrid.h)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../${file} ${STAGE}/${file} COPYONLY)
endforeach()
foreach(file stdafx.h FamiTracker.h FamiTrackerDoc.h DocumentFile.h)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/standins/${file} ${STAGE}/${file} COPYONLY)
endforeach()

enable_testing()
add_executable(clips_tests ClipsTests.cpp)
target_include_directories(clips_tests PRIVATE ${STAGE})
target_compile_options(clips_tests PRIVATE -Wno-subobject-linkage)	# Clips.cpp is included, not compiled on its own
add_test(NAME clips_tests COMMAND clips_tests)

add_executable(noteedit_tests NoteEditTests.cpp)
target_include_directories(noteedit_tests PRIVATE ${STAGE})
target_compile_options(noteedit_tests PRIVATE -Wno-subobject-linkage)
add_test(NAME noteedit_tests COMMAND noteedit_tests)

add_executable(chipswitch_tests ChipSwitchTests.cpp)
target_include_directories(chipswitch_tests PRIVATE ${STAGE})
target_compile_options(chipswitch_tests PRIVATE -Wno-subobject-linkage)
add_test(NAME chipswitch_tests COMMAND chipswitch_tests)

add_executable(drumgrid_tests DrumGridTests.cpp)
target_include_directories(drumgrid_tests PRIVATE ${STAGE})
target_compile_options(drumgrid_tests PRIVATE -Wno-subobject-linkage)
add_test(NAME drumgrid_tests COMMAND drumgrid_tests)

# The sound chips' emulation and the stereo mixer (Source/APU, with
# Blip_Buffer), from copies under the names the sources include them by (they
# were written for a filesystem that ignores case) next to stand-ins for
# stdafx.h and Common.h (standins/apu). APU_REFERENCE names a folder holding
# the APU and Blip_Buffer folders of older code to build instead: apu_tests
# then prints the hashes of the mono mix that ApuTests.cpp checks against.
set(APU_FROM ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(APU_REFERENCE)
	set(APU_FROM ${APU_REFERENCE})
endif()
set(APU_STAGE ${CMAKE_CURRENT_BINARY_DIR}/apu)
set(APU_SOURCES)
foreach(file APU.CPP DPCM.CPP FDS.CPP FDSSound.cpp MMC5.CPP Mixer.cpp N163.CPP Noise.cpp S5B.cpp Square.cpp Triangle.cpp VRC6.CPP
		VRC7.cpp emu2149.c emu2413.c)
	string(REGEX REPLACE "\\.CPP$" ".cpp" name ${file})
	configure_file(${APU_FROM}/APU/${file} ${APU_STAGE}/APU/${name} COPYONLY)
	list(APPEND APU_SOURCES ${APU_STAGE}/APU/${name})
endforeach()
foreach(file 2413tone.h APU.h Channel.h DPCM.h External.h FDS.H FDSSound.h MMC5.H Mixer.h N163.H Noise.h S5B.h Square.h Triangle.h
		Types.h VRC6.H VRC7.h emu2149.h emu2413.h vrc7tone.h)
	configure_file(${APU_FROM}/APU/${file} ${APU_STAGE}/APU/${file} COPYONLY)
endforeach()
foreach(pair APU.h:apu.h Channel.h:channel.h External.h:external.h FDS.H:FDS.h MMC5.H:MMC5.h N163.H:N163.h VRC6.H:VRC6.h)
	string(REPLACE ":" ";" names ${pair})
	list(GET names 0 from)
	list(GET names 1 to)
	configure_file(${APU_FROM}/APU/${from} ${APU_STAGE}/APU/${to} COPYONLY)
endforeach()
configure_file(${APU_FROM}/Blip_Buffer/Blip_Buffer.cpp ${APU_STAGE}/Blip_Buffer/Blip_Buffer.cpp COPYONLY)
configure_file(${APU_FROM}/Blip_Buffer/Blip_Buffer.h ${APU_STAGE}/Blip_Buffer/Blip_Buffer.h COPYONLY)
configure_file(${APU_FROM}/Blip_Buffer/Blip_Buffer.h ${APU_STAGE}/Blip_Buffer/blip_buffer.h COPYONLY)
foreach(file stdafx.h Common.h)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/standins/apu/${file} ${APU_STAGE}/${file} COPYONLY)
endforeach()
# Soft and Space (APU/Space.cpp), when the code built has them
if(EXISTS ${APU_FROM}/APU/Space.cpp)
	configure_file(${APU_FROM}/APU/Space.cpp ${APU_STAGE}/APU/Space.cpp COPYONLY)
	configure_file(${APU_FROM}/APU/Space.h ${APU_STAGE}/APU/Space.h COPYONLY)
	list(APPEND APU_SOURCES ${APU_STAGE}/APU/Space.cpp)
endif()
add_library(apu STATIC ${APU_SOURCES} ${APU_STAGE}/Blip_Buffer/Blip_Buffer.cpp)
target_include_directories(apu PUBLIC ${APU_STAGE})
target_compile_options(apu PRIVATE -w)		# as released, warnings and all
# Blip_Buffer checks its assumptions with asserts written for a 32-bit long
# (LONG_MIN >> 31 == -1), which fail on 64-bit Linux and macOS; FamiTracker
# is a 32-bit Windows program, and the emulation doesn't depend on them
target_compile_definitions(apu PRIVATE NDEBUG)

add_executable(apu_tests ApuTests.cpp)
target_link_libraries(apu_tests PRIVATE apu)
if(APU_REFERENCE)
	target_compile_definitions(apu_tests PRIVATE APU_REFERENCE)
endif()
add_test(NAME apu_tests COMMAND apu_tests)

# Plain C++: compiled from where it is
add_executable(tutorial_tests TutorialScriptTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../TutorialScript.cpp)
target_include_directories(tutorial_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tutorial_tests PRIVATE TUTORIALS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../res/tutorials"
	EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../res/examples")
add_test(NAME tutorial_tests COMMAND tutorial_tests)
