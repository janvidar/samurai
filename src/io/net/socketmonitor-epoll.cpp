/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
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

Samurai::IO::Net::EPollSocketMonitor::EPollSocketMonitor() : Samurai::IO::Net::SocketMonitor("epoll")
{
	max = Samurai::OS::getMaxOpenSockets();
	num = 0;
	act = new struct epoll_event[max];
	memset(act, 0, sizeof(struct epoll_event) * max);
	epfd = epoll_create(max);
}

Samurai::IO::Net::EPollSocketMonitor::~EPollSocketMonitor()
{
	close(epfd);
	delete[] act;
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


static void set_poll_events(struct epoll_event* handle, int trigger)
{
	memset(handle, 0, sizeof(struct epoll_event));

	if (trigger & Samurai::IO::Net::SocketMonitor::MRead || trigger & Samurai::IO::Net::SocketMonitor::MAccept || trigger & Samurai::IO::Net::SocketMonitor::MClose)
		handle->events |= EPOLLIN;

	if (trigger & Samurai::IO::Net::SocketMonitor::MWrite)
		handle->events |= EPOLLOUT;

	if (trigger & Samurai::IO::Net::SocketMonitor::MUrgent)
		handle->events |= EPOLLPRI;

#ifdef EPOLLRDHUP
	if (trigger & Samurai::IO::Net::SocketMonitor::MClose)
		handle->events |= EPOLLRDHUP;
#endif
}

static int get_poll_events(struct epoll_event* handle)
{
	short trig = handle->events;
	int evt  = 0;

	if (trig & EPOLLIN)
		evt |= Samurai::IO::Net::SocketMonitor::MRead;

	if (trig & EPOLLPRI)
		evt |= Samurai::IO::Net::SocketMonitor::MUrgent;

	if (trig & EPOLLOUT)
		evt |= Samurai::IO::Net::SocketMonitor::MWrite;

	if (trig & EPOLLHUP)
		evt |= Samurai::IO::Net::SocketMonitor::MClose;

	if (trig & EPOLLERR)
		evt |= Samurai::IO::Net::SocketMonitor::MError;

#ifdef EPOLLRDHUP
	if (trig & EPOLLRDHUP)
		evt |= Samurai::IO::Net::SocketMonitor::MClose;
#endif
	return evt;
}


void Samurai::IO::Net::EPollSocketMonitor::internal_add(Samurai::IO::Net::SocketBase* socket)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	set_poll_events(&ev, socket->getMonitorTrigger());
	ev.data.ptr = socket;
	
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

	// Linux < 2.6.9 cannot handle this as NULL
	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	struct epoll_event* ev_ptr = NULL; // &ev;
	
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
	ev.data.ptr = socket;
	int ret = epoll_ctl(epfd, EPOLL_CTL_MOD, socket->getFD(), &ev);
	if (ret == -1)
	{
		QERR("Unable to add socket to monitor");
	}
}

void Samurai::IO::Net::EPollSocketMonitor::wait(int time_ms)
{
	int nfds = epoll_wait(epfd, act, max, time_ms);
	if (nfds == 0) return;
	
	if (nfds == -1) {
		 if (NETERROR != EINTR) {
			QERR("Epoll error: %i, %s", NETERROR, strerror(NETERROR));
		}
		return;
	}
	
	for(int n = 0; n < nfds; n++)
	{
		Samurai::IO::Net::SocketBase* sock = (Samurai::IO::Net::SocketBase*) act[n].data.ptr;
		int trig = get_poll_events(&act[n]);
		handleSocketEvent(sock, trig);
	}
}

#endif // SOCKET_NOTIFY_EPOLL
