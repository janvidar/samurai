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

extern Samurai::IO::Net::DNS::ResolveConfiguration* g_dns_config;
Samurai::IO::Net::DNS::ResolveConfiguration* g_dns_config = 0;


Samurai::IO::Net::DNS::BuiltinResolver::BuiltinResolver(Samurai::IO::Net::ResolveEventHandler* eh) : Samurai::IO::Net::DNS::Resolver(eh)
{
	/* Make sure we have read and parsed the DNS configuration */
	if (!g_dns_config) {
		g_dns_config = new Samurai::IO::Net::DNS::ResolveConfiguration();
	}

	jobId = 0;
	numTries = 0;
	hostname = 0;
	rrname = 0;
	timer = 0;
}

Samurai::IO::Net::DNS::BuiltinResolver::~BuiltinResolver()
{
	delete sock;
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

	printf("Adding request for: '%s'\n", rrname->toString());

	// Write hostname.
	for (size_t n = 0; n < rrname->countParts(); n++) {
		char* part = const_cast<char*>(rrname->parts[n]->getName());
		size_t len = strlen(part);
		buffer->append((char) len);
		buffer->append(part);
	}
	buffer->append((char) 0x00);
	buffer->appendBinary((uint16_t) dns_type, Samurai::IO::Buffer::BigEndian);
	buffer->appendBinary((uint16_t) Class_IN, Samurai::IO::Buffer::BigEndian);

	DatagramPacket* packet = new DatagramPacket(buffer);

	Samurai::IO::Net::InetAddress* server = g_dns_config->getNameServer(numTries++);
	InetSocketAddress addr(server, DNS_SERVER_PORT);
	packet->setAddress(&addr);

#if 0
	/* Use TCP */
	sock = new Samurai::IO::Net::Socket(this, &addr);
	sock->connect();
#endif
	sock = new Samurai::IO::Net::DatagramSocket(this, server->getType());
	dynamic_cast<Samurai::IO::Net::DatagramSocket*>(sock)->send(packet);
	
	if (timer) delete timer;
	timer = new Samurai::Timer(this, RES_TIMEOUT, true);
}

void Samurai::IO::Net::DNS::BuiltinResolver::EventGotDatagram(DatagramSocket*, DatagramPacket* packet)
{
	puts("got dns datagram");
	
	Samurai::IO::Buffer* buffer = packet->getBuffer();
	Samurai::IO::Net::DNS::Message msg(buffer);
	enum Samurai::IO::Net::DNS::ResponseCode code = msg.decode();

	Samurai::IO::Net::DNS::CacheStorage* cache = Samurai::IO::Net::DNS::CacheStorage::getInstance();
	
	if (code == Samurai::IO::Net::DNS::DNS_STATUS_OK) {
		/* Do nothing */
		Samurai::IO::Net::DNS::ResourceRecord* rr = 0;
		puts("=> ok, things seemed to work out! cool");
		
		
		// Add everything to cache
		for (size_t n = 0; msg.getRecord(n); n++) {
			rr = msg.getRecord(n);
			cache->add(rr);
			// rr = msg.getRecord(n++);
		}

		bool found = false;

		// find whatever we are looking for
		rr = msg.getRecord(rrname);
		while (rrname && rr) {
			
			if (dynamic_cast<Samurai::IO::Net::DNS::RR_A*>(rr->rr)) {
				
				Samurai::IO::Net::DNS::RR_A* tmp = dynamic_cast<Samurai::IO::Net::DNS::RR_A*>(rr->rr);
				printf("hostname='%s', resolved_rra='%s'\n", hostname, tmp->getAddress()->toString());
				eventHandler->EventHostFound(tmp->getAddress());
				found = true;
				break;
				
			} else if (dynamic_cast<Samurai::IO::Net::DNS::RR_CNAME*>(rr->rr)) {
				Samurai::IO::Net::DNS::RR_CNAME* tmp = dynamic_cast<Samurai::IO::Net::DNS::RR_CNAME*>(rr->rr);
				printf("hostname='%s', resolved_rra='%s' (cname)\n", hostname, tmp->getName()->toString());
				
				printf("rrname='%s', cname='%s'\n", rrname->toString(), tmp->getName()->toString());
				
				delete rrname;
				rrname = new Samurai::IO::Net::DNS::Name(*tmp->getName());
				rr = msg.getRecord(rrname);
				numTries++;
				
			} else {
				puts("Fuck it! Unable to cast RR");
				break;
			}
		}
		
		if (!found)
		{
			printf("Need to query again for %s\n", rrname->toString());
		}
		
		
	} else if (code == Samurai::IO::Net::DNS::DNS_STATUS_NAME_ERROR) {
		puts("=> Not found");
		eventHandler->EventHostError(Unknown);
	} else {
		puts("=> Error");
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


