#
## Test and example programs for the Samurai library
#

# ---------------------------------------------------------------------------
# Example / manual test programs
#
# These are interactive tools rather than automated tests, so they are built
# but not registered with CTest.
# ---------------------------------------------------------------------------

# The Bitzi tigertree implementation lives with this program rather than in
# the library: it is the reference the Samurai MerkleTree is compared against.
add_executable(testtiger
    applications/tiger/tiger.cpp
    applications/tiger/tigertree-bitzi.cpp)
add_executable(dnslookup  applications/dnslookup/dnslookup.cpp)
add_executable(netcat     applications/netcat/netcat.cpp)
add_executable(wget       applications/wget/wget.cpp)
add_executable(interfaces applications/net/interfaces.cpp)

# NOTE: upnplist needs all three translation units. The old Makefile listed
# only upnplist.cpp in test_SOURCES and relied on a separate variable for the
# other two.
add_executable(upnplist
    applications/upnplist/upnp.cpp
    applications/upnplist/upnpworker.cpp
    applications/upnplist/upnplist.cpp)

set(SAMURAI_TEST_APPS
    testtiger dnslookup netcat wget interfaces upnplist)

foreach(app IN LISTS SAMURAI_TEST_APPS)
    # samurai::samurai resolves to whichever library flavour is primary, so the
    # tests build whether or not the shared library is enabled.
    target_link_libraries(${app} PRIVATE samurai::samurai)
    set_target_properties(${app} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${app} PRIVATE -Wall -Wextra)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# The exotic suite (test/exotic/*.tcc)
#
# These are the real automated tests. They are built through exotic
# (https://github.com/janvidar/exotic), which is pinned as a submodule in
# third_party/exotic; a clone made without --recurse-submodules leaves that
# directory empty, so fall back to an installed package and skip the suite when
# neither is available.
# ---------------------------------------------------------------------------

if(EXISTS ${PROJECT_SOURCE_DIR}/third_party/exotic/CMakeLists.txt)
    add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/exotic
        ${CMAKE_CURRENT_BINARY_DIR}/exotic)
else()
    find_package(Exotic QUIET)
    if(NOT Exotic_FOUND)
        message(STATUS
            "Samurai: exotic not found - skipping the test/exotic suite. Run "
            "'git submodule update --init' to enable ~580 additional tests.")
        return()
    endif()
endif()

# MODE AUTO compiles each .tcc as its own translation unit and lets the tests
# register themselves, so nothing is generated and Perl is not needed.
exotic_add_tests(autotest
    LANGUAGE CXX
    SOURCES
        exotic/base32.tcc
        exotic/buffer.tcc
        exotic/buffer_binary.tcc
        exotic/compression.tcc
        exotic/file.tcc
        exotic/hash_tiger.tcc
        exotic/inetaddress.tcc
        exotic/interface.tcc
        exotic/misc.tcc
        exotic/multicast.tcc
        exotic/notifier.tcc
        exotic/resolver.tcc
        exotic/socketmonitor.tcc
        exotic/sockets.tcc
        exotic/string.tcc
        exotic/timer.tcc
        exotic/tls.tcc
        exotic/url.tcc
        exotic/xml.tcc
    LIBRARIES samurai::samurai
    INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}/exotic   # testkeys.h is included by name
        ${PROJECT_SOURCE_DIR}/src)           # socketmonitor.tcc drives the
                                             # backends, which are declared in a
                                             # private header

# file.tcc reaches its data/ fixtures through this directory rather than by a
# path relative to the working directory, so autotest can be run from anywhere.
target_compile_definitions(autotest PRIVATE
    EXOTIC_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/exotic")

set_target_properties(autotest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# The suite was the one target built without these, so a warning in a test went
# unseen while the library it exercises stayed clean.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(autotest PRIVATE -Wall -Wextra)
endif()

set_tests_properties(autotest PROPERTIES
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
    set_property(TEST autotest APPEND PROPERTY ENVIRONMENT
        "ASAN_OPTIONS=${SAMURAI_ASAN_OPTIONS}:abort_on_error=0"
        "UBSAN_OPTIONS=print_stacktrace=1")
endif()
