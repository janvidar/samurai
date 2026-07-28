/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/dns/resolver-builtin.h>
#include <samurai/io/net/dns/cache.h>
#include <samurai/io/net/dns/dnsconfig.h>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/dns/dnsutil.h>
#include <samurai/io/net/dns/dnsmessage.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/buffer.h>
#include <samurai/util/random.h>
#include <stdlib.h>

/* static: a name at global scope is not mangled, so external linkage would
   export plain "g_dns_config" from the shared library. */
static Samurai::IO::Net::DNS::ResolveConfiguration* g_dns_config = nullptr;


Samurai::IO::Net::DNS::BuiltinResolver::BuiltinResolver(Samurai::IO::Net::ResolveEventHandler* eh) : Samurai::IO::Net::DNS::Resolver(eh)
{
	/* Make sure we have read and parsed the DNS configuration */
	if (!g_dns_config) {
		g_dns_config = new Samurai::IO::Net::DNS::ResolveConfiguration();
	}

	jobId = 0;
	numTries = 0;
	hostname = nullptr;
	rrname = nullptr;
	timer = nullptr;
}

Samurai::IO::Net::DNS::BuiltinResolver::~BuiltinResolver()
{
	sock.reset();
	delete rrname;
	free(hostname);
	delete timer;
}



void Samurai::IO::Net::DNS::BuiltinResolver::lookup(const char* name)
{
	if (!name || !strlen(name) || strlen(name) > 255) return; // FIXME: call error.
	hostname = strdup(name);

	query();
}


void Samurai::IO::Net::DNS::BuiltinResolver::query() {

	enum Samurai::IO::Net::DNS::Type dns_type = Type_A;
	if (g_dns_config->isIPv6()) dns_type = Type_AAAA;

	/*
	 * NOTE: getNameServer() returns 0 when the configuration lists no usable
	 * server, and the InetAddress copy below would dereference it.
	 */
	Samurai::IO::Net::InetAddress* server = g_dns_config->getNameServer(numTries++);
	if (!server)
	{
		QERR("[DNS] No name server configured; cannot resolve '%s'", hostname ? hostname : "");
		if (eventHandler) eventHandler->EventHostError(ServerError);
		return;
	}

	delete rrname;
	rrname = new Samurai::IO::Net::DNS::Name(hostname);
	if (!rrname->split()) return;
	if (!rrname->countParts()) return;

	if ((size_t) rrname->countParts()-1 < g_dns_config->getNDots()) {
		/* TODO: Go through domain and search options */
	}

	Samurai::IO::Buffer* buffer = new Samurai::IO::Buffer(512);
	jobId = (uint16_t) Samurai::Util::pseudoRandom(1, 65535);
	uint16_t flags = 0x0100;
	uint16_t qdcount = 1;

	/* Global DNS header */
	buffer->appendBinary((uint16_t) jobId,     Samurai::IO::Buffer::BigEndian);
	buffer->appendBinary((uint16_t) flags,     Samurai::IO::Buffer::BigEndian);
	buffer->appendBinary((uint16_t) qdcount,   Samurai::IO::Buffer::BigEndian);
	buffer->appendBinary((uint16_t) 0x0000,    Samurai::IO::Buffer::BigEndian);
	buffer->appendBinary((uint16_t) 0x0000,    Samurai::IO::Buffer::BigEndian);
	buffer->appendBinary((uint16_t) 0x0000,    Samurai::IO::Buffer::BigEndian);

	printf("Adding request for: '%s'\n", rrname->toString().c_str());

	// Write hostname.
	for (size_t n = 0; n < rrname->countParts(); n++) {
		const char* part = rrname->parts[n].getName();
		size_t len = strlen(part);
		buffer->append((char) len);
		buffer->append(part);
	}
	buffer->append((char) 0x00);
	buffer->appendBinary((uint16_t) dns_type, Samurai::IO::Buffer::BigEndian);
	buffer->appendBinary((uint16_t) Class_IN, Samurai::IO::Buffer::BigEndian);

	DatagramPacket* packet = new DatagramPacket(buffer);

	InetSocketAddress addr(server, DNS_SERVER_PORT);
	packet->setAddress(&addr);

#if 0
	/* Use TCP */
	sock = Samurai::IO::Net::Socket::create(this, &addr);
	sock->connect();
#endif
	sock = Samurai::IO::Net::DatagramSocket::create(this, server->getType());
	std::dynamic_pointer_cast<Samurai::IO::Net::DatagramSocket>(sock)->send(packet);

	/* DatagramPacket copies the buffer it is handed, and send() copies out of
	   the packet; neither was freed here. */
	delete packet;
	delete buffer;

	if (timer) delete timer;
	timer = new Samurai::Timer(this, RES_TIMEOUT, true);
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventGotDatagram(DatagramSocket*, DatagramPacket* packet)
{
	QDBG("Got DNS datagram");

	Samurai::IO::Buffer* buffer = packet->getBuffer();
	Samurai::IO::Net::DNS::Message msg(buffer);
	enum Samurai::IO::Net::DNS::ResponseCode code = msg.decode();

	Samurai::IO::Net::DNS::CacheStorage* cache = Samurai::IO::Net::DNS::CacheStorage::getInstance();

	if (code == Samurai::IO::Net::DNS::DNS_STATUS_OK) {
		bool found = false;

		/*
		 * The records are read here while the message still owns them, and are
		 * only handed to the cache at the end. Doing it the other way round
		 * would leave the cache and the message both believing they own the
		 * same records.
		 */
		Samurai::IO::Net::DNS::ResourceRecord* rr = msg.getRecord(rrname);
		while (rrname && rr) {
			if (Samurai::IO::Net::DNS::RR_A* tmp =
				dynamic_cast<Samurai::IO::Net::DNS::RR_A*>(rr->rr.get()))
			{
				QDBG("hostname='%s', resolved to '%s'", hostname, tmp->getAddress()->toString().c_str());
				/* Borrowed for the duration of the call, as with the other
				   resolver backends. */
				eventHandler->EventHostFound(tmp->getAddress());
				found = true;
				break;

			} else if (Samurai::IO::Net::DNS::RR_CNAME* tmp =
				dynamic_cast<Samurai::IO::Net::DNS::RR_CNAME*>(rr->rr.get()))
			{
				QDBG("rrname='%s' is a cname for '%s'", rrname->toString().c_str(), tmp->getName().toString().c_str());

				delete rrname;
				rrname = new Samurai::IO::Net::DNS::Name(tmp->getName());
				rr = msg.getRecord(rrname);
				numTries++;

			} else {
				QDBG("Resource record is of no type we can follow");
				break;
			}
		}

		/* Ownership moves to the cache, which stamps and expires them. */
		for (Samurai::IO::Net::DNS::ResourceRecord* record : msg.releaseRecords())
			cache->add(record);

		if (!found)
			QDBG("No address for %s yet", rrname ? rrname->toString().c_str() : "");

	} else if (code == Samurai::IO::Net::DNS::DNS_STATUS_NAME_ERROR) {
		QDBG("Host not found");
		eventHandler->EventHostError(NotFound);
	} else {
		QDBG("Resolve failed, response code %d", (int) code);
		eventHandler->EventHostError(Unknown);
	}
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventDatagramError(const DatagramSocket*, const char*)
{
	/* Error using datagram */
	puts("got dns datagram error\n");
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventConnected(const Samurai::IO::Net::Socket*)
{
//	query();
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventTimeout(const Samurai::IO::Net::Socket*)
{
	/* Error using datagram */
}


void Samurai::IO::Net::DNS::BuiltinResolver::EventDisconnected(const Samurai::IO::Net::Socket*)
{
	/* Error using datagram */
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventDataAvailable(const Samurai::IO::Net::Socket*)
{
	/* Error using datagram */
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventCanWrite(const Samurai::IO::Net::Socket*)
{
	/* Error using datagram */
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventTimeout(Samurai::Timer* timer)
{
	(void) timer;
	puts("DNS timeout\n");
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventError(const Socket*, enum SocketError error, const char* msg)
{
	(void) error;
	(void) msg;
}


