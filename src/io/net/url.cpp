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
#include <algorithm>

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


std::string Samurai::IO::Net::URL::toString()
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
	std::string hostname;
	std::string portstr;
	bool have_port = false;

	if (!authority.empty() && authority[0] == '[')
	{
		const std::string::size_type close = authority.find(']');
		if (close == std::string::npos) return;

		hostname = authority.substr(1, close - 1);

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


