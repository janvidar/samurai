/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
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

#include <fstream>
#include <iterator>
#include <samurai/io/net/tlsfactory.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <stdio.h>
#include <string>
#include <samurai/posix.h>

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

/*
 * Written next to the generated test sources, which is a writable build dir, and
 * named after the process that writes it.
 *
 * The name has to be unique per process because the destructor unlinks what it
 * wrote: two runs of this suite from one build directory would otherwise load,
 * overwrite and delete one another's key pair, and a server left without a usable
 * key offers no cipher suite at all - which surfaces as "no shared cipher" from
 * whichever handshake was in progress, in a test that says nothing about keys.
 */
static std::string tls_temp_path(const char* leaf)
{
	return std::string("samurai-tls-test-") + std::to_string(getpid())
	     + std::string("-") + leaf;
}

static bool tls_write_keypair(const std::string& key_path, const std::string& cert_path)
{
	EVP_PKEY* pkey = EVP_RSA_gen(2048);
	if (!pkey) return false;

	X509* cert = X509_new();
	if (!cert) { EVP_PKEY_free(pkey); return false; }

	/* v3, which is what carries an extension at all. */
	X509_set_version(cert, 2);

	ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
	X509_gmtime_adj(X509_getm_notBefore(cert), 0);
	/* Long enough that the suite never fails because a fixture aged out. */
	X509_gmtime_adj(X509_getm_notAfter(cert), 60L * 60 * 24 * 3650);
	X509_set_pubkey(cert, pkey);

	X509_NAME* name = X509_get_subject_name(cert);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
		(const unsigned char*) "localhost", -1, -1, 0);
	X509_set_issuer_name(cert, name);

	/*
	 * A subjectAltName, without which nothing can verify this certificate names
	 * the host it was reached at: a bare CN has not been accepted for that in a
	 * decade, and samurai checks 127.0.0.1 as an address rather than a name - so
	 * the IP entry is the one that matters and the DNS entry is for a case that
	 * connects by name.
	 */
	bool ok = false;
	{
		X509_EXTENSION* san = X509V3_EXT_conf_nid(nullptr, nullptr,
			NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1,IP:::1");
		if (san)
		{
			ok = X509_add_ext(cert, san, -1) == 1;
			X509_EXTENSION_free(san);
		}
	}

	/*
	 * Self signed, and offered as its own trust anchor by the cases that verify
	 * against it, so it has to be allowed to be one.
	 */
	if (ok)
	{
		X509_EXTENSION* basic = X509V3_EXT_conf_nid(nullptr, nullptr,
			NID_basic_constraints, "critical,CA:TRUE");
		if (basic)
		{
			ok = X509_add_ext(cert, basic, -1) == 1;
			X509_EXTENSION_free(basic);
		}
		else
		{
			ok = false;
		}
	}

	if (ok) ok = X509_sign(cert, pkey, EVP_sha256()) > 0;

	/* Through memory: OpenSSL's FILE* writers need applink on Windows. */
	const auto save = [](const std::string& path, BIO* pem)
	{
		char* bytes = nullptr;
		const long size = pem ? BIO_get_mem_data(pem, &bytes) : 0;
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		const bool written = size > 0 && out.write(bytes, size) && out.flush();
		BIO_free(pem);
		return written;
	};

	if (ok)
	{
		BIO* pem = BIO_new(BIO_s_mem());
		ok = pem && PEM_write_bio_PrivateKey(pem, pkey, nullptr, nullptr, 0, nullptr, nullptr)
			&& save(key_path, pem);
	}

	if (ok)
	{
		BIO* pem = BIO_new(BIO_s_mem());
		ok = pem && PEM_write_bio_X509(pem, cert) && save(cert_path, pem);
	}

	X509_free(cert);
	EVP_PKEY_free(pkey);
	return ok;
}

/* A file read into a memory BIO, for the same reason the pair is written through one. */
inline BIO* tls_read_pem(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) return nullptr;
	const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	BIO* bio = BIO_new(BIO_s_mem());
	if (bio && !bytes.empty() && BIO_write(bio, bytes.data(), static_cast<int>(bytes.size())) <= 0)
	{
		BIO_free(bio);
		return nullptr;
	}
	return bio;
}

/*
 * The one fixture, shared by every translation unit that includes this.
 *
 * inline rather than static, and that is the whole of it: a static function in a
 * header has internal linkage, so each of the three .tcc files including this
 * would get its own copy of the local static below - its own generated pair, and
 * its own claim on the process-wide setKeys(). Whichever ran first decided what
 * every other file's connections would present. An inline function has one
 * instance of its local static across the program, so there is one pair.
 *
 * This accessor has no effect on process state. Reading a path or asking whether
 * the pair exists must not move the keys out from under a case that pointed them
 * at a pair of its own.
 */
inline TlsTestKeys& tls_test_keys()
{
	static TlsTestKeys keys = []
	{
		TlsTestKeys k;
		k.key_path = tls_temp_path("key.pem");
		k.cert_path = tls_temp_path("cert.pem");

		if (!Samurai::IO::Net::TlsFactory::global_init()) return k;
		if (!tls_write_keypair(k.key_path, k.cert_path)) return k;

		k.ready = true;
		return k;
	}();
	return keys;
}

/*
 * The same pair, and the process keys pointed at it.
 *
 * For the top of a case, so that what a connection presents is decided by the
 * case rather than by whatever ran before it - setKeys() is process-wide, and
 * several cases here deliberately point it at a pair of their own. Claiming it
 * on the way in is what makes each case's starting state its own.
 */
inline TlsTestKeys& tls_test_keys_claimed()
{
	TlsTestKeys& keys = tls_test_keys();
	if (keys.ready)
		Samurai::IO::Net::TlsFactory::setKeys(keys.key_path.c_str(), keys.cert_path.c_str());
	return keys;
}

#endif // HAVE_SAMURAI_TEST_KEYS_H
