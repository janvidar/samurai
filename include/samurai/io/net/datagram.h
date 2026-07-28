/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_DATAGRAM_SOCKET_H
#define HAVE_SAMURAI_DATAGRAM_SOCKET_H

#include <sys/types.h>
#include <time.h>
#include <samurai/io/net/socketbase.h>
#include <memory>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>

namespace Samurai {
namespace IO {
class Buffer;
namespace Net {
class DatagramSocket;
class SocketAddress;
class InetAddress;


class DatagramPacket {
	public:
		DatagramPacket(const uint8_t* buf, size_t len);
		DatagramPacket(const char* buf);
		DatagramPacket(Samurai::IO::Buffer* buffer);
		DatagramPacket();
		~DatagramPacket();

		/* Releases raw pointers in its destructor, so the implicit copy
		 * operations would release them a second time. */
		DatagramPacket(const DatagramPacket&) = delete;
		DatagramPacket& operator=(const DatagramPacket&) = delete;

		size_t size();
		
		int peek(uint8_t*, size_t);
		int read(uint8_t*, size_t);

		void setData(const uint8_t* buf, size_t len);
		void clear();

		void setAddress(SocketAddress*);
		SocketAddress* getAddress();
		Samurai::IO::Buffer* getBuffer();

	protected:
		std::unique_ptr<Samurai::IO::Buffer> buffer;
		DatagramSocket* socket;
		std::unique_ptr<SocketAddress> addr;

	friend class DatagramSocket;
};

/**
 * A very simple to use datagram socket class for non-blocking operations.
 */
class DatagramSocket : public SocketBase {
	public:
		/**
		 * Construct a DatagramSocket owned by a shared_ptr. The constructors are
		 * protected: the socket monitor holds weak references, which requires
		 * that every socket be owned by a shared_ptr from the start.
		 */
		template<typename... Args>
		static std::shared_ptr<DatagramSocket> create(Args&&... args)
		{
			std::shared_ptr<DatagramSocket> self(new DatagramSocket(std::forward<Args>(args)...));
			self->initialize();
			return self;
		}

	protected:

		DatagramSocket(DatagramEventHandler* eh, const SocketAddress& bindAddr);
	
		/**
		 * Set up a SocketType::Datagram Socket to bind on the local address and port.
		 */
		DatagramSocket(DatagramEventHandler* eh, const InetAddress& addr, uint16_t port);
		
		/**
		 * Set up a datagram socket to bind on any IP and the given port.
		 */
		DatagramSocket(DatagramEventHandler* eh, uint16_t port);

		/**
		 * Set up a SocketType::Datagram Socket to bind on any address (0.0.0.0), and
		 * any available OS-assigned port.
		 */
		DatagramSocket(DatagramEventHandler* eh, Samurai::IO::Net::InetAddress::Version version);
	public:
		~DatagramSocket() override;

		/* Releases raw pointers in its destructor, so the implicit copy
		 * operations would release them a second time. */
		DatagramSocket(const DatagramSocket&) = delete;
		DatagramSocket& operator=(const DatagramSocket&) = delete;

		/* Virtual so that MulticastSocket, which has to set SO_REUSEPORT and
		 * register with the monitor, replaces it rather than hiding it. */
		virtual bool listen();

		void setEventHandler(DatagramEventHandler* eh);

		int send(DatagramPacket* packet);
		int read(DatagramPacket* packet);


	private:
		// Misc internal stuff
		DatagramEventHandler* eventHandler;
		std::unique_ptr<DatagramPacket> myPacket;

		/* Receive scratch. A member rather than a 64 KB stack array zeroed
		   on every read(). */
		uint8_t readbuf[65536];

	protected:
		void initialize() override;

		DatagramSocket();
		DatagramSocket(SocketAddress*);
		
		void handleMonitorEvent(SocketMonitor::Triggers trig) override;

		void internal_create();
		void internal_canRead();
		void internal_error();

	friend class SocketMonitor;
	friend class PollSocketMonitor;
	friend class EPollSocketMonitor;
	friend class SelectSocketMonitor;
	friend class KqueueSocketMonitor;
};

}
}
}

#endif // HAVE_SAMURAI_DATAGRAM_SOCKET_H
