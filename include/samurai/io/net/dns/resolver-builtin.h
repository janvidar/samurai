/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_DNSRESOLVER_BUILTIN_H
#define HAVE_QUICKDC_DNSRESOLVER_BUILTIN_H

#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/timer.h>

namespace Samurai {
namespace IO {
namespace Net {

class DatagramSocket;
class DatagramPacket;
class SocketBase;

namespace DNS {

class Name;

class BuiltinResolver :
	public Samurai::IO::Net::DNS::Resolver,
	public Samurai::IO::Net::SocketEventHandler,
	public Samurai::IO::Net::DatagramEventHandler,
	public Samurai::TimerListener
{
	public:
		BuiltinResolver(ResolveEventHandler* eventHandler);
		virtual ~BuiltinResolver();
		void lookup(const char* addr);

	protected:
		void EventGotDatagram(DatagramSocket*, DatagramPacket* packet);
		void EventDatagramError(const DatagramSocket*, const char*);

		void EventHostLookup(const Socket*) { };
		void EventHostFound(const Socket*) { };
		void EventConnecting(const Socket*) { };
		void EventConnected(const Socket*);
		void EventTimeout(const Socket*);
		void EventDisconnected(const Socket*);
		void EventDataAvailable(const Socket*);
		void EventCanWrite(const Socket*);
		void EventError(const Socket*, enum SocketError error, const char* msg);
		void EventTimeout(Samurai::Timer* timer);
		
		void query();

	protected:
		uint16_t jobId;
		SocketBase* sock;
		char* hostname;
		Samurai::IO::Net::DNS::Name* rrname;
		int numTries;
		Samurai::Timer* timer;
};

}
}
}
}

#endif // HAVE_QUICKDC_DNSRESOLVER_BUILTIN_H
