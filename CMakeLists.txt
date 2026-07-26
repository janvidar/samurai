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

# The old build defaulted to DEBUG=YES, which is what enables the QDBG/QERR
# tracing macros. Keep that default for single-config generators.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)

option(SAMURAI_BUILD_TESTS "Build the test and example programs" ON)

# The Makefile build produced libsamurai.so, so default to shared. Override
# with -DBUILD_SHARED_LIBS=OFF for a static libsamurai.a.
option(BUILD_SHARED_LIBS "Build samurai as a shared library" ON)

set(SAMURAI_TLS_PROVIDER "OPENSSL" CACHE STRING
    "TLS backend to use: OPENSSL, GNUTLS or NONE")
set_property(CACHE SAMURAI_TLS_PROVIDER PROPERTY STRINGS OPENSSL GNUTLS NONE)

set(SAMURAI_DNS_PROVIDER "BLOCKING" CACHE STRING
    "DNS resolver to use: BLOCKING, FORKED or BUILTIN")
set_property(CACHE SAMURAI_DNS_PROVIDER PROPERTY STRINGS BLOCKING FORKED BUILTIN)

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

if(SAMURAI_TLS_PROVIDER STREQUAL "OPENSSL")
    # 1.0.2 is the oldest release providing SSL_get0_param(), which the
    # hostname verification in tlsfactory-openssl.cpp relies on.
    find_package(OpenSSL 1.0.2 REQUIRED)
elseif(SAMURAI_TLS_PROVIDER STREQUAL "GNUTLS")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GNUTLS REQUIRED IMPORTED_TARGET gnutls)
    message(WARNING
        "The GnuTLS backend is unmaintained and is not expected to compile: "
        "it returns a TLS_STATUS_RETRY that is not in the TlsStatus enum and "
        "uses gnutls_*_set_priority() APIs removed in GnuTLS 3.x. "
        "Use SAMURAI_TLS_PROVIDER=OPENSSL unless you are porting it.")
elseif(SAMURAI_TLS_PROVIDER STREQUAL "NONE")
    message(STATUS "Samurai: building without TLS support")
else()
    message(FATAL_ERROR
        "Invalid SAMURAI_TLS_PROVIDER '${SAMURAI_TLS_PROVIDER}' "
        "(expected OPENSSL, GNUTLS or NONE)")
endif()

if(NOT SAMURAI_DNS_PROVIDER MATCHES "^(BLOCKING|FORKED|BUILTIN)$")
    message(FATAL_ERROR
        "Invalid SAMURAI_DNS_PROVIDER '${SAMURAI_DNS_PROVIDER}' "
        "(expected BLOCKING, FORKED or BUILTIN)")
endif()

if(NOT SAMURAI_DNS_PROVIDER STREQUAL "BLOCKING")
    message(WARNING
        "SAMURAI_DNS_PROVIDER=${SAMURAI_DNS_PROVIDER} selects a resolver that "
        "is not wired into the socket monitor and will not deliver results "
        "asynchronously. BLOCKING is the only working backend.")
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

# NOTE: src/media/** is deliberately absent; it was not part of the Makefile
# build either and does not currently compile.
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
    src/io/stream.cpp
    src/io/unicode.cpp
    src/io/net/bandwidth.cpp
    src/io/net/datagram.cpp
    src/io/net/hardwareaddress.cpp
    src/io/net/inetaddress.cpp
    src/io/net/interface.cpp
    src/io/net/multicast.cpp
    src/io/net/socket.cpp
    src/io/net/socketaddress.cpp
    src/io/net/socketbase.cpp
    src/io/net/serversocket.cpp
    src/io/net/socketmonitor.cpp
    src/io/net/socketmonitor-poll.cpp
    src/io/net/socketmonitor-epoll.cpp
    src/io/net/socketmonitor-select.cpp
    src/io/net/socketmonitor-kqueue.cpp
    src/io/net/tlsfactory.cpp
    src/io/net/tlsfactory-gnutls.cpp
    src/io/net/tlsfactory-openssl.cpp
    src/io/net/url.cpp
    src/io/net/dns/cache.cpp
    src/io/net/dns/dnsconfig.cpp
    src/io/net/dns/dnsmessage.cpp
    src/io/net/dns/dnsrrs.cpp
    src/io/net/dns/dnsutil.cpp
    src/io/net/dns/resolver.cpp
    src/io/net/dns/resolver-blocking.cpp
    src/io/net/dns/resolver-builtin.cpp
    src/io/net/dns/resolver-fork.cpp
    src/thread/thread.cpp
    src/thread/mutex.cpp
    src/util/base32.cpp
    src/util/bwestimation.cpp
    src/util/format.cpp
    src/util/random.cpp
    src/util/refcount.cpp
    src/crypto/digest/hash.cpp
    src/crypto/digest/hashvalue.cpp
    src/crypto/digest/merkletree.cpp
    src/crypto/digest/tiger.cpp
    src/crypto/digest/tigertree.cpp
    src/crypto/digest/tthsum.cpp)

add_library(samurai ${SAMURAI_SOURCES})
add_library(samurai::samurai ALIAS samurai)

target_include_directories(samurai
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(samurai PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    # NOTE: C++11 rather than a newer standard because io/device.h still
    # declares dynamic exception specifications - throw(Exception) - which
    # C++17 rejects outright.
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# NOTE: These have to be PUBLIC, not PRIVATE. SSL_SUPPORT gates a member of
# Samurai::IO::Net::Socket in the public header, and _FILE_OFFSET_BITS changes
# off_t, so a consumer compiled without them would disagree with the library
# about object layout.
target_compile_definitions(samurai
    PUBLIC
        _FILE_OFFSET_BITS=64
        $<$<CONFIG:Debug>:DEBUG>
    PRIVATE
        ZLIB
        BZIP2
        DNS_RESOLVE_${SAMURAI_DNS_PROVIDER})

target_link_libraries(samurai
    PUBLIC Threads::Threads
    PRIVATE ZLIB::ZLIB BZip2::BZip2)

if(SAMURAI_TLS_PROVIDER STREQUAL "OPENSSL")
    # PUBLIC: tlsfactory-openssl.h is a public header and includes
    # <openssl/ssl.h>.
    target_compile_definitions(samurai PUBLIC SSL_SUPPORT SSL_OPENSSL)
    target_link_libraries(samurai PUBLIC OpenSSL::SSL OpenSSL::Crypto)
elseif(SAMURAI_TLS_PROVIDER STREQUAL "GNUTLS")
    target_compile_definitions(samurai PUBLIC SSL_SUPPORT SSL_GNUTLS)
    target_link_libraries(samurai PUBLIC PkgConfig::GNUTLS)
endif()

if(WIN32)
    target_link_libraries(samurai PUBLIC ws2_32 iphlpapi)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(samurai PRIVATE -Wall -Wextra)
endif()

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

install(TARGETS samurai
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

# Record the configuration so that consumers get the same PUBLIC definitions
# and can resolve the same dependencies.
set(SAMURAI_EXPORTED_TLS_PROVIDER "${SAMURAI_TLS_PROVIDER}")

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
message(STATUS "  Shared library  : ${BUILD_SHARED_LIBS}")
message(STATUS "  TLS provider    : ${SAMURAI_TLS_PROVIDER}")
message(STATUS "  DNS provider    : ${SAMURAI_DNS_PROVIDER}")
message(STATUS "  Build tests     : ${SAMURAI_BUILD_TESTS}")
message(STATUS "")
