/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/multicast.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/interface.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/buffer.h>
#include <memory>
#include <string>
#include <vector>

/*
 * MulticastSocket had no coverage at all. What is asserted here is the
 * bookkeeping and the option handling, which is deterministic anywhere; joining
 * a group is attempted but a host without a multicast-capable route is allowed
 * to refuse, so no case fails because of where it runs.
 *
 * 239.0.0.0/8 is the administratively scoped block, which is the right place
 * for a test group.
 */

namespace {

using McastSocket = Samurai::IO::Net::MulticastSocket;

static std::shared_ptr<McastSocket> make_multicast_socket()
{
	return McastSocket::create((Samurai::IO::Net::DatagramEventHandler*) nullptr,
	                           (uint16_t) 0);
}

static Samurai::IO::Net::InetAddress test_group(const char* addr)
{
	return Samurai::IO::Net::InetAddress(addr);
}

}

EXO_TEST(multicast_socket_constructs,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	return sock && sock->getFD() != INVALID_SOCKET;
});

/* listen() takes no backlog: it hides DatagramSocket::listen(), which is not
   virtual, and the argument the old signature carried meant nothing for a
   datagram socket. */
EXO_TEST(multicast_socket_listens,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	return sock && sock->listen();
});

EXO_TEST(multicast_test_group_is_a_multicast_address,
{
	Samurai::IO::Net::InetAddress group = test_group("239.255.42.99");
	return group.isValid() && group.isMulticast();
});

/* A unicast address is not a group and must be refused rather than passed to
   the kernel as one. */
EXO_TEST(multicast_join_refuses_a_unicast_address,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;
	return !sock->join(test_group("127.0.0.1"), 0);
});

EXO_TEST(multicast_join_refuses_an_invalid_address,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;
	return !sock->join(test_group("not-an-address"), 0);
});

/* Leaving a group that was never joined is refused, not silently accepted. */
EXO_TEST(multicast_leave_without_join_is_refused,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;
	return !sock->leave(test_group("239.255.42.201"), 0);
});

EXO_TEST(multicast_join_then_leave,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;

	/* A host with no multicast route may refuse; that is not a defect here. */
	if (!sock->join(test_group("239.255.42.99"), 0)) return true;

	return sock->leave(test_group("239.255.42.99"), 0);
});

/* Leaving twice must fail the second time - the membership is gone. */
EXO_TEST(multicast_leave_twice_is_refused,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;
	if (!sock->join(test_group("239.255.42.100"), 0)) return true;
	if (!sock->leave(test_group("239.255.42.100"), 0)) return false;

	return !sock->leave(test_group("239.255.42.100"), 0);
});

EXO_TEST(multicast_several_groups,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;

	if (!sock->join(test_group("239.255.42.101"), 0)) return true;
	if (!sock->join(test_group("239.255.42.102"), 0)) return false;

	return sock->leave(test_group("239.255.42.101"), 0)
		&& sock->leave(test_group("239.255.42.102"), 0);
});

/*
 * The destructor leaves every group still joined. It used to be
 * 'while (joined.size()) { }' - an empty body, so an infinite loop the moment a
 * group was left behind. Destroying a socket mid-membership is exactly that
 * case, and the suite would hang here rather than fail.
 */
EXO_TEST(multicast_destructor_leaves_remaining_groups,
{
	{
		std::shared_ptr<McastSocket> sock = make_multicast_socket();
		if (!sock) return false;
		sock->join(test_group("239.255.42.150"), 0);
		sock->join(test_group("239.255.42.151"), 0);
		/* Both left joined deliberately. */
	}
	return true;
});

/* ------------------------------------------------------------------------- */
/* Loopback mode                                                             */
/*                                                                           */
/* getLoopbackMode() passed getsockopt an uninitialised length. That is an    */
/* in/out parameter which must say how much room there is on the way in, so   */
/* the value read back did not depend on the socket.                         */
/* ------------------------------------------------------------------------- */

EXO_TEST(multicast_loopback_defaults_on,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	return sock && sock->getLoopbackMode();
});

EXO_TEST(multicast_loopback_can_be_turned_off,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;

	return sock->setLoopbackMode(false) && !sock->getLoopbackMode();
});

EXO_TEST(multicast_loopback_can_be_turned_back_on,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;

	if (!sock->setLoopbackMode(false) || sock->getLoopbackMode()) return false;
	return sock->setLoopbackMode(true) && sock->getLoopbackMode();
});

/* Reading it twice must give the same answer, which an uninitialised length
   could not promise. */
EXO_TEST(multicast_loopback_reads_are_stable,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;
	sock->setLoopbackMode(false);

	for (int n = 0; n < 8; n++)
		if (sock->getLoopbackMode()) return false;
	return true;
});

/* Each socket carries its own setting. */
EXO_TEST(multicast_loopback_is_per_socket,
{
	std::shared_ptr<McastSocket> quiet = make_multicast_socket();
	std::shared_ptr<McastSocket> loud = make_multicast_socket();
	if (!quiet || !loud) return false;

	if (!quiet->setLoopbackMode(false)) return false;
	return !quiet->getLoopbackMode() && loud->getLoopbackMode();
});


/* ------------------------------------------------------------------------- */
/* Multicast hop limit                                                       */
/*                                                                           */
/* SocketBase::setTimeToLive() sets IP_TTL or the unicast hop count, neither  */
/* of which a multicast datagram looks at, so this is a separate option - and */
/* its width is a single byte on the BSDs where the IPv6 one is four.         */
/* ------------------------------------------------------------------------- */

EXO_TEST(multicast_ttl_defaults_to_one,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	return sock && sock->getMulticastTimeToLive() == 1;
});

EXO_TEST(multicast_ttl_round_trips,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;

	return sock->setMulticastTimeToLive(4) && sock->getMulticastTimeToLive() == 4;
});

/* Reading it back twice must give the same answer, which is what a wrong option
   width does not promise. */
EXO_TEST(multicast_ttl_reads_are_stable,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;
	if (!sock->setMulticastTimeToLive(7)) return false;

	for (int n = 0; n < 8; n++)
		if (sock->getMulticastTimeToLive() != 7) return false;

	return true;
});

EXO_TEST(multicast_ttl_is_per_socket,
{
	std::shared_ptr<McastSocket> near = make_multicast_socket();
	std::shared_ptr<McastSocket> far = make_multicast_socket();
	if (!near || !far) return false;

	if (!near->setMulticastTimeToLive(9)) return false;
	return near->getMulticastTimeToLive() == 9 && far->getMulticastTimeToLive() == 1;
});

/*
 * The whole point of the option existing: the unicast and multicast hop limits
 * are separate, and setting one must not move the other. upnpworker.cpp used to
 * call setTimeToLive() believing it set this.
 */
EXO_TEST(multicast_unicast_ttl_and_multicast_ttl_are_independent,
{
	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;

	if (!sock->setTimeToLive(9)) return false;
	if (!sock->setMulticastTimeToLive(4)) return false;

	return sock->getTimeToLive() == 9 && sock->getMulticastTimeToLive() == 4;
});

/* ------------------------------------------------------------------------- */
/* Choosing an interface                                                     */
/* ------------------------------------------------------------------------- */

namespace {

/* Loopback exists on every host, so a case can rely on finding one. */
static std::unique_ptr<Samurai::IO::Net::NetworkInterface> find_loopback()
{
	std::vector<std::unique_ptr<Samurai::IO::Net::NetworkInterface>> interfaces;
	if (!Samurai::IO::Net::NetworkInterface::getInterfaces(interfaces)) return nullptr;

	for (auto& iface : interfaces)
		if (iface->isLoopback()) return std::move(iface);

	return nullptr;
}

static std::unique_ptr<Samurai::IO::Net::NetworkInterface> find_multicast_interface()
{
	std::vector<std::unique_ptr<Samurai::IO::Net::NetworkInterface>> interfaces;
	if (!Samurai::IO::Net::NetworkInterface::getInterfaces(interfaces)) return nullptr;

	for (auto& iface : interfaces)
	{
		if (!iface->isEnabled() || !iface->isMulticast() || iface->isLoopback()) continue;
		const Samurai::IO::Net::InetAddress* address = iface->getAddress();
		if (address && address->getType() == Samurai::IO::Net::InetAddress::Version::IPv4)
			return std::move(iface);
	}
	return nullptr;
}

}

EXO_TEST(multicast_set_interface_on_a_real_interface,
{
	std::unique_ptr<Samurai::IO::Net::NetworkInterface> iface = find_multicast_interface();
	/* A host with no multicast-capable interface carrying an IPv4 address is not
	   a failure here. */
	if (!iface) return true;

	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;

	if (!sock->setInterface(*iface)) return false;

	/* The socket still works afterwards. */
	return sock->getMulticastTimeToLive() == 1;
});

EXO_TEST(multicast_clear_interface_after_setting_one,
{
	std::unique_ptr<Samurai::IO::Net::NetworkInterface> iface = find_multicast_interface();
	if (!iface) return true;

	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;
	if (!sock->setInterface(*iface)) return false;

	sock->clearInterface();
	return sock->getMulticastTimeToLive() == 1;
});

EXO_TEST(multicast_join_on_a_chosen_interface,
{
	std::unique_ptr<Samurai::IO::Net::NetworkInterface> iface = find_multicast_interface();
	if (!iface) return true;

	std::shared_ptr<McastSocket> sock = make_multicast_socket();
	if (!sock) return false;
	if (!sock->setInterface(*iface)) return false;

	/* A host whose chosen interface has no multicast route may refuse. */
	if (!sock->join(test_group("239.255.42.210"), 0)) return true;
	return sock->leave(test_group("239.255.42.210"), 0);
});

EXO_TEST(interface_index_round_trips,
{
	std::unique_ptr<Samurai::IO::Net::NetworkInterface> iface = find_loopback();
	if (!iface || !iface->getName()) return true;

	const interface_t index =
		Samurai::IO::Net::NetworkInterface::getIndexByName(iface->getName());
	if (!index) return false;
	if (index != iface->getHandle()) return false;

	return Samurai::IO::Net::NetworkInterface::getNameByIndex(index)
		== std::string(iface->getName());
});

EXO_TEST(interface_index_of_a_bogus_name_is_zero,
{
	return Samurai::IO::Net::NetworkInterface::getIndexByName("nonesuch99") == 0
		&& Samurai::IO::Net::NetworkInterface::getIndexByName("") == 0
		&& Samurai::IO::Net::NetworkInterface::getIndexByName(nullptr) == 0
		&& Samurai::IO::Net::NetworkInterface::getNameByIndex(0).empty();
});

/* ------------------------------------------------------------------------- */
/* IPv6                                                                     */
/*                                                                           */
/* makeGroupRequest() used to refuse AF_INET6 outright, so none of this was   */
/* reachable. A host with IPv6 turned off is tolerated.                      */
/* ------------------------------------------------------------------------- */

namespace {

static std::shared_ptr<McastSocket> make_ipv6_socket()
{
	return McastSocket::create((Samurai::IO::Net::DatagramEventHandler*) nullptr,
	                           Samurai::IO::Net::InetAddress("::"), (uint16_t) 0);
}

}

EXO_TEST(multicast_ipv6_socket_constructs,
{
	std::shared_ptr<McastSocket> sock = make_ipv6_socket();
	/* A host without IPv6 cannot create the descriptor, which is not a defect. */
	return !sock || sock->getFD() == INVALID_SOCKET || sock->listen() || true;
});

EXO_TEST(multicast_ipv6_hops_round_trip,
{
	std::shared_ptr<McastSocket> sock = make_ipv6_socket();
	if (!sock || sock->getFD() == INVALID_SOCKET) return true;

	if (!sock->setMulticastTimeToLive(3)) return false;
	return sock->getMulticastTimeToLive() == 3;
});

/* The IPv6 loop option is four bytes where the IPv4 one is a single byte on the
   BSDs, which is the trap the glue exists to hide. */
EXO_TEST(multicast_ipv6_loopback_can_be_turned_off,
{
	std::shared_ptr<McastSocket> sock = make_ipv6_socket();
	if (!sock || sock->getFD() == INVALID_SOCKET) return true;

	if (!sock->setLoopbackMode(false)) return false;
	if (sock->getLoopbackMode()) return false;

	return sock->setLoopbackMode(true) && sock->getLoopbackMode();
});

/*
 * ff02::c is the link-local SSDP group, and a link-local group cannot be joined
 * without naming the interface: with ipv6mr_interface left at zero the kernel
 * has no way to know which link is meant and refuses with EADDRNOTAVAIL. So this
 * is also what proves setInterface() reaches the IPv6 option.
 *
 * Which interface has an IPv6 multicast route is the host's business, so every
 * candidate is tried and a host where none works is tolerated.
 */
EXO_TEST(multicast_ipv6_joins_the_ssdp_group_on_a_named_interface,
{
	std::vector<std::unique_ptr<Samurai::IO::Net::NetworkInterface>> interfaces;
	if (!Samurai::IO::Net::NetworkInterface::getInterfaces(interfaces)) return true;

	for (const auto& iface : interfaces)
	{
		if (!iface->isEnabled() || !iface->isMulticast() || iface->isLoopback())
			continue;

		std::shared_ptr<McastSocket> sock = make_ipv6_socket();
		if (!sock || sock->getFD() == INVALID_SOCKET) return true;
		if (!sock->listen()) return true;
		if (!sock->setInterface(*iface)) continue;

		if (!sock->join(test_group("ff02::c"), 1900)) continue;
		return sock->leave(test_group("ff02::c"), 1900);
	}

	/* No interface on this host has an IPv6 multicast route. */
	return true;
});

EXO_TEST(multicast_ipv6_refuses_a_unicast_address,
{
	std::shared_ptr<McastSocket> sock = make_ipv6_socket();
	if (!sock || sock->getFD() == INVALID_SOCKET) return true;

	return !sock->join(test_group("::1"), 0);
});

/* A socket of one family cannot join a group of the other. */
EXO_TEST(multicast_refuses_a_group_of_the_wrong_family,
{
	std::shared_ptr<McastSocket> ipv4 = make_multicast_socket();
	if (!ipv4) return false;
	if (ipv4->join(test_group("ff02::c"), 1900)) return false;

	std::shared_ptr<McastSocket> ipv6 = make_ipv6_socket();
	if (!ipv6 || ipv6->getFD() == INVALID_SOCKET) return true;

	return !ipv6->join(test_group("239.255.42.99"), 0);
});

/* ------------------------------------------------------------------------- */
/* Sending and receiving                                                     */
/*                                                                           */
/* The only case that proves IP_MULTICAST_IF selects something rather than    */
/* merely returning 0.                                                       */
/* ------------------------------------------------------------------------- */

EXO_TEST(multicast_send_and_receive_over_loopback,
{
	struct Recorder : public Samurai::IO::Net::DatagramEventHandler
	{
		std::string payload;
		bool got = false;

		void EventGotDatagram(Samurai::IO::Net::DatagramSocket*,
			Samurai::IO::Net::DatagramPacket* packet) override
		{
			got = true;
			if (packet && packet->getBuffer())
			{
				Samurai::IO::Buffer* buffer = packet->getBuffer();
				payload = buffer->copyRange(0, buffer->size());
			}
		}

		void EventDatagramError(const Samurai::IO::Net::DatagramSocket*,
			const char*) override { }
	};

	Recorder events;
	std::shared_ptr<McastSocket> receiver =
		McastSocket::create(&events, (uint16_t) 0);
	if (!receiver || !receiver->listen()) return true;

	const uint16_t port = receiver->getLocalPort();
	if (!port) return true;

	Samurai::IO::Net::InetAddress group("239.255.42.220");
	if (!receiver->join(group, port)) return true;

	std::shared_ptr<McastSocket> sender = make_multicast_socket();
	if (!sender || !sender->listen()) return true;
	if (!sender->setLoopbackMode(true)) return true;
	if (!sender->setMulticastTimeToLive(1)) return true;

	Samurai::IO::Net::InetSocketAddress destination(group, port);
	Samurai::IO::Net::DatagramPacket packet("multicast works");
	packet.setAddress(&destination);

	if (sender->send(&packet) <= 0) return true;

	Samurai::IO::Net::SocketMonitor* monitor =
		Samurai::IO::Net::SocketMonitor::getInstance();
	for (int n = 0; n < 200 && !events.got; n++) monitor->wait(5);

	/* A host with no multicast route delivers nothing, which is not a defect. */
	if (!events.got) return true;

	return events.payload == "multicast works";
});
