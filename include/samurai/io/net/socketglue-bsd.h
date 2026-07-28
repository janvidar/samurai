/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKET_PORTABLE_BSDSOCKETS_H
#define HAVE_SAMURAI_SOCKET_PORTABLE_BSDSOCKETS_H

#include <samurai/samurai.h>

#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#ifdef SAMURAI_BSD
#include <net/if_dl.h>
#include <ifaddrs.h>
#endif

#define SOCKET_ERROR -1
#define INVALID_SOCKET -1

typedef int socket_t;
typedef unsigned int interface_t;

#endif // HAVE_SAMURAI_SOCKET_PORTABLE_BSDSOCKETS_H


