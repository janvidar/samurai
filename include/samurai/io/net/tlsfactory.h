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
#include <vector>

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

		/* Takes its verification settings from the process defaults, so a
		   connection that says nothing gets whatever the program asked for. */
		TlsFactory();
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

		/** The RSA modulus size of a generated key, in bits. */
		static constexpr unsigned SELF_SIGNED_KEY_BITS = 3072;

		/** How long a generated certificate is valid, in days. */
		static constexpr unsigned SELF_SIGNED_VALID_DAYS = 3650;

		/**
		 * Write a private key and a matching self-signed certificate, for a
		 * peer that has none and cannot get one signed by anybody.
		 *
		 * The certificate carries serverAuth and clientAuth, because the same
		 * one is presented at both ends of a connection - see setKeys().
		 *
		 * @param private_key_path where the PEM private key is written, created
		 *        with mode 0600. A key anyone can read is worse than no TLS.
		 *        Resolved the way File does it, so a leading '~' means the home
		 *        directory - the same two strings can be handed to setKeys().
		 * @param certificate_path where the PEM certificate is written, created
		 *        with mode 0644, being public by definition.
		 * @param common_name the name to assert, as the subject CN and as the
		 *        one subjectAltName: an iPAddress entry if it parses as an
		 *        address, a dNSName entry otherwise. Nothing verifies it - a
		 *        self-signed certificate says only what its holder chose to say
		 *        - so pass the address or name this peer is reached at when that
		 *        is known, and a stable label when it is not. Limited to 64
		 *        printable ASCII characters without spaces, which is what a CN
		 *        can hold; anything else is refused rather than truncated or
		 *        re-encoded into a name that would never match.
		 *
		 * Neither file is overwritten: an existing one fails the call, because
		 * replacing a key silently changes the keyprint every peer pinned. The
		 * caller decides what to do about that - normally generate only when
		 * getCertificate() reports nothing there.
		 *
		 * @return true if both files were written. On failure nothing is left
		 *         behind: a file this call created is removed again, and a file
		 *         it found is untouched. setKeys() is not called for you.
		 */
		static bool generateSelfSignedCertificate(const char* private_key_path,
			const char* certificate_path,
			const std::string& common_name);

		/**
		 * The same digest of the certificate we present ourselves, read from
		 * the file handed to setKeys(). False if there is no certificate, or
		 * it cannot be read.
		 */
		static std::optional<Sha256Digest> getOwnCertificateSHA256();

		/**
		 * Trust the certificates in a PEM file in addition to the system store.
		 *
		 * For a deployment whose peers are signed by a private authority the
		 * system store has never heard of - which is the only way to verify such
		 * a peer at all, short of turning verification off and getting a channel
		 * nothing authenticates.
		 *
		 * Process wide and additive, and read when a connection is initialized,
		 * so a call affects connections opened after it. Adding an anchor widens
		 * what every later connection accepts, so it is not the way to pin one
		 * peer - keyprint matching is.
		 */
		static void addTrustAnchor(const char* pem_file);

		/** Go back to the system store alone. */
		static void clearTrustAnchors();

		static const std::vector<std::string>& getTrustAnchors();

		/**
		 * Whether this connection accepts a peer whose certificate cannot be
		 * verified.
		 *
		 * False unless the process default says otherwise: a client checks the
		 * server's chain against the system trust store and its name against
		 * the certificate, and refuses the connection if either fails. Turning
		 * it on gives an unauthenticated channel, which is worth having only
		 * where the peer is authenticated some other way.
		 *
		 * Per connection, so reaching one peer that cannot be verified does not
		 * stop verifying any of the others. Read by initialize(); setting it
		 * afterwards changes nothing.
		 */
		bool allowUntrusted() const { return allow_untrusted_conn; }
		void setAllowUntrusted(bool toggle) { allow_untrusted_conn = toggle; }

		/**
		 * Whether this connection, as a server, asks its client for a
		 * certificate - mutual TLS.
		 *
		 * False unless the process default says otherwise. A server that
		 * demands one rejects every client that has none, which is most of
		 * them, so this is not implied by verifying connections.
		 */
		bool requireClientCertificate() const { return require_client_cert_conn; }
		void setRequireClientCertificate(bool toggle) { require_client_cert_conn = toggle; }

		/**
		 * The values a connection starts with. Changing a default affects
		 * connections created after the call, never one already under way.
		 */
		static bool defaultAllowUntrusted();
		static void setDefaultAllowUntrusted(bool toggle);
		static bool defaultRequireClientCertificate();
		static void setDefaultRequireClientCertificate(bool toggle);
		
		

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
		/* The process-wide defaults, and this connection's copy of them taken
		   at construction. */
		static bool allow_untrusted;
		static std::vector<std::string> trust_anchors;
		static bool require_client_cert;
		bool allow_untrusted_conn;
		bool require_client_cert_conn;
		
		/**
		 * Bumped whenever the keys or the trust anchors change.
		 *
		 * The backend builds one context per role and shares it across
		 * connections, rather than parsing the certificate and the private key
		 * again on every accept. That cache is only correct while what went into
		 * it still holds, so everything that changes it moves this on and the
		 * backend rebuilds. Connections already established are unaffected: they
		 * hold their own reference to the context they were made from.
		 *
		 * Not the per-connection toggles. Whether a given connection verifies
		 * its peer is decided on the connection itself, so those can differ
		 * between two connections sharing one context.
		 */
		static unsigned long configGeneration();

		static void priv_init();
		static void priv_fini();
		static void resetKeys();
		static void freeKeys();

	private:
		static unsigned long config_generation;
};

}
}
}

#endif // HAVE_SAMURAI_SOCKET_SSL_API_H

