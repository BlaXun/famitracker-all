# Tests for application code that can run outside Windows: the clip library
# (Source/Clips.cpp), the piano roll's note editing (Source/NoteEdit.cpp),
# switching the expansion chip (Source/ChipSwitch.cpp), the drum grid
# (Source/DrumGrid.cpp), reading tutorials (Source/TutorialScript.cpp,
# with the built-in ones in res/tutorials) and the words for the cell under
# the cursor (Source/CellHint.cpp), against working
# stand-ins for the MFC and document pieces they use (standins/). Linux and
# macOS:
#   cmake -S Source/Tests -B build/tests
#   cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure

cmake_minimum_required(VERSION 3.15)
project(fttests CXX)

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

# Plain C++: compiled from where they are
add_executable(cellhint_tests CellHintTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../CellHint.cpp)
target_include_directories(cellhint_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME cellhint_tests COMMAND cellhint_tests)

add_executable(tutorial_tests TutorialScriptTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../TutorialScript.cpp)
target_include_directories(tutorial_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tutorial_tests PRIVATE TUTORIALS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../res/tutorials")
add_test(NAME tutorial_tests COMMAND tutorial_tests)
