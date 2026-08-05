/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/timer.h>

#include <algorithm>
#include <chrono>
#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace {

/*
 * The smallest listen queue a server is given, whatever it asked for.
 *
 * The queue holds connections the kernel has completed and this process has not
 * accepted yet, so it is what covers a burst arriving between two turns of the
 * event loop. A handful - which is the historical default here - drops the rest
 * before anything in userspace can see them. The system clamps this down to its
 * own maximum on its own.
 */
constexpr size_t MIN_LISTEN_BACKLOG = 128;

/** How long a listener that ran out of descriptors is left alone. */
constexpr int ACCEPT_BACKOFF_MS = 250;

/*
 * Out of file descriptors is not a fault of the connection that exposed it, and
 * it does not go away by being reported: the connection stays queued, which keeps
 * the listener readable, and every monitor backend here is level triggered - so
 * returning and waiting to be told again is a busy loop at full speed. Stop
 * watching the listener for a moment instead and pick it up afterwards, by which
 * time whatever was holding the descriptors may have let go.
 *
 * Owns itself, and retires once the listener is armed again or once the listener
 * is gone. Only one can exist at a time, because a listener that is not being
 * watched produces no further accept attempts.
 */
class AcceptBackoff : public Samurai::TimerListener
{
	public:
		explicit AcceptBackoff(std::weak_ptr<Samurai::IO::Net::SocketBase> listener_)
			: listener(std::move(listener_))
			, timer(this, std::chrono::milliseconds(ACCEPT_BACKOFF_MS), true)
		{
		}

		void EventTimeout(Samurai::Timer*) override
		{
			if (std::shared_ptr<Samurai::IO::Net::SocketBase> sock = listener.lock())
				sock->setMonitor(Samurai::IO::Net::SocketMonitor::Triggers::Read);

			delete this;
		}

	private:
		std::weak_ptr<Samurai::IO::Net::SocketBase> listener;
		Samurai::Timer timer;
};

}

Samurai::IO::Net::ServerSocket::ServerSocket() : eventHandler(nullptr) {}

Samurai::IO::Net::ServerSocket::ServerSocket(ServerSocketEventHandler* eh, const Samurai::IO::Net::SocketAddress& addr_) :
	Samurai::IO::Net::SocketBase(addr_, SocketType::Stream), eventHandler(eh)
{
	internal_create();
}

Samurai::IO::Net::ServerSocket::ServerSocket(Samurai::IO::Net::ServerSocketEventHandler* eh, const Samurai::IO::Net::InetAddress& addr_, uint16_t port_) : Samurai::IO::Net::SocketBase(Samurai::IO::Net::InetSocketAddress(addr_, port_), SocketType::Stream), eventHandler(eh)
{
	internal_create();
}

Samurai::IO::Net::ServerSocket::ServerSocket(ServerSocketEventHandler* eh, uint16_t port_) :
	Samurai::IO::Net::SocketBase(Samurai::IO::Net::InetSocketAddress(port_), SocketType::Stream), eventHandler(eh)
{
	internal_create();
}

void Samurai::IO::Net::ServerSocket::internal_create()
{
	/* The state carries the failure a constructor cannot return; create()
	   turns it into a null shared_ptr for anyone using the factory, and
	   listen() below refuses for anyone reaching the constructor directly. */
	if (!createDescriptor(addr->getSockAddrFamily()))
		state = SocketState::Invalid;
}

Samurai::IO::Net::ServerSocket::~ServerSocket()
{
	close();
}

bool Samurai::IO::Net::ServerSocket::listen(size_t backlog) {
	std::error_code ec;
	return listen(backlog, ec);
}


bool Samurai::IO::Net::ServerSocket::listen(size_t backlog, std::error_code& ec) {
	ec.clear();

	if (!addr) { ec = Samurai::system_error(EDESTADDRREQ); return false; }
	if (sd == INVALID_SOCKET) { ec = Samurai::system_error(EBADF); return false; }
	if (!setReuseAddress(true)) { ec = Samurai::system_error(Samurai::IO::Net::net_error()); return false; }
	if (!setNonBlocking(true, ec)) return false;
	if (!bind(addr.get(), ec)) return false;

	/* NOTE: backlog is a size_t here and an int in the syscall. The caller's
	   figure is a request rather than a ceiling; see MIN_LISTEN_BACKLOG. */
	if (::listen(sd, (int) std::max(backlog, MIN_LISTEN_BACKLOG)) == -1)
	{
		ec = Samurai::system_error(Samurai::IO::Net::net_error());
		QERR("Unable to listen to socket: %s (%d)", strerror(Samurai::IO::Net::net_error()), Samurai::IO::Net::net_error());
		return false;
	}

	setMonitor(Samurai::IO::Net::SocketMonitor::Triggers::Read);
	return true;
}

/** accept() will now have something to accept, if called from SocketMonitor at least. */
void Samurai::IO::Net::ServerSocket::handleMonitorEvent(Samurai::IO::Net::SocketMonitor::Triggers trig)
{
	if (any(trig & Samurai::IO::Net::SocketMonitor::Triggers::Read))
		internal_accept();

	if (any(trig & (Samurai::IO::Net::SocketMonitor::Triggers::Error | Samurai::IO::Net::SocketMonitor::Triggers::Close)))
	{
		QERR("SocketState::Listening socket signalled error/hangup, disabling monitor");
		disableMonitor();
	}
}


void Samurai::IO::Net::ServerSocket::internal_accept() {

	struct sockaddr_storage new_addr;
	socklen_t addr_size = sizeof(new_addr);
	memset(&new_addr, 0, sizeof(new_addr));

	socket_t new_sd = ::accept(sd, (sockaddr*) &new_addr, &addr_size);

	if (new_sd == INVALID_SOCKET) {
		const int error = Samurai::IO::Net::net_error();

		// Transient: nothing to report to the event handler.
		if (error == EAGAIN || error == EWOULDBLOCK ||
		    error == EINTR  || error == ECONNABORTED)
			return;

		/* See AcceptBackoff: reporting this and returning is a busy loop. */
		if (error == EMFILE || error == ENFILE) {
			QERR("Out of file descriptors accepting a connection; pausing the listener");
			disableMonitor();
			new AcceptBackoff(weak_from_this());
			return;
		}

		if (eventHandler) eventHandler->EventAcceptError(this, strerror(error));
		return;
	}

	/* accept() does not go through SocketBase::createDescriptor(), so this is
	   the only place an inbound connection can be given the same suppression
	   every outbound one gets. */
	Samurai::IO::Net::set_nosigpipe(new_sd);

	Samurai::IO::Net::InetSocketAddress n_addr;

	if (new_addr.ss_family == AF_INET) {
		struct sockaddr_in* sin = (struct sockaddr_in*) &new_addr;
		n_addr.setRawSocketAddress((void*) &sin->sin_addr, sizeof(struct in_addr), ntohs(sin->sin_port), Samurai::IO::Net::InetAddress::Version::IPv4);

	} else if (new_addr.ss_family == AF_INET6) {
		struct sockaddr_in6* sin6 = (struct sockaddr_in6*) &new_addr;
		n_addr.setRawSocketAddress((void*) &sin6->sin6_addr, sizeof(struct in6_addr), ntohs(sin6->sin6_port), Samurai::IO::Net::InetAddress::Version::IPv6);
	}

	// Create a new socket based on the connected client,
	// and hand it over to the eventHandler.
	std::shared_ptr<Socket> sock = Socket::create(new_sd, n_addr);

	// accept() does not inherit O_NONBLOCK from the listening socket.
	if (!sock->setNonBlocking(true))
	{
		QERR("Unable to set accepted socket non-blocking: %s", strerror(Samurai::IO::Net::net_error()));
	}

	if (eventHandler)
		eventHandler->EventAcceptSocket(this, sock);
}

