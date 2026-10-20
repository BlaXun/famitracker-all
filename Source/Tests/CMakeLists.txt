# Tests for application code that can run outside Windows: the clip library
# (Source/Clips.cpp) against working stand-ins for the MFC and document pieces
# it uses (standins/). Linux and macOS:
#   cmake -S Source/Tests -B build/tests
#   cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure

cmake_minimum_required(VERSION 3.15)
project(fttests CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Clips.cpp includes "stdafx.h" from its own folder first, so it is compiled
# from a copy next to the stand-ins
set(STAGE ${CMAKE_CURRENT_BINARY_DIR}/stage)
foreach(file Clips.cpp Clips.h)
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
