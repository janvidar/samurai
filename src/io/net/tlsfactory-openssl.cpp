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
static bool sha256_of(X509* cert, uint8_t* digest, size_t length)
{
	if (length < Samurai::IO::Net::TlsFactory::SHA256_LENGTH) return false;

	unsigned char buf[EVP_MAX_MD_SIZE];
	unsigned int size = 0;

	if (X509_digest(cert, EVP_sha256(), buf, &size) != 1)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to hash the certificate: %s", msg);
		return false;
	}

	if (size != Samurai::IO::Net::TlsFactory::SHA256_LENGTH)
	{
		QERR("SHA-256 produced %u bytes rather than %u", size,
			(unsigned int) Samurai::IO::Net::TlsFactory::SHA256_LENGTH);
		return false;
	}

	memcpy(digest, buf, size);
	return true;
}

bool Samurai::IO::Net::TlsFactory::getOwnCertificateSHA256(uint8_t* digest, size_t length)
{
	Samurai::IO::File* pem = TlsFactory::getCertificate();
	if (!pem || !pem->exists()) return false;

	BIO* bio = BIO_new_file(pem->getName().c_str(), "r");
	if (!bio)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to open '%s': %s", pem->getName().c_str(), msg);
		return false;
	}

	X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);

	if (!cert)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to read a certificate from '%s': %s", pem->getName().c_str(), msg);
		return false;
	}

	bool found = sha256_of(cert, digest, length);
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
}

Samurai::IO::Net::OpenSSL::~OpenSSL()
{
	deinitialize();
}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::initialize(
		enum Samurai::IO::Net::TlsFactory::TlsOperation mode_,
		socket_t sd_)
{
	sd = sd_;
	mode = mode_;

	QDBG("Initializing OpenSSL with socket sd=%d", sd);

	/* The method accessors return a const pointer. */
	const SSL_METHOD* method;

	if (mode == Samurai::IO::Net::TlsFactory::TLS_OPERATE_CLIENT)
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

		return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}

	/*
	 * SSLv2/v3 and TLS 1.0/1.1 are obsolete and rejected by modern peers, so
	 * 1.2 is the floor. The ceiling is set explicitly rather than left at the
	 * default so that a system configuration file carrying MaxProtocol cannot
	 * quietly take TLS 1.3 away from us.
	 */
	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

	const bool verify = !TlsFactory::allowUntrustedConnections();

	if (verify)
	{
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);

		/* Without a trust store SSL_VERIFY_PEER can never succeed. */
		if (SSL_CTX_set_default_verify_paths(ctx) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load the system certificate store: %s", msg);
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
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
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
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
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		}
	}

	ssl = SSL_new(ctx);
	if (!ssl)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to create SSL session: %s", msg);
		return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}

	/*
	 * A valid certificate chain proves nothing unless we also check that the
	 * certificate was issued for the host we asked for. Without this, any
	 * peer holding any CA-signed certificate can impersonate any server.
	 */
	if (mode == Samurai::IO::Net::TlsFactory::TLS_OPERATE_CLIENT && !peer_name.empty())
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
				return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
			}

			/* Server Name Indication - required by most virtual hosts.
			   Only names, never addresses, may be sent (RFC 6066). */
			SSL_set_tlsext_host_name(ssl, peer_name.c_str());
		}
	}
	else if (mode == Samurai::IO::Net::TlsFactory::TLS_OPERATE_CLIENT && verify)
	{
		QERR("No peer name set - the certificate name cannot be verified. "
		     "Call TlsFactory::setPeerName() before initialize().");
		return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}

	SSL_set_fd(ssl, sd);
	return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::deinitialize()
{
	if (ssl) SSL_free(ssl);
	if (ctx) SSL_CTX_free(ctx);
	ssl = nullptr;
	ctx = nullptr;
	return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
}


enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::sendHandshake()
{
	if (!ssl) return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;

	ERR_clear_error();

	int ret;
	if (mode == Samurai::IO::Net::TlsFactory::TLS_OPERATE_CLIENT)
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
				return Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ;

			case SSL_ERROR_WANT_WRITE:
			case SSL_ERROR_WANT_CONNECT:
			case SSL_ERROR_WANT_ACCEPT:
				return Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE;

			case SSL_ERROR_ZERO_RETURN:
				return Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED;

			default:
			{
				char msg[SSL_ERRBUF_SIZE];
				ssl_error_string(msg, sizeof(msg));
				QERR("SSL handshake failed (%d): %s", error, msg);
				return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
			}
		}
	}

	QDBG("Handshake OK");

	const bool verify = !TlsFactory::allowUntrustedConnections();

	/*
	 * With SSL_VERIFY_PEER a failed chain aborts the handshake above, so this
	 * is a second line of defence rather than the primary check. It is the
	 * only check when running untrusted, where it stays informational.
	 */
	long status = SSL_get_verify_result(ssl);
	if (status != X509_V_OK)
	{
		if (verify)
		{
			QERR("Certificate verification failed: %s",
				X509_verify_cert_error_string(status));
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		}
		QDBG("Certificate not verified (%s), continuing untrusted",
			X509_verify_cert_error_string(status));
	}

	X509* cert = SSL_get1_peer_certificate(ssl);
	if (cert)
	{
		X509_free(cert);
	}
	else if (verify)
	{
		QERR("Peer presented no certificate");
		return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}

	return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
}

bool Samurai::IO::Net::OpenSSL::getPeerCertificateSHA256(uint8_t* digest, size_t length)
{
	if (!ssl) return false;

	X509* cert = SSL_get1_peer_certificate(ssl);
	if (!cert)
	{
		ERR_clear_error();
		return false;
	}

	bool found = sha256_of(cert, digest, length);
	X509_free(cert);
	return found;
}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::sendGoodbye() {

	if (!ssl) return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;

	ERR_clear_error();

	int ret = SSL_shutdown(ssl);

	/* 1 = peer replied, 0 = our notify is sent but the peer has not replied;
	   both mean we are done writing. */
	if (ret >= 0)
		return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
		case SSL_ERROR_WANT_READ:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ;
		case SSL_ERROR_WANT_WRITE:
		case SSL_ERROR_WANT_CONNECT:
		case SSL_ERROR_WANT_ACCEPT:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE;
		case SSL_ERROR_ZERO_RETURN:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED;
		default:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
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

ssize_t Samurai::IO::Net::OpenSSL::read(char* data, size_t length, enum Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_read(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL read error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
			return -1;
		}
	}
}

ssize_t Samurai::IO::Net::OpenSSL::peek(char* data, size_t length, enum Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_peek(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL peek error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
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

ssize_t Samurai::IO::Net::OpenSSL::write(const char* data, size_t length, enum Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_write(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_READ;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_WANT_WRITE;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_CLOSED;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL write error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
			return -1;
		}
	}
}
