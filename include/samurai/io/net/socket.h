/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKET_H
#define HAVE_SAMURAI_SOCKET_H

#include <samurai/samurai.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/proxy.h>
#include <samurai/io/net/socks5.h>
#include <samurai/io/net/tlsfactory.h>
#include <memory>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/timer.h>
#include <samurai/error.h>

#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>


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
		/**
		 * Construct a Socket owned by a shared_ptr. The constructors are
		 * protected: the socket monitor holds weak references, which requires
		 * that every socket be owned by a shared_ptr from the start.
		 */
		template<typename... Args>
		static std::shared_ptr<Socket> create(Args&&... args)
		{
			std::shared_ptr<Socket> self(new Socket(std::forward<Args>(args)...));
			self->initialize();
			return self;
		}

	protected:
		Socket();
		Socket(socket_t, const SocketAddress& addr);
		Socket(SocketEventHandler* eh, const SocketAddress& addr);
		Socket(SocketEventHandler* eh, const InetAddress& addr, uint16_t port);
		Socket(SocketEventHandler* eh, const std::string& address, uint16_t port);
	public:
		~Socket() override;

		/* Releases raw pointers in its destructor, so the implicit copy
		 * operations would release them a second time. */
		Socket(const Socket&) = delete;
		Socket& operator=(const Socket&) = delete;

		/**
		 * Resolve the peer's name, without connecting.
		 *
		 * Refused on a proxied socket, which reports SocketError::HostNotFound
		 * instead: the whole point of reaching a peer through a proxy is that
		 * its name is never handed to the system resolver, and this is the one
		 * call that would. Call connect() directly - it needs no address.
		 */
		void lookup();

		void connect();
		void disconnect();

		/**
		 * Reach the peer through a proxy rather than directly.
		 *
		 * Has to be set before connect(). A socket starts from
		 * ProxySettings::getDefault(), taken when it was constructed, so a
		 * program that proxies everything says so once rather than at every
		 * call site.
		 *
		 * A proxied socket does not resolve its peer: the name travels to the
		 * proxy inside the request. Everything above the socket is unchanged -
		 * EventConnected arrives when the tunnel is up and not before, so a TLS
		 * handshake started from it runs inside the tunnel and verifies the
		 * peer's certificate against the name that was asked for.
		 *
		 * Only outbound sockets. Setting this on one that came from
		 * ServerSocket::accept() does nothing.
		 */
		void setProxy(const ProxySettings& settings);
		const ProxySettings& getProxy() const { return proxy; }
		bool isProxied() const { return outbound && proxy.isEnabled(); }

		/**
		 * The name the connection was asked for.
		 *
		 * Under a proxy this is the only thing that identifies the peer.
		 * getAddress() reports the proxy, because that is what the descriptor is
		 * connected to, and the address in the proxy's reply is its own bound
		 * address rather than the peer's - Tor answers 0.0.0.0.
		 */
		std::string getPeerName() const;

		/**
		 * How long connect() waits for the peer before reporting
		 * SocketError::ConnectionTimeout. The default is CONNECT_TIMEOUT.
		 *
		 * Has to be set before connect(), which is where the timer is armed.
		 */
		void setConnectTimeout(std::chrono::milliseconds timeout);
		std::chrono::milliseconds getConnectTimeout() const { return connect_timeout; }

		void setEventHandler(SocketEventHandler* eh);
		SocketEventHandler* getEventHandler() const { return eventHandler; }

		// returns true if we can read more.
		void toggleWriteNotifier(bool toggle);
		bool canWrite() const { return writable; }
		int getReadable() const { return readable; }
		
		/**
		 * Read up to 'length' bytes.
		 *
		 * @param transferred set to the number of bytes read (0 unless the
		 *        result is Samurai::IO::ReadResult::Ok).
		 * @param ec set when the result is Samurai::IO::ReadResult::Error.
		 *
		 * NOTE: the ssize_t overloads below cannot distinguish a peer that
		 * closed from one that has simply sent nothing yet, nor either of
		 * those from a failed socket - all three answer 0. Prefer this one.
		 */
		Samurai::IO::ReadResult read(char* data, size_t length,
		                             size_t& transferred, std::error_code& ec);

		Samurai::IO::ReadResult peek(char* data, size_t length,
		                             size_t& transferred, std::error_code& ec);

		/**
		 * Write up to 'length' bytes.
		 * @return bytes written, or -1 on error with 'ec' set. A return of 0
		 *         with no error set means the socket would have blocked.
		 */
		ssize_t write(const char* data, size_t length, std::error_code& ec);

		/**
		 * Write several buffers as one operation, without joining them first.
		 * A protocol layer with a header and a body can send both in a single
		 * segment without copying them into one buffer.
		 *
		 * Partial writes work as they do above: the return value is the number
		 * of bytes accepted, counted across the buffers in order, and the
		 * caller resends the remainder. At most a platform-dependent number of
		 * buffers is passed to the kernel per call - a longer list simply
		 * writes fewer bytes rather than failing. Empty buffers are skipped.
		 *
		 * @return bytes written, 0 if the socket would have blocked, or -1 on
		 *         error with 'ec' set.
		 */
		ssize_t write(std::span<const std::string_view> buffers, std::error_code& ec);
		ssize_t write(std::span<const std::string_view> buffers);

		ssize_t write(std::initializer_list<std::string_view> buffers, std::error_code& ec)
		{ return write(std::span<const std::string_view>(buffers.begin(), buffers.size()), ec); }

		ssize_t write(std::initializer_list<std::string_view> buffers)
		{ return write(std::span<const std::string_view>(buffers.begin(), buffers.size())); }

		ssize_t write(const char* data, size_t length);
		ssize_t read(char* data, size_t length);
		ssize_t peek(char* data, size_t length);

		/** The same transfers with the extent carried by the argument. */
		Samurai::IO::ReadResult read(std::span<char> data, size_t& transferred, std::error_code& ec)
		{ return read(data.data(), data.size(), transferred, ec); }

		Samurai::IO::ReadResult peek(std::span<char> data, size_t& transferred, std::error_code& ec)
		{ return peek(data.data(), data.size(), transferred, ec); }

		ssize_t write(std::span<const char> data, std::error_code& ec)
		{ return write(data.data(), data.size(), ec); }

		ssize_t write(std::span<const char> data)
		{ return write(data.data(), data.size()); }
		
		/**
		 * Start TLS on this socket.
		 *
		 * Refused, returning false, while a proxy handshake is still running:
		 * TLS has to go inside the tunnel, so it cannot begin until there is
		 * one. EventConnected is the signal that there is - it is not delivered
		 * until the proxy has confirmed the peer - so a caller that starts the
		 * handshake from there is already in the right order and this guard
		 * never fires.
		 */
		bool TLSInitialize(bool server);
		void TLSDeinitialize();
		void TLSsendHandshake();
		void TLSsendGoodbye();

		/**
		 * Whether TLS on this socket accepts a peer whose certificate the
		 * system trust store cannot verify, overriding the process default.
		 *
		 * Call this before TLSInitialize(), which is what reads it. A protocol
		 * that authenticates its peer by certificate fingerprint gains nothing
		 * from a chain the peer was never issued one for, and would lose the
		 * connection during the handshake, before there is a certificate to
		 * take a fingerprint of.
		 */
		void TLSsetAllowUntrusted(bool toggle) { allow_untrusted = toggle; }

		/**
		 * The SHA-256 fingerprint of the certificate the peer presented, as
		 * TlsFactory::getPeerCertificateSHA256() defines it. False if this is
		 * not a TLS connection, or the peer sent no certificate.
		 */
		std::optional<TlsFactory::Sha256Digest> TLSgetPeerCertificateSHA256();

		/**
		 * Whether traffic on this socket is encrypted, right now.
		 *
		 * Answered from the socket rather than remembered from the handshake, so
		 * that it stays true for as long as the connection does and is false the
		 * moment it is not - a caller that tracked the transitions instead would
		 * have to see every one of them to stay right.
		 */
		bool isSecure() const
		{ return tls && state == SocketState::SSLConnected; }

	protected:
		std::unique_ptr<InetAddress> address;
		uint16_t port;
		
	private:
		// Misc internal stuff
		bool autoConnectAfterLookup;
		SocketEventHandler* eventHandler;
		std::unique_ptr<Samurai::Timer> timer;
		std::chrono::milliseconds connect_timeout;
		bool outbound;

		int readable;
		bool writable;

		void sslInitialize(bool server);
		void sslDeinitialize();

		/*
		 * Whose name the outstanding lookup is for.
		 *
		 * A proxied socket never resolves its peer, but it does have to find the
		 * proxy, and a proxy may be named rather than written as a literal.
		 * Resolving that name is not a leak - it names the proxy, which is about
		 * to be told everything anyway - but the reply has to be routed to a
		 * different place than a peer's would be.
		 */
		enum class LookupTarget { Peer, Proxy };
		LookupTarget lookup_target = LookupTarget::Peer;

		ProxySettings proxy;
		/* Where the proxy is, once it is known. Separate from 'address', which
		   holds the peer and must never be resolved when there is a proxy. */
		std::unique_ptr<InetAddress> proxy_address;
		std::unique_ptr<Socks5Handshake> handshake;

		/** Connect to the proxy rather than to the peer. */
		void internal_connect_proxy();
		/** Open the descriptor and start ::connect() to whatever addr names. */
		void internal_connect_addr();
		/** Move the proxy handshake along as far as the socket allows. */
		void internal_proxy_pump();
		void internal_proxy_failed();

	protected:
		void handleMonitorEvent(SocketMonitor::Triggers trig) override;
		size_t bufferedInput() const override;

		void internal_canRead();
		void internal_canWrite();
		void internal_error(int);
		void internal_connected();
		void internal_closed();
		void internal_lookup();
		void internal_timeout();
		
		void internal_tls_handshake();
		void internal_tls_bye();
		std::unique_ptr<TlsFactory> tls;
		/* Nothing unless TLSsetAllowUntrusted() was called, so a socket that
		   says nothing keeps the process default. */
		std::optional<bool> allow_untrusted;

		void EventHostFound(const InetAddress* addr) override;
		void EventHostError(Samurai::IO::Net::DNS::Resolver::Error error) override;
		void EventTimeout(Samurai::Timer* timer) override;
		
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

#endif // HAVE_SAMURAI_SOCKET_H
