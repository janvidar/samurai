/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/bandwidth.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/tlsfactory.h>
#include <samurai/io/net/tlsfactory-gnutls.h>
#include <samurai/io/net/tlsfactory-openssl.h>

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

Samurai::IO::Net::Socket::Socket(Samurai::IO::Net::SocketEventHandler* eh, const std::string& address_, uint16_t port_) :
	SocketBase(),
	address(0),
	port(port_),
	autoConnectAfterLookup(false),
	eventHandler(eh),
	timer(0),
	outbound(true)
#ifdef SSL_SUPPORT
	,tls(0)
#endif
{
	address = new InetAddress(address_);
}

Samurai::IO::Net::Socket::Socket(SocketEventHandler* eh, const InetAddress& addr_, uint16_t port_) :
	SocketBase(),
	address(0),
	port(port_),
	autoConnectAfterLookup(false),
	eventHandler(eh),
	timer(0),
	outbound(false)
#ifdef SSL_SUPPORT
	,tls(0)
#endif
{
	address = new InetAddress(addr_);
}

Samurai::IO::Net::Socket::Socket(socket_t sd_, const Samurai::IO::Net::SocketAddress& addr_) :
	SocketBase(sd_, addr_, Stream),
	address(0),
	port(0),
	autoConnectAfterLookup(false),
	eventHandler(0),
	timer(0),
	outbound(false)
#ifdef SSL_SUPPORT
	,tls(0)
#endif
{

}


Samurai::IO::Net::Socket::~Socket() {
	TLSDeinitialize();
	close();
	delete timer; timer = 0;
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


void Samurai::IO::Net::Socket::internal_canRead()
{
	if (state == Connected
#ifdef SSL_SUPPORT
		|| state == SSLConnected
#endif
	) {
		if (eventHandler)
			eventHandler->EventDataAvailable(this);
	}

#ifdef SSL_SUPPORT
	if (state == SSLHandshake)
	{
		TLSsendHandshake();
	}
	
	if (state == SSLBye)
	{
		TLSsendGoodbye();
	}
#endif
}

void Samurai::IO::Net::Socket::internal_canWrite()
{
	if (state == Connected
#ifdef SSL_SUPPORT
		|| state == SSLConnected
#endif
	)
	{
		if (eventHandler)
			eventHandler->EventCanWrite(this);	
	}

#ifdef SSL_SUPPORT
	if (state == SSLHandshake)
	{
		TLSsendHandshake();
	}
	
	if (state == SSLBye)
	{
		TLSsendGoodbye();
	}
#endif
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

	if (addr == 0) {
		autoConnectAfterLookup = true;
		lookup();
		return;
	}
	
	if (!create(addr->getSockAddrFamily())) {
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
	if (state == Connected || state == Connecting) {
		state = Disconnected;
		if (eventHandler) eventHandler->EventDisconnected(this);
	}
	close();
}


ssize_t Samurai::IO::Net::Socket::write(const char* data, size_t length) {
	if (state != Connected
#ifdef SSL_SUPPORT
	 && state != SSLConnected
#endif
		)
	{
		if (eventHandler) eventHandler->EventError(this, SocketWrite, "Not connected");
		return 0;
	}

	ssize_t ret = 0;

#ifdef SSL_SUPPORT
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
#endif
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
#ifdef SSL_SUPPORT
	 && state != SSLConnected
#endif
		)
	{
		if (eventHandler) eventHandler->EventError(this, SocketRead, "Not connected");
		return 0; // nothing was read.
	}
	ssize_t ret = 0;
#ifdef SSL_SUPPORT
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
#endif
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


// FIXME: does not work for SSL
ssize_t Samurai::IO::Net::Socket::peek(char* data, size_t length) {
	if (state != Connected) {
		if (eventHandler) eventHandler->EventError(this, SocketRead, "Not connected");
		return 0;
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
	addr = new InetSocketAddress(resolved_addr->toString(), port, resolved_addr->getType());
	
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
	timer = 0;
}


void Samurai::IO::Net::Socket::toggleWriteNotifier(bool toggle) {
	if (toggle) 
		setMonitor(Samurai::IO::Net::SocketMonitor::MRead | Samurai::IO::Net::SocketMonitor::MWrite);
	else
		setMonitor(Samurai::IO::Net::SocketMonitor::MRead);
}


#ifdef SSL_SUPPORT


bool Samurai::IO::Net::Socket::TLSInitialize(bool server) {
	if (tls) return false;
	
#ifdef SSL_GNUTLS
	tls = new GnuTLS();
#endif

#ifdef SSL_OPENSSL
	tls = new OpenSSL();
#endif
	
	if (!tls) {
		QERR("No TLS provider available");
		return false;
	}

	enum Samurai::IO::Net::TlsFactory::TlsStatus status = tls->initialize(server ? Samurai::IO::Net::TlsFactory::TLS_OPERATE_SERVER : Samurai::IO::Net::TlsFactory::TLS_OPERATE_CLIENT, sd);
	return (status == Samurai::IO::Net::TlsFactory::TLS_STATUS_OK);
}


void Samurai::IO::Net::Socket::TLSDeinitialize() {
	if (!tls) return;
	tls->deinitialize();
	delete tls;
	tls = 0;
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


void Samurai::IO::Net::Socket::TLSsendGoodbye() {
	if (tls && (state == SSLConnected || state == SSLBye)) {
		state = SSLHandshake;
		enum Samurai::IO::Net::TlsFactory::TlsStatus status;
		status = tls->sendHandshake();
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


#else
bool Samurai::IO::Net::Socket::TLSInitialize(bool) { return false; }
void Samurai::IO::Net::Socket::TLSDeinitialize() { }
void Samurai::IO::Net::Socket::TLSsendHandshake()  { }
void Samurai::IO::Net::Socket::TLSsendGoodbye()  { }
#endif


// eof
