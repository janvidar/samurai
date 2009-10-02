/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_SOCKETBASE_H
#define HAVE_QUICKDC_SOCKETBASE_H

#include <samurai/io/net/socketglue.h>

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
class SocketBase {
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
		
		/**
		 * Set the socket as as a non-blocking socket (asynchronous)
		 * if toggle is true. Otherwise blocking.
		 */
		bool setNonBlocking(bool toggle);
		
		/**
		 * Set socket address as reusable. This means we don't have to wait TIME_WAIT 
		 * when we shutdown until we can restart a server at the same address again.
		 */
		bool setReuseAddress(bool toggle);
		bool setReusePort(bool toggle);

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

		/**
		 * Set the send buffer size.
		 * @return true if success.
		 */
		bool setSendBufferSize(size_t size);
		
		/**
		 * Get the send buffer size.
		 */
		size_t getSendBufferSize() const;
		
		/**
		 * Set the receive buffer size.
		 * @return true if success
		 */
		bool setReceiveBufferSize(size_t size);
		
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
		 * Create socket and return true if the call succeeded.
		 * This basically call socket()
		 * If tcp is true then a TCP stream is created,
		 * otherwise udp is assumed.
		 */
		bool create(int af);
		
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

#endif // HAVE_QUICKDC_SOCKETBASE_H
