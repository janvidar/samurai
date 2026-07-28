/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/tlsfactory.h>
#include <samurai/io/net/tlsfactory-openssl.h>
#include <samurai/io/file.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/sha.h>
#include <openssl/err.h>

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <string.h>
#include <stdio.h>

/*
 * TLS is mandatory per the README and had no automated coverage at all: 720
 * lines across tlsfactory.cpp and tlsfactory-openssl.cpp, of which nothing ran.
 *
 * The key and certificate are generated in process rather than checked in.
 * That keeps a private key out of the repository, needs no 'openssl' command
 * line tool, and cannot expire out from under the suite.
 *
 * The handshake runs over a socketpair() rather than through Socket and
 * SocketMonitor: initialize() takes a descriptor, so the TLS layer can be
 * driven directly and the test does not depend on the event loop.
 */

using Tls = Samurai::IO::Net::TlsFactory;
using TlsStatus = Samurai::IO::Net::TlsFactory::TlsStatus;
using TlsOperation = Samurai::IO::Net::TlsFactory::TlsOperation;

namespace {

struct TlsFixture
{
	std::string key_path;
	std::string cert_path;
	bool ready = false;

	/* The PEM files have to outlive every case - setKeys() keeps File objects
	   pointing at them - so they are removed when the process exits. */
	~TlsFixture()
	{
		if (!key_path.empty()) unlink(key_path.c_str());
		if (!cert_path.empty()) unlink(cert_path.c_str());
	}

	TlsFixture() = default;
	TlsFixture(TlsFixture&&) = default;
	TlsFixture& operator=(TlsFixture&&) = default;
	TlsFixture(const TlsFixture&) = delete;
	TlsFixture& operator=(const TlsFixture&) = delete;
};

/* Written next to the generated test sources, which is a writable build dir. */
static std::string tls_temp_path(const char* leaf)
{
	return std::string("samurai-tls-test-") + leaf;
}

static bool tls_write_keypair(const std::string& key_path, const std::string& cert_path)
{
	EVP_PKEY* pkey = EVP_RSA_gen(2048);
	if (!pkey) return false;

	X509* cert = X509_new();
	if (!cert) { EVP_PKEY_free(pkey); return false; }

	ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
	X509_gmtime_adj(X509_getm_notBefore(cert), 0);
	/* Long enough that the suite never fails because a fixture aged out. */
	X509_gmtime_adj(X509_getm_notAfter(cert), 60L * 60 * 24 * 3650);
	X509_set_pubkey(cert, pkey);

	X509_NAME* name = X509_get_subject_name(cert);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
		(const unsigned char*) "localhost", -1, -1, 0);
	X509_set_issuer_name(cert, name);

	bool ok = X509_sign(cert, pkey, EVP_sha256()) > 0;

	if (ok)
	{
		FILE* kf = fopen(key_path.c_str(), "wb");
		ok = kf && PEM_write_PrivateKey(kf, pkey, nullptr, nullptr, 0, nullptr, nullptr);
		if (kf) fclose(kf);
	}

	if (ok)
	{
		FILE* cf = fopen(cert_path.c_str(), "wb");
		ok = cf && PEM_write_X509(cf, cert);
		if (cf) fclose(cf);
	}

	X509_free(cert);
	EVP_PKEY_free(pkey);
	return ok;
}

/* Built once: generating a 2048 bit key per case would dominate the run. */
static TlsFixture& tls_fixture()
{
	static TlsFixture fixture = []
	{
		TlsFixture f;
		f.key_path = tls_temp_path("key.pem");
		f.cert_path = tls_temp_path("cert.pem");

		if (!Tls::global_init()) return f;
		if (!tls_write_keypair(f.key_path, f.cert_path)) return f;

		Tls::setKeys(f.key_path.c_str(), f.cert_path.c_str());
		f.ready = true;
		return f;
	}();
	return fixture;
}

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

/* A connected, handshaken pair, or nothing. */
struct TlsSession
{
	SocketPair pair;
	Samurai::IO::Net::OpenSSL server;
	Samurai::IO::Net::OpenSSL client;
	bool ok = false;

	TlsSession()
	{
		if (!tls_fixture().ready || !pair.valid()) return;

		Tls::setAllowUntrustedConnections(true);
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
	return tls_fixture().ready;
});

EXO_TEST(tls_get_private_key_and_certificate,
{
	if (!tls_fixture().ready) return false;
	return Samurai::IO::Net::TlsFactory::getPrivateKey() != nullptr
		&& Samurai::IO::Net::TlsFactory::getCertificate() != nullptr;
});

/* Accessors, not factories: the same object must come back each time. */
EXO_TEST(tls_key_accessors_do_not_transfer_ownership,
{
	if (!tls_fixture().ready) return false;
	return Samurai::IO::Net::TlsFactory::getPrivateKey()
			== Samurai::IO::Net::TlsFactory::getPrivateKey()
		&& Samurai::IO::Net::TlsFactory::getCertificate()
			== Samurai::IO::Net::TlsFactory::getCertificate();
});

EXO_TEST(tls_allow_untrusted_toggles,
{
	const bool original = Samurai::IO::Net::TlsFactory::allowUntrustedConnections();

	Samurai::IO::Net::TlsFactory::setAllowUntrustedConnections(true);
	const bool on = Samurai::IO::Net::TlsFactory::allowUntrustedConnections();
	Samurai::IO::Net::TlsFactory::setAllowUntrustedConnections(false);
	const bool off = Samurai::IO::Net::TlsFactory::allowUntrustedConnections();

	Samurai::IO::Net::TlsFactory::setAllowUntrustedConnections(original);
	return on && !off;
});

EXO_TEST(tls_own_certificate_fingerprint_matches_der_sha256,
{
	if (!tls_fixture().ready) return false;

	Tls::Sha256Digest expected{};
	if (!tls_expected_fingerprint(tls_fixture().cert_path, expected)) return false;

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
	if (!tls_expected_fingerprint(tls_fixture().cert_path, expected)) return false;

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
