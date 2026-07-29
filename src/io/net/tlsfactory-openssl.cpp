/*
 *
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory-openssl.h>
#include <samurai/io/file.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <limits.h>
#include <string.h>

/*
 * The build requires OpenSSL 3.0 or LibreSSL 3.5, both of which negotiate
 * TLS 1.3 whenever the peer offers it. A stack without it would silently top
 * out at the 1.2 floor set in initialize().
 */
#ifndef TLS1_3_VERSION
#error "TLS 1.3 support is required"
#endif

/*
 * OpenSSL 3.0 renamed this. LibreSSL still has only the old name.
 */
#ifdef LIBRESSL_VERSION_NUMBER
#define SSL_get1_peer_certificate SSL_get_peer_certificate
#endif

/*
 * ERR_error_string() takes no length and requires a buffer of at least 256
 * bytes, so it can only be used safely via the counted variant.
 */
#define SSL_ERRBUF_SIZE 256

static void ssl_error_string(char* buf, size_t buflen)
{
	unsigned long code = ERR_get_error();

	if (code == 0)
	{
		/* Not every failure puts something on the error queue. */
		strncpy(buf, "no error reported", buflen - 1);
		buf[buflen - 1] = 0;
		return;
	}

	ERR_error_string_n(code, buf, buflen);
	ERR_clear_error();
}

/*
 * X509_digest() hashes the DER encoding of the whole certificate, which is what
 * a certificate fingerprint is defined over.
 */
static std::optional<Samurai::IO::Net::TlsFactory::Sha256Digest> sha256_of(X509* cert)
{
	unsigned char buf[EVP_MAX_MD_SIZE];
	unsigned int size = 0;

	if (X509_digest(cert, EVP_sha256(), buf, &size) != 1)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to hash the certificate: %s", msg);
		return std::nullopt;
	}

	if (size != Samurai::IO::Net::TlsFactory::SHA256_LENGTH)
	{
		QERR("SHA-256 produced %u bytes rather than %u", size,
			(unsigned int) Samurai::IO::Net::TlsFactory::SHA256_LENGTH);
		return std::nullopt;
	}

	Samurai::IO::Net::TlsFactory::Sha256Digest digest;
	memcpy(digest.data(), buf, digest.size());
	return digest;
}

std::optional<Samurai::IO::Net::TlsFactory::Sha256Digest> Samurai::IO::Net::TlsFactory::getOwnCertificateSHA256()
{
	Samurai::IO::File* pem = TlsFactory::getCertificate();
	if (!pem || !pem->exists()) return std::nullopt;

	BIO* bio = BIO_new_file(pem->getName().c_str(), "r");
	if (!bio)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to open '%s': %s", pem->getName().c_str(), msg);
		return std::nullopt;
	}

	X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);

	if (!cert)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to read a certificate from '%s': %s", pem->getName().c_str(), msg);
		return std::nullopt;
	}

	auto found = sha256_of(cert);
	X509_free(cert);
	return found;
}

bool Samurai::IO::Net::TlsFactory::global_init()
{
	TlsFactory::priv_init();
	/* The library initializes itself on first use. */
	return true;
}

bool Samurai::IO::Net::TlsFactory::global_deinit()
{
	TlsFactory::priv_fini();
	return true;
}


Samurai::IO::Net::OpenSSL::OpenSSL()
{
	ssl = nullptr;
	ctx = nullptr;
	verify_peer = false;
}

Samurai::IO::Net::OpenSSL::~OpenSSL()
{
	deinitialize();
}

Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::initialize(
		Samurai::IO::Net::TlsFactory::TlsOperation mode_,
		socket_t sd_)
{
	sd = sd_;
	mode = mode_;

	QDBG("Initializing OpenSSL with socket sd=%d", sd);

	/* The method accessors return a const pointer. */
	const SSL_METHOD* method;

	if (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client)
		method = TLS_client_method();
	else
		method = TLS_server_method();

	ctx = SSL_CTX_new(method);
	ssl = nullptr;
	if (!ctx)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("SSL error: %s", msg);

		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	/*
	 * SSLv2/v3 and TLS 1.0/1.1 are obsolete and rejected by modern peers, so
	 * 1.2 is the floor. The ceiling is set explicitly rather than left at the
	 * default so that a system configuration file carrying MaxProtocol cannot
	 * quietly take TLS 1.3 away from us.
	 */
	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

	const bool client = (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client);

	/*
	 * A client always wants the server's certificate. A server only wants the
	 * client's for mutual TLS, and demanding one otherwise rejects every client
	 * that has none - which is nearly all of them, and would make verifying
	 * connections unusable for a server.
	 */
	verify_peer = !allowUntrusted() && (client || requireClientCertificate());

	if (verify_peer)
	{
		/* SSL_VERIFY_FAIL_IF_NO_PEER_CERT is a server-side flag; OpenSSL
		   ignores it for a client, which fails the handshake on a missing
		   certificate regardless. */
		int flags = SSL_VERIFY_PEER;
		if (!client) flags |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

		SSL_CTX_set_verify(ctx, flags, nullptr);

		/* Without a trust store SSL_VERIFY_PEER can never succeed. */
		if (SSL_CTX_set_default_verify_paths(ctx) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load the system certificate store: %s", msg);
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}
	}
	else
	{
		SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
	}

	Samurai::IO::File* cert = TlsFactory::getCertificate();
	if (cert && cert->exists())
	{
		if (SSL_CTX_use_certificate_file(ctx, cert->getName().c_str(), SSL_FILETYPE_PEM) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load certificate: %s", msg);
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}
	}

	Samurai::IO::File* pkey = TlsFactory::getPrivateKey();
	if (pkey && pkey->exists())
	{
		if (SSL_CTX_use_PrivateKey_file(ctx, pkey->getName().c_str(), SSL_FILETYPE_PEM) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load private key: %s", msg);
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}
	}

	ssl = SSL_new(ctx);
	if (!ssl)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to create SSL session: %s", msg);
		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	/*
	 * A valid certificate chain proves nothing unless we also check that the
	 * certificate was issued for the host we asked for. Without this, any
	 * peer holding any CA-signed certificate can impersonate any server.
	 */
	if (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client && !peer_name.empty())
	{
		X509_VERIFY_PARAM* param = SSL_get0_param(ssl);

		/* An address literal must be matched against an iPAddress SAN,
		   a name against dNSName/CN. set1_ip_asc() tells us which it is. */
		bool is_address = (X509_VERIFY_PARAM_set1_ip_asc(param, peer_name.c_str()) == 1);
		ERR_clear_error(); /* a non-address name leaves an error queued */

		if (!is_address)
		{
			X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
			if (X509_VERIFY_PARAM_set1_host(param, peer_name.c_str(), 0) != 1)
			{
				QERR("Unable to set expected peer name '%s'", peer_name.c_str());
				return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			}

			/* Server Name Indication - required by most virtual hosts.
			   Only names, never addresses, may be sent (RFC 6066). */
			SSL_set_tlsext_host_name(ssl, peer_name.c_str());
		}
	}
	else if (client && verify_peer)
	{
		QERR("No peer name set - the certificate name cannot be verified. "
		     "Call TlsFactory::setPeerName() before initialize().");
		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	SSL_set_fd(ssl, sd);
	return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
}

Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::deinitialize()
{
	if (ssl) SSL_free(ssl);
	if (ctx) SSL_CTX_free(ctx);
	ssl = nullptr;
	ctx = nullptr;
	return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
}


Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::sendHandshake()
{
	if (!ssl) return Samurai::IO::Net::TlsFactory::TlsStatus::Error;

	ERR_clear_error();

	int ret;
	if (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client)
		ret = SSL_connect(ssl);
	else
		ret = SSL_accept(ssl);

	/*
	 * Only 1 means the handshake completed. 0 is a controlled failure and
	 * anything negative means either a retry or a hard error - none of which
	 * may be treated as success.
	 */
	if (ret != 1)
	{
		int error = SSL_get_error(ssl, ret);

		switch (error)
		{
			case SSL_ERROR_WANT_READ:
				return Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;

			case SSL_ERROR_WANT_WRITE:
			case SSL_ERROR_WANT_CONNECT:
			case SSL_ERROR_WANT_ACCEPT:
				return Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;

			case SSL_ERROR_ZERO_RETURN:
				return Samurai::IO::Net::TlsFactory::TlsStatus::Closed;

			default:
			{
				char msg[SSL_ERRBUF_SIZE];
				ssl_error_string(msg, sizeof(msg));
				QERR("SSL handshake failed (%d): %s", error, msg);
				return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			}
		}
	}

	QDBG("Handshake OK");

	/*
	 * With SSL_VERIFY_PEER a failed chain aborts the handshake above, so this
	 * is a second line of defence rather than the primary check. It is the
	 * only check when running untrusted, where it stays informational.
	 */
	long status = SSL_get_verify_result(ssl);
	if (status != X509_V_OK)
	{
		if (verify_peer)
		{
			QERR("Certificate verification failed: %s",
				X509_verify_cert_error_string(status));
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}
		QDBG("Certificate not verified (%s), continuing untrusted",
			X509_verify_cert_error_string(status));
	}

	X509* cert = SSL_get1_peer_certificate(ssl);
	if (cert)
	{
		X509_free(cert);
	}
	else if (verify_peer)
	{
		QERR("Peer presented no certificate");
		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
}

std::optional<Samurai::IO::Net::TlsFactory::Sha256Digest> Samurai::IO::Net::OpenSSL::getPeerCertificateSHA256()
{
	if (!ssl) return std::nullopt;

	X509* cert = SSL_get1_peer_certificate(ssl);
	if (!cert)
	{
		ERR_clear_error();
		return std::nullopt;
	}

	auto found = sha256_of(cert);
	X509_free(cert);
	return found;
}

Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::sendGoodbye() {

	if (!ssl) return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;

	ERR_clear_error();

	int ret = SSL_shutdown(ssl);

	/* 1 = peer replied, 0 = our notify is sent but the peer has not replied;
	   both mean we are done writing. */
	if (ret >= 0)
		return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
		case SSL_ERROR_WANT_READ:
			return Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;
		case SSL_ERROR_WANT_WRITE:
		case SSL_ERROR_WANT_CONNECT:
		case SSL_ERROR_WANT_ACCEPT:
			return Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;
		case SSL_ERROR_ZERO_RETURN:
			return Samurai::IO::Net::TlsFactory::TlsStatus::Closed;
		default:
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

}


/*
 * SSL_read()/SSL_write()/SSL_peek() take an int length, and SSL_get_error()
 * may only be consulted when the return value is <= 0.
 */
static int ssl_clamp_length(size_t length)
{
	if (length > (size_t) INT_MAX) return INT_MAX;
	return (int) length;
}

ssize_t Samurai::IO::Net::OpenSSL::read(char* data, size_t length, Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_read(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Closed;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL read error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			return -1;
		}
	}
}

ssize_t Samurai::IO::Net::OpenSSL::peek(char* data, size_t length, Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_peek(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Closed;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL peek error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			return -1;
		}
	}
}

size_t Samurai::IO::Net::OpenSSL::pending() const
{
	if (!ssl) return 0;

	int ret = SSL_pending(ssl);
	return (ret > 0) ? (size_t) ret : 0;
}

ssize_t Samurai::IO::Net::OpenSSL::write(const char* data, size_t length, Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_write(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Closed;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL write error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			return -1;
		}
	}
}
