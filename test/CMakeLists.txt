#
## Test and example programs for the Samurai library
#

# ---------------------------------------------------------------------------
# Example / manual test programs
#
# These are interactive tools rather than automated tests, so they are built
# but not registered with CTest.
# ---------------------------------------------------------------------------

add_executable(testtiger  applications/tiger/tiger.cpp)
add_executable(dnslookup  applications/dnslookup/dnslookup.cpp)
add_executable(netcat     applications/netcat/netcat.cpp)
add_executable(wget       applications/wget/wget.cpp)
add_executable(thread     applications/thread/thread.cpp)
add_executable(interfaces applications/net/interfaces.cpp)

# NOTE: upnplist needs all three translation units. The old Makefile listed
# only upnplist.cpp in test_SOURCES and relied on a separate variable for the
# other two.
add_executable(upnplist
    applications/upnplist/upnp.cpp
    applications/upnplist/upnpworker.cpp
    applications/upnplist/upnplist.cpp)

set(SAMURAI_TEST_APPS
    testtiger dnslookup netcat wget thread interfaces upnplist)

foreach(app IN LISTS SAMURAI_TEST_APPS)
    # samurai::samurai resolves to whichever library flavour is primary, so the
    # tests build whether or not the shared library is enabled.
    target_link_libraries(${app} PRIVATE samurai::samurai)
    set_target_properties(${app} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${app} PRIVATE -Wall -Wextra)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# The exotic suite (test/exotic/*.tcc)
#
# These are real automated tests, but they have to be run through the "exotic"
# generator (https://github.com/janvidar/exotic) to become compilable C++.
# If the generator is not installed, skip them rather than fail the build.
# ---------------------------------------------------------------------------

find_program(SAMURAI_EXOTIC_EXECUTABLE exotic
    DOC "Path to the exotic test generator")

if(NOT SAMURAI_EXOTIC_EXECUTABLE)
    message(STATUS
        "Samurai: 'exotic' generator not found - skipping the test/exotic "
        "suite. Install it to enable ~156 additional tests.")
    return()
endif()

set(SAMURAI_EXOTIC_INPUTS
    file.tcc
    hash_tiger.tcc
    inetaddress.tcc
    sockets.tcc
    url.tcc)

set(SAMURAI_EXOTIC_INPUT_PATHS "")
foreach(input IN LISTS SAMURAI_EXOTIC_INPUTS)
    list(APPEND SAMURAI_EXOTIC_INPUT_PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/exotic/${input})
endforeach()

set(SAMURAI_EXOTIC_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated_tests.cpp)

add_custom_command(
    OUTPUT ${SAMURAI_EXOTIC_GENERATED}
    COMMAND ${CMAKE_COMMAND}
        -DEXOTIC=${SAMURAI_EXOTIC_EXECUTABLE}
        -DSRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/exotic
        -DOUTPUT=${SAMURAI_EXOTIC_GENERATED}
        "-DINPUTS=${SAMURAI_EXOTIC_INPUTS}"
        -P ${PROJECT_SOURCE_DIR}/cmake/RunExotic.cmake
    DEPENDS ${SAMURAI_EXOTIC_INPUT_PATHS} ${PROJECT_SOURCE_DIR}/cmake/RunExotic.cmake
    COMMENT "Generating exotic test suite"
    VERBATIM)

# exotic --standalone inlines the contents of autotest.h (behind its own
# include guard) but still emits an #include for it, so the header only has to
# exist to satisfy the preprocessor. Prefer the real one; fall back to an empty
# stand-in so the suite builds without exotic's source tree present.
find_path(SAMURAI_EXOTIC_INCLUDE_DIR autotest.h
    PATH_SUFFIXES exotic exotic/src
    DOC "Directory containing exotic's autotest.h")

if(SAMURAI_EXOTIC_INCLUDE_DIR)
    set(SAMURAI_EXOTIC_HEADER_DIR ${SAMURAI_EXOTIC_INCLUDE_DIR})
else()
    set(SAMURAI_EXOTIC_HEADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/exotic-stub)
    file(MAKE_DIRECTORY ${SAMURAI_EXOTIC_HEADER_DIR})
    file(WRITE ${SAMURAI_EXOTIC_HEADER_DIR}/autotest.h
        "/* Intentionally empty: exotic --standalone already inlines the real\n"
        "   autotest.h, but still emits an #include for it. See test/CMakeLists.txt. */\n")
endif()

add_executable(autotest ${SAMURAI_EXOTIC_GENERATED})

target_include_directories(autotest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/exotic   # the .tcc files are #included by name
    ${SAMURAI_EXOTIC_HEADER_DIR})

# file.tcc reaches its data/ fixtures through this directory rather than by a
# path relative to the working directory, so autotest can be run from anywhere.
target_compile_definitions(autotest PRIVATE
    EXOTIC_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/exotic")

target_link_libraries(autotest PRIVATE samurai::samurai)

set_target_properties(autotest PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

add_test(NAME exotic COMMAND autotest)

set_tests_properties(exotic PROPERTIES
    # Some tests bind listening sockets and resolve names on the loopback
    # interface; give them room on a loaded machine.
    TIMEOUT 300)

if(SAMURAI_SANITIZE)
    if(SAMURAI_SANITIZE_LEAKS)
        set(SAMURAI_ASAN_OPTIONS "detect_leaks=1")
    else()
        set(SAMURAI_ASAN_OPTIONS "detect_leaks=0")
    endif()

    # print_stacktrace makes a UBSan report name the line it fired on; without
    # it the message says what happened but not where.
    set_property(TEST exotic APPEND PROPERTY ENVIRONMENT
        "ASAN_OPTIONS=${SAMURAI_ASAN_OPTIONS}:abort_on_error=0"
        "UBSAN_OPTIONS=print_stacktrace=1")
endif()
