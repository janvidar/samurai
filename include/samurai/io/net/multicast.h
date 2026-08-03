/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_MULTICAST_SOCKET_H
#define HAVE_SAMURAI_MULTICAST_SOCKET_H

#include <sys/types.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/interface.h>

namespace Samurai {
	namespace IO {
		class Buffer;
		namespace Net {

			class DatagramSocket;
			class DatagramPacket;
			class InetSocketAddress;
			class NetworkInterface;

			/**
			* A very simple to use datagram socket class for non-blocking operations.
			*/
			class MulticastSocket : public DatagramSocket {
				public:
					template<typename... Args>
					static std::shared_ptr<MulticastSocket> create(Args&&... args)
					{
						std::shared_ptr<MulticastSocket> self(new MulticastSocket(std::forward<Args>(args)...));
						self->initialize();
						return self;
					}

				public:

					/**
					 * Set up a MulticastSocket to bind on the local address and port.
					 */
					MulticastSocket(DatagramEventHandler* eh, const InetAddress& addr, uint16_t port);

					/**
					 * Set up a MulticastSocket to bind on any address and the given port.
					 */
					MulticastSocket(DatagramEventHandler* eh, uint16_t port);

					/**
					 * Join the given multicast-address.
					 */
					bool join(const InetAddress& maddr, uint16_t port);

					/**
					 * Join the given multicast-address.
					 */
					bool leave(const InetAddress& maddr, uint16_t port);

					virtual ~MulticastSocket();

					/**
					 * Choose the interface outbound multicast leaves by, and
					 * that a later join() joins on.
					 *
					 * Without this, multicast follows the default route - which
					 * on a host with a VPN, a container bridge or a second
					 * adapter is very often not the interface the group is on,
					 * and is the most common reason a search finds nothing.
					 *
					 * @return false if the interface cannot be selected for this
					 *         socket's address family. On the BSDs and on
					 *         Windows an Version::IPv4 multicast interface is
					 *         named by its own address, so one without an
					 *         Version::IPv4 address cannot be selected at all;
					 *         that is reported rather than passed over.
					 */
					bool setInterface(const NetworkInterface& iface);

					/** Go back to whichever interface the route table picks. */
					void clearInterface();

					/**
					 * The hop limit for outbound multicast datagrams.
					 *
					 * This is not SocketBase::setTimeToLive(), which sets IP_TTL
					 * or the unicast hop limit and has no effect whatsoever on a
					 * multicast datagram. The default is 1: one link, and no
					 * router will forward it.
					 */
					bool setMulticastTimeToLive(uint8_t ttl);
					uint8_t getMulticastTimeToLive() const;

					/**
					 * Toggle loopback mode (ie. recive what you send yourself).
					 */
					bool setLoopbackMode(bool toggle);

					/**
					 * Returns true if the socket is set to receive locally generated multicast
					 * packets.
					 */
					bool getLoopbackMode();

					/**
					 * Bind and start receiving. Replaces
					 * DatagramSocket::listen(); the backlog argument this used
					 * to take was ignored, and taking one made this hide the
					 * base's version rather than override it.
					 */
					bool listen() override;

				protected:
					/* Drop one group membership, ignoring whether it is
					 * recorded in 'joined'. */
					bool dropMembership(InetSocketAddress& group);

					/* Add or drop a membership, for whichever family the group
					 * belongs to, on the interface setInterface() chose. */
					bool changeMembership(InetSocketAddress& group, bool join_it);

					/* The groups this socket has joined and not yet left, held
					 * by value so the destructor can leave them all. */
					std::vector<InetSocketAddress> joined;
					interface_t netif;
					/* The chosen interface's own Version::IPv4 address, which is
					 * how every platform but Linux names one. */
					InetAddress netif_addr;
			
				friend class SocketMonitor;
				friend class PollSocketMonitor;
				friend class EPollSocketMonitor;
				friend class SelectSocketMonitor;
				friend class KqueueSocketMonitor;
			};
	
		}
	}
}

#endif // HAVE_SAMURAI_MULTICAST_SOCKET_H


