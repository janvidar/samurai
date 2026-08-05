/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/tlsfactory.h>
#include <samurai/io/net/tlsfactory-openssl.h>
#include <samurai/io/file.h>

#include "testkeys.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/sha.h>
#include <openssl/err.h>

#include <openssl/x509v3.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * TLS is mandatory per the README and had no automated coverage at all: 720
 * lines across tlsfactory.cpp and tlsfactory-openssl.cpp, of which nothing ran.
 *
 * The key and certificate come from testkeys.h, which generates them in
 * process and hands the same pair to sockets.tcc.
 *
 * The handshake runs over a socketpair() rather than through Socket and
 * SocketMonitor: initialize() takes a descriptor, so the TLS layer can be
 * driven directly and the test does not depend on the event loop. What Socket
 * does with the factory is covered in sockets.tcc, where the loop is.
 */

using Tls = Samurai::IO::Net::TlsFactory;
using TlsStatus = Samurai::IO::Net::TlsFactory::TlsStatus;
using TlsOperation = Samurai::IO::Net::TlsFactory::TlsOperation;

namespace {

/* The fingerprint as an X.509 tool reports it, computed independently of the
   library so the two are not the same code checked against itself. */
static bool tls_expected_fingerprint(const std::string& cert_path, Tls::Sha256Digest& out)
{
	FILE* f = fopen(cert_path.c_str(), "rb");
	if (!f) return false;

	X509* cert = PEM_read_X509(f, nullptr, nullptr, nullptr);
	fclose(f);
	if (!cert) return false;

	unsigned char* der = nullptr;
	const int len = i2d_X509(cert, &der);
	bool ok = len > 0;
	if (ok) SHA256(der, (size_t) len, out.data());

	OPENSSL_free(der);
	X509_free(cert);
	return ok;
}

struct SocketPair
{
	int fd[2] = { -1, -1 };

	SocketPair()
	{
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) { fd[0] = fd[1] = -1; return; }
		/* Non-blocking, so an incomplete handshake reports WantRead instead of
		   deadlocking both ends against each other. */
		for (int n = 0; n < 2; n++)
			fcntl(fd[n], F_SETFL, fcntl(fd[n], F_GETFL, 0) | O_NONBLOCK);
	}

	~SocketPair()
	{
		for (int n = 0; n < 2; n++)
			if (fd[n] != -1) close(fd[n]);
	}

	bool valid() const { return fd[0] != -1 && fd[1] != -1; }

	SocketPair(const SocketPair&) = delete;
	SocketPair& operator=(const SocketPair&) = delete;
};

static bool tls_status_is_retry(TlsStatus s)
{
	return s == TlsStatus::WantRead || s == TlsStatus::WantWrite;
}

/*
 * Drive both ends to a completed handshake. Neither can finish alone, so they
 * are stepped alternately until both report Ok.
 */
static bool tls_handshake(Samurai::IO::Net::OpenSSL& server, Samurai::IO::Net::OpenSSL& client)
{
	bool server_done = false;
	bool client_done = false;

	for (int spins = 0; spins < 200 && !(server_done && client_done); spins++)
	{
		if (!client_done)
		{
			const TlsStatus s = client.sendHandshake();
			if (s == TlsStatus::Ok) client_done = true;
			else if (!tls_status_is_retry(s)) return false;
		}

		if (!server_done)
		{
			const TlsStatus s = server.sendHandshake();
			if (s == TlsStatus::Ok) server_done = true;
			else if (!tls_status_is_retry(s)) return false;
		}
	}

	return server_done && client_done;
}

/*
 * A connected, handshaken pair, or nothing.
 *
 * The fixture certificate is self-signed and in no trust store, so both ends opt
 * out of verification - per connection, so nothing here changes what any other
 * case sees. What these exercise is the transport, not the validation; the cases
 * that do exercise validation build their own pair below.
 */
struct TlsSession
{
	SocketPair pair;
	Samurai::IO::Net::OpenSSL server;
	Samurai::IO::Net::OpenSSL client;
	bool ok = false;

	TlsSession()
	{
		if (!tls_test_keys().ready || !pair.valid()) return;

		server.setAllowUntrusted(true);
		client.setAllowUntrusted(true);

		if (server.initialize(TlsOperation::Server, pair.fd[0]) != TlsStatus::Ok) return;
		if (client.initialize(TlsOperation::Client, pair.fd[1]) != TlsStatus::Ok) return;

		ok = tls_handshake(server, client);
	}
};

/* Write every byte, retrying while the layer asks for more room. */
static bool tls_write_all(Samurai::IO::Net::OpenSSL& tls, const std::string& data)
{
	size_t sent = 0;
	for (int spins = 0; spins < 200 && sent < data.size(); spins++)
	{
		TlsStatus status = TlsStatus::Ok;
		const ssize_t n = tls.write(data.data() + sent, data.size() - sent, status);
		if (n > 0) { sent += (size_t) n; continue; }
		if (!tls_status_is_retry(status)) return false;
	}
	return sent == data.size();
}

static bool tls_read_exactly(Samurai::IO::Net::OpenSSL& tls, size_t want, std::string& out)
{
	out.clear();
	std::vector<char> buf(want);

	for (int spins = 0; spins < 200 && out.size() < want; spins++)
	{
		TlsStatus status = TlsStatus::Ok;
		const ssize_t n = tls.read(buf.data(), want - out.size(), status);
		if (n > 0) { out.append(buf.data(), (size_t) n); continue; }
		if (!tls_status_is_retry(status)) return false;
	}
	return out.size() == want;
}

}

/* ------------------------------------------------------------------------- */
/* Global state and key material                                             */
/* ------------------------------------------------------------------------- */

EXO_TEST(tls_global_init_and_fixture,
{
	return tls_test_keys().ready;
});

EXO_TEST(tls_get_private_key_and_certificate,
{
	if (!tls_test_keys().ready) return false;
	return Samurai::IO::Net::TlsFactory::getPrivateKey() != nullptr
		&& Samurai::IO::Net::TlsFactory::getCertificate() != nullptr;
});

/* Accessors, not factories: the same object must come back each time. */
EXO_TEST(tls_key_accessors_do_not_transfer_ownership,
{
	if (!tls_test_keys().ready) return false;
	return Samurai::IO::Net::TlsFactory::getPrivateKey()
			== Samurai::IO::Net::TlsFactory::getPrivateKey()
		&& Samurai::IO::Net::TlsFactory::getCertificate()
			== Samurai::IO::Net::TlsFactory::getCertificate();
});

EXO_TEST(tls_default_allow_untrusted_toggles,
{
	const bool original = Samurai::IO::Net::TlsFactory::defaultAllowUntrusted();

	Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(true);
	const bool on = Samurai::IO::Net::TlsFactory::defaultAllowUntrusted();
	Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(false);
	const bool off = Samurai::IO::Net::TlsFactory::defaultAllowUntrusted();

	Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(original);
	return on && !off;
});

EXO_TEST(tls_own_certificate_fingerprint_matches_der_sha256,
{
	if (!tls_test_keys().ready) return false;

	Tls::Sha256Digest expected{};
	if (!tls_expected_fingerprint(tls_test_keys().cert_path, expected)) return false;

	const auto got = Samurai::IO::Net::TlsFactory::getOwnCertificateSHA256();
	return got.has_value() && *got == expected;
});

EXO_TEST(tls_peer_name_round_trips,
{
	Samurai::IO::Net::OpenSSL tls;
	tls.setPeerName("example.invalid");
	return tls.getPeerName() == "example.invalid";
});

/* Before a handshake there is no peer certificate to report. */
EXO_TEST(tls_peer_fingerprint_absent_before_handshake,
{
	Samurai::IO::Net::OpenSSL tls;
	return !tls.getPeerCertificateSHA256().has_value();
});

EXO_TEST(tls_handshake_on_uninitialized_is_an_error,
{
	Samurai::IO::Net::OpenSSL tls;
	return tls.sendHandshake() == TlsStatus::Error;
});

/* ------------------------------------------------------------------------- */
/* A real handshake over a socket pair                                       */
/* ------------------------------------------------------------------------- */

EXO_TEST(tls_handshake_completes,
{
	TlsSession session;
	return session.ok;
});

EXO_TEST(tls_peer_certificate_is_the_one_we_serve,
{
	TlsSession session;
	if (!session.ok) return false;

	Tls::Sha256Digest expected{};
	if (!tls_expected_fingerprint(tls_test_keys().cert_path, expected)) return false;

	/* The client sees the server's certificate, which is the fixture. */
	const auto seen = session.client.getPeerCertificateSHA256();
	return seen.has_value() && *seen == expected;
});

EXO_TEST(tls_client_to_server_round_trip,
{
	TlsSession session;
	if (!session.ok) return false;

	const std::string message = "hello over TLS";
	if (!tls_write_all(session.client, message)) return false;

	std::string received;
	if (!tls_read_exactly(session.server, message.size(), received)) return false;

	return received == message;
});

EXO_TEST(tls_server_to_client_round_trip,
{
	TlsSession session;
	if (!session.ok) return false;

	const std::string message = "and back again";
	if (!tls_write_all(session.server, message)) return false;

	std::string received;
	if (!tls_read_exactly(session.client, message.size(), received)) return false;

	return received == message;
});

EXO_TEST(tls_binary_payload_survives,
{
	TlsSession session;
	if (!session.ok) return false;

	std::string message;
	for (int n = 0; n < 512; n++) message.push_back((char) (n & 0xff));

	if (!tls_write_all(session.client, message)) return false;

	std::string received;
	if (!tls_read_exactly(session.server, message.size(), received)) return false;

	return received == message;
});

/*
 * A record is pulled off the descriptor and decrypted whole, so application
 * data can be waiting in the TLS layer while the descriptor itself has nothing
 * to report. This is what SocketMonitor consults pending() for.
 */
EXO_TEST(tls_pending_reports_buffered_plaintext,
{
	TlsSession session;
	if (!session.ok) return false;

	const std::string message = "0123456789";
	if (!tls_write_all(session.client, message)) return false;

	/* Take one byte, which forces the whole record to be read and decrypted. */
	std::string first;
	if (!tls_read_exactly(session.server, 1, first)) return false;

	return session.server.pending() == message.size() - 1;
});

EXO_TEST(tls_pending_is_zero_before_any_data,
{
	TlsSession session;
	if (!session.ok) return false;
	return session.server.pending() == 0;
});

/* peek() must not consume: the same bytes have to read back afterwards. */
EXO_TEST(tls_peek_does_not_consume,
{
	TlsSession session;
	if (!session.ok) return false;

	const std::string message = "peekable";
	if (!tls_write_all(session.client, message)) return false;

	char peeked[16] = { 0 };
	TlsStatus status = TlsStatus::Ok;
	ssize_t n = -1;
	for (int spins = 0; spins < 200 && n <= 0; spins++)
		n = session.server.peek(peeked, message.size(), status);

	if (n != (ssize_t) message.size()) return false;
	if (memcmp(peeked, message.data(), message.size()) != 0) return false;

	std::string received;
	if (!tls_read_exactly(session.server, message.size(), received)) return false;
	return received == message;
});

EXO_TEST(tls_goodbye_and_deinitialize,
{
	TlsSession session;
	if (!session.ok) return false;

	const TlsStatus bye = session.client.sendGoodbye();
	if (bye == TlsStatus::Error) return false;

	return session.client.deinitialize() == TlsStatus::Ok
		&& session.server.deinitialize() == TlsStatus::Ok;
});

/* Repeated sessions must not leak or corrupt the shared context. */
EXO_TEST(tls_sessions_are_independent,
{
	for (int n = 0; n < 3; n++)
	{
		TlsSession session;
		if (!session.ok) return false;

		const std::string message = "session " + std::to_string(n);
		if (!tls_write_all(session.client, message)) return false;

		std::string received;
		if (!tls_read_exactly(session.server, message.size(), received)) return false;
		if (received != message) return false;
	}
	return true;
});

/* ------------------------------------------------------------------------- */
/* Verification is the default                                               */
/*                                                                           */
/* A client checks the server's chain against the system trust store and its */
/* name against the certificate, and refuses the connection when either      */
/* fails. The fixture certificate is self-signed and in no trust store, so it */
/* is exactly what must be turned away.                                      */
/* ------------------------------------------------------------------------- */

EXO_TEST(tls_verification_is_on_by_default,
{
	/* Not merely the accessor: this is what a connection gets when nobody has
	   said anything about verification. */
	Samurai::IO::Net::OpenSSL fresh;
	return Samurai::IO::Net::TlsFactory::defaultAllowUntrusted() == false
		&& fresh.allowUntrusted() == false;
});

EXO_TEST(tls_client_certificates_are_not_required_by_default,
{
	Samurai::IO::Net::OpenSSL fresh;
	return Samurai::IO::Net::TlsFactory::defaultRequireClientCertificate() == false
		&& fresh.requireClientCertificate() == false;
});

EXO_TEST(tls_default_require_client_certificate_toggles,
{
	const bool original = Samurai::IO::Net::TlsFactory::defaultRequireClientCertificate();

	Samurai::IO::Net::TlsFactory::setDefaultRequireClientCertificate(true);
	const bool on = Samurai::IO::Net::TlsFactory::defaultRequireClientCertificate();
	Samurai::IO::Net::TlsFactory::setDefaultRequireClientCertificate(false);
	const bool off = Samurai::IO::Net::TlsFactory::defaultRequireClientCertificate();

	Samurai::IO::Net::TlsFactory::setDefaultRequireClientCertificate(original);
	return on && !off;
});

/* Fail closed: with nothing to check the certificate name against, a verifying
   client refuses to start rather than connect unauthenticated. */
EXO_TEST(tls_verifying_client_without_a_peer_name_refuses_to_initialize,
{
	if (!tls_test_keys().ready) return false;

	SocketPair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::OpenSSL client;
	return client.initialize(TlsOperation::Client, pair.fd[1]) != TlsStatus::Ok;
});

/* The point of C1: a self-signed peer is turned away rather than accepted. */
EXO_TEST(tls_self_signed_certificate_is_rejected_when_verifying,
{
	if (!tls_test_keys().ready) return false;

	SocketPair pair;
	if (!pair.valid()) return false;

	Samurai::IO::Net::OpenSSL server;
	server.setAllowUntrusted(true);

	Samurai::IO::Net::OpenSSL client;
	client.setPeerName("localhost");

	if (server.initialize(TlsOperation::Server, pair.fd[0]) != TlsStatus::Ok) return false;
	if (client.initialize(TlsOperation::Client, pair.fd[1]) != TlsStatus::Ok) return false;

	/* The chain cannot be built, so the handshake must not complete. */
	return !tls_handshake(server, client);
});

/*
 * The other half of the fix: verifying connections must stay usable for a
 * server. Demanding a client certificate would reject every client that has
 * none, so a server does not ask for one unless mutual TLS is turned on.
 */
EXO_TEST(tls_verifying_server_does_not_demand_a_client_certificate,
{
	if (!tls_test_keys().ready) return false;

	SocketPair pair;
	if (!pair.valid()) return false;

	/* Server left at the verifying default. */
	Samurai::IO::Net::OpenSSL server;

	/* Client that does not check the self-signed certificate, so the only thing
	   that can fail the handshake is the server asking for something. */
	Samurai::IO::Net::OpenSSL client;
	client.setAllowUntrusted(true);

	if (server.initialize(TlsOperation::Server, pair.fd[0]) != TlsStatus::Ok) return false;
	if (client.initialize(TlsOperation::Client, pair.fd[1]) != TlsStatus::Ok) return false;

	return tls_handshake(server, client);
});

/* ------------------------------------------------------------------------- */
/* The settings are per connection                                           */
/*                                                                           */
/* Reaching one peer that cannot be verified must not stop verifying any of   */
/* the others, which a process-wide toggle could not express.                */
/* ------------------------------------------------------------------------- */

EXO_TEST(tls_a_connection_starts_from_the_process_default,
{
	Samurai::IO::Net::OpenSSL fresh;
	return fresh.allowUntrusted() == Tls::defaultAllowUntrusted()
		&& fresh.requireClientCertificate() == Tls::defaultRequireClientCertificate();
});

EXO_TEST(tls_a_connection_setting_does_not_touch_the_default,
{
	const bool before = Tls::defaultAllowUntrusted();

	Samurai::IO::Net::OpenSSL relaxed;
	relaxed.setAllowUntrusted(true);

	Samurai::IO::Net::OpenSSL strict;

	return Tls::defaultAllowUntrusted() == before
		&& relaxed.allowUntrusted()
		&& !strict.allowUntrusted();
});

/*
 * Two live sessions in one process, one verifying and one not, each behaving
 * the way it was told to. With the setting held process-wide the first one to
 * opt out decided the outcome for the second.
 */
EXO_TEST(tls_one_untrusted_connection_leaves_the_others_verifying,
{
	if (!tls_test_keys().ready) return false;

	SocketPair relaxed_pair;
	SocketPair strict_pair;
	if (!relaxed_pair.valid() || !strict_pair.valid()) return false;

	Samurai::IO::Net::OpenSSL relaxed_server;
	Samurai::IO::Net::OpenSSL relaxed_client;
	relaxed_server.setAllowUntrusted(true);
	relaxed_client.setAllowUntrusted(true);

	Samurai::IO::Net::OpenSSL strict_server;
	Samurai::IO::Net::OpenSSL strict_client;
	strict_server.setAllowUntrusted(true);
	strict_client.setPeerName("localhost");

	if (relaxed_server.initialize(TlsOperation::Server, relaxed_pair.fd[0]) != TlsStatus::Ok) return false;
	if (relaxed_client.initialize(TlsOperation::Client, relaxed_pair.fd[1]) != TlsStatus::Ok) return false;
	if (strict_server.initialize(TlsOperation::Server, strict_pair.fd[0]) != TlsStatus::Ok) return false;
	if (strict_client.initialize(TlsOperation::Client, strict_pair.fd[1]) != TlsStatus::Ok) return false;

	/* The one that opted out completes; the one that did not still refuses the
	   same self-signed certificate. */
	return tls_handshake(relaxed_server, relaxed_client)
		&& !tls_handshake(strict_server, strict_client);
});

/* ------------------------------------------------------------------------- */
/* Generating the certificate we present                                     */
/*                                                                           */
/* A peer answering over TLS needs a certificate to present, and nothing ever */
/* created one: on a fresh installation getCertificate() named a file that    */
/* was not there, so a server could not be initialized at all.                */
/* ------------------------------------------------------------------------- */

namespace {

/* A generated pair, removed again however the case ends. */
struct GeneratedKeys
{
	std::string key_path;
	std::string cert_path;
	bool ok = false;

	GeneratedKeys(const std::string& leaf, const std::string& name)
		: key_path(tls_temp_path((leaf + "-key.pem").c_str()))
		, cert_path(tls_temp_path((leaf + "-cert.pem").c_str()))
	{
		/* Neither file is overwritten by design, so a leftover from a case that
		   died would otherwise fail every run after it. */
		::unlink(key_path.c_str());
		::unlink(cert_path.c_str());

		ok = Tls::generateSelfSignedCertificate(key_path.c_str(), cert_path.c_str(), name);
	}

	~GeneratedKeys()
	{
		::unlink(key_path.c_str());
		::unlink(cert_path.c_str());
	}

	GeneratedKeys(const GeneratedKeys&) = delete;
	GeneratedKeys& operator=(const GeneratedKeys&) = delete;
};

/*
 * The process-wide key material, put back however the case ends: setKeys() is
 * static state that every other case in the suite reads, and these cases have
 * to point it at a generated pair to serve one.
 */
struct BorrowedProcessKeys
{
	std::string key_path;
	std::string cert_path;
	bool saved = false;

	BorrowedProcessKeys()
	{
		Samurai::IO::File* key = Tls::getPrivateKey();
		Samurai::IO::File* cert = Tls::getCertificate();
		if (!key || !cert) return;

		key_path = key->getName();
		cert_path = cert->getName();
		saved = true;
	}

	~BorrowedProcessKeys()
	{
		if (saved) Tls::setKeys(key_path.c_str(), cert_path.c_str());
	}

	void use(const GeneratedKeys& keys) const
	{
		Tls::setKeys(keys.key_path.c_str(), keys.cert_path.c_str());
	}

	BorrowedProcessKeys(const BorrowedProcessKeys&) = delete;
	BorrowedProcessKeys& operator=(const BorrowedProcessKeys&) = delete;
};

/* The trust anchors, likewise process wide and likewise put back. */
struct BorrowedTrustAnchors
{
	std::vector<std::string> saved;

	BorrowedTrustAnchors() : saved(Tls::getTrustAnchors()) { }

	~BorrowedTrustAnchors()
	{
		Tls::clearTrustAnchors();
		for (const std::string& anchor : saved)
			Tls::addTrustAnchor(anchor.c_str());
	}

	void only(const std::string& pem) const
	{
		Tls::clearTrustAnchors();
		Tls::addTrustAnchor(pem.c_str());
	}

	BorrowedTrustAnchors(const BorrowedTrustAnchors&) = delete;
	BorrowedTrustAnchors& operator=(const BorrowedTrustAnchors&) = delete;
};

static bool file_mode(const std::string& path, mode_t& out)
{
	struct stat st;
	if (::stat(path.c_str(), &st) != 0) return false;
	out = st.st_mode & 07777;
	return true;
}

static bool file_contents(const std::string& path, std::string& out)
{
	out.clear();

	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;

	char buf[512];
	size_t got;
	while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);

	fclose(f);
	return true;
}

/* Read back with the library's own PEM readers: what is on disk has to be what
   it claims to be, not merely something the generator was happy with. */
static X509* read_certificate(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return nullptr;

	X509* cert = PEM_read_X509(f, nullptr, nullptr, nullptr);
	fclose(f);
	return cert;
}

static EVP_PKEY* read_private_key(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return nullptr;

	EVP_PKEY* key = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
	fclose(f);
	return key;
}

/* The subjectAltName entries, as GEN_* types paired with their bytes. */
struct SubjectAltName
{
	std::vector<std::pair<int, std::string>> entries;

	explicit SubjectAltName(X509* cert)
	{
		GENERAL_NAMES* names = (GENERAL_NAMES*)
			X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr);
		if (!names) return;

		for (int n = 0; n < sk_GENERAL_NAME_num(names); n++)
		{
			const GENERAL_NAME* entry = sk_GENERAL_NAME_value(names, n);
			const ASN1_STRING* value = nullptr;

			if (entry->type == GEN_DNS) value = entry->d.dNSName;
			else if (entry->type == GEN_IPADD) value = entry->d.iPAddress;

			std::string bytes;
			if (value)
				bytes.assign((const char*) ASN1_STRING_get0_data(value),
					(size_t) ASN1_STRING_length(value));

			entries.emplace_back(entry->type, bytes);
		}

		GENERAL_NAMES_free(names);
	}
};

/*
 * A handshake between two ends that were configured by the caller, for the
 * cases that do exercise verification rather than opt out of it.
 */
static bool verified_handshake(Samurai::IO::Net::OpenSSL& server,
	Samurai::IO::Net::OpenSSL& client, const SocketPair& pair)
{
	if (server.initialize(TlsOperation::Server, pair.fd[0]) != TlsStatus::Ok) return false;
	if (client.initialize(TlsOperation::Client, pair.fd[1]) != TlsStatus::Ok) return false;
	return tls_handshake(server, client);
}

}

EXO_TEST(tls_selfsigned_writes_a_key_and_a_certificate,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("generated", "quickdc-peer");
	EXO_ASSERT(keys.ok);

	EXO_ASSERT(Samurai::IO::File::exists(keys.key_path.c_str()));
	EXO_ASSERT(Samurai::IO::File::exists(keys.cert_path.c_str()));

	EVP_PKEY* key = read_private_key(keys.key_path);
	EXO_ASSERT_NOT_NULL(key);

	X509* cert = read_certificate(keys.cert_path);
	if (!cert) { EVP_PKEY_free(key); EXO_FAIL("the certificate does not parse"); }

	const int base = EVP_PKEY_base_id(key);
	const int bits = EVP_PKEY_bits(key);
	const int matches = X509_check_private_key(cert, key);

	X509_free(cert);
	EVP_PKEY_free(key);

	EXO_ASSERT_EQ_INT(base, EVP_PKEY_RSA);
	EXO_ASSERT_EQ_INT(bits, (int) Tls::SELF_SIGNED_KEY_BITS);
	EXO_ASSERT(bits >= 2048);

	/* And they are each other's, not two unrelated files. */
	EXO_ASSERT_EQ_INT(matches, 1);
	return true;
});

/*
 * A private key anyone on the machine can read is worse than no TLS at all: it
 * looks encrypted and authenticates nobody. The mode is set explicitly rather
 * than left to the umask, so this is exact rather than an upper bound.
 */
EXO_TEST(tls_selfsigned_private_key_is_readable_only_by_its_owner,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("modes", "quickdc-peer");
	EXO_ASSERT(keys.ok);

	mode_t key_mode = 0;
	mode_t cert_mode = 0;
	EXO_ASSERT(file_mode(keys.key_path, key_mode));
	EXO_ASSERT(file_mode(keys.cert_path, cert_mode));

	EXO_ASSERT_EQ_UINT(key_mode, 0600u);

	/* Nothing outside the owner, whatever the umask was. */
	EXO_ASSERT_EQ_UINT(key_mode & 0077u, 0u);

	/* The certificate is public, and is not held to the same rule. */
	EXO_ASSERT_EQ_UINT(cert_mode, 0644u);
	return true;
});

/*
 * The name is the one thing a self-signed certificate cannot prove, since it
 * says only what its holder chose to say - in DC and ADC a peer is pinned by
 * keyprint instead. It still has to carry a subjectAltName: a bare common name
 * has not been accepted for name verification in a decade, so a certificate
 * without one cannot be verified by a peer that tries, no matter what anchor
 * it was handed. What the entry is for is that case, and its type has to match
 * how the name is looked at.
 */
EXO_TEST(tls_selfsigned_address_becomes_an_ip_subject_alt_name,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("san-ip", "127.0.0.1");
	EXO_ASSERT(keys.ok);

	X509* cert = read_certificate(keys.cert_path);
	EXO_ASSERT_NOT_NULL(cert);

	const SubjectAltName san(cert);
	X509_free(cert);

	EXO_ASSERT_EQ_UINT(san.entries.size(), 1u);
	EXO_ASSERT_EQ_INT(san.entries[0].first, GEN_IPADD);

	/* Four octets, not the text - which is what an address is matched as. */
	const std::string expected("\x7f\x00\x00\x01", 4);
	EXO_ASSERT_EQ_UINT(san.entries[0].second.size(), 4u);
	EXO_ASSERT(san.entries[0].second == expected);
	return true;
});

EXO_TEST(tls_selfsigned_name_becomes_a_dns_subject_alt_name,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("san-dns", "peer.example.invalid");
	EXO_ASSERT(keys.ok);

	X509* cert = read_certificate(keys.cert_path);
	EXO_ASSERT_NOT_NULL(cert);

	const SubjectAltName san(cert);

	char subject[256] = { 0 };
	X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName,
		subject, (int) sizeof(subject));

	X509_free(cert);

	EXO_ASSERT_EQ_UINT(san.entries.size(), 1u);
	EXO_ASSERT_EQ_INT(san.entries[0].first, GEN_DNS);
	EXO_ASSERT(san.entries[0].second == "peer.example.invalid");

	/* The common name says the same thing, for anything that still reads it. */
	EXO_ASSERT_STR_EQ(subject, "peer.example.invalid");
	return true;
});

/*
 * One certificate serves both roles: setKeys() is process wide, and
 * initialize() presents the same file whether it was asked for a client or a
 * server. A certificate carrying serverAuth alone would be refused as a client
 * certificate by a peer that verifies one.
 */
EXO_TEST(tls_selfsigned_certificate_is_fit_for_both_roles,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("purpose", "127.0.0.1");
	EXO_ASSERT(keys.ok);

	X509* cert = read_certificate(keys.cert_path);
	EXO_ASSERT_NOT_NULL(cert);

	const int as_server = X509_check_purpose(cert, X509_PURPOSE_SSL_SERVER, 0);
	const int as_client = X509_check_purpose(cert, X509_PURPOSE_SSL_CLIENT, 0);
	const int as_ca = X509_check_ca(cert);

	X509_free(cert);

	EXO_ASSERT_EQ_INT(as_server, 1);
	EXO_ASSERT_EQ_INT(as_client, 1);

	/* An end entity certificate: it signs nothing but itself. */
	EXO_ASSERT_EQ_INT(as_ca, 0);
	return true;
});

/*
 * Valid from before now, so a peer whose clock is behind does not see a
 * certificate that is not valid yet, and for long enough that it does not
 * expire under a user who installed once and never looked again.
 */
EXO_TEST(tls_selfsigned_validity_starts_in_the_past_and_runs_for_years,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("validity", "127.0.0.1");
	EXO_ASSERT(keys.ok);

	X509* cert = read_certificate(keys.cert_path);
	EXO_ASSERT_NOT_NULL(cert);

	time_t now = time(nullptr);
	time_t nearly_ten_years = now + 60L * 60 * 24 * (Tls::SELF_SIGNED_VALID_DAYS - 1);

	const int started = X509_cmp_time(X509_get0_notBefore(cert), &now);
	const int ends_after = X509_cmp_time(X509_get0_notAfter(cert), &nearly_ten_years);

	X509_free(cert);

	EXO_ASSERT_EQ_INT(started, -1);
	EXO_ASSERT_EQ_INT(ends_after, 1);
	return true;
});

/*
 * The keyprint is what a peer pins, so it has to be reported for a generated
 * pair the same way it is for any other.
 */
EXO_TEST(tls_selfsigned_keyprint_is_the_certificate_fingerprint,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("keyprint", "127.0.0.1");
	EXO_ASSERT(keys.ok);

	Tls::Sha256Digest expected{};
	EXO_ASSERT(tls_expected_fingerprint(keys.cert_path, expected));

	BorrowedProcessKeys process;
	EXO_ASSERT(process.saved);
	process.use(keys);

	const auto reported = Tls::getOwnCertificateSHA256();
	EXO_ASSERT(reported.has_value());
	EXO_ASSERT_MEM_EQ(reported->data(), expected.data(), Tls::SHA256_LENGTH);
	return true;
});

/*
 * A predictable certificate would defeat keyprint pinning entirely: every peer
 * would present the digest every other peer presents, and pinning one would
 * accept all of them. Two generated pairs must share nothing.
 */
EXO_TEST(tls_selfsigned_pairs_have_different_keyprints,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys first("distinct-one", "127.0.0.1");
	GeneratedKeys second("distinct-two", "127.0.0.1");
	EXO_ASSERT(first.ok && second.ok);

	Tls::Sha256Digest one{};
	Tls::Sha256Digest two{};
	EXO_ASSERT(tls_expected_fingerprint(first.cert_path, one));
	EXO_ASSERT(tls_expected_fingerprint(second.cert_path, two));

	EXO_ASSERT(one != two);

	/* And it is the key that differs, not only the serial number. */
	std::string first_key;
	std::string second_key;
	EXO_ASSERT(file_contents(first.key_path, first_key));
	EXO_ASSERT(file_contents(second.key_path, second_key));
	EXO_ASSERT(!first_key.empty() && first_key != second_key);
	return true;
});

/*
 * The case that matters: a certificate that parses but cannot serve a
 * connection is no use to anybody. This is the fresh-installation path end to
 * end - generate, present, and complete a handshake with it.
 */
EXO_TEST(tls_selfsigned_certificate_completes_a_handshake,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("handshake", "127.0.0.1");
	EXO_ASSERT(keys.ok);

	Tls::Sha256Digest expected{};
	EXO_ASSERT(tls_expected_fingerprint(keys.cert_path, expected));

	BorrowedProcessKeys process;
	EXO_ASSERT(process.saved);
	process.use(keys);

	TlsSession session;
	EXO_ASSERT(session.ok);

	/* The certificate the client was served is the one just generated. */
	const auto seen = session.client.getPeerCertificateSHA256();
	EXO_ASSERT(seen.has_value());
	EXO_ASSERT_MEM_EQ(seen->data(), expected.data(), Tls::SHA256_LENGTH);

	const std::string message = "carried over a generated certificate";
	EXO_ASSERT(tls_write_all(session.client, message));

	std::string received;
	EXO_ASSERT(tls_read_exactly(session.server, message.size(), received));
	EXO_ASSERT(received == message);
	return true;
});

/*
 * And a handshake that verifies rather than opting out, which is what the
 * subjectAltName is there for: the client checks the address it connected to
 * against the certificate, with the certificate itself as the anchor - the only
 * thing that can vouch for it.
 */
EXO_TEST(tls_selfsigned_certificate_verifies_as_its_own_anchor,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("verified", "127.0.0.1");
	EXO_ASSERT(keys.ok);

	SocketPair pair;
	EXO_ASSERT(pair.valid());

	BorrowedProcessKeys process;
	EXO_ASSERT(process.saved);
	process.use(keys);

	BorrowedTrustAnchors anchors;
	anchors.only(keys.cert_path);

	Samurai::IO::Net::OpenSSL server;

	/* Nothing relaxed on the client: chain and name both checked. */
	Samurai::IO::Net::OpenSSL client;
	client.setPeerName("127.0.0.1");

	EXO_ASSERT(verified_handshake(server, client, pair));
	return true;
});

/* The anchor is what did it, not verification having been skipped. */
EXO_TEST(tls_selfsigned_certificate_is_refused_without_the_anchor,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("unanchored", "127.0.0.1");
	EXO_ASSERT(keys.ok);

	SocketPair pair;
	EXO_ASSERT(pair.valid());

	BorrowedProcessKeys process;
	EXO_ASSERT(process.saved);
	process.use(keys);

	BorrowedTrustAnchors anchors;
	Tls::clearTrustAnchors();

	Samurai::IO::Net::OpenSSL server;
	Samurai::IO::Net::OpenSSL client;
	client.setPeerName("127.0.0.1");

	EXO_ASSERT(!verified_handshake(server, client, pair));
	return true;
});

/*
 * Both ends of one connection, from the one certificate: the server demands a
 * client certificate and verifies it, which is what a certificate carrying
 * serverAuth alone would fail.
 */
EXO_TEST(tls_selfsigned_certificate_serves_a_mutually_verified_handshake,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	GeneratedKeys keys("mutual", "127.0.0.1");
	EXO_ASSERT(keys.ok);

	SocketPair pair;
	EXO_ASSERT(pair.valid());

	BorrowedProcessKeys process;
	EXO_ASSERT(process.saved);
	process.use(keys);

	BorrowedTrustAnchors anchors;
	anchors.only(keys.cert_path);

	Samurai::IO::Net::OpenSSL server;
	server.setRequireClientCertificate(true);

	Samurai::IO::Net::OpenSSL client;
	client.setPeerName("127.0.0.1");

	EXO_ASSERT(verified_handshake(server, client, pair));
	return true;
});

/* ------------------------------------------------------------------------- */
/* Failing cleanly                                                           */
/* ------------------------------------------------------------------------- */

/*
 * Replacing a key silently would change the keyprint every peer pinned, so an
 * existing file fails the call rather than being overwritten. The caller
 * decides what to do about it.
 */
EXO_TEST(tls_selfsigned_refuses_to_replace_an_existing_certificate,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	const std::string key_path = tls_temp_path("existing-cert-key.pem");
	const std::string cert_path = tls_temp_path("existing-cert.pem");
	::unlink(key_path.c_str());

	const std::string original = "not a certificate\n";
	FILE* f = fopen(cert_path.c_str(), "wb");
	EXO_ASSERT_NOT_NULL(f);
	fwrite(original.data(), 1, original.size(), f);
	fclose(f);

	const bool generated = Tls::generateSelfSignedCertificate(
		key_path.c_str(), cert_path.c_str(), "127.0.0.1");

	std::string after;
	const bool read_back = file_contents(cert_path, after);
	const bool key_written = Samurai::IO::File::exists(key_path.c_str());

	::unlink(key_path.c_str());
	::unlink(cert_path.c_str());

	EXO_ASSERT(!generated);

	/* Untouched, and no key left beside it that no certificate matches. */
	EXO_ASSERT(read_back && after == original);
	EXO_ASSERT(!key_written);
	return true;
});

EXO_TEST(tls_selfsigned_refuses_to_replace_an_existing_private_key,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	const std::string key_path = tls_temp_path("existing-key.pem");
	const std::string cert_path = tls_temp_path("existing-key-cert.pem");
	::unlink(cert_path.c_str());

	const std::string original = "not a private key\n";
	FILE* f = fopen(key_path.c_str(), "wb");
	EXO_ASSERT_NOT_NULL(f);
	fwrite(original.data(), 1, original.size(), f);
	fclose(f);

	const bool generated = Tls::generateSelfSignedCertificate(
		key_path.c_str(), cert_path.c_str(), "127.0.0.1");

	std::string after;
	const bool read_back = file_contents(key_path, after);
	const bool cert_written = Samurai::IO::File::exists(cert_path.c_str());

	::unlink(key_path.c_str());
	::unlink(cert_path.c_str());

	EXO_ASSERT(!generated);
	EXO_ASSERT(read_back && after == original);
	EXO_ASSERT(!cert_written);
	return true;
});

/*
 * Half a pair is no better than none: a key written and a certificate that
 * could not be must leave nothing behind, or the next run finds the key there
 * and refuses to generate at all.
 */
EXO_TEST(tls_selfsigned_leaves_no_key_behind_when_the_certificate_cannot_be_written,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	const std::string key_path = tls_temp_path("orphan-key.pem");
	const std::string cert_path = tls_temp_path("absent-directory/cert.pem");
	::unlink(key_path.c_str());

	const bool generated = Tls::generateSelfSignedCertificate(
		key_path.c_str(), cert_path.c_str(), "127.0.0.1");

	const bool key_left = Samurai::IO::File::exists(key_path.c_str());
	::unlink(key_path.c_str());

	EXO_ASSERT(!generated);
	EXO_ASSERT(!key_left);
	return true;
});

/* A directory that cannot be written to fails the call rather than the process. */
EXO_TEST(tls_selfsigned_fails_in_an_unwritable_directory,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");
	if (geteuid() == 0) EXO_SKIP("running as root, which ignores the directory mode");

	const std::string directory = tls_temp_path("unwritable");
	Samurai::IO::File::rmdir(directory.c_str());
	if (Samurai::IO::File::mkdir(directory.c_str(), 0500) != 0)
		EXO_SKIP("the working directory is not writable");

	const std::string key_path = directory + "/key.pem";
	const std::string cert_path = directory + "/cert.pem";

	const bool generated = Tls::generateSelfSignedCertificate(
		key_path.c_str(), cert_path.c_str(), "127.0.0.1");

	const bool key_left = Samurai::IO::File::exists(key_path.c_str());
	const bool cert_left = Samurai::IO::File::exists(cert_path.c_str());

	Samurai::IO::File::rmdir(directory.c_str());

	EXO_ASSERT(!generated);
	EXO_ASSERT(!key_left && !cert_left);
	return true;
});

/*
 * A name that cannot be encoded as the certificate would have to carry it is
 * refused, rather than truncated or re-encoded into something no peer checking
 * the name could match. Nothing is written for any of them, and no key is
 * generated - the name is checked before that work is done.
 */
EXO_TEST(tls_selfsigned_refuses_a_name_it_cannot_carry,
{
	if (!tls_test_keys().ready) EXO_SKIP("the working directory is not writable");

	const std::string key_path = tls_temp_path("badname-key.pem");
	const std::string cert_path = tls_temp_path("badname-cert.pem");
	::unlink(key_path.c_str());
	::unlink(cert_path.c_str());

	const std::string names[] = {
		"",                              /* nothing to assert at all */
		std::string(65, 'a'),            /* a common name holds 64 characters */
		"has a space",
		std::string("nul\0inside", 10),
		"control\tcharacter",
		"non-ascii-\xc3\xa6",
	};

	bool refused = true;
	for (const std::string& name : names)
	{
		if (Tls::generateSelfSignedCertificate(key_path.c_str(), cert_path.c_str(), name))
			refused = false;
	}

	const bool key_left = Samurai::IO::File::exists(key_path.c_str());
	const bool cert_left = Samurai::IO::File::exists(cert_path.c_str());

	::unlink(key_path.c_str());
	::unlink(cert_path.c_str());

	EXO_ASSERT(refused);
	EXO_ASSERT(!key_left && !cert_left);
	return true;
});

/* Neither path is optional, and a null one must not be dereferenced. */
EXO_TEST(tls_selfsigned_refuses_a_missing_path,
{
	const std::string key_path = tls_temp_path("nopath-key.pem");
	const std::string cert_path = tls_temp_path("nopath-cert.pem");

	EXO_ASSERT(!Tls::generateSelfSignedCertificate(nullptr, cert_path.c_str(), "127.0.0.1"));
	EXO_ASSERT(!Tls::generateSelfSignedCertificate(key_path.c_str(), nullptr, "127.0.0.1"));
	EXO_ASSERT(!Tls::generateSelfSignedCertificate("", cert_path.c_str(), "127.0.0.1"));
	EXO_ASSERT(!Tls::generateSelfSignedCertificate(key_path.c_str(), "", "127.0.0.1"));

	EXO_ASSERT(!Samurai::IO::File::exists(key_path.c_str()));
	EXO_ASSERT(!Samurai::IO::File::exists(cert_path.c_str()));
	return true;
});

/*
 * The paths are resolved the way File resolves them, because setKeys() reads the
 * same strings through File: '~' meaning the home directory in one and a literal
 * directory named '~' in the other would put the key somewhere the process then
 * could not find.
 */
EXO_TEST(tls_generate_resolves_a_path_the_way_setkeys_does,
{
	const char* home = getenv("HOME");
	if (!home || !*home) EXO_SKIP("no HOME to resolve against");

	const std::string leaf = "samurai_tls_tilde_" + std::to_string((int) getpid());
	const std::string key = "~/" + leaf + ".key";
	const std::string crt = "~/" + leaf + ".crt";
	const std::string real_key = std::string(home) + "/" + leaf + ".key";
	const std::string real_crt = std::string(home) + "/" + leaf + ".crt";

	EXO_ASSERT(Samurai::IO::Net::TlsFactory::generateSelfSignedCertificate(
		key.c_str(), crt.c_str(), "127.0.0.1"));

	/* Written where File would look, and not into a directory called '~'. */
	const bool key_there = Samurai::IO::File(real_key).exists();
	const bool crt_there = Samurai::IO::File(real_crt).exists();
	const bool literal = Samurai::IO::File("./~/" + leaf + ".key").exists();

	::unlink(real_key.c_str());
	::unlink(real_crt.c_str());

	EXO_ASSERT(key_there);
	EXO_ASSERT(crt_there);
	EXO_ASSERT(!literal);
	return 1;
});
