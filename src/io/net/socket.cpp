/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/bandwidth.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/error.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/tlsfactory.h>
#include <samurai/io/net/tlsfactory-openssl.h>

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifdef SAMURAI_POSIX
#include <sys/uio.h>
#endif

Samurai::IO::Net::Socket::Socket(Samurai::IO::Net::SocketEventHandler* eh, const std::string& address_, uint16_t port_) :
	SocketBase(),
	address(nullptr),
	port(port_),
	autoConnectAfterLookup(false),
	eventHandler(eh),
	timer(nullptr),
	outbound(true),
	readable(0),
	writable(false)
	,tls(nullptr)
{
	address = new InetAddress(address_);
}

Samurai::IO::Net::Socket::Socket(SocketEventHandler* eh, const InetAddress& addr_, uint16_t port_) :
	SocketBase(),
	address(nullptr),
	port(port_),
	autoConnectAfterLookup(false),
	eventHandler(eh),
	timer(nullptr),
	outbound(false),
	readable(0),
	writable(false)
	,tls(nullptr)
{
	address = new InetAddress(addr_);
}

Samurai::IO::Net::Socket::Socket(socket_t sd_, const Samurai::IO::Net::SocketAddress& addr_) :
	SocketBase(sd_, addr_, Stream),
	address(nullptr),
	port(0),
	autoConnectAfterLookup(false),
	eventHandler(nullptr),
	timer(nullptr),
	outbound(false),
	readable(0),
	writable(false)
	,tls(nullptr)
{

}


Samurai::IO::Net::Socket::~Socket() {
	TLSDeinitialize();
	close();
	delete timer; timer = nullptr;
	delete address;
}

void Samurai::IO::Net::Socket::setEventHandler(Samurai::IO::Net::SocketEventHandler* eh)
{
	if (!eventHandler && eh && sd != INVALID_SOCKET)
	{
		setMonitor(Samurai::IO::Net::SocketMonitor::MRead);
	}
	eventHandler = eh;
}

void Samurai::IO::Net::Socket::lookup() {
	if (state != Disconnected) return;

	if (address) {
		if (eventHandler) eventHandler->EventHostLookup(this);
		address->lookup(this);
	}
}


void Samurai::IO::Net::Socket::handleMonitorEvent(int trig)
{
	if (trig & SocketMonitor::MRead)
	{
		switch (state)
		{
			case Connected:
			{
				char buf[1];
#ifndef SAMURAI_WINSOCK
				int x = recv(sd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
#else
				int x = recv(sd, (char*) &buf, 1, MSG_PEEK);
#endif
				if (x > 0)
				{
					internal_canRead();
				}
				else if (x == 0)
				{
					internal_closed();
				}
				else if (x == -1)
				{
					if (NETERROR != EAGAIN && NETERROR != EINTR)
					{
						QERR("Socket recv error: %d (%s) (x=%d)\n", NETERROR, strerror(NETERROR), x);
						internal_error(NETERROR);
					}
				}
			}
			break;


			case SSLBye:
			case SSLHandshake:
			case SSLConnected:
				/* NOTE: This is a *read* event, so it has to dispatch to
				   internal_canRead().

				   No MSG_PEEK probe as in the Connected case above: a
				   complete TLS record may already be buffered inside the
				   TLS library while the socket itself reads empty, so
				   peeking would skip valid reads. End-of-stream arrives
				   as TLS_STATUS_CLOSED through read() instead. */
				internal_canRead();
				break;


			default:
				// ignore
				break;
		}
	}

	if (trig & SocketMonitor::MWrite)
	{
		switch (state)
		{
			case Connecting:
				internal_connected();
				break;

			case SSLBye:
			case SSLHandshake:
			case Connected:
			case SSLConnected:
				internal_canWrite();
				break;

			default:
				// ERROR!
				break;
		}
	}

	/* NOTE: Handled last, so that data still buffered on a socket that has
	   also errored is delivered before the connection is torn down. */
	if (trig & (SocketMonitor::MError | SocketMonitor::MClose))
	{
		/* The MRead path may already have torn this down; do not report twice. */
		if (state == Invalid || state == Disconnected)
			return;

		if (trig & SocketMonitor::MError)
		{
			int value = 0;
			socklen_t valsize = sizeof(value);
			if (SAMURAI_GETSOCKOPT(sd, SOL_SOCKET, SO_ERROR, &value, &valsize) != 0)
				value = 0;
			internal_error(value);
		}
		else
		{
			internal_closed();
		}
	}
}


size_t Samurai::IO::Net::Socket::bufferedInput() const
{
	if (state != SSLConnected || !tls) return 0;
	return tls->pending();
}

void Samurai::IO::Net::Socket::internal_canRead()
{
	if (state == Connected
		|| state == SSLConnected
	) {
		if (eventHandler)
			eventHandler->EventDataAvailable(this);
	}

	if (state == SSLHandshake)
	{
		TLSsendHandshake();
	}

	if (state == SSLBye)
	{
		TLSsendGoodbye();
	}
}

void Samurai::IO::Net::Socket::internal_canWrite()
{
	if (state == Connected
		|| state == SSLConnected
	)
	{
		if (eventHandler)
			eventHandler->EventCanWrite(this);
	}

	if (state == SSLHandshake)
	{
		TLSsendHandshake();
	}

	if (state == SSLBye)
	{
		TLSsendGoodbye();
	}
}

void Samurai::IO::Net::Socket::internal_error(int socket_error) {
	state = Invalid;
	const char* message = "Unknown socket error.";
	switch (socket_error)
	{
		case EOPNOTSUPP:    message = "Operation not supported"; break;
		case EAFNOSUPPORT:  message = "Address family not supported"; break;
		case EADDRINUSE:    message = "Address is in use"; break;
		case EADDRNOTAVAIL: message = "Address is not available"; break;
		case ENETDOWN:      message = "Network is down"; break;
		case ENETUNREACH:   message = "Network is unreachable"; break;
		case ENETRESET:     message = "Network dropped connection because of reset"; break;
		case ECONNABORTED:  message = "Software caused connection abort"; break;
		case ECONNRESET:    message = "Connection reset by peer"; break;
		case EHOSTDOWN:     message = "Host is down"; break;
		case EHOSTUNREACH:  message = "No route to host"; break;
	}

	if (eventHandler) eventHandler->EventError(this, SocketUnknown, message);
	disableMonitor();
}

void Samurai::IO::Net::Socket::internal_closed() {
	state = Disconnected;
	if (eventHandler) eventHandler->EventDisconnected(this);
	disableMonitor();
}

void Samurai::IO::Net::Socket::internal_connected() {
#ifdef SAMURAI_POSIX
	int value;
	socklen_t valsize = sizeof(value);
	int ret = getsockopt(sd, SOL_SOCKET, SO_ERROR, &value, &valsize);
	if (ret != 0 || value != 0) {
		enum Samurai::IO::Net::SocketError sockErr;
		const char* error = strerror(value);
		switch (value) {
			case ECONNREFUSED:   sockErr = ConnectionRefused; break;
			case EHOSTDOWN:      sockErr = HostDown;          break;
			case EHOSTUNREACH:   sockErr = HostUnreachable;   break;
			case ENETDOWN:       sockErr = NetDown;           break;
			case ENETUNREACH:    sockErr = NetUnreachable;    break;
			case ENETRESET:
			case ECONNRESET:
			default:
				sockErr = ConnectionRefused;
		}
		close();
		if (eventHandler) eventHandler->EventError(this, sockErr, error);
		return;
	}
#endif // SAMURAI_POSIX

	toggleWriteNotifier(false);
	state = Connected;
	if (eventHandler) eventHandler->EventConnected(this);
}


void Samurai::IO::Net::Socket::internal_timeout() {
	close();
	if (eventHandler) eventHandler->EventError(this, ConnectionTimeout, "Connection timed out.");
}


void Samurai::IO::Net::Socket::connect()
{
	if (!(state == Disconnected || state == HostFound)) return;

	if (addr == nullptr) {
		autoConnectAfterLookup = true;
		lookup();
		return;
	}

	if (!createDescriptor(addr->getSockAddrFamily())) {
		state = Invalid;
		disableMonitor();
		if (eventHandler) eventHandler->EventError(this, SocketUnknown, strerror(NETERROR));
		return;
	}

	if (!setNonBlocking(true)) {
		state = Invalid;
		disableMonitor();
		if (eventHandler) eventHandler->EventError(this, SocketUnknown, strerror(NETERROR));
		return;
	}

	setMonitor(Samurai::IO::Net::SocketMonitor::MRead |  Samurai::IO::Net::SocketMonitor::MWrite);

	// connect and reset connection timer.
	delete timer;
	timer = nullptr;

	int ret = ::connect(sd, addr->getSockAddr(), addr->getSockAddrSize());
	timer = new Samurai::Timer(this, CONNECT_TIMEOUT, true);
	if (ret == -1) {
		if (NETERROR == EINPROGRESS) {
			state = Connecting;
			if (eventHandler) {
				eventHandler->EventConnecting(this);
			}
			return;
		} else {
			state = Invalid;
			disableMonitor();
			if (eventHandler) eventHandler->EventError(this, SocketUnknown, strerror(NETERROR));
			return;
		}
	}

	internal_connected();
}


void Samurai::IO::Net::Socket::disconnect() {
	switch (state) {
		case Connecting:
		case Connected:
		case SSLHandshake:
		case SSLConnected:
		case SSLBye:
			state = Disconnected;
			if (eventHandler) eventHandler->EventDisconnected(this);
			break;

		default:
			break;
	}

	close();
}


ssize_t Samurai::IO::Net::Socket::write(const char* data, size_t length) {
	if (state != Connected
	 && state != SSLConnected
		)
	{
		if (eventHandler) eventHandler->EventError(this, SocketWrite, "Not connected");
		return 0;
	}

	ssize_t ret = 0;

	if (state == SSLConnected && tls)
	{
		enum Samurai::IO::Net::TlsFactory::TlsStatus status;

		ret = tls->write(data, length, status);

		switch (status) {
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_OK:
				return ret;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE:
				toggleWriteNotifier(true);
				return 0;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ:
				return 0;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED:
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR:
				if (eventHandler) eventHandler->EventTLSDisconnected(this);
				state = Invalid;
				disableMonitor();
				if (eventHandler) eventHandler->EventError(this, SocketWrite, "FIXME: SSL/TLS send error");
				return -1;

			default:
				break;
		}
	}
	else
	{
		ret = ::send(sd, data, length, SAMURAI_SENDFLAGS);
		if (ret == -1) {
			if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK || NETERROR == EINTR) {
				return 0;
			} else {
				state = Invalid;
				disableMonitor();
				if (eventHandler) eventHandler->EventError(this, SocketWrite, strerror(NETERROR));
				return -1;
			}
		}

		if (ret == 0) {
			int error = 0;
			socklen_t sz = sizeof(error);
			SAMURAI_GETSOCKOPT(sd, SOL_SOCKET, SO_ERROR, &error, &sz);
		}
	}

	if (bandwidthManager) bandwidthManager->dataSendTCP((size_t) ret);
	return ret;
}


ssize_t Samurai::IO::Net::Socket::read(char* data, size_t length) {
	if (state != Connected
	 && state != SSLConnected
		)
	{
		if (eventHandler) eventHandler->EventError(this, SocketRead, "Not connected");
		return 0; // nothing was read.
	}
	ssize_t ret = 0;
	if (state == SSLConnected && tls)
	{
		enum Samurai::IO::Net::TlsFactory::TlsStatus status;
		ret = tls->read(data, length, status);

		switch (status) {
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_OK:
				return ret;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE:
				toggleWriteNotifier(true);
				return 0;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ:
				return 0;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED:
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR:
				if (eventHandler) eventHandler->EventTLSDisconnected(this);
				state = Invalid;
				disableMonitor();
				if (eventHandler) eventHandler->EventError(this, SocketRead, "FIXME: SSL/TLS read error");
				return -1;
		}
	}
	else
	{
		ret = ::recv(sd, data, length, 0);
		if (ret == -1) {
			if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK || NETERROR == EINTR)
			{
				// try again later
				return 0;
			}

			state = Invalid;
			disableMonitor();
			if (eventHandler) eventHandler->EventError(this, SocketRead, strerror(NETERROR));
			return 0; // nothing was read.
		}
	}
	if (bandwidthManager) bandwidthManager->dataRecvTCP((size_t) ret);
	return ret;
}


ssize_t Samurai::IO::Net::Socket::peek(char* data, size_t length) {
	if (state != Connected
	 && state != SSLConnected
		)
	{
		if (eventHandler) eventHandler->EventError(this, SocketRead, "Not connected");
		return 0;
	}

	if (state == SSLConnected && tls)
	{
		enum Samurai::IO::Net::TlsFactory::TlsStatus status;
		ssize_t tls_ret = tls->peek(data, length, status);

		switch (status) {
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_OK:
				return tls_ret;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE:
				toggleWriteNotifier(true);
				return 0;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ:
				return 0;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED:
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR:
				if (eventHandler) eventHandler->EventTLSDisconnected(this);
				state = Invalid;
				disableMonitor();
				if (eventHandler) eventHandler->EventError(this, SocketRead, "SSL/TLS peek error");
				return 0;
		}
	}

	ssize_t ret = ::recv(sd, data, length, MSG_PEEK);
	if (ret == -1) {
		if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK)
		{
			// try again later
			return 0;
		}

		state = Invalid;
		disableMonitor();
		if (eventHandler) eventHandler->EventError(this, SocketRead, strerror(NETERROR));
		return 0;
	}
	return ret;
}


void Samurai::IO::Net::Socket::EventHostFound(Samurai::IO::Net::InetAddress* resolved_addr) {
	if (addr) delete addr;
	addr = new InetSocketAddress(*resolved_addr, port);

	state = HostFound;
	if (eventHandler) eventHandler->EventHostFound(this);
	if (autoConnectAfterLookup) connect();
}


void Samurai::IO::Net::Socket::EventHostError(enum Samurai::IO::Net::DNS::Resolver::Error /*error*/) {
	state = Invalid;
	disableMonitor();
	if (eventHandler) eventHandler->EventError(this, HostNotFound, "Host not found.");
}


void Samurai::IO::Net::Socket::EventTimeout(Samurai::Timer*) {
	if (state == Connecting) {
		internal_timeout();
	}

	delete timer;
	timer = nullptr;
}


void Samurai::IO::Net::Socket::toggleWriteNotifier(bool toggle) {
	if (toggle)
		setMonitor(Samurai::IO::Net::SocketMonitor::MRead | Samurai::IO::Net::SocketMonitor::MWrite);
	else
		setMonitor(Samurai::IO::Net::SocketMonitor::MRead);
}




bool Samurai::IO::Net::Socket::TLSInitialize(bool server) {
	if (tls) return false;

	tls = new OpenSSL();

	/* The name we asked for - not one derived from the connection - is the
	   only thing a certificate can meaningfully be verified against. */
	if (!server && address && !address->getHostname().empty())
		tls->setPeerName(address->getHostname());

	enum Samurai::IO::Net::TlsFactory::TlsStatus status = tls->initialize(server ? Samurai::IO::Net::TlsFactory::TLS_OPERATE_SERVER : Samurai::IO::Net::TlsFactory::TLS_OPERATE_CLIENT, sd);
	return (status == Samurai::IO::Net::TlsFactory::TLS_STATUS_OK);
}


void Samurai::IO::Net::Socket::TLSDeinitialize() {
	if (!tls) return;
	tls->deinitialize();
	delete tls;
	tls = nullptr;
}


void Samurai::IO::Net::Socket::TLSsendHandshake() {
	if (tls && (state == Connected || state == SSLHandshake)) {
		state = SSLHandshake;
		enum Samurai::IO::Net::TlsFactory::TlsStatus status;
		status = tls->sendHandshake();
		switch (status) {
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_OK:
				toggleWriteNotifier(false);
				state = SSLConnected;
				if (eventHandler) eventHandler->EventTLSConnected(this);
				break;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE:
				toggleWriteNotifier(true);
				state = SSLHandshake; /* try again */
				break;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ:
				toggleWriteNotifier(false);
				state = SSLHandshake; /* try again */
				break;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED:
				toggleWriteNotifier(false);
				state = Connected;
				/* wtf? */
				break;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR:
				toggleWriteNotifier(false);
				QERR("TLS handshake failed.");
				close();
				internal_error(-1);
				break;
		}
	}
}


bool Samurai::IO::Net::Socket::TLSgetPeerCertificateSHA256(uint8_t* digest, size_t length) {
	if (!tls) return false;
	return tls->getPeerCertificateSHA256(digest, length);
}


void Samurai::IO::Net::Socket::TLSsendGoodbye() {
	if (tls && (state == SSLConnected || state == SSLBye)) {
		state = SSLBye;
		enum Samurai::IO::Net::TlsFactory::TlsStatus status;
		status = tls->sendGoodbye();
		switch (status) {
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_OK:
				toggleWriteNotifier(false);
				state = Connected;
				if (eventHandler) eventHandler->EventTLSDisconnected(this);
				break;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE:
				toggleWriteNotifier(true);
				state = SSLBye; /* try again */
				break;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ:
				toggleWriteNotifier(false);
				state = SSLBye; /* try again */
				break;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED:
				toggleWriteNotifier(false);
				state = Connected;
				/* wtf? */
				break;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR:
				toggleWriteNotifier(false);
				QERR("TLS goodbye failed.");
				close();
				internal_error(-1);
				break;
		}
	}
}





/*
 * NOTE: The three functions below are the reporting API. The ssize_t
 * read()/peek()/write() overloads above delegate to them, discarding the
 * detail, and are kept for source compatibility.
 */

Samurai::IO::ReadResult Samurai::IO::Net::Socket::read(char* data, size_t length,
                                                       size_t& transferred, std::error_code& ec)
{
	transferred = 0;
	ec.clear();

	if (state != Connected && state != SSLConnected)
	{
		ec = Samurai::system_error(ENOTCONN);
		return Samurai::IO::ReadError;
	}

	if (state == SSLConnected && tls)
	{
		enum Samurai::IO::Net::TlsFactory::TlsStatus status;
		ssize_t ret = tls->read(data, length, status);

		switch (status) {
			case Samurai::IO::Net::TlsFactory::TLS_STATUS_OK:
				if (ret > 0) { transferred = (size_t) ret; return Samurai::IO::ReadOk; }
				return Samurai::IO::ReadEndOfFile;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE:
				toggleWriteNotifier(true);
				return Samurai::IO::ReadWouldBlock;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ:
				return Samurai::IO::ReadWouldBlock;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED:
				return Samurai::IO::ReadEndOfFile;

			case Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR:
				ec = Samurai::system_error(EPROTO);
				return Samurai::IO::ReadError;
		}
		return Samurai::IO::ReadWouldBlock;
	}

	ssize_t ret = ::recv(sd, data, length, 0);

	if (ret > 0)
	{
		transferred = (size_t) ret;
		if (bandwidthManager) bandwidthManager->dataRecvTCP(transferred);
		return Samurai::IO::ReadOk;
	}

	/* recv() returning 0 on a stream socket is an orderly shutdown by the
	   peer, which the ssize_t overload could not express. */
	if (ret == 0)
		return Samurai::IO::ReadEndOfFile;

	if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK || NETERROR == EINTR)
		return Samurai::IO::ReadWouldBlock;

	ec = Samurai::system_error(NETERROR);
	return Samurai::IO::ReadError;
}


Samurai::IO::ReadResult Samurai::IO::Net::Socket::peek(char* data, size_t length,
                                                       size_t& transferred, std::error_code& ec)
{
	transferred = 0;
	ec.clear();

	if (state != Connected && state != SSLConnected)
	{
		ec = Samurai::system_error(ENOTCONN);
		return Samurai::IO::ReadError;
	}

	ssize_t ret = ::recv(sd, data, length, MSG_PEEK);

	if (ret > 0) { transferred = (size_t) ret; return Samurai::IO::ReadOk; }
	if (ret == 0) return Samurai::IO::ReadEndOfFile;

	if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK || NETERROR == EINTR)
		return Samurai::IO::ReadWouldBlock;

	ec = Samurai::system_error(NETERROR);
	return Samurai::IO::ReadError;
}


ssize_t Samurai::IO::Net::Socket::write(const char* data, size_t length, std::error_code& ec)
{
	ec.clear();

	if (state != Connected && state != SSLConnected)
	{
		ec = Samurai::system_error(ENOTCONN);
		return -1;
	}

	if (state == SSLConnected && tls)
		return write(data, length);

	ssize_t ret = ::send(sd, data, length, SAMURAI_SENDFLAGS);
	if (ret >= 0)
	{
		if (bandwidthManager) bandwidthManager->dataSendTCP((size_t) ret);
		return ret;
	}

	if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK || NETERROR == EINTR)
		return 0;

	ec = Samurai::system_error(NETERROR);
	return -1;
}


/* POSIX only guarantees IOV_MAX is at least 16; Linux allows 1024. Staying
   under whatever the platform allows keeps a long list from failing outright -
   the surplus buffers are left for the caller to resend. */
#if defined(IOV_MAX) && IOV_MAX < 64
#define SAMURAI_MAX_IOV IOV_MAX
#else
#define SAMURAI_MAX_IOV 64
#endif

ssize_t Samurai::IO::Net::Socket::write(std::span<const std::string_view> buffers, std::error_code& ec)
{
	ec.clear();

	if (state != Connected && state != SSLConnected)
	{
		ec = Samurai::system_error(ENOTCONN);
		return -1;
	}

	/* TLS has no vectored write, so the buffers go out one record at a time.
	   Stopping at the first short write keeps the partial-write contract: what
	   is reported as written is a prefix of the whole sequence. */
	if (state == SSLConnected && tls)
	{
		ssize_t total = 0;
		for (size_t n = 0; n < buffers.size(); n++)
		{
			if (buffers[n].empty()) continue;

			ssize_t ret = write(buffers[n].data(), buffers[n].size(), ec);
			if (ret < 0)
			{
				if (total == 0) return -1;
				ec.clear();
				return total;
			}

			total += ret;
			if ((size_t) ret < buffers[n].size()) break;
		}
		return total;
	}

#ifdef SAMURAI_WINSOCK
	WSABUF vec[SAMURAI_MAX_IOV];
#else
	struct iovec vec[SAMURAI_MAX_IOV];
#endif

	size_t count = 0;
	for (size_t n = 0; n < buffers.size() && count < SAMURAI_MAX_IOV; n++)
	{
		if (buffers[n].empty()) continue;

#ifdef SAMURAI_WINSOCK
		vec[count].buf = (CHAR*) buffers[n].data();
		vec[count].len = (ULONG) buffers[n].size();
#else
		vec[count].iov_base = (void*) buffers[n].data();
		vec[count].iov_len = buffers[n].size();
#endif
		count++;
	}

	if (!count) return 0;

#ifdef SAMURAI_WINSOCK
	DWORD sent = 0;
	int ret = WSASend(sd, vec, (DWORD) count, &sent, 0, nullptr, nullptr);
	if (ret == 0)
	{
		if (bandwidthManager) bandwidthManager->dataSendTCP((size_t) sent);
		return (ssize_t) sent;
	}
#else
	struct msghdr msg;
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = vec;
	msg.msg_iovlen = count;

	ssize_t ret = ::sendmsg(sd, &msg, SAMURAI_SENDFLAGS);
	if (ret >= 0)
	{
		if (bandwidthManager) bandwidthManager->dataSendTCP((size_t) ret);
		return ret;
	}
#endif

	if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK || NETERROR == EINTR)
		return 0;

	ec = Samurai::system_error(NETERROR);
	return -1;
}


ssize_t Samurai::IO::Net::Socket::write(std::span<const std::string_view> buffers)
{
	std::error_code ec;
	ssize_t ret = write(buffers, ec);
	return (ret < 0) ? 0 : ret;
}

// eof
