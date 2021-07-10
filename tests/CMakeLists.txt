set(FILECHECK_EXECUTABLE "FileCheck" CACHE STRING "Path to FileCheck")
set(LIT_EXECUTABLE "lit" CACHE STRING "Path to LLVM Integrated Tester")

set(SHOW_TEST_STATS true CACHE BOOL "Show statistics after running tests")
if (SHOW_TEST_STATS)
  message(STATUS "Will show statistics after each test run")
  set(LIT_OPTIONS "-sv")
else ()
  message(STATUS "Will run successful tests quietly")
  set(LIT_OPTIONS "-qv")
endif ()

add_custom_target(check
	COMMAND
		${LIT_EXECUTABLE} ${LIT_OPTIONS} ${CMAKE_CURRENT_SOURCE_DIR}
		-D buildroot=${CMAKE_BINARY_DIR}
		-D filecheck=${FILECHECK_EXECUTABLE}
		-D outputdir=${CMAKE_CURRENT_BINARY_DIR}
		-D rtti=${USE_RTTI}
		--xunit-xml-output=test-results.xml

	BYPRODUCTS test-results.xml
	COMMENT "Running test suite"
)

add_dependencies(check pegmatite)
