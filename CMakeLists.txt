#
## CMake build for the Samurai library
## Copyright (c) 2001-2009, Jan Vidar Krey
#

cmake_minimum_required(VERSION 3.16)

project(samurai
    VERSION 0.1.0
    DESCRIPTION "Portable C++ platform abstraction library"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)

option(SAMURAI_BUILD_TESTS "Build the test and example programs" ON)

# The old build defaulted to DEBUG=YES, which is what the QDBG/QERR macros in
# debug/dbg.h compile against. That used to be tied to a Debug build, so the
# build type chosen for symbols and sanitizers decided whether the tracing
# subsystem was compiled in as well - and it writes whatever it is given,
# including a consumer's secrets, to files. The two are separate questions now,
# and this one answers no unless asked.
option(SAMURAI_TRACE
    "Compile in the QDBG/QNET/QHUB tracing subsystem (writes plaintext logs)" OFF)

option(SAMURAI_SANITIZE
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# ThreadSanitizer needs its own build: it cannot be combined with
# AddressSanitizer, and it instruments every memory access, so a run is slower
# than an ASan one rather than comparable to it.
option(SAMURAI_SANITIZE_THREAD
    "Build with ThreadSanitizer (cannot be combined with SAMURAI_SANITIZE)" OFF)

if(SAMURAI_SANITIZE AND SAMURAI_SANITIZE_THREAD)
    message(FATAL_ERROR
        "SAMURAI_SANITIZE and SAMURAI_SANITIZE_THREAD are mutually exclusive; "
        "AddressSanitizer and ThreadSanitizer cannot instrument the same build. "
        "Configure two build directories instead.")
endif()

# LeakSanitizer rides along with AddressSanitizer. Turn it off if the ptrace
# restrictions on the host make stopping the world too slow to be useful.
#
# Darwin has no LeakSanitizer at all: asking for it there makes the process
# print "detect_leaks is not supported on this platform" and abort before it
# runs a single test, so a sanitized ctest run fails for that reason alone.
if(APPLE)
    set(SAMURAI_SANITIZE_LEAKS_DEFAULT OFF)
else()
    set(SAMURAI_SANITIZE_LEAKS_DEFAULT ON)
endif()

option(SAMURAI_SANITIZE_LEAKS
    "Enable LeakSanitizer as well (requires SAMURAI_SANITIZE)"
    ${SAMURAI_SANITIZE_LEAKS_DEFAULT})

if(SAMURAI_SANITIZE AND SAMURAI_SANITIZE_LEAKS AND APPLE)
    message(WARNING
        "SAMURAI_SANITIZE_LEAKS is on, but Darwin has no LeakSanitizer; the "
        "test run will abort on startup. Use 'leaks' or a Linux host instead.")
endif()

# The Makefile build had rules for both libsamurai.so and libsamurai.a, so
# build both by default. They share a single compile pass.
#
# -DBUILD_SHARED_LIBS=OFF is still honoured as a request for a static-only
# build, since that is what it meant before these two options existed.
if(DEFINED BUILD_SHARED_LIBS AND NOT BUILD_SHARED_LIBS)
    set(SAMURAI_SHARED_DEFAULT OFF)
else()
    set(SAMURAI_SHARED_DEFAULT ON)
endif()

option(SAMURAI_BUILD_SHARED "Build the shared library (libsamurai.so)" ${SAMURAI_SHARED_DEFAULT})
option(SAMURAI_BUILD_STATIC "Build the static library (libsamurai.a)" ON)

if(NOT SAMURAI_BUILD_SHARED AND NOT SAMURAI_BUILD_STATIC)
    message(FATAL_ERROR
        "Nothing to build: enable at least one of SAMURAI_BUILD_SHARED or "
        "SAMURAI_BUILD_STATIC.")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# NOTE: io/compression.cpp includes <zlib.h> and <bzlib.h> unconditionally,
# so these are hard requirements. The old build's ZLIB_SUPPORT/BZIP2_SUPPORT
# switches only dropped the -l flags and could not actually disable either.
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# TLS is mandatory, and there is only one backend: OpenSSL 3.0 or newer, or
# LibreSSL 3.5 or newer, which provides the same API subset.
find_package(OpenSSL REQUIRED)

# FindOpenSSL derives OPENSSL_VERSION from the OPENSSL_VERSION_NUMBER macro,
# which LibreSSL hardcodes to 0x20000000L - so every LibreSSL, old or new,
# reports itself as "OpenSSL 2.0.0" and cannot be version checked as OpenSSL.
# Its real version is in LIBRESSL_VERSION_TEXT.
set(SAMURAI_LIBRESSL_VERSION "")
foreach(dir IN LISTS OPENSSL_INCLUDE_DIR)
    if(NOT SAMURAI_LIBRESSL_VERSION AND EXISTS "${dir}/openssl/opensslv.h")
        file(STRINGS "${dir}/openssl/opensslv.h" libressl_version_str
            REGEX "^#[\t ]*define[\t ]+LIBRESSL_VERSION_TEXT[\t ]+\"LibreSSL [0-9.]+\"")
        if(libressl_version_str)
            string(REGEX REPLACE "^.*\"LibreSSL ([0-9.]+)\".*$" "\\1"
                SAMURAI_LIBRESSL_VERSION "${libressl_version_str}")
        endif()
    endif()
endforeach()

if(SAMURAI_LIBRESSL_VERSION)
    set(SAMURAI_TLS_NAME "LibreSSL ${SAMURAI_LIBRESSL_VERSION}")
    # 3.5 is the oldest series with TLS 1.3 on both the client and the server
    # side, and with the X509_VERIFY_PARAM hostname APIs the verification in
    # tlsfactory-openssl.cpp relies on.
    if(SAMURAI_LIBRESSL_VERSION VERSION_LESS 3.5.0)
        message(FATAL_ERROR
            "LibreSSL 3.5.0 or newer is required, found "
            "${SAMURAI_LIBRESSL_VERSION} in ${OPENSSL_INCLUDE_DIR}.")
    endif()
else()
    set(SAMURAI_TLS_NAME "OpenSSL ${OPENSSL_VERSION}")
    if(OPENSSL_VERSION VERSION_LESS 3.0.0)
        message(FATAL_ERROR
            "OpenSSL 3.0 or newer is required, found ${OPENSSL_VERSION} in "
            "${OPENSSL_INCLUDE_DIR}. Point OPENSSL_ROOT_DIR at a newer "
            "installation, or use LibreSSL 3.5 or newer.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

set(SAMURAI_SOURCES
    src/os.cpp
    src/messagehandler.cpp
    src/stdc.cpp
    src/timer.cpp
    src/timestamp.cpp
    src/debug/dbg.cpp
    src/io/buffer.cpp
    src/io/codec.cpp
    src/io/compression.cpp
    src/io/dir.cpp
    src/io/file.cpp
    src/io/unicode.cpp
    src/io/xml.cpp
    src/io/net/bandwidth.cpp
    src/io/net/datagram.cpp
    src/io/net/hardwareaddress.cpp
    src/io/net/inetaddress.cpp
    src/io/net/interface.cpp
    src/io/net/multicast.cpp
    src/io/net/notifier.cpp
    src/io/net/proxy.cpp
    src/io/net/socket.cpp
    src/io/net/socketaddress.cpp
    src/io/net/socketbase.cpp
    src/io/net/serversocket.cpp
    src/io/net/socketmonitor.cpp
    src/io/net/socketmonitor-poll.cpp
    src/io/net/socketmonitor-epoll.cpp
    src/io/net/socketmonitor-select.cpp
    src/io/net/socketmonitor-kqueue.cpp
    src/io/net/socks5.cpp
    src/io/net/tlsfactory.cpp
    src/io/net/tlsfactory-openssl.cpp
    src/io/net/url.cpp
    src/io/net/dns/resolver.cpp
    src/io/net/dns/resolver-pool.cpp
    src/io/net/dns/resolver-socks.cpp
    src/io/net/http/client.cpp
    src/io/net/http/response.cpp
    src/io/net/upnp/description.cpp
    src/io/net/upnp/error.cpp
    src/io/net/upnp/gateway.cpp
    src/io/net/upnp/portmap.cpp
    src/io/net/upnp/portmapper.cpp
    src/io/net/upnp/soap.cpp
    src/io/net/upnp/ssdp.cpp
    src/util/base32.cpp
    src/util/bwestimation.cpp
    src/util/format.cpp
    src/util/random.cpp
    src/util/string.cpp
    src/crypto/digest/hash.cpp
    src/crypto/digest/hashvalue.cpp
    src/crypto/digest/merkletree.cpp
    src/crypto/digest/tiger.cpp)

# The sources are compiled once into an object library, and the enabled
# library flavours are then assembled from those objects. The objects are
# position independent so that the same ones serve the shared library and a
# static library that can still be linked into a PIE.
add_library(samurai_objects OBJECT ${SAMURAI_SOURCES})

set(SAMURAI_LIBRARIES "")

if(SAMURAI_BUILD_SHARED)
    add_library(samurai SHARED $<TARGET_OBJECTS:samurai_objects>)
    set_target_properties(samurai PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
    list(APPEND SAMURAI_LIBRARIES samurai)
endif()

if(SAMURAI_BUILD_STATIC)
    add_library(samurai_static STATIC $<TARGET_OBJECTS:samurai_objects>)
    add_library(samurai::samurai_static ALIAS samurai_static)
    # Produce libsamurai.a rather than libsamurai_static.a. Not done on
    # Windows, where the shared library's import library would collide with it.
    if(NOT WIN32)
        set_target_properties(samurai_static PROPERTIES OUTPUT_NAME samurai)
    endif()
    list(APPEND SAMURAI_LIBRARIES samurai_static)
endif()

# samurai::samurai is whichever flavour is the primary one, so that in-tree
# consumers and the tests do not have to care which are enabled.
if(SAMURAI_BUILD_SHARED)
    add_library(samurai::samurai ALIAS samurai)
else()
    add_library(samurai::samurai ALIAS samurai_static)
endif()

set(SAMURAI_SANITIZE_FLAGS "")
if(SAMURAI_SANITIZE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "SAMURAI_SANITIZE needs GCC or Clang, not ${CMAKE_CXX_COMPILER_ID}")
    endif()

    # -fno-sanitize-recover: UBSan otherwise prints the diagnostic and carries
    # on, so a run that reported undefined behaviour would still exit 0 and
    # ctest would call it a pass.
    set(SAMURAI_SANITIZE_FLAGS
        -fsanitize=address,undefined
        -fno-sanitize-recover=address,undefined
        -fno-omit-frame-pointer
        -g)

    # Optimisation is what makes a sanitized run bearable, but at -O0 the
    # traces are the ones worth reading. Leave the build type alone and just
    # say what was chosen.
    message(STATUS "Sanitizers enabled (address, undefined)")
endif()

if(SAMURAI_SANITIZE_THREAD)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "SAMURAI_SANITIZE_THREAD needs GCC or Clang, not ${CMAKE_CXX_COMPILER_ID}")
    endif()

    # -fno-sanitize-recover, as above: a reported race must fail the run rather
    # than print and continue to a zero exit.
    set(SAMURAI_SANITIZE_FLAGS
        -fsanitize=thread
        -fno-sanitize-recover=thread
        -fno-omit-frame-pointer
        -g)

    message(STATUS "Sanitizers enabled (thread)")
endif()

# Applied to the object library as well as to the finished libraries: the
# objects need the include paths and definitions in order to compile, and the
# libraries need them as usage requirements for consumers.
foreach(target IN ITEMS samurai_objects ${SAMURAI_LIBRARIES})
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

    # CXX_STANDARD is not exported, so consumers of the installed package would
    # not inherit it. The compile feature is a usage requirement and is.
    target_compile_features(${target} PUBLIC cxx_std_20)

    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON)

    # NOTE: These have to be PUBLIC, not PRIVATE. _FILE_OFFSET_BITS changes
    # off_t, so a consumer compiled without it would disagree with the library
    # about object layout.
    # DEBUG is PUBLIC because dbg.h is a public header and a consumer that
    # disagreed about it would call sinks the library did not compile.
    target_compile_definitions(${target}
        PUBLIC
            _FILE_OFFSET_BITS=64
            $<$<BOOL:${SAMURAI_TRACE}>:DEBUG>
        PRIVATE
            ZLIB
            BZIP2)

    # Linking these to the object library contributes no libraries to it, but
    # does give it their include directories to compile against.
    # PUBLIC for OpenSSL: tlsfactory-openssl.h is a public header and includes
    # <openssl/ssl.h>.
    target_link_libraries(${target}
        PUBLIC Threads::Threads OpenSSL::SSL OpenSSL::Crypto
        PRIVATE ZLIB::ZLIB BZip2::BZip2)

    # bcrypt supplies BCryptGenRandom, which is where Util::secureRandom() gets
    # its bytes on Windows.
    if(WIN32)
        target_link_libraries(${target} PUBLIC ws2_32 iphlpapi bcrypt)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()

    # PUBLIC, and on the link line as well as the compile line: a consumer that
    # is not built with the same -fsanitize= set fails to link against a
    # sanitized library with undefined references to __asan_* / __ubsan_* /
    # __tsan_*.
    if(SAMURAI_SANITIZE OR SAMURAI_SANITIZE_THREAD)
        target_compile_options(${target} PUBLIC ${SAMURAI_SANITIZE_FLAGS})
        target_link_options(${target} PUBLIC ${SAMURAI_SANITIZE_FLAGS})
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(SAMURAI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS ${SAMURAI_LIBRARIES}
    EXPORT samuraiTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/samurai
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")

install(EXPORT samuraiTargets
    FILE samuraiTargets.cmake
    NAMESPACE samurai::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/samurai)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/samuraiConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/samuraiConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/samurai)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/samuraiConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/samuraiConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/samuraiConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/samurai)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "Samurai ${PROJECT_VERSION} configured:")
message(STATUS "  Build type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared library  : ${SAMURAI_BUILD_SHARED}")
message(STATUS "  Static library  : ${SAMURAI_BUILD_STATIC}")
message(STATUS "  TLS             : ${SAMURAI_TLS_NAME}")
message(STATUS "  TLS versions    : 1.2 minimum, 1.3 preferred")
message(STATUS "  DNS resolver    : thread pool")
message(STATUS "  Build tests     : ${SAMURAI_BUILD_TESTS}")
message(STATUS "  Tracing (DEBUG) : ${SAMURAI_TRACE}")
if(SAMURAI_SANITIZE)
    message(STATUS "  Sanitizers      : address, undefined")
    message(STATUS "  Leak detection  : ${SAMURAI_SANITIZE_LEAKS}")
elseif(SAMURAI_SANITIZE_THREAD)
    message(STATUS "  Sanitizers      : thread")
else()
    message(STATUS "  Sanitizers      : OFF")
endif()
message(STATUS "")
