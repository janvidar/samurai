/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_DNSRESOLVER_BUILTIN_H
#define HAVE_SAMURAI_DNSRESOLVER_BUILTIN_H

#include <memory>
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

class BuiltinResolver final :
	public Samurai::IO::Net::DNS::Resolver,
	public Samurai::IO::Net::SocketEventHandler,
	public Samurai::IO::Net::DatagramEventHandler,
	public Samurai::TimerListener
{
	public:
		BuiltinResolver(ResolveEventHandler* eventHandler);
		~BuiltinResolver() override;

		/* Releases raw pointers in its destructor, so the implicit copy
		 * operations would release them a second time. */
		BuiltinResolver(const BuiltinResolver&) = delete;
		BuiltinResolver& operator=(const BuiltinResolver&) = delete;
		void lookup(const char* addr) override;

	protected:
		void EventGotDatagram(DatagramSocket*, DatagramPacket* packet) override;
		void EventDatagramError(const DatagramSocket*, const char*) override;

		void EventHostLookup(const Socket*) override { };
		void EventHostFound(const Socket*) override { };
		void EventConnecting(const Socket*) override { };
		void EventConnected(const Socket*) override;
		void EventTimeout(const Socket*) override;
		void EventDisconnected(const Socket*) override;
		void EventDataAvailable(const Socket*) override;
		void EventCanWrite(const Socket*) override;
		void EventError(const Socket*, enum SocketError error, const char* msg) override;
		void EventTimeout(Samurai::Timer* timer) override;
		
		void query();

	protected:
		uint16_t jobId;
		std::shared_ptr<SocketBase> sock;
		char* hostname;
		Samurai::IO::Net::DNS::Name* rrname;
		int numTries;
		Samurai::Timer* timer;
};

}
}
}
}

#endif // HAVE_SAMURAI_DNSRESOLVER_BUILTIN_H
