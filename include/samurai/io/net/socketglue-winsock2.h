/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKET_PORTABLE_WINSOCK2_H
#define HAVE_SAMURAI_SOCKET_PORTABLE_WINSOCK2_H

/*
 * See Microsoft's reference on Winsock2:
 * http://msdn.microsoft.com/library/en-us/winsock/winsock/windows_sockets_start_page_2.asp
 *
 * NOTE: these two must not be wrapped in an extern "C" block. Both already
 * guard their own declarations with __cplusplus, and C linkage would be forced
 * on everything in the SDK include chain that is not inside those guards -
 * which is ill-formed for anything that is a template, an overload or a class
 * member.
 */

#include <winsock2.h>
#include <ws2tcpip.h>

#include <errno.h>

typedef SOCKET socket_t;
typedef int socklen_t;
typedef int interface_t;

/* shutdown()'s POSIX names. */
#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

/* MSVC's errno.h has these with POSIX values; a socket reports the WSA ones. */



// #define EINTR           WSAEINTR
// #define EACCES          WSAEACCES
// #define EFAULT          WSAEFAULT
// #define EINVAL          WSAEINVAL
// #define EMFILE          WSAEMFILE
#undef EWOULDBLOCK
#define EWOULDBLOCK     WSAEWOULDBLOCK
#undef EINPROGRESS
#define EINPROGRESS     WSAEINPROGRESS
#undef EALREADY
#define EALREADY        WSAEALREADY
#undef ENOTSOCK
#define ENOTSOCK        WSAENOTSOCK
#undef EDESTADDRREQ
#define EDESTADDRREQ    WSAEDESTADDRREQ
#undef EMSGSIZE
#define EMSGSIZE        WSAEMSGSIZE
#undef EPROTOTYPE
#define EPROTOTYPE      WSAEPROTOTYPE
#undef ENOPROTOOPT
#define ENOPROTOOPT     WSAENOPROTOOPT
#undef EPROTONOSUPPORT
#define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#undef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT
#undef EOPNOTSUPP
#define EOPNOTSUPP      WSAEOPNOTSUPP
#undef EPFNOSUPPORT
#define EPFNOSUPPORT    WSAEPFNOSUPPORT
#undef EAFNOSUPPORT
#define EAFNOSUPPORT    WSAEAFNOSUPPORT
#undef EADDRINUSE
#define EADDRINUSE      WSAEADDRINUSE
#undef EADDRNOTAVAIL
#define EADDRNOTAVAIL   WSAEADDRNOTAVAIL
#undef ENETDOWN
#define ENETDOWN        WSAENETDOWN
#undef ENETUNREACH
#define ENETUNREACH     WSAENETUNREACH
#undef ENETRESET
#define ENETRESET       WSAENETRESET
#undef ECONNABORTED
#define ECONNABORTED    WSAECONNABORTED
#undef ECONNRESET
#define ECONNRESET      WSAECONNRESET
#undef ENOBUFS
#define ENOBUFS         WSAENOBUFS
#undef EISCONN
#define EISCONN         WSAEISCONN
#undef ENOTCONN
#define ENOTCONN        WSAENOTCONN
#undef ESHUTDOWN
#define ESHUTDOWN       WSAESHUTDOWN
#undef ETOOMANYREFS
#define ETOOMANYREFS    WSAETOOMANYREFS
#undef ETIMEDOUT
#define ETIMEDOUT       WSAETIMEDOUT
#undef ECONNREFUSED
#define ECONNREFUSED    WSAECONNREFUSED
#undef ELOOP
#define ELOOP           WSAELOOP
// #define ENAMETOOLONG    WSAENAMETOOLONG
#undef EHOSTDOWN
#define EHOSTDOWN       WSAEHOSTDOWN
#undef EHOSTUNREACH
#define EHOSTUNREACH    WSAEHOSTUNREACH
// #define ENOTEMPTY       WSAENOTEMPTY
#undef EPROCLIM
#define EPROCLIM        WSAEPROCLIM
#undef EUSERS
#define EUSERS          WSAEUSERS
#undef EDQUOT
#define EDQUOT          WSAEDQUOT
#undef ESTALE
#define ESTALE          WSAESTALE
#undef EREMOTE
#define EREMOTE         WSAEREMOTE

#endif // HAVE_SAMURAI_SOCKET_PORTABLE_WINSOCK2_H

