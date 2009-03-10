/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include "socketmonitor-backend.h"

#ifdef SOCKET_NOTIFY_KQUEUE

#include <samurai/samurai.h>
#include <samurai/os.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/datagram.h>

#include <sys/event.h>

#define MAXSOCK 4096
#define MAXCHANGES 32

Samurai::IO::Net::KQueueSocketMonitor::KQueueSocketMonitor() : Samurai::IO::Net::SocketMonitor("kqueue")
{
	numChanges = 0;
	max = MIN(Samurai::OS::getMaxOpenSockets(), MAXSOCK);
	num = 0;
	events = new struct kevent[max];
	change = new struct kevent[MAXCHANGES];

	memset(events, 0, sizeof(struct kevent) * max);
	memset(change, 0, sizeof(struct kevent) * max);
	
	kfd = kqueue();
	if (kfd == -1)
	{
		QERR("kqueue() failed");
	}
}

Samurai::IO::Net::KQueueSocketMonitor::~KQueueSocketMonitor()
{
	close(kfd);
	delete[] events;
	delete[] change;
}

bool Samurai::IO::Net::KQueueSocketMonitor::isValid()
{
	if (getenv("SAMURAI_NO_KQUEUE"))
	{
		return false;
	}
	
	if (kfd == -1)
		return false;
	
	return true;
}

static int get_poll_events(struct kevent* handle)
{
        short trig = handle->filter;
        int evt  = 0;

        if (trig & EVFILT_READ)
                evt |= Samurai::IO::Net::SocketMonitor::MRead;

        if (trig & EVFILT_WRITE)
                evt |= Samurai::IO::Net::SocketMonitor::MWrite;

        if (trig & EV_EOF)
                evt |= Samurai::IO::Net::SocketMonitor::MClose;

        if (trig & EV_ERROR)
                evt |= Samurai::IO::Net::SocketMonitor::MError;

        return evt;
}

struct kevent* Samurai::IO::Net::KQueueSocketMonitor::getChangeEventSlot()
{
	// If change buffer is full, commit it to the kqueue.
	if (numChanges + 1 == MAXCHANGES)
	{
		struct timespec timeout;
		timeout.tv_sec  = 0;
		timeout.tv_nsec = 0;
		struct kevent* ev = &change[numChanges++];
		memset(ev, 0, sizeof(struct kevent));
		ev->filter = EV_RECEIPT;
		kevent(kfd, change, numChanges, events, 0, &timeout);
		numChanges = 0;
	}
	
	struct kevent* ev = &change[numChanges++];
	memset(ev, 0, sizeof(struct kevent));
	return ev;
}

static void print_kevent(struct kevent* event)
{
	if (!event)
		return;
	
	char* filter = new char[50]; filter[0] = 0;
	char* flags = new char[50]; flags[0] = 0;
	
	switch (event->filter)
	{
		case EVFILT_READ:     strcat(filter, "EVFILT_READ");     break;
		case EVFILT_WRITE:    strcat(filter, "EVFILT_WRITE");    break;
		case EVFILT_AIO:      strcat(filter, "EVFILT_AIO");      break;
		case EVFILT_VNODE:    strcat(filter, "EVFILT_VNODE");    break;
		case EVFILT_PROC:     strcat(filter, "EVFILT_PROC");     break;
		case EVFILT_SIGNAL:   strcat(filter, "EVFILT_SIGNAL");   break;
		case EVFILT_TIMER:    strcat(filter, "EVFILT_TIMER");    break;
		case EVFILT_MACHPORT: strcat(filter, "EVFILT_MACHPORT"); break;
		case EVFILT_FS:       strcat(filter, "EVFILT_FS");       break;
		case EVFILT_SYSCOUNT: strcat(filter, "EVFILT_SYSCOUNT"); break;
		default:
			sprintf(filter, "%d", event->filter);
			break;
	}
	
	if (event->flags & EV_ADD)  strcat(flags, "EV_ADD");
	if (event->flags & EV_DELETE)  { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_DELETE"); }
	if (event->flags & EV_ENABLE)  { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_ENABLE"); }
	if (event->flags & EV_DISABLE) { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_DISABLE"); }
	if (event->flags & EV_RECEIPT) { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_RECEIPT"); }
	if (event->flags & EV_ONESHOT) { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_ONESHOT"); }
	if (event->flags & EV_CLEAR)   { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_CLEAR"); }
	if (event->flags & EV_EOF)     { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_EOF"); }
	if (event->flags & EV_ERROR)   { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_ERROR"); }
	if (event->flags & EV_OOBAND)  { if (strlen(flags)) strcat(flags, " | "); strcat(flags, "EV_OOBAND"); }
	
	if (!strlen(flags))
	{
		sprintf(flags, "%d", (int) event->flags);
	}

	QDBG("print_kevent: ev=%p, { %d, %s, %s, %u, %d, %p }", event, (int) event->ident, filter, flags, (unsigned int) event->fflags, (int) event->data, (void*) event->udata);
	
	delete[] filter;
	delete[] flags;
}


void Samurai::IO::Net::KQueueSocketMonitor::internal_add(Samurai::IO::Net::SocketBase* socket)
{
	QDBG("kqueue - add (ptr=%p, sd=%d)", socket, socket->getFD());
	struct kevent* ev = getChangeEventSlot();
	short filter = 0;
	if (socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MRead
			|| socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MAccept
			|| socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MClose
			|| socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MUrgent
		)
		filter |= EVFILT_READ;
	if (socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MWrite)
		filter |= EVFILT_WRITE;
	
	short flags = EV_ADD | EV_ENABLE;
	if (socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MUrgent)
		flags |= EV_OOBAND;
	
	EV_SET(ev, socket->getFD(), filter, flags, 0, 0, socket);
	print_kevent(ev);
	num++;
}


void Samurai::IO::Net::KQueueSocketMonitor::internal_remove(Samurai::IO::Net::SocketBase* socket)
{
	QDBG("kqueue - del (ptr=%p, sd=%d)", socket, socket->getFD());
	struct kevent* ev = getChangeEventSlot();
	EV_SET(ev, socket->getFD(), 0, EV_DELETE | EV_DISABLE, 0, 0, 0);
	print_kevent(ev);
	num--;
}

void Samurai::IO::Net::KQueueSocketMonitor::internal_modify(Samurai::IO::Net::SocketBase* socket)
{
	QDBG("kqueue - mod (ptr=%p, sd=%d)", socket, socket->getFD());
	struct kevent* ev = getChangeEventSlot();

	short filter = 0;
	if (socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MRead
			|| socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MAccept
			|| socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MClose
			|| socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MUrgent
		)
		filter |= EVFILT_READ;
	if (socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MWrite)
		filter |= EVFILT_WRITE;
	
	short flags = EV_ADD | EV_ENABLE;
	if (socket->getMonitorTrigger() & Samurai::IO::Net::SocketMonitor::MUrgent)
		flags |= EV_OOBAND;

	EV_SET(ev, socket->getFD(), filter, flags, 0, 0, socket);
	print_kevent(ev);
}

void Samurai::IO::Net::KQueueSocketMonitor::wait(int time_ms)
{
	struct timespec timeout;
	timeout.tv_sec  = time_ms / 1000;
	timeout.tv_nsec = (time_ms % 1000) * 1000;
	
	int ret = kevent(kfd, change, numChanges, events, max, &timeout);
	QDBG("kqueue - run changes=%d, max=%d, ret=%d", numChanges, max, ret);
	numChanges = 0;

	if (ret == -1)
	{
		if (NETERROR != EINTR)
		{
			QERR("kevent error: %i, %s", NETERROR, strerror(NETERROR));
		}
		return;
	}
	
	for (int n = 0; n < ret; n++)
	{
		struct kevent* ev = &events[n];
		SocketBase* sock = (SocketBase*) ev->udata;
		print_kevent(ev);
		if (!sock) continue;
		int trig = get_poll_events(ev);
		QDBG("kqueue - SIG (ptr=%p, sd=%d) trigger=%x (%d/%d) ev={%d, %d, %d, %p}", sock, sock ? sock->getFD() : -1, trig, n+1, ret, ev->ident, ev->filter, ev->fflags, ev->udata);
		handleSocketEvent(sock, trig);
	}

}

#endif // SOCKET_NOTIFY_KQUEUE
