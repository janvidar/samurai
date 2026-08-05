/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SSL_SOCKET_OPENSSL_H___
#define HAVE_SSL_SOCKET_OPENSSL_H___

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory.h>
#include <openssl/ssl.h>

namespace Samurai {
namespace IO {
namespace Net {

class OpenSSL final : public TlsFactory {
	public:
		OpenSSL();
		~OpenSSL() override;

		/* Releases raw pointers in its destructor, so the implicit copy
		 * operations would release them a second time. */
		OpenSSL(const OpenSSL&) = delete;
		OpenSSL& operator=(const OpenSSL&) = delete;
	
		TlsStatus initialize(TlsOperation mode, socket_t sd) override;
		TlsStatus deinitialize() override;
		TlsStatus sendHandshake() override;
		TlsStatus sendGoodbye() override;

		std::optional<Sha256Digest> getPeerCertificateSHA256() override;

		ssize_t write(const char* data, size_t length, TlsStatus& status) override;
		ssize_t read(char* data, size_t length, TlsStatus& status) override;
		ssize_t peek(char* data, size_t length, TlsStatus& status) override;

		size_t pending() const override;

	protected:
		/* The context is shared between connections of a role and is not owned
		   here - see ssl_shared_context(). Only the session is. */
		SSL *ssl;

		/* Decided in initialize() and read again after the handshake, rather
		   than asked of the static twice: a toggle flipped in between would
		   otherwise leave the two halves disagreeing about what was required. */
		bool verify_peer;
};

}
}
}

#endif // HAVE_SSL_SOCKET_OPENSSL_H___

