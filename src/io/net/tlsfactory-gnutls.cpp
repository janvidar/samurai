/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#if defined(SSL_SUPPORT) && defined(SSL_GNUTLS)

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory-gnutls.h>
#include <gnutls/gnutls.h>

#define CAFILE   "ca.pem"
#define CRLFILE  "crl.pem"
#define KEYFILE  "key.pem"
#define CERTFILE "cert.pem"
#define DH_BITS 1024

// FIXME: Return values?
bool Samurai::IO::Net::TlsFactory::global_init()
{
	TlsFactory::priv_init();
	gnutls_global_init();
	return true;
}

// FIXME: Return values?
bool Samurai::IO::Net::TlsFactory::global_deinit()
{
	gnutls_global_deinit();
	TlsFactory::priv_fini();
	return true;
}

Samurai::IO::Net::GnuTLS::GnuTLS()
{

}

Samurai::IO::Net::GnuTLS::~GnuTLS()
{

}

/*
static const int tls_kx_prio[] = { GNUTLS_KX_ANON_DH, 0 };
*/
enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::GnuTLS::initialize(
		enum Samurai::IO::Net::TlsFactory::TlsOperation mode,
		socket_t sd_
	)
{
	const static int tls_protocol_prio[] = {
		GNUTLS_TLS1,
		GNUTLS_SSL3,
		0
	};
	const static int tls_kx_prio[] = {
		GNUTLS_KX_RSA,
		GNUTLS_KX_DHE_DSS,
		GNUTLS_KX_DHE_RSA,
		GNUTLS_KX_SRP,
		// GNUTLS_KX_ANON_DH,    // FIXME: Anonymous DH -- probably not needed?
		// GNUTLS_KX_RSA_EXPORT,
		0
	};
	const static int tls_cipher_prio[] = {
		GNUTLS_CIPHER_3DES_CBC,
		GNUTLS_CIPHER_ARCFOUR_128,
		GNUTLS_CIPHER_AES_128_CBC,
		GNUTLS_CIPHER_AES_256_CBC,

		
/*
		GNUTLS_CIPHER_DHE_DSS_ARCFOUR_SHA1,
		DHE_DSS_3DES_EDE_CBC_SHA1,
		DHE_DSS_AES_128_CBC_SHA1,
		DHE_RSA_3DES_EDE_CBC_SHA1,
		DHE_RSA_AES_128_CBC_SHA1,
		RSA_EXPORT_ARCFOUR_40_MD5,
		RSA_ARCFOUR_SHA1,
		RSA_ARCFOUR_MD5,
		RSA_3DES_EDE_CBC_SHA1,
		RSA_AES_128_CBC_SHA1,
*/
		0
	};
	
	const static int tls_comp_prio[] = {
		GNUTLS_COMP_NULL,
		GNUTLS_COMP_ZLIB,
		0
	};

	const static int tls_mac_prio[] = {
		GNUTLS_MAC_MD5,
		GNUTLS_MAC_SHA,
		0
	};

	const static int tls_cert_type_prio[] = {
		GNUTLS_CRT_X509,
		GNUTLS_CRT_OPENPGP,
		0
	};

	sd = sd_;
	QDBG("Initializing GNUTLS for socket sd=%d (%s mode)", sd, mode == Samurai::IO::Net::TlsFactory::TLS_OPERATE_SERVER ? "server" : "client");

	if (mode == Samurai::IO::Net::TlsFactory::TLS_OPERATE_SERVER) {
		gnutls_init(&tls_session, GNUTLS_SERVER);
	
	} else {
		gnutls_init(&tls_session, GNUTLS_CLIENT);
	}

	gnutls_certificate_allocate_credentials(&tls_xcred);

	gnutls_certificate_set_x509_trust_file(tls_xcred,  CAFILE, GNUTLS_X509_FMT_PEM);
	gnutls_certificate_set_x509_crl_file(tls_xcred,   CRLFILE, GNUTLS_X509_FMT_PEM);
	gnutls_certificate_set_x509_key_file(tls_xcred,  CERTFILE, KEYFILE, GNUTLS_X509_FMT_PEM);

	
	/* Set defaults: FIXME - Perhaps hook to prefs? */
	gnutls_set_default_priority(tls_session);
	gnutls_protocol_set_priority(tls_session, tls_protocol_prio);
	gnutls_cipher_set_priority(tls_session, tls_cipher_prio);
	gnutls_compression_set_priority(tls_session, tls_comp_prio);
	gnutls_kx_set_priority(tls_session, tls_kx_prio);
	gnutls_mac_set_priority(tls_session, tls_mac_prio);
	gnutls_certificate_type_set_priority(tls_session, tls_cert_type_prio);

	/*
	FIXME: This *might* be useful
	gnutls_server_name_set(tls_session, GNUTLS_NAME_DNS, server_name, sizeof(server_name) - 1);
	*/

	gnutls_credentials_set(tls_session, GNUTLS_CRD_CERTIFICATE, tls_xcred);
	gnutls_dh_set_prime_bits(tls_session, DH_BITS);

	gnutls_transport_set_ptr(tls_session, (gnutls_transport_ptr_t) sd);

	return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::GnuTLS::deinitialize() {
	gnutls_deinit(tls_session);
	gnutls_certificate_free_credentials(tls_xcred);
	return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::GnuTLS::sendHandshake() {
	ssize_t ret = gnutls_handshake(tls_session);
	switch (ret) {
		case GNUTLS_E_SUCCESS:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
		case GNUTLS_E_INTERRUPTED:
		case GNUTLS_E_AGAIN:
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_RETRY;
		default:
		{
			QERR("Handshake failed (tls closed): %s", gnutls_strerror(ret));
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		}
	}

}

enum Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::GnuTLS::sendGoodbye() {
	fcntl(sd, F_SETFL, O_RDWR);
	switch (gnutls_bye(tls_session, GNUTLS_SHUT_RDWR)) {
		case GNUTLS_E_SUCCESS:
			fcntl(sd, F_SETFL, O_RDWR | O_NONBLOCK);
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_OK;
		case GNUTLS_E_INTERRUPTED:
		case GNUTLS_E_AGAIN:
			fcntl(sd, F_SETFL, O_RDWR | O_NONBLOCK);
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_RETRY;
		default:
			fcntl(sd, F_SETFL, O_RDWR | O_NONBLOCK);
			return Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
	}
	fcntl(sd, F_SETFL, O_RDWR | O_NONBLOCK);
}


ssize_t Samurai::IO::Net::GnuTLS::read(char* data, size_t length, enum Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	ssize_t ret = gnutls_record_recv(tls_session, data, length);
	if (ret == GNUTLS_E_INTERRUPTED || ret == GNUTLS_E_AGAIN) {
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_RETRY;
		return 0;
	} else if (ret < 0) {
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		return -1;
	}
	return ret;
}

ssize_t Samurai::IO::Net::GnuTLS::peek(char*, size_t, enum Samurai::IO::Net::TlsFactory::TlsStatus&)
{
	// FIXME: Not implemented.
	return 0;
}

ssize_t Samurai::IO::Net::GnuTLS::write(const char* data, size_t length, enum Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	ssize_t ret = gnutls_record_send(tls_session, data, length);
	if (ret == GNUTLS_E_INTERRUPTED || ret == GNUTLS_E_AGAIN) {
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_RETRY;
		return 0;
	} else if (ret < 0) {
		status = Samurai::IO::Net::TlsFactory::TLS_STATUS_ERROR;
		return -1;
	}
	return ret;
}

#endif // SSL && SSL_GNUTLS
