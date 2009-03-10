/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */


#ifndef HAVE_QUICKDC_DNSRESOLVER_H
#define HAVE_QUICKDC_DNSRESOLVER_H

namespace Samurai {
namespace IO {
namespace Net {

class ResolveEventHandler;
class InetAddress;

namespace DNS {



class Resolver {
	public:
		enum Error
		{
			NotFound,       //< "Host not found"
			NoAddress,      //< "Hostname exists, but has no address"
			ServerError,    //< "A name server error occured"
			TryAgain,       //< "A temporary name server error occured"
			Unknown         //< "Unknown resolve error"
		};
		
		enum State {
			Idle,           //< "Ready to accept lookup jobs"
			Busy,           //< "Busy handling a lookup job"
			ResolveError    //< "An error has occured, check the error enum"
		};
		
		virtual ~Resolver();
		
	public:
		static Resolver* getHostByName(Samurai::IO::Net::ResolveEventHandler*, const char* name);
		static Resolver* getNameByAddress(Samurai::IO::Net::ResolveEventHandler*, InetAddress* address);
		
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

#endif // HAVE_QUICKDC_DNSRESOLVER_H
