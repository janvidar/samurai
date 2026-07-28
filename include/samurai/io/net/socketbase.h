/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKETBASE_H
#define HAVE_SAMURAI_SOCKETBASE_H

#include <samurai/io/net/socketglue.h>

#include <memory>
#include <samurai/error.h>

namespace Samurai {
namespace IO {
namespace Net {
class BandwidthManager;
class EventHandler;
class SocketMonitor;
class SocketAddress;
class InetAddress;

enum SocketState
{
	Disconnected,
	HostLookup,
	HostFound,
	Connecting,
	Connected,
	Disconnecting,
	Listening,
	SSLHandshake,
	SSLConnected,
	SSLBye,
	Invalid
};

enum SocketError
{
	ConnectionTimeout,
	ConnectionRefused,
	HostUnreachable,
	HostDown,
	HostNotFound,
	NetDown,
	NetUnreachable,
	SocketRead,
	SocketWrite,
	SocketAccept,
	SocketUnknown
};

enum SSLCertificateError
{
	CertInvalid,
	CertSignerNotFound,
	CertRevoked,
	CertInitialization,
	CertNotFound,
	CertParseError,
	CertExpired,
	CertNotActive,
	CertHostnameMismatch
};

enum SSLDirection
{
	SSLClient,
	SSLServer
};

/**
 * The base for Socket and ServerSocket.
 * This class is not interresting for anything else.
 */
/*
 * Sockets are owned through std::shared_ptr so that the socket monitor can
 * hold a weak reference and detect that an owner has released a socket rather
 * than dispatching an event to freed memory. Construction therefore goes
 * through the create() factory on each concrete socket class; the constructors
 * are protected so that a raw `new Socket` cannot produce an object that
 * shared_from_this() would reject.
 */
class SocketBase : public std::enable_shared_from_this<SocketBase> {
	public:
		enum SocketType { Stream, Datagram };
		
		SocketBase(const Samurai::IO::Net::InetAddress& addr, uint16_t port, enum SocketType = Stream);
		SocketBase(socket_t sd_, const Samurai::IO::Net::SocketAddress& addr, enum SocketType = Stream);
		SocketBase(const Samurai::IO::Net::SocketAddress& addr, enum SocketType = Stream);
		SocketBase(enum SocketType = Stream);
		virtual ~SocketBase();
		
		socket_t getFD() const { return sd; }
		
		/**
		 * Bind the socket to the given local address.
		 */
		bool bind(SocketAddress* localaddr);
		bool bind(SocketAddress* localaddr, std::error_code& ec);
		
		/**
		 * Set the socket as as a non-blocking socket (asynchronous)
		 * if toggle is true. Otherwise blocking.
		 */
		bool setNonBlocking(bool toggle);
		bool setNonBlocking(bool toggle, std::error_code& ec);
		
		/**
		 * Set socket address as reusable. This means we don't have to wait TIME_WAIT 
		 * when we shutdown until we can restart a server at the same address again.
		 */
		bool setReuseAddress(bool toggle);
		bool setReuseAddress(bool toggle, std::error_code& ec);
		bool setReusePort(bool toggle);
		bool setReusePort(bool toggle, std::error_code& ec);

		/**
		 * Get the local address part of the socket.
		 */
		const InetAddress* getLocalAddress() const;
		
		/**
		 * Get the local port address of the socket
		 */
		uint16_t getLocalPort() const;

		/**
		 * Get the remote address part of a connected socket.
		 */
		const InetAddress* getAddress() const;
		
		/**
		 * Get the remote portpart of a connected socket.
		 */
		uint16_t getPort() const;
		
		/**
		 * Toggle keepalive for a TCP socket.
		 */
		bool setKeepAlive(bool toggle);
		bool setKeepAlive(bool toggle, std::error_code& ec);

		/**
		 * Set the send buffer size.
		 * @return true if success.
		 */
		bool setSendBufferSize(size_t size);
		bool setSendBufferSize(size_t size, std::error_code& ec);
		
		/**
		 * Get the send buffer size.
		 */
		size_t getSendBufferSize() const;
		
		/**
		 * Set the receive buffer size.
		 * @return true if success
		 */
		bool setReceiveBufferSize(size_t size);
		bool setReceiveBufferSize(size_t size, std::error_code& ec);
		
		/**
		 * Get the receive buffer size.
		 */
		size_t getReceiveBufferSize() const;

		/**
		 * Returns the IP layer time to live which is
		 * set for the current socket descriptor.
		 */
		uint8_t getTimeToLive() const;
		
		/**
		 * Specifies an IP layer time to live for the given socket descriptor.
		 */
		bool setTimeToLive(uint8_t ttl);
		bool setTimeToLive(uint8_t ttl, std::error_code& ec);

		/**
		 * Restrict an IPv6 socket to IPv6, or allow it to accept IPv4-mapped
		 * connections as well. Without this the behaviour is whatever the
		 * platform defaults to, which differs between Linux and the BSDs.
		 */
		bool setIPv6Only(bool toggle);
		bool setIPv6Only(bool toggle, std::error_code& ec);

		/**
		 * Close the socket.
		 */
		void close();
		
		bool isMonitored() { return monitored; }
		int getMonitorTrigger() { return monitor_trigger; }

		void setMonitor(int trigger);
		void disableMonitor();
	
	protected:
		/**
		 * Called by create() once the object is owned by a shared_ptr.
		 * Anything needing shared_from_this() - registering with the socket
		 * monitor above all - has to happen here rather than in a
		 * constructor, where no shared_ptr exists yet.
		 */
		virtual void initialize() { }

		/**
		 * Act on a readiness event from the socket monitor.
		 *
		 * NOTE: the monitor used to work out what kind of socket it was
		 * holding with three dynamic_casts per event, on every event, and then
		 * branch on the state of a class it should not have had to know about.
		 * Each socket class knows what its own states mean, so it answers for
		 * itself.
		 *
		 * @param trig see SocketMonitor::Triggers (ORed)
		 */
		virtual void handleMonitorEvent(int trig) { (void) trig; }

		/**
		 * Input already buffered above the descriptor, which a readiness poll
		 * cannot see.
		 *
		 * Only a TLS socket ever has any: reading one record decrypts all of
		 * it, so the descriptor can be drained while application data remains
		 * to be handed out. The monitor uses this to decide whether it may
		 * block, and which sockets are readable regardless of what the
		 * operating system says.
		 */
		virtual size_t bufferedInput() const { return 0; }

		/**
		 * Create socket and return true if the call succeeded.
		 * This basically call socket()
		 * If tcp is true then a TCP stream is created,
		 * otherwise udp is assumed.
		 */
		bool createDescriptor(int af);
		bool isIPv6() const;
		
		void setState(enum SocketState state);

	protected:
		socket_t sd;
		Samurai::IO::Net::SocketAddress* addr;
		enum SocketState state;
		mutable InetAddress* ia;

		// See SocketMonitor::Triggers
		int monitor_trigger;
		bool monitored;
		enum SocketType type;
		Samurai::IO::Net::BandwidthManager* bandwidthManager;
		
	friend class SocketMonitor;
};

}
}
}

#endif // HAVE_SAMURAI_SOCKETBASE_H
