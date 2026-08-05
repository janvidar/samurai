/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include "socketmonitor-backend.h"

#ifdef SOCKET_NOTIFY_EPOLL

#include <samurai/samurai.h>
#include <samurai/os.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/datagram.h>

#include <sys/epoll.h>
#include <stdlib.h>

/*
 * How many ready descriptors one epoll_wait() may report, which is not the
 * same thing as how many sockets may be monitored - epoll has no limit on the
 * latter and reports whatever does not fit on the next call. Anything left over
 * is picked up immediately, because the descriptors stay ready.
 */
#define EPOLL_BATCH 256

Samurai::IO::Net::EPollSocketMonitor::EPollSocketMonitor() : Samurai::IO::Net::SocketMonitor("epoll")
{
	max = Samurai::OS::getMaxOpenSockets();
	num = 0;
	act.assign(EPOLL_BATCH, epoll_event{});
	epfd = epoll_create(max);
}

Samurai::IO::Net::EPollSocketMonitor::~EPollSocketMonitor()
{
	close(epfd);
}

bool Samurai::IO::Net::EPollSocketMonitor::isValid()
{
	if (getenv("SAMURAI_NO_EPOLL"))
	{
		return false;
	}
	
	if (epfd == -1)
		return false;
	
	return true;
}


/*
 * NOTE: the two sides are not symmetric. 'trigger' is a Triggers bitmask, so it
 * is tested with any(); 'handle->events' is the kernel's own uint32_t of EPOLL*
 * bits, which is tested with a plain '&'.
 */
static void set_poll_events(struct epoll_event* handle, Samurai::IO::Net::SocketMonitor::Triggers trigger)
{
	memset(handle, 0, sizeof(struct epoll_event));

	if (any(trigger & (Samurai::IO::Net::SocketMonitor::Triggers::Read
	                 | Samurai::IO::Net::SocketMonitor::Triggers::Accept
	                 | Samurai::IO::Net::SocketMonitor::Triggers::Close)))
		handle->events |= EPOLLIN;

	if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Write))
		handle->events |= EPOLLOUT;

	if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Urgent))
		handle->events |= EPOLLPRI;

#ifdef EPOLLRDHUP
	if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Close))
		handle->events |= EPOLLRDHUP;
#endif
}

static Samurai::IO::Net::SocketMonitor::Triggers get_poll_events(struct epoll_event* handle)
{
	const uint32_t trig = handle->events;
	Samurai::IO::Net::SocketMonitor::Triggers evt = Samurai::IO::Net::SocketMonitor::Triggers::None;

	if (trig & EPOLLIN)
		evt |= Samurai::IO::Net::SocketMonitor::Triggers::Read;

	if (trig & EPOLLPRI)
		evt |= Samurai::IO::Net::SocketMonitor::Triggers::Urgent;

	if (trig & EPOLLOUT)
		evt |= Samurai::IO::Net::SocketMonitor::Triggers::Write;

	if (trig & EPOLLHUP)
		evt |= Samurai::IO::Net::SocketMonitor::Triggers::Close;

	if (trig & EPOLLERR)
		evt |= Samurai::IO::Net::SocketMonitor::Triggers::Error;

#ifdef EPOLLRDHUP
	if (trig & EPOLLRDHUP)
		evt |= Samurai::IO::Net::SocketMonitor::Triggers::Close;
#endif
	return evt;
}


void Samurai::IO::Net::EPollSocketMonitor::internal_add(Samurai::IO::Net::SocketBase* socket)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	set_poll_events(&ev, socket->getMonitorTrigger());
	ev.data.fd = socket->getFD();
	
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, socket->getFD(), &ev);
	if (ret == -1)
	{
		QERR("Unable to add socket to monitor");
		return;
	}
	num++;
}

void Samurai::IO::Net::EPollSocketMonitor::debug()
{
}

void Samurai::IO::Net::EPollSocketMonitor::internal_remove(Samurai::IO::Net::SocketBase* socket)
{
	if (socket->getFD() == INVALID_SOCKET) return;

	// Linux < 2.6.9 cannot handle this as a null pointer
	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	struct epoll_event* ev_ptr = nullptr; // &ev;
	
	int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, socket->getFD(), ev_ptr);
	if (ret == -1 && errno == ENOENT)
	{
		QERR("Socket is not monitored");
		return;
	}
	
	
	if (ret == -1)
	{
		QERR("Unable to remove socket from monitor: %d, %s (%d)", errno, strerror(errno), socket->getFD());
		return;
	}
	num--;
}

void Samurai::IO::Net::EPollSocketMonitor::internal_modify(Samurai::IO::Net::SocketBase* socket)
{
	struct epoll_event ev;
	set_poll_events(&ev, socket->getMonitorTrigger());
	ev.data.fd = socket->getFD();
	int ret = epoll_ctl(epfd, EPOLL_CTL_MOD, socket->getFD(), &ev);
	if (ret == -1)
	{
		QERR("Unable to add socket to monitor");
	}
}

void Samurai::IO::Net::EPollSocketMonitor::internal_wait(int time_ms)
{
	int nfds = epoll_wait(epfd, act.data(), EPOLL_BATCH, time_ms);
	if (nfds == 0) return;
	
	if (nfds == -1) {
		 if (Samurai::IO::Net::net_error() != EINTR) {
			QERR("Epoll error: %i, %s", Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
		}
		return;
	}
	
	for(int n = 0; n < nfds; n++)
	{
		Samurai::IO::Net::SocketMonitor::Triggers trig = get_poll_events(&act[n]);
		dispatch(act[n].data.fd, trig);
	}
}

#endif // SOCKET_NOTIFY_EPOLL
