/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/multicast.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/inetaddress.h>
#include <memory>
#include <string>

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

