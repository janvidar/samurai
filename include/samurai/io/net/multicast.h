/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_MULTICAST_SOCKET_H
#define HAVE_SAMURAI_MULTICAST_SOCKET_H

#include <sys/types.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/datagram.h>
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
					 * Set the interface to be used for multicast.
					 */
					void setInterface(NetworkInterface* iface);

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
					 * Hides DatagramSocket::listen(), which takes no backlog.
					 */
					bool listen(size_t backlog = 5);

				protected:
					/* Drop one group membership, ignoring whether it is
					 * recorded in 'joined'. */
					bool dropMembership(InetSocketAddress& group);

					/* The groups this socket has joined and not yet left, held
					 * by value so the destructor can leave them all. */
					std::vector<InetSocketAddress> joined;
					interface_t netif;
			
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


