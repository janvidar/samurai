/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_SERVERSOCKET_H
#define HAVE_QUICKDC_SERVERSOCKET_H

#include <samurai/samurai.h>
#include <samurai/io/net/socketbase.h>

namespace Samurai {
namespace IO {
namespace Net {

class InetAddress;
class ServerSocketEventHandler;
class SocketMonitor;
class IOBuffer;
struct timeval;
struct timezone;

/**
 * A very simple asynchronous server socket class.
 */
class ServerSocket : public SocketBase {
	public:
	
		/**
		 * Set up a socket and bind to the specified address.
		 */
		ServerSocket(Samurai::IO::Net::ServerSocketEventHandler* eh, const Samurai::IO::Net::SocketAddress& addr);
		
		/**
		 * Set up a socket and bind to the specified port (on any IP).
		 */
		ServerSocket(Samurai::IO::Net::ServerSocketEventHandler* eh, uint16_t port);
		
		/**
		 * Set up a socket and bind to the specified address and port.
		 */
		ServerSocket(Samurai::IO::Net::ServerSocketEventHandler* eh, const Samurai::IO::Net::InetAddress& addr, uint16_t port);
		
		virtual ~ServerSocket();

		virtual bool listen(size_t backlog = 5);

	protected:
		ServerSocket();
		void internal_create();
		void internal_accept();
		bool isServer() const { return true; }
		
		ServerSocketEventHandler* eventHandler;
		
	friend class SocketMonitor;
	friend class PollSocketMonitor;
	friend class EPollSocketMonitor;
	friend class SelectSocketMonitor;
	friend class KqueueSocketMonitor;

};

}
}
}

#endif // HAVE_QUICKDC_SERVERSOCKET_H
