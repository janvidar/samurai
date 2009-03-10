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

Samurai::IO::Net::ServerSocket::ServerSocket() : eventHandler(0) {}

Samurai::IO::Net::ServerSocket::ServerSocket(ServerSocketEventHandler* eh, const Samurai::IO::Net::SocketAddress& addr_) :
	Samurai::IO::Net::SocketBase(addr_, Stream), eventHandler(eh)
{
	internal_create();
}

Samurai::IO::Net::ServerSocket::ServerSocket(Samurai::IO::Net::ServerSocketEventHandler* eh, const Samurai::IO::Net::InetAddress& addr_, uint16_t port_) : Samurai::IO::Net::SocketBase(Samurai::IO::Net::InetSocketAddress(addr_, port_), Stream), eventHandler(eh)
{
	internal_create();
}

Samurai::IO::Net::ServerSocket::ServerSocket(ServerSocketEventHandler* eh, uint16_t port_) :
	Samurai::IO::Net::SocketBase(Samurai::IO::Net::InetSocketAddress(port_), Stream), eventHandler(eh)
{
	internal_create();
}

void Samurai::IO::Net::ServerSocket::internal_create()
{
	create(addr->getSockAddrFamily());
}

Samurai::IO::Net::ServerSocket::~ServerSocket()
{
	close();
}

bool Samurai::IO::Net::ServerSocket::listen(size_t backlog) {
	if (!addr) return false;
	if (!setReuseAddress(true)) return false;
	if (!setNonBlocking(true)) return false;
	if (!bind(addr)) return false;
	
	int ret = ::listen(sd, backlog);
	if (ret == -1)
	{
		QERR("Unable to listen to socket: %s (%d)", strerror(NETERROR), NETERROR);
		return false;
	}
	
	setMonitor(SocketMonitor::MRead);
	return true;
}

/** accept() will now have something to accept, if called from SocketMonitor at least. */
void Samurai::IO::Net::ServerSocket::internal_accept() {

	socket_t new_sd = INVALID_SOCKET;
	Samurai::IO::Net::InetSocketAddress n_addr;
	
	if (addr->getSockAddrFamily() == AF_INET) {
		struct sockaddr_in new_addr;
		socklen_t addr_size = sizeof(struct sockaddr_in6);
		new_sd = ::accept(sd, (sockaddr*) &new_addr, (socklen_t*) &addr_size);
		((InetSocketAddress*)&n_addr)->setRawSocketAddress((void*) &new_addr.sin_addr, sizeof(struct in_addr), ntohs(new_addr.sin_port), Samurai::IO::Net::InetAddress::IPv4);
		
	} else if (addr->getSockAddrFamily() == AF_INET6) {
		struct sockaddr_in6 new_addr;
		socklen_t addr_size = sizeof(struct sockaddr_in6);
		new_sd = ::accept(sd, (sockaddr*) &new_addr, (socklen_t*) &addr_size);
		((InetSocketAddress*)&n_addr)->setRawSocketAddress((void*) &new_addr.sin6_addr, sizeof(struct in6_addr), ntohs(new_addr.sin6_port), Samurai::IO::Net::InetAddress::IPv6);
		
	}
	
	if (new_sd == INVALID_SOCKET) {
		eventHandler->EventAcceptError(this, strerror(NETERROR));
		return;
	}
	
	// Create a new socket based on the connected client, 
	// and hand it over to the eventHandler.
	Socket* sock = new Socket(new_sd, n_addr);
	eventHandler->EventAcceptSocket(this, sock);
}

