/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */


#ifndef HAVE_SAMURAI_DNSRESOLVER_H
#define HAVE_SAMURAI_DNSRESOLVER_H

#include <memory>

namespace Samurai {
namespace IO {
namespace Net {

class ResolveEventHandler;
class InetAddress;

namespace DNS {



class Resolver {
	public:
		enum class Error
		{
			NotFound,       //< "Host not found"
			NoAddress,      //< "Hostname exists, but has no address"
			ServerError,    //< "A name server error occured"
			TryAgain,       //< "A temporary name server error occured"
			Unknown         //< "Unknown resolve error"
		};
		
		virtual ~Resolver();
		
	public:
		/**
		 * Start a lookup.
		 *
		 * The caller owns the returned resolver and must keep it alive until
		 * the event handler has been called: an asynchronous backend needs the
		 * object to still exist when the reply arrives.
		 *
		 * @return null if no backend could be started.
		 */
		static std::unique_ptr<Resolver> getHostByName(Samurai::IO::Net::ResolveEventHandler*, const char* name);
		static std::unique_ptr<Resolver> getNameByAddress(Samurai::IO::Net::ResolveEventHandler*, InetAddress* address);
		
	protected:
		Resolver(Samurai::IO::Net::ResolveEventHandler*);
		
		
		virtual void lookup(const char*) = 0;
		
	protected:
		Samurai::IO::Net::ResolveEventHandler* eventHandler;
};


}
}
}
}

#endif // HAVE_SAMURAI_DNSRESOLVER_H
