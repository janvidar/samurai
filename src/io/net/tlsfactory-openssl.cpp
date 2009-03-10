/*
 *
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory-openssl.h>
#include <samurai/io/file.h>

#if defined(SSL_SUPPORT) && defined(SSL_OPENSSL)

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include <openssl/err.h>

bool Samurai::IO::Net::TlsFactory::global_init() {
	TlsFactory::priv_init();
	SSL_library_init();
	return true;
}

bool Samurai::IO::Net::TlsFactory::global_deinit() {
//	EVP_cleanup();
	TlsFactory::priv_fini();
	return true;
}


Samurai::IO::Net::OpenSSL::OpenSSL() {
	ssl = 0;
	ctx = 0;
}

Samurai::IO::Net::OpenSSL::~OpenSSL() {
	deinitialize();
}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::initialize(
		enum Samurai::IO::Net::TlsFactory::TlsOperation mode_,
		socket_t sd_)
{
	sd = sd_;
	mode = mode_;
	
	QDBG("Initializing OpenSSL with socket sd=%d", sd);

	SSL_METHOD* method;
	
	if (mode == Samurai::IO::Net::TlsFactory::TLS_OPERATE_CLIENT) {
		method = TLSv1_client_method();
		// method = SSLv23_client_method();
	} else {
		method = TLSv1_server_method();
	}
	
	ctx = SSL_CTX_new(method);
	ssl = 0;
	if (!ctx) {
			char msg[128] = { 0, };
#ifndef SAMURAI_OS_MACOSX
			ERR_error_string(ERR_get_error(), msg);
#endif
			QERR("SSL error: %s", msg);
			
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}
	
	SSL_CTX_set_verify(ctx, TlsFactory::allowUntrustedConnections() ? SSL_VERIFY_NONE : SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, 0);
	
	
	Samurai::IO::File* cert = TlsFactory::getCertificate();
	if (cert && cert->exists())
	{
		if (SSL_CTX_use_certificate_file(ctx, cert->getName().c_str(), SSL_FILETYPE_PEM) != 1)
		{
			QERR("Unable to load certificate");
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		}
	}
#ifdef SSL_REQUIRE_CERTIFICATE
	else
	{
			QERR("Certificate not found");
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}
#endif
	
	
	Samurai::IO::File* pkey = TlsFactory::getPrivateKey();
	if (pkey && pkey->exists())
	{
		if (SSL_CTX_use_PrivateKey_file(ctx, pkey->getName().c_str(), SSL_FILETYPE_PEM) != 1)
		{
			QERR("Unable to load private key");
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		}
	}
#ifdef SSL_REQUIRE_CERTIFICATE
	else
	{
			QERR("Private key found");
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}
#endif

	ssl = SSL_new(ctx);
	SSL_set_fd(ssl, sd);
	return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::deinitialize() {
	if (ssl) SSL_free(ssl);
	if (ctx) SSL_CTX_free(ctx);
	ssl = 0;
	ctx = 0;
	return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
}


// FIXME: Cleanup!!!
enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::sendHandshake() {
	// fcntl(sd, F_SETFL, O_RDWR);
	int ret = 0;
	
	if (mode == Samurai::IO::Net::TlsFactory::TLS_OPERATE_CLIENT)
		ret = SSL_connect(ssl);
	else
		ret = SSL_accept(ssl);
	
	// fcntl(sd, F_SETFL, O_RDWR | O_NONBLOCK);
	// return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
	int error = SSL_get_error(ssl, ret);

	if (ret == -1 && (error == SSL_ERROR_SYSCALL || error == SSL_ERROR_SSL)) {
		char msg[128] = { 0, };
#ifndef SAMURAI_OS_MACOSX
		ERR_error_string(ERR_get_error(), msg);
#endif
		QERR("SSL handshake error: '%s'\n", msg);

		return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}
	
	if (ret == -1) {
		switch (error) {
			case SSL_ERROR_NONE:
				QERR("Handshake ok: %s", "OpenSSL_strerror(ret)");
				return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
			case SSL_ERROR_WANT_READ:  /* hm? */
			case SSL_ERROR_WANT_WRITE: /* hm? */
			case SSL_ERROR_WANT_CONNECT:
			case SSL_ERROR_WANT_ACCEPT:
				// QERR("Handshake retry: %d", error);
				return Samurai::IO::Net::TlsFactory::TLS_STATUS_RETRY;
			default:
			{
				QERR("Handshake failed: %d", error);
				return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
			}
		}
	}
	
	return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::sendGoodbye() {
	
	// fcntl(sd, F_SETFL, O_RDWR);
	int ret = SSL_shutdown(ssl);
	// fcntl(sd, F_SETFL, O_RDWR | O_NONBLOCK);
	
	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE:
		case SSL_ERROR_WANT_CONNECT:/* hm? */
		case SSL_ERROR_WANT_ACCEPT: /* hm? */
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_RETRY;
		default:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}
	
}


ssize_t Samurai::IO::Net::OpenSSL::read(char* data, size_t length, enum Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	ssize_t ret = SSL_read(ssl, data, length);
	
	if (ret <= 0)
	{
		char msg[128] = { 0, };
#ifndef SAMURAI_OS_MACOSX
		ERR_error_string(ERR_get_error(), msg);
#endif
		QERR("SSL connect error: '%s'\n", msg);
	
		int error = SSL_get_error(ssl, ret);
		switch (error) {
			case SSL_ERROR_NONE:
				status = Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
				return 0;
			
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				status = Samurai::IO::Net::TlsFactory::TLS_STATUS_RETRY;
				return 0;
				
			default:
				status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
				return -1;
		}
	}
	
	return ret;
}

ssize_t Samurai::IO::Net::OpenSSL::peek(char*, size_t, enum Samurai::IO::Net::TlsFactory::TlsStatus&)
{
	// FIXME: Not implemented.
	return 0;
}

ssize_t Samurai::IO::Net::OpenSSL::write(const char* data, size_t length, enum Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	ssize_t ret = SSL_write(ssl, data, length);
	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
			return ret;
	
		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_RETRY;	
			return 0;
		
		default:
			status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
			return -1;
	}
}

#endif // SSL_SUPPORT && SSL_OPENSSL
