/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <algorithm>
#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
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
		registry[socket->getFD()] = socket->shared_from_this();
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

	registry.erase(socket->sd);
	internal_remove(socket);
}


void Samurai::IO::Net::SocketMonitor::modify(Samurai::IO::Net::SocketBase* socket)
{
	if (!socket || (int) socket->sd == (int) INVALID_SOCKET)
	{
		QERR("Trying to modify invalid socket: %p, sd=%d", socket, socket ? (int) socket->sd : -666);
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
	for (const auto& [fd, weak] : registry)
	{
		const std::shared_ptr<SocketBase> socket = weak.lock();
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

	for (const auto& [fd, weak] : registry)
	{
		const std::shared_ptr<SocketBase> socket = weak.lock();
		if (socket && socket->bufferedInput())
			ready.push_back(fd);
	}

	for (const socket_t fd : ready)
		dispatch(fd, Samurai::IO::Net::SocketMonitor::Triggers::Read);
}


void Samurai::IO::Net::SocketMonitor::dispatch(socket_t fd, Triggers trig)
{
	std::map<socket_t, std::weak_ptr<SocketBase> >::iterator it = registry.find(fd);
	if (it == registry.end())
		return;

	/* The shared_ptr is held for the whole callback: a handler that reacts to
	   EventDisconnected by dropping its own reference must not leave this
	   dispatch holding freed memory. */
	std::shared_ptr<SocketBase> socket = it->second.lock();
	if (!socket)
	{
		registry.erase(it);
		return;
	}

	handleSocketEvent(socket.get(), trig);
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


