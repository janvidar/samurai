/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */


#ifndef HAVE_SAMURAI_DNSRESOLVER_H
#define HAVE_SAMURAI_DNSRESOLVER_H

#include <memory>

namespace Samurai {

class EventLoop;

namespace IO {
namespace Net {

class ResolveEventHandler;
class InetAddress;

namespace DNS {



class Resolver {
	public:
		enum class Error
		{
			NotFound,       //< the name does not resolve
			NoAddress,      //< the name exists, but carries no address
			ServerError,    //< a name server error
			TryAgain,       //< a temporary name server error
			Unknown         //< anything else
		};
		
		virtual ~Resolver();
		
	public:
		/**
		 * Start a lookup, answered on 'loop'.
		 *
		 * The caller owns the returned resolver and must keep it alive until
		 * the event handler has been called: an asynchronous backend needs the
		 * object to still exist when the reply arrives.
		 *
		 * @return null if no backend could be started.
		 */
		static std::unique_ptr<Resolver> getHostByName(Samurai::EventLoop& loop, Samurai::IO::Net::ResolveEventHandler*, const char* name);
		static std::unique_ptr<Resolver> getNameByAddress(Samurai::EventLoop& loop, Samurai::IO::Net::ResolveEventHandler*, InetAddress* address);
		
	protected:
		Resolver(Samurai::IO::Net::ResolveEventHandler*);
		
		
		virtual void lookup(const char*) = 0;
		
	protected:
		Samurai::IO::Net::ResolveEventHandler* eventHandler;
};


/**
 * Why a lookup failed, worded to follow the name it failed for.
 *
 * Which kind it was decides what the reader should do: a name that does not
 * exist is a typo to correct, and a temporary name server error is the same
 * lookup worth trying again.
 */
const char* toString(Resolver::Error error);


}
}
}
}

#endif // HAVE_SAMURAI_DNSRESOLVER_H
