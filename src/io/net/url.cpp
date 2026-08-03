/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <samurai/io/net/url.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/util/string.h>
#include <algorithm>
#include <vector>

Samurai::IO::Net::URL::URL(const char* cstr) : port(0), valid(false)
{
	url = std::string(cstr);
	parse();
}

Samurai::IO::Net::URL::URL(const std::string& string) : url(string), port(0), valid(false)
{
	parse();
}


Samurai::IO::Net::URL::URL(const URL& u) : url(u.url), port(0), valid(false)
{
	parse();
}

Samurai::IO::Net::URL::URL(URL* u) : url(u->url), port(0), valid(false)
{
	parse();
}


Samurai::IO::Net::URL& Samurai::IO::Net::URL::operator=(const Samurai::IO::Net::URL& u)
{
	url = std::string(u.url);
	parse();
	return *this;
}

bool Samurai::IO::Net::URL::operator==(const URL& u) const
{
	if (this == &u) return true;
	return u.url == url;
}

bool Samurai::IO::Net::URL::operator!=(const URL& u) const {
	if (this == &u) return false;
	return u.url != url;
}

Samurai::IO::Net::URL::~URL()
{
}


std::string Samurai::IO::Net::URL::toString() const
{
	return url;
}


/*
 * NOTE: this follows the RFC 3986 shape. The port is searched for within the
 * authority only, so a colon in the path is not mistaken for one.
 *
 *   scheme "://" [ userinfo "@" ] host [ ":" port ] path [ "?" query ] [ "#" frag ]
 */
void Samurai::IO::Net::URL::parse()
{
	/* Reset first: parse() is called from operator= as well as the
	   constructors, and a rejected URL must not inherit the old one's host. */
	scheme.clear();
	host = Samurai::IO::Net::InetAddress();
	hostname.clear();
	zone.clear();
	port = 0;
	path.clear();
	query.clear();
	username.clear();
	password.clear();
	file.clear();
	valid = false;

	/* scheme */
	std::string::size_type pos = url.find(':');
	if (pos == std::string::npos || pos == 0) return;

	scheme = url.substr(0, pos);
	for (std::string::size_type n = 0; n < scheme.size(); n++)
		scheme[n] = (char) tolower((unsigned char) scheme[n]);

	if (!isalpha((unsigned char) scheme[0])) return;
	for (std::string::size_type n = 0; n < scheme.size(); n++)
	{
		const unsigned char c = (unsigned char) scheme[n];
		if (!(isalnum(c) || c == '+' || c == '-' || c == '.')) return;
	}

	pos++;
	if (url.compare(pos, 2, "//") != 0) return;
	pos += 2;

	/* The authority runs to the first '/', '?' or '#'. Bounding it here is
	   what stops a colon in the path being mistaken for a port separator. */
	std::string::size_type auth_end = url.find_first_of("/?#", pos);
	if (auth_end == std::string::npos) auth_end = url.size();

	std::string authority = url.substr(pos, auth_end - pos);

	/* userinfo: split on the last '@', since one may appear in a password. */
	const std::string::size_type at = authority.rfind('@');
	if (at != std::string::npos)
	{
		const std::string userinfo = authority.substr(0, at);
		authority.erase(0, at + 1);

		const std::string::size_type colon = userinfo.find(':');
		if (colon == std::string::npos)
		{
			username = userinfo;
		}
		else
		{
			username = userinfo.substr(0, colon);
			password = userinfo.substr(colon + 1);
		}
	}

	/* host [ ":" port ] */
	std::string portstr;
	bool have_port = false;

	if (!authority.empty() && authority[0] == '[')
	{
		const std::string::size_type close = authority.find(']');
		if (close == std::string::npos) return;

		hostname = authority.substr(1, close - 1);

		/*
		 * A zone identifier belongs to the interface the address is reached by,
		 * not to the address, so it is split off before the literal is parsed -
		 * which would otherwise reject the whole thing.
		 *
		 * RFC 6874 spells the delimiter "%25", being the escaped form of the
		 * '%' that separates a zone outside a URI. Devices send the bare '%'
		 * too, so both are accepted. That leaves "%25x" ambiguous between the
		 * escaped delimiter before "x" and a bare one before "25x"; the
		 * conforming reading wins.
		 */
		const std::string::size_type pct = hostname.find('%');
		if (pct != std::string::npos)
		{
			std::string encoded = hostname.substr(pct + 1);
			hostname.erase(pct);

			if (hostname.empty()) return;
			if (encoded.compare(0, 2, "25") == 0) encoded.erase(0, 2);
			if (!Samurai::Util::percent_decode(encoded, zone)) return;
			if (zone.empty()) return;
		}

		if (close + 1 < authority.size())
		{
			if (authority[close + 1] != ':') return;
			portstr = authority.substr(close + 2);
			have_port = true;
		}
	}
	else
	{
		const std::string::size_type colon = authority.find(':');
		if (colon == std::string::npos)
		{
			hostname = authority;
		}
		else
		{
			hostname = authority.substr(0, colon);
			portstr = authority.substr(colon + 1);
			have_port = true;

			/* A second colon means an Version::IPv6 literal that was not bracketed. */
			if (portstr.find(':') != std::string::npos) return;
		}
	}

	if (have_port)
	{
		/* to_uint16() rejects anything that is not all digits, and returns 0
		   for an empty string or an out-of-range value. Port 0 is not usable
		   as a destination, so it is treated as a parse failure. */
		port = Samurai::Util::Convert::to_uint16(portstr);
		if (port == 0) return;
	}

	/* An empty host is legal: "http://" and "file:///path" both have one. */
	if (!hostname.empty())
		host = hostname;

	/* path [ "?" query ] [ "#" fragment ] */
	std::string rest = url.substr(auth_end);

	const std::string::size_type frag = rest.find('#');
	if (frag != std::string::npos) rest.erase(frag);

	const std::string::size_type q = rest.find('?');
	if (q != std::string::npos)
	{
		query = rest.substr(q + 1);
		path  = rest.substr(0, q);
	}
	else
	{
		path = rest;
	}

	if (path.empty()) path = "/";

	/* getFile() has always meant "everything after the authority", query
	   included; it is the request target. */
	file = url.substr(auth_end);
	if (file.empty()) file = "/";

	valid = true;
}

bool Samurai::IO::Net::URL::isValid() const
{
	return valid;
}


namespace {

struct SchemePort
{
	std::string_view scheme;
	uint16_t port;
};

/* Only the schemes something in the tree connects to. An unlisted scheme has no
   default, which getEffectivePort() reports as 0 rather than guessing. */
constexpr SchemePort scheme_ports[] = {
	{ "http",  80  },
	{ "https", 443 },
	{ "ftp",   21  },
	{ "ws",    80  },
	{ "wss",   443 },
};

/**
 * RFC 3986 section 5.2.4 remove_dot_segments.
 *
 * A "." segment names the directory it sits in and a ".." segment its parent,
 * and both have to be resolved before the path is used: a server is entitled to
 * treat them literally, so leaving them in means asking for a different
 * resource than the reference named. ".." at the root is discarded rather than
 * escaping above it.
 */
std::string remove_dot_segments(std::string_view path)
{
	std::vector<std::string_view> out;
	const bool absolute = !path.empty() && path.front() == '/';
	bool trailing_slash = false;

	size_t pos = absolute ? 1 : 0;
	while (pos <= path.size())
	{
		const size_t slash = path.find('/', pos);
		const std::string_view segment = (slash == std::string_view::npos)
			? path.substr(pos)
			: path.substr(pos, slash - pos);

		if (segment == ".")
		{
			trailing_slash = true;
		}
		else if (segment == "..")
		{
			if (!out.empty()) out.pop_back();
			trailing_slash = true;
		}
		else if (segment.empty())
		{
			trailing_slash = true;
		}
		else
		{
			out.push_back(segment);
			trailing_slash = false;
		}

		if (slash == std::string_view::npos) break;
		pos = slash + 1;
	}

	std::string result;
	if (absolute) result += '/';

	for (size_t n = 0; n < out.size(); n++)
	{
		if (n) result += '/';
		result += out[n];
	}

	if (trailing_slash && !(absolute && out.empty())) result += '/';

	return result;
}

/** RFC 3986 section 5.2.3 merge. */
std::string merge_paths(const std::string& base_path, bool base_has_authority,
                        std::string_view reference)
{
	if (base_has_authority && base_path.empty())
	{
		std::string merged = "/";
		merged.append(reference);
		return merged;
	}

	const std::string::size_type slash = base_path.rfind('/');
	if (slash == std::string::npos) return std::string(reference);

	std::string merged = base_path.substr(0, slash + 1);
	merged.append(reference);
	return merged;
}

}


uint16_t Samurai::IO::Net::URL::getDefaultPort(std::string_view scheme_name)
{
	for (const SchemePort& entry : scheme_ports)
		if (Samurai::Util::iequals(entry.scheme, scheme_name)) return entry.port;

	return 0;
}


uint16_t Samurai::IO::Net::URL::getEffectivePort() const
{
	if (!valid) return 0;
	if (port) return port;
	return getDefaultPort(scheme);
}


std::string Samurai::IO::Net::URL::getHostname() const
{
	return hostname;
}


Samurai::IO::Net::URL Samurai::IO::Net::URL::resolve(std::string_view reference) const
{
	if (!valid) return Samurai::IO::Net::URL("");

	/*
	 * Section 5.3 recomposition, over the components section 5.2.2 selects.
	 * The result is assembled as text and parsed, rather than written into a
	 * URL's members: this class keeps the string it was given, compares on it
	 * and hands it back from toString(), so a URL whose members disagreed with
	 * its text would be inconsistent in three places at once.
	 */
	std::string target_scheme;
	std::string target_authority;
	std::string target_path;
	std::string target_query;
	bool have_authority = false;
	bool have_query = false;

	/* The reference's own scheme, if it has one, makes it absolute. */
	std::string_view rest = reference;
	{
		const size_t colon = rest.find(':');
		const size_t delim = rest.find_first_of("/?#");
		if (colon != std::string_view::npos &&
		    (delim == std::string_view::npos || colon < delim))
		{
			bool scheme_shaped = colon > 0;
			for (size_t n = 0; n < colon && scheme_shaped; n++)
			{
				const unsigned char c = (unsigned char) rest[n];
				scheme_shaped = (n == 0)
					? (isalpha(c) != 0)
					: (isalnum(c) || c == '+' || c == '-' || c == '.');
			}

			/* An absolute reference stands on its own; nothing of the base
			   contributes to it. */
			if (scheme_shaped) return Samurai::IO::Net::URL(std::string{reference});
		}
	}

	/* A fragment is not part of what this class models, and never reaches the
	   request target, so it is dropped rather than carried. */
	{
		const size_t hash = rest.find('#');
		if (hash != std::string_view::npos) rest = rest.substr(0, hash);
	}

	std::string_view ref_query;
	{
		const size_t q = rest.find('?');
		if (q != std::string_view::npos)
		{
			ref_query = rest.substr(q + 1);
			have_query = true;
			rest = rest.substr(0, q);
		}
	}

	target_scheme = scheme;

	/* The base authority, recomposed from what parse() kept. */
	const std::string base_authority = [this]() {
		std::string out;
		if (!username.empty() || !password.empty())
		{
			out += username;
			if (!password.empty()) { out += ':'; out += password; }
			out += '@';
		}

		if (hostname.find(':') != std::string::npos)
		{
			out += '[';
			out += hostname;
			if (!zone.empty()) { out += "%25"; out += zone; }
			out += ']';
		}
		else
		{
			out += hostname;
		}

		if (port)
		{
			out += ':';
			out += std::to_string(port);
		}
		return out;
	}();

	const bool base_has_authority = !hostname.empty();

	if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/')
	{
		/* A network-path reference replaces the authority. */
		std::string_view authority_and_path = rest.substr(2);
		const size_t slash = authority_and_path.find('/');

		have_authority = true;
		if (slash == std::string_view::npos)
		{
			target_authority = std::string(authority_and_path);
			target_path.clear();
		}
		else
		{
			target_authority = std::string(authority_and_path.substr(0, slash));
			target_path = remove_dot_segments(authority_and_path.substr(slash));
		}
	}
	else
	{
		have_authority = base_has_authority;
		target_authority = base_authority;

		if (rest.empty())
		{
			target_path = path;
			if (!have_query)
			{
				ref_query = query;
				have_query = !query.empty();
			}
		}
		else if (rest.front() == '/')
		{
			target_path = remove_dot_segments(rest);
		}
		else
		{
			target_path = remove_dot_segments(
				merge_paths(path, base_has_authority, rest));
		}
	}

	target_query = std::string(ref_query);

	std::string out = target_scheme;
	out += ':';
	if (have_authority)
	{
		out += "//";
		out += target_authority;
	}
	out += target_path;
	if (have_query)
	{
		out += '?';
		out += target_query;
	}

	return Samurai::IO::Net::URL(out);
}


