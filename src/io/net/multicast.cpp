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

namespace {

/*
 * Describe 'address' as an IPv4 group membership request.
 *
 * Returns false for anything that is not an IPv4 address: getSockAddr() yields
 * null for an unset address, and an IPv6 one produces a sockaddr_in6 that must
 * not be read as a sockaddr_in.
 */
bool makeGroupRequest(Samurai::IO::Net::InetSocketAddress& address, struct ip_mreq& mreq)
{
	memset(&mreq, 0, sizeof(mreq));

	struct sockaddr* sa = address.getSockAddr();
	if (!sa || sa->sa_family != AF_INET) return false;

	struct sockaddr_in* iaddr = (struct sockaddr_in*) sa;
	memcpy(&mreq.imr_multiaddr, &iaddr->sin_addr, sizeof(mreq.imr_multiaddr));
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	return true;
}

}

/*
 * The base destructor is what closes the socket, and it runs after this one, so
 * the descriptor is still usable here and the memberships can be dropped.
 */
Samurai::IO::Net::MulticastSocket::~MulticastSocket()
{
	while (!joined.empty()) {
		Samurai::IO::Net::InetSocketAddress group = joined.back();
		joined.pop_back();
		dropMembership(group);
	}
}

bool Samurai::IO::Net::MulticastSocket::dropMembership(Samurai::IO::Net::InetSocketAddress& group)
{
	struct ip_mreq mreq;
	if (!makeGroupRequest(group, mreq)) return false;

	if (SAMURAI_SETSOCKOPT(sd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) != 0)
	{
		QERR("Unable to leave multicast address: (%d) %s", NETERROR, strerror(NETERROR));
		return false;
	}

	return true;
}

bool Samurai::IO::Net::MulticastSocket::join(const Samurai::IO::Net::InetAddress& maddr, uint16_t port)
{
	Samurai::IO::Net::InetSocketAddress address(maddr, port);
	QDBG("Join multicast address: %s", address.toString().c_str());

	// FIXME: Use specified Interface?
	// int actual_interface = netif;

	struct ip_mreq mreq;
	if (!makeGroupRequest(address, mreq))
	{
		QERR("Not an IPv4 multicast address: %s", address.toString().c_str());
		return false;
	}

	if (SAMURAI_SETSOCKOPT(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0)
	{
		QERR("Unable to join multicast address: (%d) %s", NETERROR, strerror(NETERROR));
		return false;
	}

	/* Recorded so that the destructor, or a later leave(), can drop it. */
	joined.push_back(address);
	return true;
}

bool Samurai::IO::Net::MulticastSocket::leave(const Samurai::IO::Net::InetAddress& maddr, uint16_t port)
{
	Samurai::IO::Net::InetSocketAddress address(maddr, port);
	QDBG("Leave multicast address: %s", address.toString().c_str());

	if (!dropMembership(address)) return false;

	/* Matched on the textual form: InetAddress::operator== is neither a const
	   member nor takes a const argument, so a const address cannot use it. */
	const std::string wanted = address.toString();
	for (std::vector<Samurai::IO::Net::InetSocketAddress>::iterator it = joined.begin();
		it != joined.end(); ++it)
	{
		if (it->toString() == wanted) {
			joined.erase(it);
			break;
		}
	}

	return true;
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

bool Samurai::IO::Net::MulticastSocket::listen()
{
        if (!addr) return false;
	if (!setReusePort(true)) return false;
        if (!setReuseAddress(true)) return false;
        if (!setNonBlocking(true)) return false;
        if (!bind(addr)) return false;

        setMonitor(SocketMonitor::MRead);
        return true;
}
