/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory.h>
#include <samurai/io/file.h>


Samurai::IO::File* Samurai::IO::Net::TlsFactory::pem_key = 0;
Samurai::IO::File* Samurai::IO::Net::TlsFactory::pem_cert = 0;
bool Samurai::IO::Net::TlsFactory::allow_untrusted = true;

void Samurai::IO::Net::TlsFactory::priv_init()
{
	resetKeys();
}

void Samurai::IO::Net::TlsFactory::priv_fini()
{
	freeKeys();
}

bool Samurai::IO::Net::TlsFactory::allowUntrustedConnections()
{
	return allow_untrusted;
}

void Samurai::IO::Net::TlsFactory::setAllowUntrustedConnections(bool toggle)
{
	allow_untrusted = toggle;
}


void Samurai::IO::Net::TlsFactory::resetKeys()
{
	pem_key = 0;
	pem_cert = 0;
}

void Samurai::IO::Net::TlsFactory::freeKeys()
{
	delete pem_key;
	delete pem_cert;
	resetKeys();
}

void Samurai::IO::Net::TlsFactory::setKeys(const char* private_key, const char* public_key)
{
	freeKeys();
	
	pem_key = new Samurai::IO::File(private_key);
	pem_cert = new Samurai::IO::File(public_key);
}

Samurai::IO::File* Samurai::IO::Net::TlsFactory::getPrivateKey()
{
	return pem_key;
}

Samurai::IO::File* Samurai::IO::Net::TlsFactory::getCertificate()
{
	return pem_cert;
}

