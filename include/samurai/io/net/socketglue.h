/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */
#ifndef HAVE_SAMURAI_SOCKET_PORTABLE_H
#define HAVE_SAMURAI_SOCKET_PORTABLE_H

#include <span>
#include <stdint.h>

#include <samurai/samurai.h>

#ifdef SAMURAI_OS_WINDOWS
#define SAMURAI_WINSOCK
#endif

namespace Samurai {
namespace IO {
namespace Net {
inline constexpr size_t MAXSOCKETS = 1024;
inline constexpr size_t DEFAULT_CHUNK_SIZE = 8192;
inline constexpr int CONNECT_TIMEOUT = 30;
}
}
}

#ifdef SAMURAI_WINSOCK
#include <samurai/io/net/socketglue-winsock2.h>
#endif

#ifdef SAMURAI_POSIX
#include <samurai/io/net/socketglue-bsd.h>
#endif

#ifdef SAMURAI_OS_LINUX
# define SAMURAI_SENDFLAGS MSG_NOSIGNAL
#else
# if defined(SAMURAI_BSD)
#  define SAMURAI_SENDFLAGS SO_NOSIGPIPE
# else
#  define SAMURAI_SENDFLAGS 0
# endif
#endif // SAMURAI_OS_LINUX

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

namespace Samurai {
	namespace IO {
		namespace Net {
			/**
			 * Storage for either an IPv4 or an IPv6 address.
			 *
			 * The accessors below replace a set of macros that expanded to
			 * 'data->internal.in6...', which meant they only compiled where the
			 * caller happened to have a variable called 'data'. The platform
			 * differences in how in6_addr names its members live inside these
			 * three functions instead of at every use site.
			 */
			struct __InternalAddress {
				union __internal_union {
					struct in_addr in;  // use: s_addr
					struct in6_addr in6; // use: s6_addr, s6_addr16 or s6_addr32
				} internal;

				uint32_t& ipv4() { return internal.in.s_addr; }
				uint32_t ipv4() const { return internal.in.s_addr; }

#if defined(SAMURAI_BSD)
#define SAMURAI_IN6_32 internal.in6.__u6_addr.__u6_addr32
#define SAMURAI_IN6_16 internal.in6.__u6_addr.__u6_addr16
#define SAMURAI_IN6_08 internal.in6.__u6_addr.__u6_addr8
#elif defined(SAMURAI_WINSOCK) || defined(SAMURAI_OS_SOLARIS)
#define SAMURAI_IN6_32 internal.in6._S6_un._S6_u32
#define SAMURAI_IN6_16 internal.in6._S6_un._S6_u16
#define SAMURAI_IN6_08 internal.in6._S6_un._S6_u8
#elif defined(SAMURAI_OS_LINUX)
#define SAMURAI_IN6_32 internal.in6.s6_addr32
#define SAMURAI_IN6_16 internal.in6.s6_addr16
#define SAMURAI_IN6_08 internal.in6.s6_addr
#else
#error "No in6_addr member layout is known for this platform"
#endif

				/** The IPv6 address as 4 words, 8 shorts or 16 bytes. */
				std::span<uint32_t, 4>  ipv6_32()       { return std::span<uint32_t, 4>(reinterpret_cast<uint32_t*>(&SAMURAI_IN6_32[0]), 4); }
				std::span<const uint32_t, 4> ipv6_32() const { return std::span<const uint32_t, 4>(reinterpret_cast<const uint32_t*>(&SAMURAI_IN6_32[0]), 4); }
				std::span<uint16_t, 8>  ipv6_16()       { return std::span<uint16_t, 8>(reinterpret_cast<uint16_t*>(&SAMURAI_IN6_16[0]), 8); }
				std::span<const uint16_t, 8> ipv6_16() const { return std::span<const uint16_t, 8>(reinterpret_cast<const uint16_t*>(&SAMURAI_IN6_16[0]), 8); }
				std::span<uint8_t, 16>  ipv6_08()       { return std::span<uint8_t, 16>(reinterpret_cast<uint8_t*>(&SAMURAI_IN6_08[0]), 16); }
				std::span<const uint8_t, 16> ipv6_08() const { return std::span<const uint8_t, 16>(reinterpret_cast<const uint8_t*>(&SAMURAI_IN6_08[0]), 16); }

#undef SAMURAI_IN6_32
#undef SAMURAI_IN6_16
#undef SAMURAI_IN6_08
			};
		}
	}
}


#endif // HAVE_SAMURAI_SOCKET_PORTABLE_H


