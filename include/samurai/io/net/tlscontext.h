/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_TLS_CONTEXT_H
#define HAVE_SAMURAI_TLS_CONTEXT_H

#include <samurai/io/net/tlsfactory.h>

#include <memory>
#include <optional>

namespace Samurai {
namespace IO {

class File;

namespace Net {

/**
 * The certificate and private key one owner presents, for every connection it
 * makes or accepts - so two owners in one process, such as two event loops, can
 * each be who they are rather than whoever last called TlsFactory::setKeys().
 *
 * Until setKeys() is called the process-wide keys stand in, and connections
 * share the process-wide contexts as they always did. Once it is, connections
 * made with this context build their own per role, and only from the thread the
 * owner runs on, which is what makes two loops on two threads safe.
 *
 * Trust anchors and the verification defaults stay process wide.
 */
class TlsContext {
	public:
		TlsContext();
		~TlsContext();

		TlsContext(const TlsContext&) = delete;
		TlsContext& operator=(const TlsContext&) = delete;

		/** Present these two files from now on; connections already made keep theirs. */
		void setKeys(const char* private_key, const char* certificate);

		/** Whether setKeys() was called, so this context does not fall back on the process's. */
		bool hasKeys() const { return pem_key != nullptr; }

		/* This context's own, or else the process's; borrowed, as TlsFactory's are. */
		Samurai::IO::File* getPrivateKey() const;
		Samurai::IO::File* getCertificate() const;

		/** The fingerprint of the certificate getCertificate() names. */
		std::optional<TlsFactory::Sha256Digest> getOwnCertificateSHA256() const;

	private:
		std::unique_ptr<Samurai::IO::File> pem_key;
		std::unique_ptr<Samurai::IO::File> pem_cert;

		/* The backend's contexts built from these keys, one per role. */
		struct Cache;
		std::unique_ptr<Cache> cache;
		unsigned long generation = 0;

		friend class OpenSSL;
};

}
}
}

#endif // HAVE_SAMURAI_TLS_CONTEXT_H
