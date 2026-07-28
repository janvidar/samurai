/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <algorithm>
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

#include <vector>

#define MAXSOCK 1024

Samurai::IO::Net::SelectSocketMonitor::SelectSocketMonitor() : Samurai::IO::Net::SocketMonitor("select")
{
	max = std::min<size_t>(Samurai::OS::getMaxOpenSockets(), MAXSOCK);
	act.resize(max);
	for (size_t n = 0; n < max; n++)
	{
		act[n].fd = INVALID_SOCKET;
		act[n].trig = Samurai::IO::Net::SocketMonitor::Triggers::None;
	}
}


Samurai::IO::Net::SelectSocketMonitor::~SelectSocketMonitor() = default;

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
 * fd_sets living on this function's stack frame. Nothing bounds the descriptor
 * numbers: 'max' sizes the act[] array, not the values. A process whose
 * descriptor limit is above FD_SETSIZE can perfectly well be handed a socket
 * numbered 5000 while monitoring only a handful.
 *
 * select() cannot represent such a descriptor at all, so the only honest thing
 * is to leave it out and say so. The other backends have no such limit, and
 * this one is the last-resort fallback.
 */
static bool fd_fits_in_set(socket_t fd)
{
	return fd != INVALID_SOCKET && (long) fd >= 0 && (long) fd < (long) FD_SETSIZE;
}


/*
 * A descriptor closed behind the monitor's back makes select() fail with EBADF
 * for the whole call, which would stall every socket in the process until it is
 * identified and torn down.
 */
static bool fd_is_usable(socket_t fd)
{
	int type = 0;
	socklen_t len = sizeof(type);
	return SAMURAI_GETSOCKOPT(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0;
}


void Samurai::IO::Net::SelectSocketMonitor::internal_wait(int time_ms) {
	fd_set rfds;
	fd_set wfds;
	fd_set efds;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);

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

		const Samurai::IO::Net::SocketMonitor::Triggers trigger = sock->getMonitorTrigger();

		/* NOTE: not mutually exclusive: connect() and
		   toggleWriteNotifier(true) register a socket for read and write at
		   once, so both sets have to be set. */
		if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Write))  FD_SET(fd, &wfds);
		if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Read))   FD_SET(fd, &rfds);

		/* select()'s third set is out-of-band data, which is what Samurai::IO::Net::SocketMonitor::Triggers::Urgent
		   means. */
		if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Urgent)) FD_SET(fd, &efds);

		if (!any(trigger & (Samurai::IO::Net::SocketMonitor::Triggers::Read | Samurai::IO::Net::SocketMonitor::Triggers::Write | Samurai::IO::Net::SocketMonitor::Triggers::Urgent))) continue;

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

	int ret = ::select(maxfd + 1, &rfds, &wfds, &efds, &timeout);
	if (ret == 0) return;

	if (ret == -1)
	{
		if (NETERROR == EINTR) return;

		if (NETERROR == EBADF)
		{
			/* Report the unusable descriptors so their owners tear them down.
			   Collected first, because dispatching can modify 'sockets'. */
			std::vector<socket_t> bad;
			for (std::vector<SocketBase*>::iterator b = sockets.begin(); b != sockets.end(); b++)
			{
				const socket_t fd = (*b)->getFD();
				if (fd != INVALID_SOCKET && !fd_is_usable(fd)) bad.push_back(fd);
			}

			QERR("Select: %lu stale descriptor(s) in the set; reporting them as errors",
			     (unsigned long) bad.size());

			for (size_t n = 0; n < bad.size(); n++)
				dispatch(bad[n], Samurai::IO::Net::SocketMonitor::Triggers::Error);

			return;
		}

		QERR("Select error: %i, %s", NETERROR, strerror(NETERROR));
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

		Samurai::IO::Net::SocketMonitor::Triggers trig = Samurai::IO::Net::SocketMonitor::Triggers::None;
		if (FD_ISSET(fd, &wfds)) trig |= Samurai::IO::Net::SocketMonitor::Triggers::Write;
		if (FD_ISSET(fd, &rfds)) trig |= Samurai::IO::Net::SocketMonitor::Triggers::Read;
		if (FD_ISSET(fd, &efds)) trig |= Samurai::IO::Net::SocketMonitor::Triggers::Urgent;

		if (!any(trig)) continue;

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
