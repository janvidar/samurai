/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/proxy.h>
#include <samurai/io/net/url.h>
#include <samurai/stdc.h>

namespace {

/* RFC 1929 puts a single length octet in front of each of the two fields. */
const size_t SOCKS5_MAX_CREDENTIAL = 255;

/* The port IANA registers for SOCKS, which is what a spec naming no port means.
   Tor's SocksPort is 9050 instead, which is why ProxySettings::tor() exists
   rather than leaving a caller to remember it. */
const uint16_t SOCKS5_DEFAULT_PORT = 1080;

/*
 * The process default. A function-local static rather than a namespace-scope
 * one so that it is constructed on first use: a Socket built from a constructor
 * of another static object would otherwise read it before it existed.
 */
Samurai::IO::Net::ProxySettings& defaultProxy()
{
	static Samurai::IO::Net::ProxySettings settings;
	return settings;
}

}


Samurai::IO::Net::ProxySettings::ProxySettings(const std::string& host_, uint16_t port_)
	: kind(host_.empty() || port_ == 0 ? Kind::None : Kind::Socks5)
	, host(host_)
	, port(port_)
{
	/* A host with no port, or a port with no host, cannot be connected to.
	   Reporting that as Kind::None means a caller that ignored the difference
	   connects directly rather than to nothing at all. */
	if (kind == Kind::None)
	{
		host.clear();
		port = 0;
	}
}


Samurai::IO::Net::ProxySettings Samurai::IO::Net::ProxySettings::tor()
{
	return ProxySettings("127.0.0.1", 9050);
}


Samurai::IO::Net::ProxySettings Samurai::IO::Net::ProxySettings::fromURL(const Samurai::IO::Net::URL& url)
{
	if (!url.isValid()) return ProxySettings();

	const std::string scheme = url.getScheme();

	/* socks5h:// is the same thing here: a name is never resolved locally, so
	   there is no other spelling to distinguish it from. */
	if (!Samurai::Util::iequals(scheme, "socks5")
	 && !Samurai::Util::iequals(scheme, "socks5h"))
		return ProxySettings();

	const uint16_t port = url.getPort() ? url.getPort() : SOCKS5_DEFAULT_PORT;

	ProxySettings settings(url.getHostname(), port);
	if (!settings.isEnabled()) return settings;

	if (!url.getUsername().empty() || !url.getPassword().empty())
	{
		if (!settings.setCredentials(url.getUsername(), url.getPassword()))
			return ProxySettings();
	}

	return settings;
}


Samurai::IO::Net::ProxySettings Samurai::IO::Net::ProxySettings::fromString(const std::string& spec)
{
	if (spec.empty()) return ProxySettings();

	/* Anything with a scheme goes through the URL parser, which already knows
	   about credentials, bracketed IPv6 literals and zone identifiers. */
	if (spec.find("://") != std::string::npos)
		return fromURL(URL(spec));

	/* A bare "host:port", which is what a command line hands over. Parsed by
	   giving it a scheme rather than by splitting on the last colon here, so
	   that "[::1]:9050" means what it does everywhere else. */
	return fromURL(URL("socks5://" + spec));
}


bool Samurai::IO::Net::ProxySettings::setCredentials(const std::string& user, const std::string& pass)
{
	if (user.size() > SOCKS5_MAX_CREDENTIAL) return false;
	if (pass.size() > SOCKS5_MAX_CREDENTIAL) return false;

	username = user;
	password = pass;
	return true;
}


std::string Samurai::IO::Net::ProxySettings::toString() const
{
	if (kind == Kind::None) return "none";

	/* Bracketed the way a URL would write it, so a literal with colons in it
	   does not read as a host and a port. The credentials are deliberately not
	   included: this ends up in log lines. */
	std::string out;
	if (host.find(':') != std::string::npos)
	{
		out += '[';
		out += host;
		out += ']';
	}
	else
	{
		out += host;
	}

	out += ':';
	out += std::to_string(port);
	return out;
}


bool Samurai::IO::Net::ProxySettings::operator==(const Samurai::IO::Net::ProxySettings& other) const
{
	return kind == other.kind
		&& host == other.host
		&& port == other.port
		&& username == other.username
		&& password == other.password
		&& tor_extensions == other.tor_extensions
		&& allow_private == other.allow_private;
}


const Samurai::IO::Net::ProxySettings& Samurai::IO::Net::ProxySettings::getDefault()
{
	return defaultProxy();
}


void Samurai::IO::Net::ProxySettings::setDefault(const Samurai::IO::Net::ProxySettings& proxy)
{
	defaultProxy() = proxy;
}


void Samurai::IO::Net::ProxySettings::clearDefault()
{
	defaultProxy() = ProxySettings();
}

// eof
