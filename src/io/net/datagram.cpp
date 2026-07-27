/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/bandwidth.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/buffer.h>

#ifdef SAMURAI_POSIX
#include <signal.h>
#include <sys/wait.h>
#endif

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BUF_SIZE 65536

Samurai::IO::Net::DatagramPacket::DatagramPacket() : buffer(0), socket(0), addr(0) {
	buffer = new Samurai::IO::Buffer(MAX_BUF_SIZE);
}

Samurai::IO::Net::DatagramPacket::DatagramPacket(const uint8_t* buf, size_t len) : buffer(0), socket(0), addr(0) {
	buffer = new Samurai::IO::Buffer(len);
	buffer->append((char*) buf, len);
}

Samurai::IO::Net::DatagramPacket::DatagramPacket(const char* buf) : buffer(0), socket(0), addr(0) {
	buffer = new Samurai::IO::Buffer(strlen(buf));
	buffer->append(buf);

}

Samurai::IO::Net::DatagramPacket::DatagramPacket(Samurai::IO::Buffer* buf) : buffer(0), socket(0), addr(0) {
	buffer = new Samurai::IO::Buffer(buf);
}

void Samurai::IO::Net::DatagramPacket::setData(const uint8_t* buf, size_t len) {
	buffer->clear();
	buffer->append((char*) buf, len);
}

void Samurai::IO::Net::DatagramPacket::setAddress(Samurai::IO::Net::SocketAddress* addr_)
{
	delete addr; addr = 0;
	addr = new InetSocketAddress(dynamic_cast<InetSocketAddress*>(addr_));
}

Samurai::IO::Net::SocketAddress* Samurai::IO::Net::DatagramPacket::getAddress()
{
	return addr;
}

void Samurai::IO::Net::DatagramPacket::clear() {
	buffer->clear();
}

Samurai::IO::Net::DatagramPacket::~DatagramPacket() {
	delete buffer;
	delete addr;
}

size_t Samurai::IO::Net::DatagramPacket::size() {
	return buffer->size();
}

Samurai::IO::Buffer* Samurai::IO::Net::DatagramPacket::getBuffer() {
	return buffer;
}

Samurai::IO::Net::DatagramSocket::DatagramSocket(DatagramEventHandler* eh, enum InetAddress::Version version) : SocketBase(Datagram), eventHandler(eh), myPacket(0)
{
	int af = (version == InetAddress::IPv4 ? AF_INET : version == InetAddress::IPv6 ? AF_INET6 : AF_UNSPEC);
	createDescriptor(af);
}

/*
 * NOTE: this used to call internal_create(), which dereferenced a null 'addr'.
 * The immediate crash was guarded, but the object was still left with no
 * address and no descriptor while reporting itself as constructed. There is no
 * useful datagram socket to build without knowing an address family, so the
 * constructor now produces a plainly unbound socket and says so, and listen()
 * and send() already refuse an INVALID_SOCKET.
 */
Samurai::IO::Net::DatagramSocket::DatagramSocket() : SocketBase(Datagram), eventHandler(0), myPacket(0) {
	QERR("DatagramSocket: default-constructed sockets have no address family "
	     "and cannot be used; construct with an address, port or version");
}

Samurai::IO::Net::DatagramSocket::DatagramSocket(Samurai::IO::Net::DatagramEventHandler* eh, const Samurai::IO::Net::SocketAddress& bindAddr)
	: SocketBase(bindAddr, Datagram), eventHandler(eh), myPacket(0)
{
	internal_create();
}

Samurai::IO::Net::DatagramSocket::DatagramSocket(DatagramEventHandler* eh, const Samurai::IO::Net::InetAddress& bindaddr, uint16_t bindport)
	: SocketBase(bindaddr, bindport, Datagram), eventHandler(eh), myPacket(0)
{
	internal_create();
}

Samurai::IO::Net::DatagramSocket::DatagramSocket(DatagramEventHandler* eh, uint16_t bindport)
	: SocketBase(Samurai::IO::Net::InetAddress("0.0.0.0"), bindport, Datagram), eventHandler(eh), myPacket(0)
{
	internal_create();
}


void Samurai::IO::Net::DatagramSocket::internal_create() {
	if (addr) createDescriptor(addr->getSockAddrFamily());
}

/* Registering with the monitor needs shared_from_this(), so it cannot happen
   in a constructor. */
void Samurai::IO::Net::DatagramSocket::initialize() {
	if (sd != INVALID_SOCKET)
		setMonitor(Samurai::IO::Net::SocketMonitor::MRead);
}


Samurai::IO::Net::DatagramSocket::~DatagramSocket() {
	disableMonitor();
	close();
	delete myPacket;
}

void Samurai::IO::Net::DatagramSocket::setEventHandler(DatagramEventHandler* eh) {
	eventHandler = eh;
}

bool Samurai::IO::Net::DatagramSocket::listen() {
	if (!addr || sd == INVALID_SOCKET) return false;
	if (!setReuseAddress(true)) return false;
	if (!setNonBlocking(true)) return false;
	if (!bind(addr)) return false;
	return true;
}


int Samurai::IO::Net::DatagramSocket::send(DatagramPacket* packet) {
	if (!packet || !packet->addr) return -1;


	size_t length = packet->buffer->size();
	uint8_t data[MAX_BUF_SIZE] = { 0, };
	if (length > MAX_BUF_SIZE) length = MAX_BUF_SIZE;

	packet->buffer->pop((char*) data, length);

	struct sockaddr* sa = packet->addr->getSockAddr();
	size_t sa_len = packet->addr->getSockAddrSize();

	ssize_t ret = ::sendto(sd, SENDTO_CAST_PREFIX data, length, SAMURAI_SENDFLAGS, (sockaddr*) sa, sa_len);

	if (ret == -1) {
		if (NETERROR == EAGAIN || NETERROR == EWOULDBLOCK || NETERROR == EINTR) {
			return 0;
		} else {
			if (eventHandler) eventHandler->EventDatagramError(this, strerror(NETERROR));
			return -1;
		}
	}

	/* NOTE: Buffer::pop() copies without consuming, so the packet's buffer
	   never drained and every subsequent send() resent the same bytes with
	   the new ones appended behind them. */
	packet->buffer->remove((size_t) ret);

	if (bandwidthManager) bandwidthManager->dataSendUDP((size_t) ret);
	return ret;
}

int Samurai::IO::Net::DatagramSocket::read(DatagramPacket* packet) {
	size_t length = MAX_BUF_SIZE;
	uint8_t data[MAX_BUF_SIZE] = { 0, };

	/* NOTE: this used a struct sockaddr_in and always built an IPv4 source
	   address, so the sender of any IPv6 datagram came out as garbage. It also
	   converted the address before checking whether recvfrom() had failed. */
	struct sockaddr_storage sa;
	socklen_t sl = sizeof(sa);
	memset(&sa, 0, sizeof(sa));

	const ssize_t status = ::recvfrom(sd, (char*) data, length, 0, (sockaddr*) &sa, &sl);

	if (status == -1) {
		QERR("recvfrom err: %s", strerror(NETERROR));
		return -1;
	}

	if (status == 0) {
		packet->clear();
		return 0;
	}

	InetSocketAddress taddr;
	bool have_addr = false;

	if (sa.ss_family == AF_INET) {
		struct sockaddr_in* sin = (struct sockaddr_in*) &sa;
		taddr.setRawSocketAddress(&sin->sin_addr, sizeof(sin->sin_addr),
		                          ntohs(sin->sin_port), Samurai::IO::Net::InetAddress::IPv4);
		have_addr = true;
	} else if (sa.ss_family == AF_INET6) {
		struct sockaddr_in6* sin6 = (struct sockaddr_in6*) &sa;
		taddr.setRawSocketAddress(&sin6->sin6_addr, sizeof(sin6->sin6_addr),
		                          ntohs(sin6->sin6_port), Samurai::IO::Net::InetAddress::IPv6);
		have_addr = true;
	}

	packet->setData(data, (size_t) status);
	if (have_addr) packet->setAddress(&taddr);

	if (bandwidthManager) bandwidthManager->dataRecvUDP((size_t) status);
	return (int) status;
}


void Samurai::IO::Net::DatagramSocket::internal_canRead() {
	if (!myPacket) myPacket = new DatagramPacket();
	
	switch (read(myPacket)) {
		case -1:
			if (eventHandler) eventHandler->EventDatagramError(this, strerror(NETERROR));
			break;
		case 0:
			break;
		default:
			if (eventHandler) eventHandler->EventGotDatagram(this, myPacket);
			break;
	}
}

void Samurai::IO::Net::DatagramSocket::internal_error() {
	if (eventHandler) eventHandler->EventDatagramError(this, strerror(NETERROR));
	disableMonitor();
}

