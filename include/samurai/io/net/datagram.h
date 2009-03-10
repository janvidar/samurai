/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_DATAGRAM_SOCKET_H
#define HAVE_QUICKDC_DATAGRAM_SOCKET_H

#include <sys/types.h>
#include <time.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>

// struct sockaddr;

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
		virtual ~DatagramPacket();

		size_t size();
		
		int peek(uint8_t*, size_t);
		int read(uint8_t*, size_t);

		void setData(const uint8_t* buf, size_t len);
		void clear();

		void setAddress(SocketAddress*);
		SocketAddress* getAddress();
		Samurai::IO::Buffer* getBuffer();

	protected:
		Samurai::IO::Buffer* buffer;
		DatagramSocket* socket;
		SocketAddress* addr;

	friend class DatagramSocket;
};

/**
 * A very simple to use datagram socket class for non-blocking operations.
 */
class DatagramSocket : public SocketBase {
	public:
	
		DatagramSocket(DatagramEventHandler* eh, const SocketAddress& bindAddr);
	
		/**
		 * Set up a Datagram Socket to bind on the local address and port.
		 */
		DatagramSocket(DatagramEventHandler* eh, const InetAddress& addr, uint16_t port);
		
		/**
		 * Set up a datagram socket to bind on any IP and the given port.
		 */
		DatagramSocket(DatagramEventHandler* eh, uint16_t port);

		/**
		 * Set up a Datagram Socket to bind on any address (0.0.0.0), and
		 * any available OS-assigned port.
		 */
		DatagramSocket(DatagramEventHandler* eh, enum Samurai::IO::Net::InetAddress::Version version);
		virtual ~DatagramSocket();

		bool listen();

		void setEventHandler(DatagramEventHandler* eh);

		int send(DatagramPacket* packet);
		int read(DatagramPacket* packet);


	private:
		// Misc internal stuff
		DatagramEventHandler* eventHandler;
		DatagramPacket* myPacket;

	protected:
		DatagramSocket();
		DatagramSocket(SocketAddress*);
		
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

#endif // HAVE_QUICKDC_DATAGRAM_SOCKET_H
