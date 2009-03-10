/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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


void Samurai::IO::Net::URL::parse()
{
	valid = true;
	std::string::size_type split = url.find_first_of(':');
	
	if (split == std::string::npos || split == 0)
	{
		valid = false;
		return;
	}

	scheme = url.substr(0, split);
	int (*pf)(int)=tolower;
	std::transform(scheme.begin(), scheme.end(), scheme.begin(), pf);
	split++;
	
	if (split >= url.size() || url.compare(split, 2, "//") != 0)
	{
		valid = false;
		return;
	}
	else
	{
		split += 2;
	}

	// Guess the end of the hostname area
	std::string::size_type split_end = std::string::npos;
	if (split_end == std::string::npos)
		split_end = url.find_first_of('/', split);
	if (split_end == std::string::npos)
		split_end = url.find_first_of('?', split);
	if (split_end == std::string::npos)
		split_end = url.size();

	std::string::size_type split_host_end = split_end;
	
	/* Check for IPv6 address packed in square brackets */
	std::string::size_type split_ipv6 = std::string::npos;
	if (url[split] == '[' && split+1 != split_host_end)
	{
		split_ipv6 = url.find_first_of(']', split);
		if (split_ipv6 == std::string::npos || split_ipv6 > split_end)
		{
			valid = false;
		}
		else
		{
			split = split_ipv6;
		}
	}
	
	std::string::size_type split_port = url.find_first_of(':', split);
	if (split_port != std::string::npos)
	{
		port = Samurai::Util::Convert::to_uint16(url.substr(split_port+1, split_end-(split_port+1)));
		if (port == 0)
		{
			valid = false;
		}
		else
		{
			split_host_end = split_port;
		}
	}

	host = url.substr(split, split_host_end-split);
	file = url.substr(split_end);
}

bool Samurai::IO::Net::URL::isValid() const
{
	return valid;
}


