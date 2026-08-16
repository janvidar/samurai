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

add_executable(upnplist   applications/upnplist/upnplist.cpp)
add_executable(portmap    applications/portmap/portmap.cpp)

set(SAMURAI_TEST_APPS
    testtiger dnslookup netcat wget interfaces upnplist portmap)

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
# (https://github.com/janvidar/exotic), which this library does not carry a copy
# of: exotic is a dependency of samurai's tests and of QuickDC's both, and a copy
# owned by one of them is a copy the other has to reach inside for. So it is
# looked for in three places, in the order that costs least.
#
# A superproject that has already added exotic has defined the target, and that
# one is used. Adding the same source directory a second time would not produce
# a second library - it would collide on the target names and stop the configure.
# ---------------------------------------------------------------------------

# Where a standalone build looks: beside this checkout, which is the layout a
# superproject holding both of them produces anyway.
set(EXOTIC_SOURCE_DIR "${PROJECT_SOURCE_DIR}/../exotic" CACHE PATH
    "Where the exotic test framework is checked out, when it is not installed")

if(NOT TARGET Exotic::exotic)
    if(EXISTS ${EXOTIC_SOURCE_DIR}/CMakeLists.txt)
        add_subdirectory(${EXOTIC_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/exotic)
    else()
        find_package(Exotic QUIET)
        if(NOT Exotic_FOUND)
            message(STATUS
                "Samurai: exotic not found - skipping the test/exotic suite. Clone "
                "https://github.com/janvidar/exotic beside this checkout, or set "
                "EXOTIC_SOURCE_DIR, to enable the autotest suite.")
            return()
        endif()
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
        exotic/hash_sha.tcc
        exotic/hash_tiger.tcc
        exotic/http.tcc
        exotic/inetaddress.tcc
        exotic/interface.tcc
        exotic/json.tcc
        exotic/misc.tcc
        exotic/multicast.tcc
        exotic/notifier.tcc
        exotic/random.tcc
        exotic/resolver.tcc
        exotic/socketmonitor.tcc
        exotic/sockets.tcc
        exotic/socks5.tcc
        exotic/string.tcc
        exotic/timer.tcc
        exotic/tls.tcc
        exotic/upnp_discovery.tcc
        exotic/upnp_gateway.tcc
        exotic/upnp_soap.tcc
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
