# The NSF importer: the library (a 6502, the sound chips' registers, the
# player, the conversion to a FamiTracker module), the nsf2ftm tool and the
# tests. Builds on Linux, macOS and Windows independently of the MFC
# application:
#   cmake -S Source/Nsf -B build/nsf
#   cmake --build build/nsf --config Release
#   ctest --test-dir build/nsf -C Release --output-on-failure

cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
project(nsf CXX)

set(CMAKE_CXX_STANDARD 14)	# FamiTracker itself builds as C++14
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")	# static CRT, like FamiTracker
	add_compile_options(/W3)
else()
	add_compile_options(-Wall -Wextra)
endif()

add_library(nsf STATIC
	Cpu6502.cpp
	NsfFile.cpp
	Chips.cpp
	Player.cpp
	Module.cpp
	Convert.cpp
	Import.cpp
	Simulate.cpp
	Study.cpp
)
target_include_directories(nsf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(nsf2ftm tools/nsf2ftm.cpp)
target_link_libraries(nsf2ftm PRIVATE nsf)

enable_testing()
add_executable(nsf_tests tests/NsfTests.cpp)
target_link_libraries(nsf_tests PRIVATE nsf)
add_test(NAME nsf_tests COMMAND nsf_tests)
