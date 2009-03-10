/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/dns/dnsrrs.h>
#include <samurai/io/net/inetaddress.h>

Samurai::IO::Net::DNS::ResourceRecord::ResourceRecord() {
	name = new Name();
	rr = 0;
}


Samurai::IO::Net::DNS::ResourceRecord::~ResourceRecord() {
	delete name;
	delete rr;
}

bool Samurai::IO::Net::DNS::ResourceRecord::isExpired() const {
	return false;
}

Samurai::IO::Net::DNS::RR::RR()
{

}

Samurai::IO::Net::DNS::RR::~RR() {

}



Samurai::IO::Net::DNS::RR_SOA::RR_SOA(const Samurai::IO::Net::DNS::Name& sone_, const Samurai::IO::Net::DNS::Name& email_, uint32_t serial_, uint32_t refresh_, uint32_t retry_, uint32_t expire_, int32_t ttl_)
{
	primary = new Samurai::IO::Net::DNS::Name(sone_);
	email = new Samurai::IO::Net::DNS::Name(email_);
	serial = serial_;
	refresh = refresh_;
	retry = retry_;
	expire = expire_;
	ttl = ttl_;
	
}

Samurai::IO::Net::DNS::RR_SOA::~RR_SOA()
{
	delete primary;
	delete email;
	
}

Samurai::IO::Net::DNS::RR_CNAME::RR_CNAME(const Samurai::IO::Net::DNS::Name& name_)
{
	name = new Samurai::IO::Net::DNS::Name(name_);
}

Samurai::IO::Net::DNS::RR_CNAME::~RR_CNAME() {
	delete name;
}

Samurai::IO::Net::DNS::RR_PTR::RR_PTR(const Samurai::IO::Net::DNS::Name& name_)
{
	name = new Samurai::IO::Net::DNS::Name(name_);
}

Samurai::IO::Net::DNS::RR_PTR::~RR_PTR()
{
	delete name;
}

Samurai::IO::Net::DNS::RR_NS::RR_NS(const Samurai::IO::Net::DNS::Name& name_)
{
	name = new Samurai::IO::Net::DNS::Name(name_);
}

Samurai::IO::Net::DNS::RR_NS::~RR_NS()
{
	delete name;
}

Samurai::IO::Net::DNS::RR_A::RR_A(const Samurai::IO::Net::InetAddress& addr_)
{
	addr = new Samurai::IO::Net::InetAddress(addr_);
}

Samurai::IO::Net::DNS::RR_A::~RR_A()
{
	delete addr;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::DNS::RR_A::getAddress()
{
	return addr;
}

Samurai::IO::Net::DNS::RR_AAAA::RR_AAAA(const Samurai::IO::Net::InetAddress& addr_)
{
	addr = new Samurai::IO::Net::InetAddress(addr_);
}

Samurai::IO::Net::DNS::RR_AAAA::~RR_AAAA()
{
	delete addr;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::DNS::RR_AAAA::getAddress()
{
	return addr;
}


Samurai::IO::Net::DNS::RR_TXT::RR_TXT(const char* txt_)
{
	txt = 0;
	if (txt_)
		txt = strdup(txt_);
}

Samurai::IO::Net::DNS::RR_TXT::~RR_TXT()
{
	delete txt;
}

