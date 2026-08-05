/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory.h>
#include <samurai/io/file.h>


std::unique_ptr<Samurai::IO::File> Samurai::IO::Net::TlsFactory::pem_key;
std::unique_ptr<Samurai::IO::File> Samurai::IO::Net::TlsFactory::pem_cert;
bool Samurai::IO::Net::TlsFactory::allow_untrusted = false;
std::vector<std::string> Samurai::IO::Net::TlsFactory::trust_anchors;
bool Samurai::IO::Net::TlsFactory::require_client_cert = false;
unsigned long Samurai::IO::Net::TlsFactory::config_generation = 0;

Samurai::IO::Net::TlsFactory::TlsFactory()
	: sd(INVALID_SOCKET)
	, mode(TlsOperation::Client)
	, allow_untrusted_conn(allow_untrusted)
	, require_client_cert_conn(require_client_cert)
{
}

void Samurai::IO::Net::TlsFactory::priv_init()
{
	resetKeys();
}

void Samurai::IO::Net::TlsFactory::priv_fini()
{
	freeKeys();
}

bool Samurai::IO::Net::TlsFactory::defaultAllowUntrusted()
{
	return allow_untrusted;
}

void Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(bool toggle)
{
	allow_untrusted = toggle;
}

bool Samurai::IO::Net::TlsFactory::defaultRequireClientCertificate()
{
	return require_client_cert;
}

void Samurai::IO::Net::TlsFactory::setDefaultRequireClientCertificate(bool toggle)
{
	require_client_cert = toggle;
}


unsigned long Samurai::IO::Net::TlsFactory::configGeneration()
{
	return config_generation;
}


void Samurai::IO::Net::TlsFactory::resetKeys()
{
	pem_key.reset();
	pem_cert.reset();
	config_generation++;
}

void Samurai::IO::Net::TlsFactory::freeKeys()
{
	resetKeys();
}

void Samurai::IO::Net::TlsFactory::setKeys(const char* private_key, const char* public_key)
{
	freeKeys();
	
	pem_key = std::make_unique<Samurai::IO::File>(private_key);
	pem_cert = std::make_unique<Samurai::IO::File>(public_key);
	config_generation++;
}

Samurai::IO::File* Samurai::IO::Net::TlsFactory::getPrivateKey()
{
	return pem_key.get();
}

Samurai::IO::File* Samurai::IO::Net::TlsFactory::getCertificate()
{
	return pem_cert.get();
}


void Samurai::IO::Net::TlsFactory::addTrustAnchor(const char* pem_file)
{
	if (!pem_file || !*pem_file) return;
	trust_anchors.push_back(pem_file);
	config_generation++;
}


void Samurai::IO::Net::TlsFactory::clearTrustAnchors()
{
	trust_anchors.clear();
	config_generation++;
}


const std::vector<std::string>& Samurai::IO::Net::TlsFactory::getTrustAnchors()
{
	return trust_anchors;
}
