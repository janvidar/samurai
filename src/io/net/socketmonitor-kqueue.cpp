/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
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

/* How many ready descriptors one kevent() may report, rather than one slot per
   monitorable socket. Anything that does not fit is reported on the next call. */
#define KQUEUE_BATCH 256

Samurai::IO::Net::KQueueSocketMonitor::KQueueSocketMonitor() : Samurai::IO::Net::SocketMonitor("kqueue")
{
	numChanges = 0;
	max = std::min<size_t>(Samurai::OS::getMaxOpenSockets(), MAXSOCK);
	num = 0;
	events.resize(KQUEUE_BATCH);
	change.resize(MAXCHANGES);

	memset(events.data(), 0, sizeof(struct kevent) * events.size());
	memset(change.data(), 0, sizeof(struct kevent) * change.size());

	kfd = kqueue();
	if (kfd == -1)
	{
		QERR("kqueue() failed");
	}
}

Samurai::IO::Net::KQueueSocketMonitor::~KQueueSocketMonitor()
{
	if (kfd != -1) close(kfd);
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

/* ev->filter names the single filter that fired; it is an identifier (a small
   negative number), not a bitmask, so it has to be compared and not masked.
   The EV_* conditions live in ev->flags. */
static Samurai::IO::Net::SocketMonitor::Triggers get_poll_events(struct kevent* handle)
{
	Samurai::IO::Net::SocketMonitor::Triggers evt = Samurai::IO::Net::SocketMonitor::Triggers::None;

	switch (handle->filter)
	{
		case EVFILT_READ:
			evt |= Samurai::IO::Net::SocketMonitor::Triggers::Read;
			break;

		case EVFILT_WRITE:
			evt |= Samurai::IO::Net::SocketMonitor::Triggers::Write;
			break;

		default:
			break;
	}

	if (handle->flags & EV_EOF)
		evt |= Samurai::IO::Net::SocketMonitor::Triggers::Close;

	return evt;
}

struct kevent* Samurai::IO::Net::KQueueSocketMonitor::getChangeEventSlot()
{
	/*
	 * Grown rather than flushed here.
	 *
	 * Committing mid-stream had to pass an eventlist of zero length, since
	 * there is nowhere to deliver readiness events from this call. But with no
	 * room in the eventlist kevent() cannot report a rejected change as an
	 * EV_ERROR entry, so it returns -1 at the first one and abandons every
	 * change after it - and the result was not checked. One EV_DELETE for a
	 * descriptor that had already been closed, which is ordinary, therefore
	 * silently discarded the registrations queued behind it.
	 *
	 * internal_wait() submits the whole list with room to report each
	 * rejection, and already skips EV_ERROR entries. Letting the list grow
	 * keeps that the only place changes are committed.
	 */
	if (numChanges == change.size())
		change.resize(change.size() * 2);

	struct kevent* ev = &change[numChanges++];
	memset(ev, 0, sizeof(struct kevent));
	return ev;
}

static std::string kevent_filter_name(int16_t filter)
{
	switch (filter)
	{
		case EVFILT_READ:     return "EVFILT_READ";
		case EVFILT_WRITE:    return "EVFILT_WRITE";
		case EVFILT_AIO:      return "EVFILT_AIO";
		case EVFILT_VNODE:    return "EVFILT_VNODE";
		case EVFILT_PROC:     return "EVFILT_PROC";
		case EVFILT_SIGNAL:   return "EVFILT_SIGNAL";
		case EVFILT_TIMER:    return "EVFILT_TIMER";
		case EVFILT_MACHPORT: return "EVFILT_MACHPORT";
		case EVFILT_FS:       return "EVFILT_FS";
		case EVFILT_SYSCOUNT: return "EVFILT_SYSCOUNT";
		default:              return std::format("{}", filter);
	}
}

static std::string kevent_flag_names(uint16_t flags)
{
	static constexpr std::pair<uint16_t, std::string_view> names[] = {
		{ EV_ADD,     "EV_ADD"     },
		{ EV_DELETE,  "EV_DELETE"  },
		{ EV_ENABLE,  "EV_ENABLE"  },
		{ EV_DISABLE, "EV_DISABLE" },
		{ EV_RECEIPT, "EV_RECEIPT" },
		{ EV_ONESHOT, "EV_ONESHOT" },
		{ EV_CLEAR,   "EV_CLEAR"   },
		{ EV_EOF,     "EV_EOF"     },
		{ EV_ERROR,   "EV_ERROR"   },
		{ EV_OOBAND,  "EV_OOBAND"  },
	};

	std::string out;
	for (const auto& [bit, name] : names)
	{
		if (!(flags & bit))
			continue;
		if (!out.empty())
			out += " | ";
		out += name;
	}

	if (out.empty())
		return std::format("{}", flags);

	return out;
}

static void print_kevent(struct kevent* event)
{
	if (!event)
		return;

	const std::string filter = kevent_filter_name(event->filter);
	const std::string flags = kevent_flag_names(event->flags);

	QDBG("print_kevent: ev=%p, { %d, %s, %s, %u, %d, %p }", event, (int) event->ident,
		filter.c_str(), flags.c_str(), (unsigned int) event->fflags, (int) event->data,
		(void*) event->udata);
}


/* Because a filter is an identifier rather than a bit, a socket that wants both
   readability and writability needs one kevent per filter -- ORing the two
   together yields EVFILT_READ (-1 | -2 == -1) and silently drops the write
   interest, so a connecting socket never learns that its connect() completed.

   Both filters are always registered and only enabled or disabled, which keeps
   EV_DELETE off the modify path: deleting a filter that was never registered
   fails with ENOENT. */
void Samurai::IO::Net::KQueueSocketMonitor::internal_set(Samurai::IO::Net::SocketBase* socket)
{
	Samurai::IO::Net::SocketMonitor::Triggers trigger = socket->getMonitorTrigger();

	bool want_read = any(trigger & (Samurai::IO::Net::SocketMonitor::Triggers::Read | Samurai::IO::Net::SocketMonitor::Triggers::Accept | Samurai::IO::Net::SocketMonitor::Triggers::Close | Samurai::IO::Net::SocketMonitor::Triggers::Urgent));
	bool want_write = any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Write);

	short flags = EV_ADD;
	if (any(trigger & Samurai::IO::Net::SocketMonitor::Triggers::Urgent))
		flags |= EV_OOBAND;

	struct kevent* ev = getChangeEventSlot();
	EV_SET(ev, socket->getFD(), EVFILT_READ,
		flags | (want_read ? EV_ENABLE : EV_DISABLE), 0, 0, socket);
	print_kevent(ev);

	ev = getChangeEventSlot();
	EV_SET(ev, socket->getFD(), EVFILT_WRITE,
		flags | (want_write ? EV_ENABLE : EV_DISABLE), 0, 0, socket);
	print_kevent(ev);
}

void Samurai::IO::Net::KQueueSocketMonitor::internal_add(Samurai::IO::Net::SocketBase* socket)
{
	QDBG("kqueue - add (ptr=%p, sd=%d)", socket, socket->getFD());
	internal_set(socket);
	num++;
}


void Samurai::IO::Net::KQueueSocketMonitor::internal_remove(Samurai::IO::Net::SocketBase* socket)
{
	QDBG("kqueue - del (ptr=%p, sd=%d)", socket, socket->getFD());

	/* udata is left null so that the EV_ERROR entries kqueue reports for a
	   descriptor that has already been closed are not mistaken for readiness
	   events on a dangling socket. */
	struct kevent* ev = getChangeEventSlot();
	EV_SET(ev, socket->getFD(), EVFILT_READ, EV_DELETE, 0, 0, 0);
	print_kevent(ev);

	ev = getChangeEventSlot();
	EV_SET(ev, socket->getFD(), EVFILT_WRITE, EV_DELETE, 0, 0, 0);
	print_kevent(ev);

	num--;
}

void Samurai::IO::Net::KQueueSocketMonitor::internal_modify(Samurai::IO::Net::SocketBase* socket)
{
	QDBG("kqueue - mod (ptr=%p, sd=%d)", socket, socket->getFD());
	internal_set(socket);
}

void Samurai::IO::Net::KQueueSocketMonitor::internal_wait(int time_ms)
{
	struct timespec timeout;
	timeout.tv_sec  = time_ms / 1000;
	timeout.tv_nsec = (time_ms % 1000) * 1000000;

	int ret = kevent(kfd, change.data(), numChanges, events.data(), (int) events.size(), &timeout);
	QDBG("kqueue - run changes=%zu, max=%zu, ret=%d", numChanges, max, ret);
	numChanges = 0;

	if (ret == -1)
	{
		if (Samurai::IO::Net::net_error() != EINTR)
		{
			QERR("kevent error: %i, %s", Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
		}
		return;
	}

	/*
	 * A call that reported a rejected change has not waited and has not looked
	 * at readiness: kevent() returns as soon as it has an EV_ERROR entry to
	 * hand back. Rejections are ordinary here - closing a descriptor removes
	 * its knotes, so the EV_DELETE queued when a socket was released is
	 * answered with ENOENT on the next pass - and letting one swallow the wait
	 * left a descriptor that was already readable unreported until something
	 * else woke the loop. Wait again, with the changelist now committed.
	 */
	int errors = 0;
	for (int n = 0; n < ret; n++)
	{
		if (events[n].flags & EV_ERROR) errors++;
	}

	if (ret > 0 && errors == ret)
	{
		QDBG("kqueue - %d rejected change(s) and nothing else; waiting again", errors);
		ret = kevent(kfd, nullptr, 0, events.data(), (int) events.size(), &timeout);
		QDBG("kqueue - rerun ret=%d", ret);

		if (ret == -1)
		{
			if (Samurai::IO::Net::net_error() != EINTR)
			{
				QERR("kevent error: %i, %s", Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
			}
			return;
		}
	}

	for (int n = 0; n < ret; n++)
	{
		struct kevent* ev = &events[n];
		print_kevent(ev);

		/* EV_ERROR in the event list reports a rejected *change*, with errno in
		   ev->data - not a condition on the socket. Dispatching it as readiness
		   would fabricate a read or write event. */
		if (ev->flags & EV_ERROR)
		{
			QDBG("kqueue - change rejected (sd=%d, filter=%d): %s",
				(int) ev->ident, (int) ev->filter, strerror((int) ev->data));
			continue;
		}

		Samurai::IO::Net::SocketMonitor::Triggers trig = get_poll_events(ev);
		QDBG("kqueue - SIG (sd=%d) trigger=%x (%d/%d) ev={%d, %d, %d, %p}",
			(int) ev->ident, trig, n+1, ret, (int) ev->ident, (int) ev->filter,
			(unsigned int) ev->fflags, (void*) ev->udata);

		/* Keyed by the descriptor rather than by the SocketBase* parked in
		   ev->udata, and dispatched through dispatch() as the other backends
		   do: a handler run earlier in this batch is free to release a socket
		   that a later event in the same batch still points at, so a pointer
		   collected from udata may already be dangling by the time its turn
		   comes. Looking the descriptor up instead skips a socket that has
		   gone away, and holds the one that has not for the duration of the
		   callback. */
		dispatch((socket_t) ev->ident, trig);
	}

}

#endif // SOCKET_NOTIFY_KQUEUE
