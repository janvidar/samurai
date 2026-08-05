/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_DNSRESOLVER_SOCKS_H
#define HAVE_SAMURAI_DNSRESOLVER_SOCKS_H

#include <samurai/samurai.h>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/proxy.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socks5.h>
#include <samurai/timer.h>

#include <memory>
#include <string>

namespace Samurai {
namespace IO {
namespace Net {

class Socket;

namespace DNS {

/**
 * Asks the proxy the question instead of asking the system resolver.
 *
 * Tor extends SOCKS5 with two commands that return an answer rather than open a
 * stream: RESOLVE (0xF0) and RESOLVE_PTR (0xF1). They exist because a program
 * that has to resolve a name for its own sake - to print it, or to decide
 * something before connecting - would otherwise have to ask the system resolver,
 * which is the one question a tunnel cannot carry.
 *
 * Selected by Resolver::getHostByName() when the process-wide proxy has
 * ProxySettings::setTorExtensions() on, and not otherwise: a SOCKS5 proxy that
 * is not Tor answers an unknown command with command-not-supported, so asking
 * one would cost a round trip and then fail a lookup that would have worked.
 *
 * The proxy has to be given as an address literal for this to be used. A proxy
 * named rather than numbered would have to have its own name resolved, and the
 * only resolver available for that is this one - which would ask the proxy where
 * the proxy is. Tor's 127.0.0.1 is a literal, so the constraint costs nothing in
 * the case it was written for; anything else falls back to the system resolver
 * for that one lookup, which is not a leak, the proxy's name being about to be
 * contacted openly in any case.
 */
class SocksResolver final
	: public Resolver
	, public Samurai::IO::Net::SocketEventHandler
	, public Samurai::TimerListener
{
	public:
		/**
		 * @return null when this backend cannot serve the lookup, in which case
		 *         the caller should fall back to the pooled resolver. That is
		 *         the case when no proxy is set, when Tor extensions are off,
		 *         and when the proxy is named rather than numbered.
		 */
		static std::unique_ptr<Resolver> forward(ResolveEventHandler* eh, const char* name);
		static std::unique_ptr<Resolver> reverse(ResolveEventHandler* eh,
		                                         const InetAddress& address);

		explicit SocksResolver(ResolveEventHandler* eh);
		~SocksResolver() override;

		SocksResolver(const SocksResolver&) = delete;
		SocksResolver& operator=(const SocksResolver&) = delete;

		void lookup(const char* name) override;

		/** The reverse form, which asks about an address rather than a name. */
		void lookupAddress(const InetAddress& address);

	private:
		void EventConnected(const Socket*) override;
		void EventDataAvailable(const Socket*) override;
		void EventCanWrite(const Socket*) override;
		void EventDisconnected(const Socket*) override;
		void EventError(const Socket*, SocketError, const char*) override;

		/* Both bases declare an EventTimeout, so both are named here: leaving
		   one out would hide it rather than override it. */
		void EventTimeout(const Socket*) override;
		void EventTimeout(Samurai::Timer* timer) override;

		void internal_start(Socks5Handshake::Command command, const std::string& query);
		void internal_pump();
		void internal_found(const InetAddress& address);

		/**
		 * Report a failure without doing it from inside the call that started
		 * the lookup.
		 *
		 * Resolver::getHostByName() hands its result back to a caller that
		 * assigns it - InetAddress::lookup() stores it in a member - so a handler
		 * called before that assignment could destroy this object while it is
		 * still being constructed. A zero-length timer moves the report to the
		 * next pass of the loop, which is where every other outcome arrives from
		 * anyway.
		 */
		void internal_fail_later(Resolver::Error why);

		void internal_teardown();

		ProxySettings settings;
		std::shared_ptr<Socket> socket;
		std::unique_ptr<Socks5Handshake> handshake;
		std::unique_ptr<Samurai::Timer> deferred;
		/*
		 * Covers the whole exchange. The socket's own connect timeout only
		 * covers reaching the proxy; a proxy that accepts the connection and
		 * then never answers the question would otherwise leave the lookup
		 * outstanding for good.
		 */
		std::unique_ptr<Samurai::Timer> deadline;

		/* The address the question was about, which a reverse answer travels
		   attached to rather than replacing - the same convention the pooled
		   resolver uses. */
		InetAddress queried;
		Resolver::Error deferred_error = Resolver::Error::Unknown;
		bool reverse_lookup = false;
		bool reported = false;
};

}
}
}
}

#endif // HAVE_SAMURAI_DNSRESOLVER_SOCKS_H
