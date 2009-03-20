/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#if defined(SSL_SUPPORT) && defined(SSL_OPENSSL)

#ifndef HAVE_SSL_SOCKET_OPENSSL_H___
#define HAVE_SSL_SOCKET_OPENSSL_H___

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory.h>
#include <openssl/ssl.h>

namespace Samurai {
namespace IO {
namespace Net {

class OpenSSL : public TlsFactory {
	public:
		OpenSSL();
		virtual ~OpenSSL();
	
		enum TlsStatus initialize(enum TlsOperation mode, socket_t sd);
		enum TlsStatus deinitialize();
		enum TlsStatus sendHandshake();
		enum TlsStatus sendGoodbye();
		
		ssize_t write(const char* data, size_t length, enum TlsStatus& status);
		ssize_t read(char* data, size_t length, enum TlsStatus& status);
		ssize_t peek(char* data, size_t length, enum TlsStatus& status);

	protected:
		SSL_CTX *ctx;
		SSL *ssl;
};

}
}
}

#endif // HAVE_SSL_SOCKET_OPENSSL_H___

#endif // SSL_SUPPORT && SSL_OPENSSL

