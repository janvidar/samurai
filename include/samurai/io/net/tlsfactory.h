/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_SOCKET_SSL_API_H
#define HAVE_QUICKDC_SOCKET_SSL_API_H

#include <samurai/io/net/socketglue.h>
#include <string>

namespace Samurai {
namespace IO {

class File;

namespace Net {

class TlsFactory {
	public:
		enum TlsOperation {
			TLS_OPERATE_CLIENT,	/**<< Operate as a TLS/SSL client */
			TLS_OPERATE_SERVER	/**<< Operate as a TLS/SSL server */
		};

		enum TlsStatus {
			TLS_STATUS_OK,
			TLS_STATUS_WANT_WRITE,
			TLS_STATUS_WANT_READ,
			TLS_STATUS_CLOSED,
			TLS_STATUS_ERROR
		};

		virtual ~TlsFactory() { }

		/**
		 * Set the name the peer is expected to present in its certificate,
		 * as used for hostname verification and SNI. This must be the name
		 * that was looked up - not an address obtained from the connection -
		 * or verification is meaningless.
		 *
		 * Call this before initialize(). Without it no hostname verification
		 * is possible, and a valid certificate for *any* host is accepted.
		 */
		void setPeerName(const std::string& name) { peer_name = name; }
		const std::string& getPeerName() const { return peer_name; }

		/**
		 * Initialize SSL contexts, etc.
		 */
		virtual enum TlsStatus initialize(enum TlsOperation mode, socket_t sd) = 0;
	
		/**
		 * Deinitialize SSL contexts, etc.
		 */
		virtual enum TlsStatus deinitialize() = 0;

		/**
		 * Send SSL handshake.
		 */
		virtual enum TlsStatus sendHandshake() = 0;
	
		/**
		 * Send SSL goodbye.
		 */
		virtual enum TlsStatus sendGoodbye() = 0;
		
		/**
		 * Perform a global initializeation of the TLS stack.
		 * No TLS operations will work before this is done.
		 */
		static bool global_init();
		
		/**
		 * Perform a global shutdown of the SSL stack.
		 * No TLS operations will work after this is done.
		 */
		static bool global_deinit();
		
		/**
		 * Specify a global certificate and private key.
		 */
		static void setKeys(const char* private_key, const char* public_key);
		
		static Samurai::IO::File* getPrivateKey();
		static Samurai::IO::File* getCertificate();
		
		static bool allowUntrustedConnections();
		static void setAllowUntrustedConnections(bool toggle);
		
		

	public:
		virtual ssize_t write(const char* data, size_t length, enum TlsStatus& status) = 0;
		virtual ssize_t read(char* data, size_t length, enum TlsStatus& status) = 0;
		virtual ssize_t peek(char* data, size_t length, enum TlsStatus& status) = 0;

	protected:
		socket_t sd;
		enum TlsOperation mode;
		std::string peer_name;

		static Samurai::IO::File* pem_key;
		static Samurai::IO::File* pem_cert;
		static bool allow_untrusted;
		
		static void priv_init();
		static void priv_fini();
		static void resetKeys();
		static void freeKeys();
};

}
}
}

#endif // HAVE_QUICKDC_SOCKET_SSL_API_H

