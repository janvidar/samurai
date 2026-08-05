/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/dns/resolver-socks.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketaddress.h>

namespace {

/* Long enough that a busy proxy is not given up on, short enough that a
   lookup nobody will ever answer does not hold a caller for good. */
const std::chrono::seconds SOCKS_RESOLVE_TIMEOUT(30);

/**
 * Whether this backend can serve a lookup at all.
 *
 * The literal requirement is not fussiness: a proxy named rather than numbered
 * would have to have its own name resolved, and with Tor extensions on this is
 * the resolver that would be asked to do it - which would ask the proxy where
 * the proxy is.
 */
bool usable(const Samurai::IO::Net::ProxySettings& settings)
{
	if (!settings.isEnabled()) return false;
	if (!settings.getTorExtensions()) return false;

	Samurai::IO::Net::InetAddress host(settings.getHost());
	return host.getType() != Samurai::IO::Net::InetAddress::Version::Unspecified;
}

}


/* static */
std::unique_ptr<Samurai::IO::Net::DNS::Resolver>
Samurai::IO::Net::DNS::SocksResolver::forward(Samurai::IO::Net::ResolveEventHandler* eh,
                                              const char* name)
{
	if (!name || !usable(Samurai::IO::Net::ProxySettings::getDefault())) return nullptr;

	auto resolver = std::make_unique<SocksResolver>(eh);
	resolver->lookup(name);
	return resolver;
}


/* static */
std::unique_ptr<Samurai::IO::Net::DNS::Resolver>
Samurai::IO::Net::DNS::SocksResolver::reverse(Samurai::IO::Net::ResolveEventHandler* eh,
                                              const Samurai::IO::Net::InetAddress& address)
{
	if (!usable(Samurai::IO::Net::ProxySettings::getDefault())) return nullptr;

	auto resolver = std::make_unique<SocksResolver>(eh);
	resolver->lookupAddress(address);
	return resolver;
}


Samurai::IO::Net::DNS::SocksResolver::SocksResolver(Samurai::IO::Net::ResolveEventHandler* eh)
	: Samurai::IO::Net::DNS::Resolver(eh)
	, settings(Samurai::IO::Net::ProxySettings::getDefault())
{
}


Samurai::IO::Net::DNS::SocksResolver::~SocksResolver()
{
	/* Nothing is reported from here. The caller owns this object and destroying
	   it is how it says it stopped caring; calling back into a handler that is
	   being torn down is what that promise is worth avoiding. */
	reported = true;
	internal_teardown();
}


void Samurai::IO::Net::DNS::SocksResolver::internal_teardown()
{
	deadline.reset();
	deferred.reset();
	handshake.reset();

	if (socket)
	{
		socket->setEventHandler(nullptr);
		socket->disconnect();
		socket.reset();
	}
}


void Samurai::IO::Net::DNS::SocksResolver::lookup(const char* name)
{
	reverse_lookup = false;
	internal_start(Socks5Handshake::Command::Resolve, name ? name : "");
}


void Samurai::IO::Net::DNS::SocksResolver::lookupAddress(const Samurai::IO::Net::InetAddress& address)
{
	reverse_lookup = true;
	queried = address;
	internal_start(Socks5Handshake::Command::ResolvePtr, address.getAddress());
}


void Samurai::IO::Net::DNS::SocksResolver::internal_start(
	Samurai::IO::Net::Socks5Handshake::Command command, const std::string& query)
{
	/*
	 * A lookup is not a connection, so the policy that refuses to ask a proxy
	 * for a peer on the local network does not apply: asking what a name
	 * resolves to reveals nothing to the local network and reaches nothing on
	 * it. The answer may well be a private address, and reporting it is the
	 * caller's business rather than something to hide from them.
	 */
	settings.setAllowPrivateTargets(true);

	handshake = std::make_unique<Socks5Handshake>(settings, command, query, 0);

	if (handshake->getStatus() == Socks5Handshake::Status::Failed)
	{
		internal_fail_later(Resolver::Error::NotFound);
		return;
	}

	InetAddress proxy_host(settings.getHost());
	socket = Socket::create(this, proxy_host, settings.getPort());

	if (!socket)
	{
		internal_fail_later(Resolver::Error::ServerError);
		return;
	}

	/* Explicitly unproxied: this socket goes to the proxy, so letting it inherit
	   the process default would have it negotiate a tunnel to the proxy through
	   the proxy. The negotiation here is driven by hand instead, because a
	   lookup ends with the reply rather than with a stream. */
	socket->setProxy(ProxySettings());
	socket->setConnectTimeout(SOCKS_RESOLVE_TIMEOUT);

	deadline = std::make_unique<Samurai::Timer>(this, SOCKS_RESOLVE_TIMEOUT, true);
	socket->connect();
}


void Samurai::IO::Net::DNS::SocksResolver::internal_fail_later(Samurai::IO::Net::DNS::Resolver::Error why)
{
	deferred_error = why;
	deferred = std::make_unique<Samurai::Timer>(this, std::chrono::milliseconds(0), true);
}


void Samurai::IO::Net::DNS::SocksResolver::EventTimeout(Samurai::Timer* timer)
{
	if (reported) return;
	reported = true;

	const Resolver::Error why = (timer == deferred.get())
		? deferred_error : Resolver::Error::TryAgain;

	internal_teardown();
	if (eventHandler) eventHandler->EventHostError(why);
}


void Samurai::IO::Net::DNS::SocksResolver::EventConnected(const Samurai::IO::Net::Socket*)
{
	internal_pump();
}


void Samurai::IO::Net::DNS::SocksResolver::EventDataAvailable(const Samurai::IO::Net::Socket*)
{
	internal_pump();
}


void Samurai::IO::Net::DNS::SocksResolver::EventCanWrite(const Samurai::IO::Net::Socket*)
{
	internal_pump();
}


void Samurai::IO::Net::DNS::SocksResolver::EventDisconnected(const Samurai::IO::Net::Socket*)
{
	if (reported) return;
	reported = true;

	internal_teardown();
	if (eventHandler) eventHandler->EventHostError(Resolver::Error::ServerError);
}


void Samurai::IO::Net::DNS::SocksResolver::EventTimeout(const Samurai::IO::Net::Socket*)
{
	if (reported) return;
	reported = true;

	internal_teardown();
	if (eventHandler) eventHandler->EventHostError(Resolver::Error::TryAgain);
}


void Samurai::IO::Net::DNS::SocksResolver::EventError(const Samurai::IO::Net::Socket*,
                                                      Samurai::IO::Net::SocketError,
                                                      const char*)
{
	if (reported) return;
	reported = true;

	internal_teardown();
	if (eventHandler) eventHandler->EventHostError(Resolver::Error::ServerError);
}


void Samurai::IO::Net::DNS::SocksResolver::internal_pump()
{
	if (reported || !socket || !handshake) return;

	bool want_write = false;
	std::error_code ec;
	const Socks5Handshake::Status status =
		Samurai::IO::Net::socks5_pump(*handshake, socket->getFD(), want_write, ec);

	socket->toggleWriteNotifier(want_write);

	if (ec || status == Socks5Handshake::Status::Failed)
	{
		reported = true;

		/*
		 * A proxy that answered "host unreachable" said the name does not
		 * resolve; anything else went wrong before it got that far. Both are
		 * failures to the caller, and telling them apart is what lets one be
		 * retried and the other not.
		 */
		Resolver::Error why = Resolver::Error::ServerError;
		if (!ec && handshake->getReplyCode() == 0x04)
			why = Resolver::Error::NotFound;

		internal_teardown();
		if (eventHandler) eventHandler->EventHostError(why);
		return;
	}

	if (status != Socks5Handshake::Status::Done) return;

	const Socks5Handshake::Result& result = handshake->getResult();

	if (reverse_lookup)
	{
		/* The name a reverse lookup answers with travels attached to the address
		   that was asked about, rather than replacing it - the convention the
		   pooled resolver already reports by. */
		if (result.name.empty())
		{
			reported = true;
			internal_teardown();
			if (eventHandler) eventHandler->EventHostError(Resolver::Error::NoAddress);
			return;
		}

		InetAddress answer(queried);
		answer.setHostname(result.name);
		internal_found(answer);
		return;
	}

	if (!result.address.isResolved())
	{
		reported = true;
		internal_teardown();
		if (eventHandler) eventHandler->EventHostError(Resolver::Error::NoAddress);
		return;
	}

	internal_found(result.address);
}


void Samurai::IO::Net::DNS::SocksResolver::internal_found(const Samurai::IO::Net::InetAddress& address)
{
	reported = true;

	/*
	 * Copied out before the teardown, and the teardown done before the handler
	 * runs: the address lives inside the handshake, the handler is entitled to
	 * destroy this resolver, and reporting from on top of state that is about to
	 * be released is how that becomes a use after free.
	 */
	InetAddress answer(address);
	internal_teardown();

	if (eventHandler) eventHandler->EventHostFound(&answer);
}

// eof
