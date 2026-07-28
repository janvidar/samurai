/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKETEVENT_H
#define HAVE_SAMURAI_SOCKETEVENT_H

#include <samurai/samurai.h>
#include <memory>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/socketbase.h>

namespace Samurai {
namespace IO {
namespace Net {

class InetAddress;
class SocketBase;
class Socket;
class ServerSocket;
class DatagramSocket;
class DatagramPacket;


/*
 * Common base of the four handler interfaces below, and forward declared in
 * socketbase.h. The destructor is virtual because a handler is only ever held
 * as a base pointer, so destroying one through this type has to reach the
 * derived destructor.
 */
class EventHandler {
	public:
		virtual ~EventHandler() = default;
};
	
/**
 * Implement this class to handle socket events.
 */
class SocketEventHandler : public EventHandler {
	public:
		SocketEventHandler() { }
		virtual ~SocketEventHandler() { }
	
	protected:
		/**
		 * Host lookup started
		 */
		virtual void EventHostLookup(const Socket*) { }
		
		/**
		 * Host found. If you started it by lookup()
		 * you will need to connect() here.
		 * Otherwise, you can always use connect().
		 */
		virtual void EventHostFound(const Socket*) { }
		
		/**
		 * Informal message, now connecting.
		 */
		virtual void EventConnecting(const Socket*) { }
		
		/**
		 * Your socket is now connected.
		 */
		virtual void EventConnected(const Socket*) { }
		
		/**
		 * Connect failed due to timeout.
		 */
		virtual void EventTimeout(const Socket*) { }
		
		/**
		 * Socket is now disconnected
		 */
		virtual void EventDisconnected(const Socket*) { }
		
		/**
		 * Data is available from the socket.
		 * This flag is cleared by calling read().
		 */
		virtual void EventDataAvailable(const Socket*) { }
		
		/**
		 * Writing data to the socket will not block.
		 * This flag is cleared by calling write().
		 */
		virtual void EventCanWrite(const Socket*) { }
		
		/**
		 * TLS event: connected
		 * TODO: In the future certificate info should probably be included here.
		 */
		virtual void EventTLSConnected(const Socket*) { }
		
		/**
		 * TLS event: disconnected
		 * This only means the TLS layer has been shutdown, and the connection
		 * is still up.
		 */
		virtual void EventTLSDisconnected(const Socket*) { }
		
		/** 
		 * An error occured, see the error message.
		 */
		virtual void EventError(const Socket*, enum SocketError, const char*) { }
		
	friend class Socket;
};

/**
 * Implement this to handle server socket events.
 */
class ServerSocketEventHandler : public EventHandler {
	public:
		ServerSocketEventHandler() { }
		virtual ~ServerSocketEventHandler() { }
		
	protected:
		virtual void EventAcceptError(const ServerSocket*, const char* msg) = 0;
		/* The accepted socket is handed over as a shared_ptr: the handler
		   takes ownership by keeping it, or lets it go by not. */
		virtual void EventAcceptSocket(const ServerSocket*, std::shared_ptr<Socket> socket) = 0;
	
	friend class ServerSocket;
};

/**
 * Implement this to handle DNS resolve events
 */
class ResolveEventHandler : public EventHandler {
	public:
		ResolveEventHandler() { }
		virtual ~ResolveEventHandler() { }
		
	public:
		/* Borrowed for the duration of the call; copy it to keep it. */
		virtual void EventHostFound(const InetAddress* addr) = 0;
		virtual void EventHostError(Samurai::IO::Net::DNS::Resolver::Error error) = 0;
	
	friend class Resolver;

};


/**
 * Implement this to handle datagram packets (UDP).
 */
class DatagramEventHandler : public EventHandler {
	public:
		DatagramEventHandler() { }
		virtual ~DatagramEventHandler() { }
		
	protected:
		virtual void EventGotDatagram(DatagramSocket*, DatagramPacket*) = 0;
		virtual void EventDatagramError(const DatagramSocket*, const char* msg) = 0;
	
	friend class DatagramSocket;
};

}
}
}

#endif // HAVE_SAMURAI_SOCKETEVENT_H
