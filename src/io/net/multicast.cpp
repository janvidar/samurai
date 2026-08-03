/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <algorithm>
#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/interface.h>
#include <samurai/io/net/multicast.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketmonitor.h>


Samurai::IO::Net::MulticastSocket::MulticastSocket(DatagramEventHandler* eh_, const InetAddress& addr_, uint16_t port_) : DatagramSocket(eh_, addr_, port_), netif(0)
{

}

Samurai::IO::Net::MulticastSocket::MulticastSocket(DatagramEventHandler* eh_, uint16_t port_) : DatagramSocket(eh_, port_), netif(0)
{

}

namespace {

/**
 * The address family of a group, or AF_UNSPEC when it is not a usable one.
 *
 * getSockAddr() yields null for an unset address, and reading a sockaddr_in6 as
 * a sockaddr_in would take the wrong bytes for the group.
 */
int groupFamily(Samurai::IO::Net::InetSocketAddress& address)
{
	struct sockaddr* sa = address.getSockAddr();
	if (!sa) return AF_UNSPEC;
	if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6) return AF_UNSPEC;
	return sa->sa_family;
}

}


/**
 * Add or drop a membership, for whichever family the group belongs to.
 *
 * The interface chosen through setInterface() is what it joins on. Leaving that
 * to the route table - which is what this used to do, with INADDR_ANY - joins on
 * whichever interface the default route happens to name, so a group present on
 * another interface is never received.
 */
bool Samurai::IO::Net::MulticastSocket::changeMembership(
	Samurai::IO::Net::InetSocketAddress& group, bool join_it)
{
	const int family = groupFamily(group);
	if (family == AF_UNSPEC) return false;

	/* A socket of one family cannot join a group of the other. */
	if (family != addr->getSockAddrFamily()) return false;

	struct sockaddr* sa = group.getSockAddr();

	if (family == AF_INET)
	{
		struct sockaddr_in* wanted = (struct sockaddr_in*) sa;

		struct in_addr iface;
		memset(&iface, 0, sizeof(iface));
		iface.s_addr = htonl(INADDR_ANY);

		/* Reached through a socket address rather than the InetAddress, whose
		   storage is private to it. */
		if (netif_addr.getType() == Samurai::IO::Net::InetAddress::Version::IPv4)
		{
			Samurai::IO::Net::InetSocketAddress chosen(netif_addr, 0);
			if (struct sockaddr* chosen_sa = chosen.getSockAddr())
				iface = ((struct sockaddr_in*) chosen_sa)->sin_addr;
		}

		const int option = join_it ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
		if (Samurai::IO::Net::membership4(sd, option, wanted->sin_addr, iface, netif) != 0)
		{
			QERR("Unable to %s multicast group: (%d) %s", join_it ? "join" : "leave",
			     Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
			return false;
		}
		return true;
	}

	struct sockaddr_in6* wanted = (struct sockaddr_in6*) sa;
	const int option = join_it ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP;

	if (Samurai::IO::Net::membership6(sd, option, wanted->sin6_addr, netif) != 0)
	{
		QERR("Unable to %s multicast group: (%d) %s", join_it ? "join" : "leave",
		     Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
		return false;
	}
	return true;
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
	return changeMembership(group, false);
}

bool Samurai::IO::Net::MulticastSocket::join(const Samurai::IO::Net::InetAddress& maddr, uint16_t port)
{
	if (!addr) return false;

	Samurai::IO::Net::InetSocketAddress address(maddr, port);
	QDBG("Join multicast address: %s", address.toString().c_str());

	if (!maddr.isMulticast())
	{
		QERR("Not a multicast address: %s", address.toString().c_str());
		return false;
	}

	if (!changeMembership(address, true)) return false;

	/* Recorded so that the destructor, or a later leave(), can drop it. */
	joined.push_back(address);
	return true;
}

bool Samurai::IO::Net::MulticastSocket::leave(const Samurai::IO::Net::InetAddress& maddr, uint16_t port)
{
	if (!addr) return false;

	Samurai::IO::Net::InetSocketAddress address(maddr, port);
	QDBG("Leave multicast address: %s", address.toString().c_str());

	/* Matched on the textual form because InetSocketAddress has no equality
	   operator; it carries a sockaddr whose comparison depends on the family. */
	const std::string wanted = address.toString();
	const auto it = std::ranges::find_if(joined,
		[&wanted](const Samurai::IO::Net::InetSocketAddress& joined_address) {
			return joined_address.toString() == wanted;
		});

	/* Refused rather than passed to the kernel: leaving a group that was never
	   joined is a bookkeeping error in the caller. */
	if (it == joined.end()) return false;

	if (!changeMembership(address, false)) return false;

	joined.erase(it);
	return true;
}

/*
 * Both halves of choosing an interface: the option that sends by it, and the
 * address and index a later join() needs. Storing the handle and doing neither -
 * which is what this used to do - left every caller believing it had chosen while
 * the traffic went out the default route.
 */
bool Samurai::IO::Net::MulticastSocket::setInterface(
	const Samurai::IO::Net::NetworkInterface& iface)
{
	if (!addr) return false;

	const interface_t index = iface.getHandle();

	if (addr->getSockAddrFamily() == AF_INET6)
	{
		if (Samurai::IO::Net::set_multicast_if6(sd, index) != 0)
		{
			QERR("Unable to select multicast interface %s: (%d) %s",
			     iface.getName() ? iface.getName() : "?",
			     Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
			return false;
		}

		netif = index;
		netif_addr = Samurai::IO::Net::InetAddress();
		return true;
	}

	/*
	 * Everywhere but Linux names an Version::IPv4 multicast interface by its own
	 * address, so an interface without one cannot be selected at all. Said rather
	 * than passed over, because the alternative is traffic quietly leaving by
	 * another interface.
	 */
	const InetAddress* address = iface.getAddress();
	if (!address || address->getType() != Samurai::IO::Net::InetAddress::Version::IPv4)
	{
#ifndef SAMURAI_OS_LINUX
		QERR("Interface %s has no IPv4 address to select it by",
		     iface.getName() ? iface.getName() : "?");
		return false;
#endif
	}

	struct in_addr chosen;
	memset(&chosen, 0, sizeof(chosen));
	chosen.s_addr = htonl(INADDR_ANY);

	if (address && address->getType() == Samurai::IO::Net::InetAddress::Version::IPv4)
	{
		Samurai::IO::Net::InetSocketAddress wanted(*address, 0);
		if (struct sockaddr* wanted_sa = wanted.getSockAddr())
			chosen = ((struct sockaddr_in*) wanted_sa)->sin_addr;
	}

	if (Samurai::IO::Net::set_multicast_if4(sd, chosen, index) != 0)
	{
		QERR("Unable to select multicast interface %s: (%d) %s",
		     iface.getName() ? iface.getName() : "?",
		     Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
		return false;
	}

	netif = index;
	if (address) netif_addr = *address;
	return true;
}

void Samurai::IO::Net::MulticastSocket::clearInterface()
{
	netif = 0;
	netif_addr = Samurai::IO::Net::InetAddress();

	if (!addr) return;

	if (addr->getSockAddrFamily() == AF_INET6)
	{
		Samurai::IO::Net::set_multicast_if6(sd, 0);
		return;
	}

	struct in_addr any;
	memset(&any, 0, sizeof(any));
	any.s_addr = htonl(INADDR_ANY);
	Samurai::IO::Net::set_multicast_if4(sd, any, 0);
}

/*
 * The multicast hop limit, which SocketBase::setTimeToLive() does not set: that
 * one sets IP_TTL or the unicast hop count, neither of which a multicast datagram
 * looks at.
 */
bool Samurai::IO::Net::MulticastSocket::setMulticastTimeToLive(uint8_t ttl)
{
	if (!addr) return false;

	const int ret = (addr->getSockAddrFamily() == AF_INET6)
		? Samurai::IO::Net::set_multicast_hops6(sd, ttl)
		: Samurai::IO::Net::set_multicast_ttl(sd, ttl);

	if (ret != 0)
	{
		QERR("Unable to set multicast hop limit (%d) %s",
		     Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
		return false;
	}
	return true;
}

uint8_t Samurai::IO::Net::MulticastSocket::getMulticastTimeToLive() const
{
	if (!addr) return 0;

	uint8_t value = 0;
	const int ret = (addr->getSockAddrFamily() == AF_INET6)
		? Samurai::IO::Net::get_multicast_hops6(sd, value)
		: Samurai::IO::Net::get_multicast_ttl(sd, value);

	if (ret != 0)
	{
		QERR("Unable to read multicast hop limit (%d) %s",
		     Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
		return 0;
	}
	return value;
}

/*
 * NOTE: the width of these options does not follow the address family - the
 * IPv4 ones are a single byte on the BSDs while the IPv6 ones are four. The
 * table in socketglue.h is where that lives; passing an int for the IPv4 option
 * is refused outright on FreeBSD and OpenBSD.
 */
bool Samurai::IO::Net::MulticastSocket::setLoopbackMode(bool toggle)
{
	if (!addr) return false;

	const int ret = (addr->getSockAddrFamily() == AF_INET6)
		? Samurai::IO::Net::set_multicast_loop6(sd, toggle)
		: Samurai::IO::Net::set_multicast_loop(sd, toggle);

	if (ret != 0)
	{
		QERR("Unable to set loopback mode (%d) %s",
		     Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
		return false;
	}
	return true;
}

bool Samurai::IO::Net::MulticastSocket::getLoopbackMode()
{
	if (!addr) return false;

	bool loop = false;
	const int ret = (addr->getSockAddrFamily() == AF_INET6)
		? Samurai::IO::Net::get_multicast_loop6(sd, loop)
		: Samurai::IO::Net::get_multicast_loop(sd, loop);

	if (ret != 0)
	{
		QERR("Unable to get loopback mode (%d) %s",
		     Samurai::IO::Net::net_error(), strerror(Samurai::IO::Net::net_error()));
		return false;
	}
	return loop;
}

bool Samurai::IO::Net::MulticastSocket::listen()
{
        if (!addr) return false;
	if (!setReusePort(true)) return false;
        if (!setReuseAddress(true)) return false;
        if (!setNonBlocking(true)) return false;
        if (!bind(addr.get())) return false;

        setMonitor(Samurai::IO::Net::SocketMonitor::Triggers::Read);
        return true;
}
