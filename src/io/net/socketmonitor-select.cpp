/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include "socketmonitor-backend.h"

#ifdef SOCKET_NOTIFY_SELECT

#include <samurai/os.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/datagram.h>

#define MAXSOCK 1024

Samurai::IO::Net::SelectSocketMonitor::SelectSocketMonitor() : Samurai::IO::Net::SocketMonitor("select")
{
	max = MIN(Samurai::OS::getMaxOpenSockets(), MAXSOCK);
	act = new struct poll_act[max];
	for (size_t n = 0; n < max; n++)
	{
		act[n].fd = INVALID_SOCKET;
		act[n].trig = 0;
	}
}


Samurai::IO::Net::SelectSocketMonitor::~SelectSocketMonitor()
{
	delete[] act;
}

bool Samurai::IO::Net::SelectSocketMonitor::isValid()
{
	return true;
}


void Samurai::IO::Net::SelectSocketMonitor::internal_add(Samurai::IO::Net::SocketBase* socket)
{
	sockets.push_back(socket);
}


void Samurai::IO::Net::SelectSocketMonitor::internal_remove(Samurai::IO::Net::SocketBase* socket) {
	std::vector<SocketBase*>::iterator it = sockets.begin();
	for (; it != sockets.end(); it++)
	{
		SocketBase* sock = (*it);
		if (sock == socket)
		{
			sockets.erase(it);
			return;
		}
	}
}


void Samurai::IO::Net::SelectSocketMonitor::internal_modify(Samurai::IO::Net::SocketBase*)
{
}


/*
 * NOTE: fd_set is a fixed size bitmap of FD_SETSIZE bits, and FD_SET() on a
 * descriptor at or above that limit writes past the end of it - here, past two
 * fd_sets living on this function's stack frame. Nothing bounded the
 * descriptor numbers: 'max' sizes the act[] array, not the values. A process
 * whose descriptor limit is above FD_SETSIZE can perfectly well be handed a
 * socket numbered 5000 while monitoring only a handful, so this was reachable
 * without anything unusual happening.
 *
 * select() cannot represent such a descriptor at all, so the only honest thing
 * is to leave it out and say so. The other backends have no such limit, and
 * this one is the last-resort fallback.
 */
static bool fd_fits_in_set(socket_t fd)
{
	return fd != INVALID_SOCKET && (long) fd >= 0 && (long) fd < (long) FD_SETSIZE;
}


void Samurai::IO::Net::SelectSocketMonitor::wait(int time_ms) {
	fd_set rfds;
	fd_set wfds;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	socket_t maxfd = INVALID_SOCKET;
	size_t skipped = 0;

	std::vector<SocketBase*>::iterator it = sockets.begin();
	for (; it != sockets.end(); it++) {
		SocketBase* sock = (*it);
		const socket_t fd = sock->getFD();

		if (!fd_fits_in_set(fd))
		{
			skipped++;
			continue;
		}

		const int trigger = sock->getMonitorTrigger();

		/* NOTE: these were 'if (MWrite) ... else if (MRead)', so a socket
		   registered for both - which is what connect() and
		   toggleWriteNotifier(true) do - was never placed in rfds and its
		   reads went unreported for as long as the write notifier was on. */
		if (trigger & MWrite) FD_SET(fd, &wfds);
		if (trigger & MRead)  FD_SET(fd, &rfds);

		if (!(trigger & (MRead | MWrite))) continue;

		if (maxfd == INVALID_SOCKET || maxfd < fd)
			maxfd = fd;
	}

	if (skipped)
	{
		QERR("select(): %lu socket(s) have a descriptor >= FD_SETSIZE (%d) and "
		     "cannot be monitored; use the poll or epoll backend",
		     (unsigned long) skipped, (int) FD_SETSIZE);
	}

	struct ::timeval timeout;
	timeout.tv_sec = (time_ms / 1000);
	timeout.tv_usec = ((time_ms % 1000) * 1000);

	int ret = ::select(maxfd + 1, &rfds, &wfds, 0, &timeout);
	if (ret == 0) return;

	if (ret == -1)
	{
		if (NETERROR != EINTR)
		{
			QERR("Select error: %i, %s", NETERROR, strerror(NETERROR));
		}
		return;
	}

	size_t act_num = 0;
	it = sockets.begin();
	for (; it != sockets.end(); it++)
	{
		SocketBase* sock = (*it);
		const socket_t fd = sock->getFD();

		/* Same bound as above: FD_ISSET reads the bitmap too. */
		if (!fd_fits_in_set(fd)) continue;

		int trig = 0;
		if (FD_ISSET(fd, &wfds)) trig |= MWrite;
		if (FD_ISSET(fd, &rfds)) trig |= MRead;

		if (!trig) continue;

		if (act_num == max)
		{
			QERR("select(): more than %lu ready sockets, deferring the rest",
			     (unsigned long) max);
			break;
		}

		act[act_num].fd = fd;
		act[act_num].trig = trig;
		act_num++;
	}

	for (size_t n = 0; n < act_num; n++)
		dispatch(act[n].fd, act[n].trig);
}

#endif // SOCKET_NOTIFY_SELECT
