/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_SOCKET_PORTABLE_WINSOCK2_H
#define HAVE_QUICKDC_SOCKET_PORTABLE_WINSOCK2_H

/*
 * See Microsoft's reference on Winsock2:
 * http://msdn.microsoft.com/library/en-us/winsock/winsock/windows_sockets_start_page_2.asp
 */

extern "C" {
#include <winsock2.h>
#include <ws2tcpip.h>
}

typedef SOCKET socket_t;
typedef int socklen_t;
typedef int interface_t;

#define SOCKET_CLOSE(x) closesocket(x)
#define SENDTO_CAST_PREFIX (char*)
#define NETERROR WSAGetLastError()

#define SAMURAI_GETSOCKOPT(SD, LEV, OPT, VAL, LEN) getsockopt(SD, LEV, OPT, (char*) VAL, LEN)
#define SAMURAI_SETSOCKOPT(SD, LEV, OPT, VAL, LEN) setsockopt(SD, LEV, OPT, (const char*) VAL, LEN)

// #define EINTR           WSAEINTR
// #define EACCES          WSAEACCES
// #define EFAULT          WSAEFAULT
// #define EINVAL          WSAEINVAL
// #define EMFILE          WSAEMFILE
#define EWOULDBLOCK     WSAEWOULDBLOCK
#define EINPROGRESS     WSAEINPROGRESS
#define EALREADY        WSAEALREADY
#define ENOTSOCK        WSAENOTSOCK
#define EDESTADDRREQ    WSAEDESTADDRREQ
#define EMSGSIZE        WSAEMSGSIZE
#define EPROTOTYPE      WSAEPROTOTYPE
#define ENOPROTOOPT     WSAENOPROTOOPT
#define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT
#define EOPNOTSUPP      WSAEOPNOTSUPP
#define EPFNOSUPPORT    WSAEPFNOSUPPORT
#define EAFNOSUPPORT    WSAEAFNOSUPPORT
#define EADDRINUSE      WSAEADDRINUSE
#define EADDRNOTAVAIL   WSAEADDRNOTAVAIL
#define ENETDOWN        WSAENETDOWN
#define ENETUNREACH     WSAENETUNREACH
#define ENETRESET       WSAENETRESET
#define ECONNABORTED    WSAECONNABORTED
#define ECONNRESET      WSAECONNRESET
#define ENOBUFS         WSAENOBUFS
#define EISCONN         WSAEISCONN
#define ENOTCONN        WSAENOTCONN
#define ESHUTDOWN       WSAESHUTDOWN
#define ETOOMANYREFS    WSAETOOMANYREFS
#define ETIMEDOUT       WSAETIMEDOUT
#define ECONNREFUSED    WSAECONNREFUSED
#define ELOOP           WSAELOOP
// #define ENAMETOOLONG    WSAENAMETOOLONG
#define EHOSTDOWN       WSAEHOSTDOWN
#define EHOSTUNREACH    WSAEHOSTUNREACH
// #define ENOTEMPTY       WSAENOTEMPTY
#define EPROCLIM        WSAEPROCLIM
#define EUSERS          WSAEUSERS
#define EDQUOT          WSAEDQUOT
#define ESTALE          WSAESTALE
#define EREMOTE         WSAEREMOTE

#endif // HAVE_QUICKDC_SOCKET_PORTABLE_WINSOCK2_H

