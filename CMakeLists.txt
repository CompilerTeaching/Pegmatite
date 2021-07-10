cmake_minimum_required(VERSION 2.8)
cmake_policy(SET CMP0042 NEW)

project(libpegmatite)

if (WIN32)
	# No support on Windows for a shared library.
	set(BUILD_SHARED_LIB OFF)
else()
	option(BUILD_SHARED_LIB "Build shared library" ON)
endif()

set(libpegmatite 0.1)

set(libpegmatite_CXX_SRCS
	ast.cc
	parser.cc
)

option(USE_RTTI "Use native C++ RTTI" ON)
option(BUILD_TRACING "Build with debug parse tracing support" OFF)
option(BUILD_AST_TRACING "Build with debug AST construction tracing support" OFF)

macro(set_options target)
	if (WIN32)
		target_compile_definitions(${target} PUBLIC -DPEGMATITE_PLATFORM_WINDOWS)
	elseif(UNIX)
		target_compile_definitions(${target} PUBLIC -DPEGMATITE_PLATFORM_UNIX)
	endif()
	if (USE_RTTI)
		target_compile_definitions(${target} PUBLIC -DUSE_RTTI=1)
	endif ()
	if (BUILD_TRACING)
		target_compile_definitions(${target} PUBLIC -DDEBUG_PARSING)
	endif()
	if (BUILD_AST_TRACING)
		target_compile_definitions(${target} PUBLIC -DDEBUG_AST_CONSTRUCTION)
	endif()
	target_include_directories(${target} PUBLIC .)
endmacro()

if (BUILD_SHARED_LIB)
	add_library(pegmatite SHARED ${libpegmatite_CXX_SRCS})
	set_options(pegmatite)
endif()

add_library(pegmatite-static STATIC ${libpegmatite_CXX_SRCS})
set_options(pegmatite-static)
set_target_properties(pegmatite-static PROPERTIES
	POSITION_INDEPENDENT_CODE true
	OUTPUT_NAME "pegmatite")

if (CMAKE_BUILD_TYPE MATCHES "Debug")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
endif()

set(CMAKE_CXX_STANDARD 11)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(CLANG_FLAGS "-Weverything -Wno-c++98-compat -Wno-padded")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CLANG_FLAGS}")
else()
	if(MSVC)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
	endif()
endif()

option(BUILD_DOCUMENTATION "Use Doxygen to create the HTML based API documentation" OFF)
if(BUILD_DOCUMENTATION)
	FIND_PACKAGE(Doxygen)
	if (NOT DOXYGEN_FOUND)
		message(FATAL_ERROR 
			"Doxygen is needed to build the documentation. Please install it correctly")
	endif()
	#-- Configure the Template Doxyfile for our specific project
	configure_file(Doxyfile.in 
	               ${PROJECT_BINARY_DIR}/Doxyfile @ONLY IMMEDIATE)
	#-- Add a custom target to run Doxygen when ever the project is built
	add_custom_target (Docs ALL 
	                   COMMAND ${DOXYGEN_EXECUTABLE} ${PROJECT_BINARY_DIR}/Doxyfile
	                   SOURCES ${PROJECT_BINARY_DIR}/Doxyfile)
endif()

option(BUILD_EXAMPLES "Build examples" OFF)
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

add_subdirectory(tests)
