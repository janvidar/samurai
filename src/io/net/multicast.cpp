/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/interface.h>
#include <samurai/io/net/multicast.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketmonitor.h>


Samurai::IO::Net::MulticastSocket::MulticastSocket(DatagramEventHandler* eh_, const InetAddress& addr_, uint16_t port_) : DatagramSocket(eh_, addr_, port_), netif(0)
{

}

Samurai::IO::Net::MulticastSocket::MulticastSocket(DatagramEventHandler* eh_, uint16_t port_) : DatagramSocket(eh_, port_), netif(0)
{

}

Samurai::IO::Net::MulticastSocket::~MulticastSocket()
{
	while (joined.size()) {
		// leave all sockets.
	}

}

bool Samurai::IO::Net::MulticastSocket::join(const Samurai::IO::Net::InetAddress& maddr, uint16_t port)
{
	Samurai::IO::Net::InetSocketAddress* address = new Samurai::IO::Net::InetSocketAddress(maddr, port);
	QDBG("Join multicast address: %s", address->toString());
	
	// FIXME: Use specified Interface?
	// int actual_interface = netif;
	
	struct ip_mreq mreq;
	memset(&mreq, 0, sizeof(mreq));
	struct sockaddr_in* iaddr = (struct sockaddr_in*) address->getSockAddr();
	
	
	void* src = &iaddr->sin_addr;
	memcpy(&mreq.imr_multiaddr, src, sizeof(mreq.imr_multiaddr));
	
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);

	if (SAMURAI_SETSOCKOPT(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0)
	{
		QERR("Unable to join multicast address: (%d) %s", NETERROR, strerror(NETERROR));
		return false;
	}
	
	return true;
}

bool Samurai::IO::Net::MulticastSocket::leave(const Samurai::IO::Net::InetAddress& maddr, uint16_t port)
{
	Samurai::IO::Net::InetSocketAddress* address = new Samurai::IO::Net::InetSocketAddress(maddr, port);
	QDBG("Leave multicast address: %s", address->toString());
	delete address;
	return false;
}

void Samurai::IO::Net::MulticastSocket::setInterface(Samurai::IO::Net::NetworkInterface* iface)
{
	if (iface)
	{
		netif = iface->getHandle();
	}
}

bool Samurai::IO::Net::MulticastSocket::setLoopbackMode(bool toggle)
{
	socklen_t loop = toggle ? 1 : 0;
	if (SAMURAI_SETSOCKOPT(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) != 0)
	{
		QERR("Unable to set loopback mode (%d) %s", NETERROR, strerror(NETERROR));
		return false;
	}
	return true;
}

bool Samurai::IO::Net::MulticastSocket::getLoopbackMode()
{
	socklen_t loop;
	socklen_t size;
	if (SAMURAI_GETSOCKOPT(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, &size) != 0)
	{
		QERR("Unable to get loopback mode (%d) %s", NETERROR, strerror(NETERROR));
		return false;
	}
	return loop != 0;
}

bool Samurai::IO::Net::MulticastSocket::listen(size_t backlog)
{
	(void) backlog;

        if (!addr) return false;
	if (!setReusePort(true)) return false;
        if (!setReuseAddress(true)) return false;
        if (!setNonBlocking(true)) return false;
        if (!bind(addr)) return false;

        setMonitor(SocketMonitor::MRead);
        return true;
}
