/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <algorithm>
#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <memory>
#include <samurai/io/net/datagram.h>
#include <samurai/timer.h>

#include "socketmonitor-backend.h"

#include <samurai/io/net/tlsfactory.h>

/* FIXME: Must not initialize SSL and WSA for each SocketMonitor created! */

namespace {

/*
 * How many readiness reports in a row one descriptor may answer with nothing at
 * all before the monitor takes it out of the poll.
 *
 * Nothing at all means precisely that: no byte moved either way, the socket did
 * not change what it is waiting for, and it did not leave the monitor. A handler
 * doing any of those is making progress, however slowly, and is left alone
 * however many passes it takes. A handler doing none of them will do none of
 * them on the next pass either, because the descriptor and the socket are in the
 * same state they were - so the count is a way of noticing a fixed point rather
 * than a slow one, and the figure only has to be past what a legitimate run of
 * spurious wakeups could reach.
 */
constexpr unsigned SPIN_LIMIT = 64;

/*
 * How long such a descriptor is left out of the poll.
 *
 * The same figure and the same reasoning as the accept backoff in
 * serversocket.cpp: long enough that the loop costs nothing, short enough that a
 * condition which does clear is picked up while it still matters.
 */
constexpr int SPIN_BACKOFF_MS = 250;

}

Samurai::IO::Net::SocketMonitor::SocketMonitor(std::string_view name_) : name(name_)
{
/* NOTE: no socket call works on Windows until WSAStartup() has run. */
#ifdef SAMURAI_WINSOCK
	QDBG("Initializing Winsock library...");
	WSAData wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != NO_ERROR) {
		QERR("ERROR: Unable to initialize winsock");
		abort();
	}
#endif

	Samurai::IO::Net::TlsFactory::global_init();
}


Samurai::IO::Net::SocketMonitor::~SocketMonitor()
{
	Samurai::IO::Net::TlsFactory::global_deinit();

#ifdef SAMURAI_WINSOCK
	WSACleanup();
#endif
}


void Samurai::IO::Net::SocketMonitor::add(Samurai::IO::Net::SocketBase* socket)
{
	if (!socket || (int) socket->getFD() == (int) INVALID_SOCKET)
	{
		return;
	}

	/* A socket built by its create() factory is always held by a shared_ptr,
	   so this only throws for one built some other way - in which case it is
	   better to refuse than to monitor something whose lifetime cannot be
	   observed. */
	try
	{
		/* Replaced outright rather than updated: a descriptor number is reused as
		   soon as it is closed, so an entry already here under this number belongs
		   to a socket that has gone - along with whatever the runaway guard had
		   concluded about it. */
		Watch fresh;
		fresh.socket = socket->shared_from_this();
		registry[socket->getFD()] = fresh;
	}
	catch (const std::bad_weak_ptr&)
	{
		QERR("Socket is not owned by a shared_ptr; use Socket::create()");
		return;
	}

	internal_add(socket);
}


void Samurai::IO::Net::SocketMonitor::remove(Samurai::IO::Net::SocketBase* socket)
{
	if (!socket || (int) socket->sd == (int) INVALID_SOCKET)
	{
		return;
	}

	/* A paused descriptor is already out of the backend, and asking a second time
	   is answered with a complaint about a socket that is not monitored. */
	const std::map<socket_t, Watch>::iterator it = registry.find(socket->sd);
	const bool paused = (it != registry.end() && it->second.paused);

	if (it != registry.end())
		registry.erase(it);

	if (!paused)
		internal_remove(socket);
}


void Samurai::IO::Net::SocketMonitor::modify(Samurai::IO::Net::SocketBase* socket)
{
	if (!socket || (int) socket->sd == (int) INVALID_SOCKET)
	{
		QERR("Trying to modify invalid socket: %p, sd=%d", socket, socket ? (int) socket->sd : -666);
		return;
	}

	/*
	 * Changing what a descriptor waits for is the socket deciding something, which
	 * is the one thing a runaway is not doing - so a paused one is taken at its
	 * word and put straight back in rather than left to sit out the rest of its
	 * backoff. It has to be added rather than modified: it is not in the backend.
	 */
	const std::map<socket_t, Watch>::iterator it = registry.find(socket->sd);
	if (it != registry.end() && it->second.paused)
	{
		it->second.paused = false;
		it->second.idle = 0;
		internal_add(socket);
		return;
	}

	internal_modify(socket);
}


void Samurai::IO::Net::SocketMonitor::wait(int time_ms)
{
	/* Blocking for the full timeout with data already decrypted and waiting
	   would delay it by that much for no reason, so the poll only collects
	   whatever the descriptors have to add. */
	const bool buffered = haveBufferedInput();
	int timeout = buffered ? 0 : time_ms;

	/* A deadline that falls before the caller's timeout has to cut the poll
	   short, or the timer fires late by however much the caller asked for -
	   and callers ask for whole seconds. */
	if (!buffered)
	{
		const int next = Samurai::TimerManager::getInstance()->timeToNext();
		if (next >= 0 && (timeout < 0 || next < timeout)) timeout = next;
	}

	/* Before the poll, so that a descriptor whose backoff has expired is watched
	   on this pass rather than on the next one, and so that the poll does not
	   sleep through the moment it was due back. */
	const int resume = resumePaused();
	if (resume >= 0 && (timeout < 0 || resume < timeout)) timeout = resume;

	internal_wait(timeout);

	/* After the poll has dispatched, so a timer callback acts on the freshest
	   socket state, and unconditionally, so a deadline is still met on a pass
	   where no descriptor had anything to report. */
	Samurai::TimerManager::getInstance()->process();

	/* Rescanned rather than reusing the list gathered above: the poll just
	   dispatched, and a handler may have consumed the buffered data, closed the
	   socket holding it, or produced more on another one. */
	dispatchBufferedInput();
}


bool Samurai::IO::Net::SocketMonitor::haveBufferedInput() const
{
	for (const auto& [fd, watch] : registry)
	{
		/* A descriptor sitting out a backoff is skipped, or the buffered input it
		   holds would keep the poll from ever blocking - which is the busy loop
		   the backoff exists to stop, arrived at from the other side. */
		if (watch.paused) continue;

		const std::shared_ptr<SocketBase> socket = watch.socket.lock();
		if (socket && socket->bufferedInput())
			return true;
	}
	return false;
}


void Samurai::IO::Net::SocketMonitor::dispatchBufferedInput()
{
	/* Collected before dispatching anything: a handler is free to add or
	   remove registry entries, which would invalidate an iterator held across
	   the callback. dispatch() looks each descriptor up again and skips one
	   that has since gone away. */
	std::vector<socket_t> ready;

	for (const auto& [fd, watch] : registry)
	{
		if (watch.paused) continue;

		const std::shared_ptr<SocketBase> socket = watch.socket.lock();
		if (socket && socket->bufferedInput())
			ready.push_back(fd);
	}

	for (const socket_t fd : ready)
		dispatch(fd, Samurai::IO::Net::SocketMonitor::Triggers::Read);
}


void Samurai::IO::Net::SocketMonitor::dispatch(socket_t fd, Triggers trig)
{
	std::map<socket_t, Watch>::iterator it = registry.find(fd);
	if (it == registry.end())
		return;

	/* The shared_ptr is held for the whole callback: a handler that reacts to
	   EventDisconnected by dropping its own reference must not leave this
	   dispatch holding freed memory. */
	std::shared_ptr<SocketBase> socket = it->second.socket.lock();
	if (!socket)
	{
		registry.erase(it);
		return;
	}

	const uint64_t progress = socket->getProgress();
	const Triggers trigger = socket->getMonitorTrigger();

	handleSocketEvent(socket.get(), trig);

	/*
	 * Whether anything came of it. The iterator is looked up again rather than
	 * held across the callback: a handler is free to close its socket, which
	 * takes the entry out of here, and one that did needs no watching - it will
	 * not be reported again.
	 */
	it = registry.find(fd);
	if (it == registry.end())
		return;

	/* And the entry has to still be the same socket's. A handler that closed one
	   socket and opened another may have been given this descriptor number back,
	   in which case add() has already put a fresh watch here. */
	if (it->second.socket.lock() != socket)
		return;

	Watch& watch = it->second;

	if (socket->getProgress() != progress
	 || socket->getMonitorTrigger() != trigger
	 || !socket->isMonitored())
	{
		watch.idle = 0;
		watch.reported = false;
		return;
	}

	if (++watch.idle >= SPIN_LIMIT)
		pauseWatch(fd, watch, socket.get(), trig);
}


void Samurai::IO::Net::SocketMonitor::pauseWatch(socket_t fd, Watch& watch,
                                                 SocketBase* socket, Triggers trig)
{
	/*
	 * Said once per episode and said as an error, because this is not a condition
	 * the monitor can put right: something above it is being handed a descriptor
	 * it will not act on, and all that happens here is that the cost of that
	 * stops being a whole processor. What it is waiting for and who is on the
	 * other end are what identify which of the handlers it is.
	 */
	if (!watch.reported)
	{
		watch.reported = true;

		const Samurai::IO::Net::InetAddress* peer = socket->getAddress();
		QERR("Descriptor %d answered %u readiness reports (trigger=0x%x, waiting for 0x%x, peer=%s) "
		     "without reading, writing or closing; leaving it out of the poll for %d ms at a time",
			(int) fd, watch.idle, (unsigned) trig, (unsigned) socket->getMonitorTrigger(),
			peer ? peer->toString().c_str() : "unknown", SPIN_BACKOFF_MS);
	}

	watch.paused = true;
	watch.resume = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(SPIN_BACKOFF_MS);

	internal_remove(socket);
}


int Samurai::IO::Net::SocketMonitor::resumePaused()
{
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	int soonest = -1;

	std::map<socket_t, Watch>::iterator it = registry.begin();
	while (it != registry.end())
	{
		Watch& watch = it->second;

		if (!watch.paused)
		{
			it++;
			continue;
		}

		/* Nothing to put back: the owner released the socket while it was sitting
		   out, and the descriptor went with it. */
		const std::shared_ptr<SocketBase> socket = watch.socket.lock();
		if (!socket)
		{
			it = registry.erase(it);
			continue;
		}

		if (watch.resume > now)
		{
			const int left = (int) std::chrono::ceil<std::chrono::milliseconds>(
				watch.resume - now).count();
			if (soonest < 0 || left < soonest) soonest = left;
			it++;
			continue;
		}

		/*
		 * Back in, with the count cleared so that a handler which has since
		 * started answering is not paused again on the strength of what it did
		 * before. One that has not is paused again in SPIN_LIMIT passes, which is
		 * the four times a second this is meant to cost.
		 */
		watch.paused = false;
		watch.idle = 0;
		internal_add(socket.get());
		it++;
	}

	return soonest;
}


void Samurai::IO::Net::SocketMonitor::handleSocketEvent(Samurai::IO::Net::SocketBase* sock, Triggers trig)
{
	sock->handleMonitorEvent(trig);
}

Samurai::IO::Net::SocketMonitor* Samurai::IO::Net::SocketMonitor::socket_monitor = nullptr;


bool Samurai::IO::Net::SocketMonitor::setSocketMonitor(Samurai::IO::Net::SocketMonitor* monitor)
{
	if (!monitor)
	{
		socket_monitor = nullptr;
		return true;
	}
	
	if (socket_monitor)
		return false;
	
	if (!monitor->isValid())
		return false;
	
	socket_monitor = monitor;
	
	QDBG("SocketMonitor: Using %.*s backend", (int) monitor->name.size(), monitor->name.data());
	return true;
}



/*
 * Construct a backend and keep it only if it came up. The winner is released
 * from the unique_ptr deliberately: the chosen monitor is never destroyed.
 */
template<typename T>
static Samurai::IO::Net::SocketMonitor* tryBackend()
{
	auto candidate = std::make_unique<T>();
	return candidate->isValid() ? candidate.release() : nullptr;
}

static Samurai::IO::Net::SocketMonitor* createDefaultMonitor()
{
	Samurai::IO::Net::SocketMonitor* monitor = nullptr;

#ifdef SOCKET_NOTIFY_EPOLL
	if (!monitor)
	{
		monitor = tryBackend<Samurai::IO::Net::EPollSocketMonitor>();
	}
#endif // SOCKET_NOTIFY_EPOLL

#ifdef SOCKET_NOTIFY_KQUEUE
	if (!monitor)
	{
		monitor = tryBackend<Samurai::IO::Net::KQueueSocketMonitor>();
	}
#endif // SOCKET_NOTIFY_KQUEUE


#ifdef SOCKET_NOTIFY_POLL
	if (!monitor)
	{
		monitor = tryBackend<Samurai::IO::Net::PollSocketMonitor>();
	}
#endif // SOCKET_NOTIFY_POLL

#ifdef SOCKET_NOTIFY_SELECT
	if (!monitor)
	{
		monitor = tryBackend<Samurai::IO::Net::SelectSocketMonitor>();
	}
#endif // SOCKET_NOTIFY_SELECT

	return monitor;
}


Samurai::IO::Net::SocketMonitor* Samurai::IO::Net::SocketMonitor::getInstance()
{
	if (socket_monitor)
		return socket_monitor;

	/* Chosen once, and deliberately never destroyed: a socket outliving it
	   calls disableMonitor() from its destructor, which comes back here. */
	static Samurai::IO::Net::SocketMonitor* fallback = createDefaultMonitor();

	setSocketMonitor(fallback);

	if (!socket_monitor)
	{
		QERR("Unable to find a suitable socket monitor.");
	}

	return socket_monitor;
}


