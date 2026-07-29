/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKET_SSL_API_H
#define HAVE_SAMURAI_SOCKET_SSL_API_H

#include <samurai/io/net/socketglue.h>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace Samurai {
namespace IO {

class File;

namespace Net {

class TlsFactory {
	public:
		enum class TlsOperation {
			Client,	/**<< Operate as a TLS/SSL client */
			Server	/**<< Operate as a TLS/SSL server */
		};

		enum class TlsStatus {
			Ok,
			WantWrite,
			WantRead,
			Closed,
			Error
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

		/** Bytes in the digest the two calls below produce. */
		static constexpr size_t SHA256_LENGTH = 32;

		/** A SHA-256 certificate fingerprint. */
		using Sha256Digest = std::array<uint8_t, SHA256_LENGTH>;

		/**
		 * The SHA-256 digest of the certificate the peer presented, taken over
		 * its DER encoding - what X.509 tools call the certificate fingerprint.
		 *
		 * Only meaningful once the handshake has completed. False if it has
		 * not, if the peer presented no certificate, or if the buffer is
		 * smaller than SHA256_LENGTH.
		 */
		/**
		 * @return the peer certificate's fingerprint, or nothing if there is no
		 *         peer certificate. Returning the digest removes the buffer and
		 *         length the caller previously had to size correctly.
		 */
		virtual std::optional<Sha256Digest> getPeerCertificateSHA256() = 0;

		/**
		 * Initialize SSL contexts, etc.
		 */
		virtual TlsStatus initialize(TlsOperation mode, socket_t sd) = 0;
	
		/**
		 * Deinitialize SSL contexts, etc.
		 */
		virtual TlsStatus deinitialize() = 0;

		/**
		 * Send SSL handshake.
		 */
		virtual TlsStatus sendHandshake() = 0;
	
		/**
		 * Send SSL goodbye.
		 */
		virtual TlsStatus sendGoodbye() = 0;
		
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

		/**
		 * The same digest of the certificate we present ourselves, read from
		 * the file handed to setKeys(). False if there is no certificate, or
		 * it cannot be read.
		 */
		static std::optional<Sha256Digest> getOwnCertificateSHA256();

		static bool allowUntrustedConnections();
		static void setAllowUntrustedConnections(bool toggle);
		
		

	public:
		virtual ssize_t write(const char* data, size_t length, TlsStatus& status) = 0;
		virtual ssize_t read(char* data, size_t length, TlsStatus& status) = 0;
		virtual ssize_t peek(char* data, size_t length, TlsStatus& status) = 0;

		/**
		 * Decrypted application data already held by the TLS layer, waiting to
		 * be read.
		 *
		 * A read of a single TLS record pulls the whole record off the
		 * descriptor and decrypts it, so more application data can be sitting
		 * here than the caller asked for while the descriptor itself has
		 * nothing left to report. A readiness poll cannot see this, which is
		 * why SocketMonitor asks.
		 */
		virtual size_t pending() const = 0;

	protected:
		socket_t sd;
		TlsOperation mode;
		std::string peer_name;

		/* Owned here. getPrivateKey() and getCertificate() hand out a
		   borrowed pointer: they are accessors to this static state, not
		   factories, so they must not transfer ownership. */
		static std::unique_ptr<Samurai::IO::File> pem_key;
		static std::unique_ptr<Samurai::IO::File> pem_cert;
		static bool allow_untrusted;
		
		static void priv_init();
		static void priv_fini();
		static void resetKeys();
		static void freeKeys();
};

}
}
}

#endif // HAVE_SAMURAI_SOCKET_SSL_API_H

