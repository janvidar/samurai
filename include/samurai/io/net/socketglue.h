/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */
#ifndef HAVE_QUICKDC_SOCKET_PORTABLE_H
#define HAVE_QUICKDC_SOCKET_PORTABLE_H

#include <samurai/samurai.h>

#ifdef SAMURAI_OS_WINDOWS
#define SAMURAI_WINSOCK
#endif

#define MAXSOCKETS 1024
#define DEFAULT_CHUNK_SIZE 8192
#define CONNECT_TIMEOUT 30

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
			struct __InternalAddress {
				union __internal_union {
					struct in_addr in;  // use: s_addr
					struct in6_addr in6; // use: s6_addr, s6_addr16 or s6_addr32

#if defined(SAMURAI_BSD)
#define X_IP4_32 data->internal.in.s_addr
#define X_IP6_32 data->internal.in6.__u6_addr.__u6_addr32 /*  4 of these */
#define X_IP6_16 data->internal.in6.__u6_addr.__u6_addr16 /*  8 of these */
#define X_IP6_08 data->internal.in6.__u6_addr.__u6_addr8  /* 16 of these */
#endif

#if defined(SAMURAI_WINSOCK) || defined(SAMURAI_OS_SOLARIS)
#define X_IP4_32 data->internal.in.s_addr
#define X_IP6_32 data->internal.in6._S6_un._S6_u32 /*  4 of these */
#define X_IP6_16 data->internal.in6._S6_un._S6_u16 /*  8 of these */
#define X_IP6_08 data->internal.in6._S6_un._S6_u8  /* 16 of these */
#endif

#if defined(SAMURAI_OS_LINUX)
#define X_IP4_32 data->internal.in.s_addr
#define X_IP6_32 data->internal.in6.s6_addr32 /*  4 of these */
#define X_IP6_16 data->internal.in6.s6_addr16 /*  8 of these */
#define X_IP6_08 data->internal.in6.s6_addr  /* 16 of these */
#endif
				} internal;
			};
		}
	}
}


#endif // HAVE_QUICKDC_SOCKET_PORTABLE_H


