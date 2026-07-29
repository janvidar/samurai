/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_TEST_KEYS_H
#define HAVE_SAMURAI_TEST_KEYS_H

/*
 * A private key and a self-signed certificate for whatever needs one.
 *
 * They are generated in process rather than checked in: that keeps a private
 * key out of the repository, needs no 'openssl' command line tool, and cannot
 * expire out from under the suite.
 *
 * TlsFactory::setKeys() is process-wide static state, so this is built exactly
 * once and shared. Both tls.tcc, which drives the factory directly, and
 * sockets.tcc, which drives it through Socket and the event loop, want the same
 * key - and generating a 2048 bit one per case would dominate the run.
 */

#include <samurai/io/net/tlsfactory.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <stdio.h>
#include <string>
#include <unistd.h>

struct TlsTestKeys
{
	std::string key_path;
	std::string cert_path;
	bool ready = false;

	/* The PEM files have to outlive every case - setKeys() keeps File objects
	   pointing at them - so they are removed when the process exits. */
	~TlsTestKeys()
	{
		if (!key_path.empty()) unlink(key_path.c_str());
		if (!cert_path.empty()) unlink(cert_path.c_str());
	}

	TlsTestKeys() = default;
	TlsTestKeys(TlsTestKeys&&) = default;
	TlsTestKeys& operator=(TlsTestKeys&&) = default;
	TlsTestKeys(const TlsTestKeys&) = delete;
	TlsTestKeys& operator=(const TlsTestKeys&) = delete;
};

/* Written next to the generated test sources, which is a writable build dir. */
static std::string tls_temp_path(const char* leaf)
{
	return std::string("samurai-tls-test-") + leaf;
}

static bool tls_write_keypair(const std::string& key_path, const std::string& cert_path)
{
	EVP_PKEY* pkey = EVP_RSA_gen(2048);
	if (!pkey) return false;

	X509* cert = X509_new();
	if (!cert) { EVP_PKEY_free(pkey); return false; }

	ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
	X509_gmtime_adj(X509_getm_notBefore(cert), 0);
	/* Long enough that the suite never fails because a fixture aged out. */
	X509_gmtime_adj(X509_getm_notAfter(cert), 60L * 60 * 24 * 3650);
	X509_set_pubkey(cert, pkey);

	X509_NAME* name = X509_get_subject_name(cert);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
		(const unsigned char*) "localhost", -1, -1, 0);
	X509_set_issuer_name(cert, name);

	bool ok = X509_sign(cert, pkey, EVP_sha256()) > 0;

	if (ok)
	{
		FILE* kf = fopen(key_path.c_str(), "wb");
		ok = kf && PEM_write_PrivateKey(kf, pkey, nullptr, nullptr, 0, nullptr, nullptr);
		if (kf) fclose(kf);
	}

	if (ok)
	{
		FILE* cf = fopen(cert_path.c_str(), "wb");
		ok = cf && PEM_write_X509(cf, cert);
		if (cf) fclose(cf);
	}

	X509_free(cert);
	EVP_PKEY_free(pkey);
	return ok;
}

static TlsTestKeys& tls_test_keys()
{
	static TlsTestKeys keys = []
	{
		TlsTestKeys k;
		k.key_path = tls_temp_path("key.pem");
		k.cert_path = tls_temp_path("cert.pem");

		if (!Samurai::IO::Net::TlsFactory::global_init()) return k;
		if (!tls_write_keypair(k.key_path, k.cert_path)) return k;

		Samurai::IO::Net::TlsFactory::setKeys(k.key_path.c_str(), k.cert_path.c_str());
		k.ready = true;
		return k;
	}();
	return keys;
}

#endif // HAVE_SAMURAI_TEST_KEYS_H
