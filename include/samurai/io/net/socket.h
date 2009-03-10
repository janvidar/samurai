/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_SOCKET_H
#define HAVE_QUICKDC_SOCKET_H

#include <samurai/samurai.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/timer.h>


namespace Samurai {
namespace IO {
namespace Net {

class InetAddress;
class SocketAddress;
class SocketEventHandler;
class SocketMonitor;
class TlsFactory;


/**
 * A very simple to use socket class for non-blocking operations.
 */
class Socket :
	public SocketBase,
	public ResolveEventHandler,
	public Samurai::TimerListener
{
	public:
	
		Socket();
		Socket(socket_t, const SocketAddress& addr);
		Socket(SocketEventHandler* eh, const SocketAddress& addr);
		Socket(SocketEventHandler* eh, const InetAddress& addr, uint16_t port);
		Socket(SocketEventHandler* eh, const std::string& address, uint16_t port);
		virtual ~Socket();

		void lookup();

		void connect();
		void disconnect();

		void setEventHandler(SocketEventHandler* eh);
		SocketEventHandler* getEventHandler() const { return eventHandler; }

		// returns true if we can read more.
		void toggleWriteNotifier(bool toggle);
		bool canWrite() const { return writable; }
		int getReadable() const { return readable; }
		
		ssize_t write(const char* data, size_t length);
		ssize_t read(char* data, size_t length);
		ssize_t peek(char* data, size_t length);
		
		bool TLSInitialize(bool server);
		void TLSDeinitialize();
		void TLSsendHandshake();
		void TLSsendGoodbye();
		
	protected:
		InetAddress* address;
		uint16_t port;
		
	private:
		// Misc internal stuff
		bool autoConnectAfterLookup;
		SocketEventHandler* eventHandler;
		Samurai::Timer* timer;
		bool outbound;
		
		int readable;
		bool writable;

		void sslInitialize(bool server);
		void sslDeinitialize();

	protected:
		void internal_canRead();
		void internal_canWrite();
		void internal_error(int);
		void internal_connected();
		void internal_closed();
		void internal_lookup();
		void internal_timeout();
		
#ifdef SSL_SUPPORT
		void internal_tls_handshake();
		void internal_tls_bye();
		TlsFactory* tls;
#endif

		bool checkConnectTimeout();

		void EventHostFound(InetAddress* addr);
		void EventHostError(enum Samurai::IO::Net::DNS::Resolver::Error error);
		void EventTimeout(Samurai::Timer* timer);
		
	friend class ServerSocket;
	friend class SocketMonitor;
	friend class PollSocketMonitor;
	friend class EPollSocketMonitor;
	friend class SelectSocketMonitor;
	friend class KqueueSocketMonitor;
};

}
}
}

#endif // HAVE_QUICKDC_SOCKET_H
