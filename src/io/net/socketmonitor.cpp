/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/datagram.h>

#include "socketmonitor-backend.h"

#include <samurai/io/net/tlsfactory.h>

/* FIXME: Must not initialize SSL and WSA for each SocketMonitor created! */

Samurai::IO::Net::SocketMonitor::SocketMonitor(const char* name_) : name(name_)
{
/* NOTE: this was #ifdef WINSOCK, a macro defined nowhere in the tree - the
   rest of the sources use SAMURAI_WINSOCK from socketglue.h. WSAStartup()
   therefore never ran, and no socket call works on Windows until it has. */
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


void Samurai::IO::Net::SocketMonitor::dispatch(socket_t fd, int trig)
{
	std::map<socket_t, std::weak_ptr<SocketBase> >::iterator it = registry.find(fd);
	if (it == registry.end())
		return;

	/* Holding the lock for the whole callback is the point of the exercise:
	   a handler that reacts to EventDisconnected by dropping its own
	   reference used to leave the rest of this dispatch walking freed
	   memory. */
	std::shared_ptr<SocketBase> socket = it->second.lock();
	if (!socket)
	{
		registry.erase(it);
		return;
	}

	handleSocketEvent(socket.get(), trig);
}


void Samurai::IO::Net::SocketMonitor::handleSocketEvent(Samurai::IO::Net::SocketBase* sock, int trig)
{
	sock->handleMonitorEvent(trig);
}

Samurai::IO::Net::SocketMonitor* Samurai::IO::Net::SocketMonitor::socket_monitor = 0;


bool Samurai::IO::Net::SocketMonitor::setSocketMonitor(Samurai::IO::Net::SocketMonitor* monitor)
{
	if (!monitor)
	{
		socket_monitor = 0;
		return true;
	}
	
	if (socket_monitor)
		return false;
	
	if (!monitor->isValid())
		return false;
	
	socket_monitor = monitor;
	
	QDBG("SocketMonitor: Using %s backend", monitor->name);
	return true;
}



static Samurai::IO::Net::SocketMonitor* createDefaultMonitor()
{
	Samurai::IO::Net::SocketMonitor* monitor = 0;

#ifdef SOCKET_NOTIFY_EPOLL
	if (!monitor)
	{
		monitor = new Samurai::IO::Net::EPollSocketMonitor();
		if (!monitor->isValid())
		{
			delete monitor;
			monitor = 0;
		}
	}
#endif // SOCKET_NOTIFY_EPOLL

#ifdef SOCKET_NOTIFY_KQUEUE
	if (!monitor)
	{
		monitor = new Samurai::IO::Net::KQueueSocketMonitor();
		if (!monitor->isValid())
		{
			delete monitor;
			monitor = 0;
		}
	}
#endif // SOCKET_NOTIFY_KQUEUE


#ifdef SOCKET_NOTIFY_POLL
	if (!monitor)
	{
		monitor = new Samurai::IO::Net::PollSocketMonitor();
		if (!monitor->isValid())
		{
			delete monitor;
			monitor = 0;
		}
	}
#endif // SOCKET_NOTIFY_POLL

#ifdef SOCKET_NOTIFY_SELECT
	if (!monitor)
	{
		monitor = new Samurai::IO::Net::SelectSocketMonitor();
		if (!monitor->isValid())
		{
			delete monitor;
			monitor = 0;
		}
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


