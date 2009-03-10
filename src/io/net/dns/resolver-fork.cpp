/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/dns/resolver-fork.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/inetaddress.h>

#ifdef SAMURAI_POSIX
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <samurai/io/net/socketmonitor.h>

/*
 * FIXME FIXME FIXME: This will not work until the socket monitor can handle this. So we either need to create a
 * SocketBase class which we can poll using the process ID as socket descriptor, or we need to create a PipeSocket class (ideal).
 */


Samurai::IO::Net::DNS::ForkResolver::ForkResolver(ResolveEventHandler* eh) : Samurai::IO::Net::DNS::Resolver(eh), childPid(-1), sd(-1)
{

}

Samurai::IO::Net::DNS::ForkResolver::~ForkResolver() {
	if (childPid != -1) {
		kill(childPid, 9);
	}
}


/**
 * This does asynchronous DNS lookup by creating a pipe, 
 * forking the process and sending the result back through
 * the pipe when it's done.
 *
 * This is fast on Linux, which use Copy-on-Write for forked
 * processes, but might hurt on other Unixes.
 * The Unix world do need an asynchronous DNS resolver, that
 * is free (BSD) and standardized.
 *
 * Note: if NO_FORKED_GETHOSTBYNAME is defined, a blocking 
 * resolver is used. This can be useful for debugging purposes.
 */
void Samurai::IO::Net::DNS::ForkResolver::lookup(const char* address) {
	int FDs[2];
	int p = pipe(FDs);
	if (p == -1) {
		QERR("Unable to perform DNS lookup: pipe() failed");
		eventHandler->EventHostError(Unknown);
		return;
	}
	
	p = fork();
	if (p == 0) {
		close(FDs[0]);
		sd = FDs[1];

		// We close the reading part, as we are not interrested in reading from
		// the parent process, but rather just writing back the result as soon as it gets in.
		// close(fds[1]);
		struct hostent* host = gethostbyname(address);

		if (!host) {
			// host not found, return h_errno.
			::write(sd, (void*) 0, 4);
			close(sd);
			exit(h_errno);

		} else {
			// host found, return 0.
			::write(sd, (void*) host->h_addr_list[0], 4);
			close(sd);
			exit(0);
		}
		
	} else if (p > 0) {
		childPid = p;
	
		// parent process. We close so we cannot write, but will still be able to read from
		// the child process that will lookup the DNS queries for us.
		close(FDs[1]);
		sd = FDs[0];
		// getSocketMonitorInstance()->add(this);
		
	} else {
		QERR("Unable to perform DNS lookup: fork() failed");
		eventHandler->EventHostError(Unknown);
	}
}


void Samurai::IO::Net::DNS::ForkResolver::internal_lookup() {
	char ipaddress[4];
	
	::read(sd, (void*) ipaddress, 4);
	int status;	
	waitpid(childPid, &status, 0);

	// getSocketMonitorInstance()->remove(this);

	childPid = -1;
	close(sd); sd = -1;
	
	if (status == 0) {
		Samurai::IO::Net::InetAddress inet_addr;
		inet_addr.setRawAddress(ipaddress, 4, Samurai::IO::Net::InetAddress::IPv4);
		eventHandler->EventHostFound(&inet_addr);
	} else {
		switch (status) {
			case HOST_NOT_FOUND:
				eventHandler->EventHostError(NotFound);
				break;

			case NO_ADDRESS: // case NO_DATA:
				eventHandler->EventHostError(NoAddress);
				break;

			case NO_RECOVERY:
				eventHandler->EventHostError(ServerError);
				break;
			
			case TRY_AGAIN:
				eventHandler->EventHostError(TryAgain);
				break;
				
			default:
				eventHandler->EventHostError(Unknown);
		}
	}
}

#endif // SAMURAI_POSIX
