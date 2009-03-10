/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#if defined(SSL_SUPPORT) && defined(SSL_GNUTLS)

#ifndef HAVE_SSL_SOCKET_GNUTLS_H
#define HAVE_SSL_SOCKET_GNUTLS_H

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory.h>
#include <gnutls/gnutls.h>

namespace Samurai {
namespace IO {
namespace Net {

class GnuTLS : public TlsFactory {
	public:
		GnuTLS();
		virtual ~GnuTLS();
	
		enum TlsStatus initialize(enum TlsOperation mode, socket_t sd);
		enum TlsStatus deinitialize();
		enum TlsStatus sendHandshake();
		enum TlsStatus sendGoodbye();
		
		ssize_t write(const char* data, size_t length, enum TlsStatus& status);
		ssize_t read(char* data, size_t length, enum TlsStatus& status);
		ssize_t peek(char* data, size_t length, enum TlsStatus& status);

	protected:
		gnutls_session_t tls_session;
		gnutls_certificate_credentials_t tls_xcred;
};

}
}
}

#endif // HAVE_SSL_SOCKET_GNUTLS_H

#endif // SSL && SSL_GNUTLS

