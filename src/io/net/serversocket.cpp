/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
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

#include <stdio.h>
#include <string.h>

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
	createDescriptor(addr->getSockAddrFamily());
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
	if (!setReuseAddress(true)) { ec = Samurai::system_error(NETERROR); return false; }
	if (!setNonBlocking(true, ec)) return false;
	if (!bind(addr.get(), ec)) return false;

	/* NOTE: backlog is a size_t here and an int in the syscall. */
	if (::listen(sd, (int) backlog) == -1)
	{
		ec = Samurai::system_error(NETERROR);
		QERR("Unable to listen to socket: %s (%d)", strerror(NETERROR), NETERROR);
		return false;
	}

	setMonitor(SocketMonitor::MRead);
	return true;
}

/** accept() will now have something to accept, if called from SocketMonitor at least. */
void Samurai::IO::Net::ServerSocket::handleMonitorEvent(int trig)
{
	if (trig & SocketMonitor::MRead)
		internal_accept();

	if (trig & (SocketMonitor::MError | SocketMonitor::MClose))
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
		// Transient: nothing to report to the event handler.
		if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK ||
		    NETERROR == EINTR  || NETERROR == ECONNABORTED)
			return;

		if (eventHandler) eventHandler->EventAcceptError(this, strerror(NETERROR));
		return;
	}

	Samurai::IO::Net::InetSocketAddress n_addr;

	if (new_addr.ss_family == AF_INET) {
		struct sockaddr_in* sin = (struct sockaddr_in*) &new_addr;
		n_addr.setRawSocketAddress((void*) &sin->sin_addr, sizeof(struct in_addr), ntohs(sin->sin_port), Samurai::IO::Net::InetAddress::IPv4);

	} else if (new_addr.ss_family == AF_INET6) {
		struct sockaddr_in6* sin6 = (struct sockaddr_in6*) &new_addr;
		n_addr.setRawSocketAddress((void*) &sin6->sin6_addr, sizeof(struct in6_addr), ntohs(sin6->sin6_port), Samurai::IO::Net::InetAddress::IPv6);
	}

	// Create a new socket based on the connected client,
	// and hand it over to the eventHandler.
	std::shared_ptr<Socket> sock = Socket::create(new_sd, n_addr);

	// accept() does not inherit O_NONBLOCK from the listening socket.
	if (!sock->setNonBlocking(true))
	{
		QERR("Unable to set accepted socket non-blocking: %s", strerror(NETERROR));
	}

	if (eventHandler)
		eventHandler->EventAcceptSocket(this, sock);
}

