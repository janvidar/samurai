/*
 *
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory-openssl.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/file.h>

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * The build requires OpenSSL 3.0 or LibreSSL 3.5, both of which negotiate
 * TLS 1.3 whenever the peer offers it. A stack without it would silently top
 * out at the 1.2 floor set in initialize().
 */
#ifndef TLS1_3_VERSION
#error "TLS 1.3 support is required"
#endif

/*
 * OpenSSL 3.0 renamed this. LibreSSL still has only the old name.
 */
#ifdef LIBRESSL_VERSION_NUMBER
#define SSL_get1_peer_certificate SSL_get_peer_certificate
#endif

/*
 * ERR_error_string() takes no length and requires a buffer of at least 256
 * bytes, so it can only be used safely via the counted variant.
 */
#define SSL_ERRBUF_SIZE 256

static void ssl_error_string(char* buf, size_t buflen)
{
	unsigned long code = ERR_get_error();

	if (code == 0)
	{
		/* Not every failure puts something on the error queue. */
		strncpy(buf, "no error reported", buflen - 1);
		buf[buflen - 1] = 0;
		return;
	}

	ERR_error_string_n(code, buf, buflen);
	ERR_clear_error();
}

/*
 * X509_digest() hashes the DER encoding of the whole certificate, which is what
 * a certificate fingerprint is defined over.
 */
static std::optional<Samurai::IO::Net::TlsFactory::Sha256Digest> sha256_of(X509* cert)
{
	unsigned char buf[EVP_MAX_MD_SIZE];
	unsigned int size = 0;

	if (X509_digest(cert, EVP_sha256(), buf, &size) != 1)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to hash the certificate: %s", msg);
		return std::nullopt;
	}

	if (size != Samurai::IO::Net::TlsFactory::SHA256_LENGTH)
	{
		QERR("SHA-256 produced %u bytes rather than %u", size,
			(unsigned int) Samurai::IO::Net::TlsFactory::SHA256_LENGTH);
		return std::nullopt;
	}

	Samurai::IO::Net::TlsFactory::Sha256Digest digest;
	memcpy(digest.data(), buf, digest.size());
	return digest;
}

std::optional<Samurai::IO::Net::TlsFactory::Sha256Digest> Samurai::IO::Net::TlsFactory::getOwnCertificateSHA256()
{
	Samurai::IO::File* pem = TlsFactory::getCertificate();
	if (!pem || !pem->exists()) return std::nullopt;

	BIO* bio = BIO_new_file(pem->getName().c_str(), "r");
	if (!bio)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to open '%s': %s", pem->getName().c_str(), msg);
		return std::nullopt;
	}

	X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);

	if (!cert)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to read a certificate from '%s': %s", pem->getName().c_str(), msg);
		return std::nullopt;
	}

	auto found = sha256_of(cert);
	X509_free(cert);
	return found;
}

static bool ssl_failure(const char* what)
{
	char msg[SSL_ERRBUF_SIZE];
	ssl_error_string(msg, sizeof(msg));
	QERR("%s: %s", what, msg);
	return false;
}

/* The OpenSSL objects a generated certificate needs, freed on every path out. */
template <typename T, void (*Free)(T*)>
struct SslDeleter
{
	void operator()(T* p) const { Free(p); }
};

using PkeyPtr = std::unique_ptr<EVP_PKEY, SslDeleter<EVP_PKEY, EVP_PKEY_free>>;
using CertPtr = std::unique_ptr<X509, SslDeleter<X509, X509_free>>;
using BioPtr = std::unique_ptr<BIO, SslDeleter<BIO, BIO_free_all>>;
using BignumPtr = std::unique_ptr<BIGNUM, SslDeleter<BIGNUM, BN_free>>;
using ExtensionPtr = std::unique_ptr<X509_EXTENSION, SslDeleter<X509_EXTENSION, X509_EXTENSION_free>>;
using GeneralNamePtr = std::unique_ptr<GENERAL_NAME, SslDeleter<GENERAL_NAME, GENERAL_NAME_free>>;
using GeneralNamesPtr = std::unique_ptr<GENERAL_NAMES, SslDeleter<GENERAL_NAMES, GENERAL_NAMES_free>>;

/*
 * What a certificate name may hold: RFC 5280 caps a common name at 64
 * characters, and a name outside printable ASCII either cannot be encoded as
 * the IA5String a dNSName is, or would be re-encoded into something no peer
 * checking the name could ever match. Refused rather than truncated.
 */
static bool ssl_name_is_usable(const std::string& name)
{
	if (name.empty() || name.size() > 64) return false;

	for (const char c : name)
		if ((unsigned char) c <= 0x20 || (unsigned char) c >= 0x7f) return false;

	return true;
}

/*
 * A subjectAltName, without which nothing can verify the certificate names the
 * peer it was reached at: a bare common name has not been accepted for that in
 * a decade.
 *
 * Built through GENERAL_NAME rather than a configuration string so that a name
 * carrying a separator cannot add entries of its own.
 */
static bool ssl_add_subject_alt_name(X509* cert, const std::string& name)
{
	GeneralNamePtr entry(GENERAL_NAME_new());
	GeneralNamesPtr names(sk_GENERAL_NAME_new_null());
	if (!entry || !names) return false;

	/*
	 * An address literal is matched against an iPAddress entry and a name
	 * against dNSName, never the other way about. initialize() decides which
	 * of the two a peer name is by the same test, so an entry typed the other
	 * way could never match.
	 */
	ASN1_OCTET_STRING* address = a2i_IPADDRESS(name.c_str());
	if (address)
	{
		GENERAL_NAME_set0_value(entry.get(), GEN_IPADD, address);
	}
	else
	{
		ERR_clear_error(); /* a name that is not an address leaves an error queued */

		ASN1_IA5STRING* dns = ASN1_IA5STRING_new();
		if (!dns) return false;

		if (ASN1_STRING_set(dns, name.data(), (int) name.size()) != 1)
		{
			ASN1_IA5STRING_free(dns);
			return false;
		}
		GENERAL_NAME_set0_value(entry.get(), GEN_DNS, dns);
	}

	if (sk_GENERAL_NAME_push(names.get(), entry.get()) <= 0) return false;
	(void) entry.release(); /* the stack owns it now */

	return X509_add1_ext_i2d(cert, NID_subject_alt_name, names.get(), 0,
		X509V3_ADD_DEFAULT) == 1;
}

static bool ssl_add_extension(X509* cert, int nid, const char* value)
{
	ExtensionPtr ext(X509V3_EXT_conf_nid(nullptr, nullptr, nid, value));
	if (!ext) return false;
	return X509_add_ext(cert, ext.get(), -1) == 1;
}

/*
 * A serial has to be unpredictable rather than a counter: two peers generating
 * their own certificate would otherwise both call theirs number one.
 */
static bool ssl_set_random_serial(X509* cert)
{
	unsigned char bytes[16];
	if (RAND_bytes(bytes, (int) sizeof(bytes)) != 1) return false;

	/* A serial is a positive integer, so the sign bit must be clear. */
	bytes[0] &= 0x7f;

	BignumPtr serial(BN_bin2bn(bytes, (int) sizeof(bytes), nullptr));
	if (!serial) return false;

	return BN_to_ASN1_INTEGER(serial.get(), X509_get_serialNumber(cert)) != nullptr;
}

/*
 * Created with O_EXCL, so a file that is already there is never overwritten,
 * and given the mode asked for explicitly rather than left to the process
 * umask: a umask that widened the mode would leave a private key readable by
 * others, and one that narrowed it to nothing would leave a key we cannot read
 * back ourselves.
 */
static BIO* ssl_create_pem_file(const char* path, mode_t perms)
{
	const int fd = ::open(path, O_WRONLY | O_CREAT | O_EXCL, perms);
	if (fd == -1)
	{
		QERR("Unable to create '%s': %s", path, strerror(errno));
		return nullptr;
	}

	BIO* bio = nullptr;
	if (::fchmod(fd, perms) == 0)
	{
		/* BIO_CLOSE hands the descriptor over, so the BIO closes it. */
		bio = BIO_new_fd(fd, BIO_CLOSE);
		if (bio) return bio;
	}
	else
	{
		QERR("Unable to set the mode of '%s': %s", path, strerror(errno));
	}

	::close(fd);
	::unlink(path);
	return nullptr;
}

bool Samurai::IO::Net::TlsFactory::generateSelfSignedCertificate(
	const char* private_key_path,
	const char* certificate_path,
	const std::string& common_name)
{
	if (!private_key_path || !*private_key_path || !certificate_path || !*certificate_path)
	{
		QERR("Both a private key path and a certificate path are required");
		return false;
	}

	if (!ssl_name_is_usable(common_name))
	{
		QERR("'%s' cannot be a certificate name: 1 to 64 printable ASCII "
		     "characters, and no spaces", common_name.c_str());
		return false;
	}

	/*
	 * Resolved the same way File does it, because setKeys() is handed the same
	 * two strings and reads them through File: a caller passing "~/.quickdc/..."
	 * to both has to mean one pair of files, not a literal '~' directory here
	 * and a home directory there.
	 */
	const std::string key_path = Samurai::IO::File::resolvePath(private_key_path);
	const std::string cert_path = Samurai::IO::File::resolvePath(certificate_path);

	ERR_clear_error();

	PkeyPtr pkey(EVP_RSA_gen(SELF_SIGNED_KEY_BITS));
	if (!pkey) return ssl_failure("Unable to generate a private key");

	CertPtr cert(X509_new());
	if (!cert) return ssl_failure("Unable to create a certificate");

	/* Version 3, which is the one that can carry an extension at all. */
	if (X509_set_version(cert.get(), 2) != 1)
		return ssl_failure("Unable to set the certificate version");

	if (!ssl_set_random_serial(cert.get()))
		return ssl_failure("Unable to set the certificate serial");

	/*
	 * Backdated an hour, so a peer whose clock is behind ours does not see a
	 * certificate that is not valid yet, and valid for long enough that it does
	 * not expire under a user who installed once and never looked again. Expiry
	 * buys nothing here in any case: there is no authority to renew from and no
	 * revocation, and a peer that pinned the keyprint would have to be told
	 * about a new one.
	 */
	if (!X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60L * 60)
		|| !X509_gmtime_adj(X509_getm_notAfter(cert.get()),
			60L * 60 * 24 * SELF_SIGNED_VALID_DAYS))
		return ssl_failure("Unable to set the certificate validity");

	if (X509_set_pubkey(cert.get(), pkey.get()) != 1)
		return ssl_failure("Unable to set the certificate public key");

	X509_NAME* subject = X509_get_subject_name(cert.get());
	if (X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
			(const unsigned char*) common_name.c_str(), -1, -1, 0) != 1)
		return ssl_failure("Unable to set the certificate subject");

	/* Self-signed: the issuer is the subject. */
	if (X509_set_issuer_name(cert.get(), subject) != 1)
		return ssl_failure("Unable to set the certificate issuer");

	if (!ssl_add_subject_alt_name(cert.get(), common_name))
		return ssl_failure("Unable to add a subjectAltName");

	/*
	 * An end entity certificate, not an authority: it signs nothing but itself,
	 * and OpenSSL still accepts it as its own trust anchor for a peer that was
	 * handed it out of band.
	 */
	if (!ssl_add_extension(cert.get(), NID_basic_constraints, "critical,CA:FALSE"))
		return ssl_failure("Unable to add basicConstraints");

	/* digitalSignature covers TLS 1.3 and the ECDHE_RSA exchanges, and
	   keyEncipherment the TLS 1.2 exchange that encrypts to the key. */
	if (!ssl_add_extension(cert.get(), NID_key_usage,
			"critical,digitalSignature,keyEncipherment"))
		return ssl_failure("Unable to add keyUsage");

	/*
	 * Both purposes, because one certificate serves both roles here - setKeys()
	 * is process wide and initialize() presents the same certificate whether it
	 * was asked for a client or a server. A certificate carrying serverAuth
	 * alone is refused as a client certificate by a peer that verifies one.
	 */
	if (!ssl_add_extension(cert.get(), NID_ext_key_usage, "serverAuth,clientAuth"))
		return ssl_failure("Unable to add extendedKeyUsage");

	if (X509_sign(cert.get(), pkey.get(), EVP_sha256()) <= 0)
		return ssl_failure("Unable to sign the certificate");

	BioPtr key_file(ssl_create_pem_file(key_path.c_str(), 0600));
	if (!key_file) return false;

	if (PEM_write_bio_PrivateKey(key_file.get(), pkey.get(), nullptr, nullptr, 0,
			nullptr, nullptr) != 1
		|| BIO_flush(key_file.get()) != 1)
	{
		key_file.reset();
		::unlink(key_path.c_str());
		return ssl_failure("Unable to write the private key");
	}
	key_file.reset();

	BioPtr cert_file(ssl_create_pem_file(cert_path.c_str(), 0644));
	if (!cert_file)
	{
		::unlink(key_path.c_str());
		return false;
	}

	if (PEM_write_bio_X509(cert_file.get(), cert.get()) != 1
		|| BIO_flush(cert_file.get()) != 1)
	{
		cert_file.reset();
		::unlink(cert_path.c_str());
		::unlink(key_path.c_str());
		return ssl_failure("Unable to write the certificate");
	}

	return true;
}

/*
 * The contexts, one per role.
 *
 * A context used to be built for every connection, which meant parsing the
 * certificate and the private key off disk on every accept. That was affordable
 * while almost nothing used TLS; it is not now that peer transfers are encrypted
 * by default and every one of them pays for an RSA key parse before the
 * handshake starts. Everything in here is the same for every connection of a
 * role, so it is built once and shared.
 *
 * What is not shared is anything a caller can decide per connection - whether to
 * verify the peer, and which name to expect - so that stays on the SSL object,
 * where two connections from one context can disagree about it.
 *
 * Sharing is safe: OpenSSL refcounts a context and SSL_new() takes a reference,
 * so a connection outlives a rebuild that drops the cache's own reference. The
 * cache is not guarded by a lock, which is the contract the rest of this class
 * already has - setKeys() and the trust anchor list are unguarded statics too.
 */
namespace {

struct SharedContexts
{
	SSL_CTX* client = nullptr;
	SSL_CTX* server = nullptr;
	unsigned long generation = 0;
	bool primed = false;
};

SharedContexts shared;

void ssl_release_shared()
{
	if (shared.client) SSL_CTX_free(shared.client);
	if (shared.server) SSL_CTX_free(shared.server);
	shared.client = nullptr;
	shared.server = nullptr;
}

/*
 * Everything the broken ciphers have in common is that a peer offering them is
 * either ancient or lying, and TLS 1.2 is already the floor - so this bans them
 * rather than naming an allow-list, and puts the forward-secret exchanges first
 * so they are what gets chosen.
 *
 * It stops short of *requiring* forward secrecy. A peer whose only key exchange
 * is RSA still connects, because the alternative for a client that has to reach
 * a long tail of other implementations is a failed handshake the user reads as
 * "encryption does not work", and the way that gets resolved is by turning
 * encryption off. TLS 1.3 is left alone: every suite it has is acceptable.
 */
const char* SSL_CIPHER_POLICY =
	"ECDHE:DHE:HIGH:"
	"!aNULL:!eNULL:!EXPORT:!DES:!3DES:!RC4:!MD5:!PSK:!SRP:!SEED:!IDEA";

/* Longer than any real chain, and short enough to bound the work a peer can ask
   for by sending one that goes nowhere. */
#define SSL_VERIFY_DEPTH 9

SSL_CTX* ssl_build_context(bool client)
{
	SSL_CTX* ctx = SSL_CTX_new(client ? TLS_client_method() : TLS_server_method());
	if (!ctx)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to create an SSL context: %s", msg);
		return nullptr;
	}

	/*
	 * SSLv2/v3 and TLS 1.0/1.1 are obsolete and rejected by modern peers, so
	 * 1.2 is the floor. The ceiling is set explicitly rather than left at the
	 * default so that a system configuration file carrying MaxProtocol cannot
	 * quietly take TLS 1.3 away from us.
	 */
	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

	if (SSL_CTX_set_cipher_list(ctx, SSL_CIPHER_POLICY) != 1)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("No acceptable cipher remains under the policy: %s", msg);
		SSL_CTX_free(ctx);
		return nullptr;
	}

	/*
	 * Renegotiation has no use here and is where several protocol attacks live.
	 * Compression is the CRIME attack and is disabled in the library by default,
	 * said again so a build with it enabled does not inherit it.
	 */
	SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);

	/* Which cipher gets used is the server's decision to make, not the peer's. */
	if (!client) SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

	SSL_CTX_set_verify_depth(ctx, SSL_VERIFY_DEPTH);

	/*
	 * Off in the context, because whether a connection verifies is decided on
	 * the connection - see initialize(). The trust store is loaded either way:
	 * it costs nothing until something verifies against it, and building it here
	 * is the point of sharing the context at all.
	 */
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

	/* Without a trust store SSL_VERIFY_PEER can never succeed. */
	if (SSL_CTX_set_default_verify_paths(ctx) != 1)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to load the system certificate store: %s", msg);
		SSL_CTX_free(ctx);
		return nullptr;
	}

	/*
	 * And whatever else was named, for a peer signed by an authority the system
	 * store does not carry. A file that cannot be read is fatal rather than
	 * skipped: silently carrying on would verify against the system store alone
	 * and refuse every peer the caller added the anchor for, which reads as the
	 * peer being untrustworthy rather than as a path being wrong.
	 */
	for (const std::string& anchor : Samurai::IO::Net::TlsFactory::getTrustAnchors())
	{
		if (SSL_CTX_load_verify_locations(ctx, anchor.c_str(), nullptr) == 1)
			continue;

		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to load the trust anchor '%s': %s", anchor.c_str(), msg);
		SSL_CTX_free(ctx);
		return nullptr;
	}

	/*
	 * The chain variant rather than the single-certificate one: a certificate
	 * signed by an intermediate is useless to a peer that does not already hold
	 * that intermediate, and the file is read the same way either way.
	 */
	Samurai::IO::File* cert = Samurai::IO::Net::TlsFactory::getCertificate();
	Samurai::IO::File* pkey = Samurai::IO::Net::TlsFactory::getPrivateKey();

	if (cert && cert->exists())
	{
		if (SSL_CTX_use_certificate_chain_file(ctx, cert->getName().c_str()) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load certificate: %s", msg);
			SSL_CTX_free(ctx);
			return nullptr;
		}
	}

	if (pkey && pkey->exists())
	{
		if (SSL_CTX_use_PrivateKey_file(ctx, pkey->getName().c_str(),
				SSL_FILETYPE_PEM) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load private key: %s", msg);
			SSL_CTX_free(ctx);
			return nullptr;
		}
	}

	/*
	 * That the two belong together needs no separate check: loading a private key
	 * against a certificate already held refuses a mismatch, so a pair that does
	 * not match fails above rather than during a handshake.
	 */
	return ctx;
}

/*
 * The context for a role, built on first use and rebuilt when what went into it
 * has changed. A failed build is not cached, so a certificate that is put right
 * is picked up by the next connection rather than needing a restart.
 */
SSL_CTX* ssl_shared_context(bool client, unsigned long now)
{
	if (!shared.primed || shared.generation != now)
	{
		ssl_release_shared();
		shared.generation = now;
		shared.primed = true;
	}

	SSL_CTX*& slot = client ? shared.client : shared.server;
	if (!slot) slot = ssl_build_context(client);
	return slot;
}

/*
 * The transport OpenSSL is given for a connection.
 *
 * OpenSSL's own socket BIO writes with write(), which carries no flag to
 * suppress SIGPIPE, so a record written to a peer that has gone raises the
 * signal and kills the process. Nothing on the descriptor saves it where the
 * platform has no SO_NOSIGPIPE - which is Linux, where the flag on the call is
 * the only means there is. Every other write in the library goes out through
 * send() with send_flags for that reason; this is the same BIO OpenSSL would
 * have built, making the same call.
 *
 * The method is built on first use and not guarded by a lock, which is the
 * contract the shared context above already has.
 */
BIO_METHOD* bio_method = nullptr;

socket_t bio_descriptor(BIO* bio)
{
	return *static_cast<socket_t*>(BIO_get_data(bio));
}

/* A call cut short, or one that found no room, is one to make again - and is
   not the peer going away. */
bool bio_should_retry()
{
	const int error = Samurai::IO::Net::net_error();
	return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

int bio_read(BIO* bio, char* data, int length)
{
	if (!data || length <= 0) return 0;

	const ssize_t ret = ::recv(bio_descriptor(bio), data, (size_t) length, 0);

	BIO_clear_retry_flags(bio);
	if (ret < 0 && bio_should_retry()) BIO_set_retry_read(bio);

	return (int) ret;
}

int bio_write(BIO* bio, const char* data, int length)
{
	if (!data || length <= 0) return 0;

	const ssize_t ret = ::send(bio_descriptor(bio), data, (size_t) length,
	                           Samurai::IO::Net::send_flags);

	BIO_clear_retry_flags(bio);
	if (ret < 0 && bio_should_retry()) BIO_set_retry_write(bio);

	return (int) ret;
}

long bio_ctrl(BIO* bio, int cmd, [[maybe_unused]] long argument, void* pointer)
{
	switch (cmd)
	{
		case BIO_C_GET_FD:
		{
			if (!BIO_get_init(bio)) return -1;
			const socket_t sd = bio_descriptor(bio);
			if (pointer) *static_cast<int*>(pointer) = (int) sd;
			return (long) sd;
		}

		/* The descriptor belongs to the Socket, which closes it. A BIO that
		   closed it too would be closing whatever the descriptor named by the
		   time it got there. */
		case BIO_CTRL_GET_CLOSE:
			return BIO_NOCLOSE;

		/* Nothing is held back, so there is nothing a flush has to do. */
		case BIO_CTRL_FLUSH:
			return 1;

		default:
			return 0;
	}
}

BIO_METHOD* ssl_bio_method()
{
	if (bio_method) return bio_method;

	bio_method = BIO_meth_new(
		BIO_get_new_index() | BIO_TYPE_SOURCE_SINK | BIO_TYPE_DESCRIPTOR,
		"samurai socket");

	if (!bio_method) return nullptr;

	if (BIO_meth_set_read(bio_method, bio_read) != 1
		|| BIO_meth_set_write(bio_method, bio_write) != 1
		|| BIO_meth_set_ctrl(bio_method, bio_ctrl) != 1)
	{
		BIO_meth_free(bio_method);
		bio_method = nullptr;
	}

	return bio_method;
}

void ssl_release_bio_method()
{
	if (bio_method) BIO_meth_free(bio_method);
	bio_method = nullptr;
}

}

bool Samurai::IO::Net::TlsFactory::global_init()
{
	TlsFactory::priv_init();
	/* The library initializes itself on first use. */
	return true;
}

bool Samurai::IO::Net::TlsFactory::global_deinit()
{
	ssl_release_shared();
	ssl_release_bio_method();
	TlsFactory::priv_fini();
	return true;
}


Samurai::IO::Net::OpenSSL::OpenSSL()
{
	ssl = nullptr;
	verify_peer = false;
}

Samurai::IO::Net::OpenSSL::~OpenSSL()
{
	deinitialize();
}

Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::initialize(
		Samurai::IO::Net::TlsFactory::TlsOperation mode_,
		socket_t sd_)
{
	sd = sd_;
	mode = mode_;

	QDBG("Initializing OpenSSL with socket sd=%d", sd);

	const bool client = (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client);

	SSL_CTX* ctx = ssl_shared_context(client, configGeneration());
	if (!ctx) return Samurai::IO::Net::TlsFactory::TlsStatus::Error;

	ssl = SSL_new(ctx);
	if (!ssl)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to create SSL session: %s", msg);
		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	/*
	 * A client always wants the server's certificate. A server only wants the
	 * client's for mutual TLS, and demanding one otherwise rejects every client
	 * that has none - which is nearly all of them, and would make verifying
	 * connections unusable for a server.
	 *
	 * Set on the connection rather than on the context, because both toggles it
	 * reads are per connection: one peer that cannot be verified must not stop
	 * the next connection from verifying, and they share this context.
	 */
	verify_peer = !allowUntrusted() && (client || requireClientCertificate());

	if (verify_peer)
	{
		/* SSL_VERIFY_FAIL_IF_NO_PEER_CERT is a server-side flag; OpenSSL
		   ignores it for a client, which fails the handshake on a missing
		   certificate regardless. */
		int flags = SSL_VERIFY_PEER;
		if (!client) flags |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

		SSL_set_verify(ssl, flags, nullptr);
	}
	else
	{
		SSL_set_verify(ssl, SSL_VERIFY_NONE, nullptr);
	}

	/*
	 * A valid certificate chain proves nothing unless we also check that the
	 * certificate was issued for the host we asked for. Without this, any
	 * peer holding any CA-signed certificate can impersonate any server.
	 */
	if (client && !peer_name.empty())
	{
		X509_VERIFY_PARAM* param = SSL_get0_param(ssl);

		/* An address literal must be matched against an iPAddress SAN,
		   a name against dNSName/CN. set1_ip_asc() tells us which it is. */
		bool is_address = (X509_VERIFY_PARAM_set1_ip_asc(param, peer_name.c_str()) == 1);
		ERR_clear_error(); /* a non-address name leaves an error queued */

		if (!is_address)
		{
			X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
			if (X509_VERIFY_PARAM_set1_host(param, peer_name.c_str(), 0) != 1)
			{
				QERR("Unable to set expected peer name '%s'", peer_name.c_str());
				return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			}

			/* Server Name Indication - required by most virtual hosts.
			   Only names, never addresses, may be sent (RFC 6066). */
			SSL_set_tlsext_host_name(ssl, peer_name.c_str());
		}
	}
	else if (client && verify_peer)
	{
		QERR("No peer name set - the certificate name cannot be verified. "
		     "Call TlsFactory::setPeerName() before initialize().");
		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	/*
	 * SSL_set_fd() would give the connection OpenSSL's socket BIO, which cannot
	 * be told to suppress SIGPIPE - see ssl_bio_method(). set_nosigpipe() is
	 * said here as well, because a descriptor a caller made itself has not been
	 * through SocketBase and is otherwise unprotected on a platform where that
	 * is what does the suppressing.
	 */
	Samurai::IO::Net::set_nosigpipe(sd);

	BIO_METHOD* method = ssl_bio_method();
	BIO* bio = method ? BIO_new(method) : nullptr;

	if (!bio)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to create the transport for an SSL session: %s", msg);
		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	/* The descriptor is read out of this object on every call, so the BIO holds
	   the member rather than a copy of it. */
	BIO_set_data(bio, &sd);
	BIO_set_init(bio, 1);

	/* Both directions are the same descriptor, and SSL_set_bio() takes one
	   reference when it is handed the same BIO twice. */
	SSL_set_bio(ssl, bio, bio);
	return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
}

Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::deinitialize()
{
	/* Only the session. The context is shared, and SSL_free() drops the
	   reference SSL_new() took on it - the cache keeps its own. */
	if (ssl) SSL_free(ssl);
	ssl = nullptr;
	return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
}


Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::sendHandshake()
{
	if (!ssl) return Samurai::IO::Net::TlsFactory::TlsStatus::Error;

	ERR_clear_error();

	int ret;
	if (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client)
		ret = SSL_connect(ssl);
	else
		ret = SSL_accept(ssl);

	/*
	 * Only 1 means the handshake completed. 0 is a controlled failure and
	 * anything negative means either a retry or a hard error - none of which
	 * may be treated as success.
	 */
	if (ret != 1)
	{
		int error = SSL_get_error(ssl, ret);

		switch (error)
		{
			case SSL_ERROR_WANT_READ:
				return Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;

			case SSL_ERROR_WANT_WRITE:
			case SSL_ERROR_WANT_CONNECT:
			case SSL_ERROR_WANT_ACCEPT:
				return Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;

			case SSL_ERROR_ZERO_RETURN:
				return Samurai::IO::Net::TlsFactory::TlsStatus::Closed;

			default:
			{
				char msg[SSL_ERRBUF_SIZE];
				ssl_error_string(msg, sizeof(msg));
				QERR("SSL handshake failed (%d): %s", error, msg);
				return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			}
		}
	}

	QDBG("Handshake OK");

	/*
	 * With SSL_VERIFY_PEER a failed chain aborts the handshake above, so this
	 * is a second line of defence rather than the primary check. It is the
	 * only check when running untrusted, where it stays informational.
	 */
	long status = SSL_get_verify_result(ssl);
	if (status != X509_V_OK)
	{
		if (verify_peer)
		{
			QERR("Certificate verification failed: %s",
				X509_verify_cert_error_string(status));
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}
		QDBG("Certificate not verified (%s), continuing untrusted",
			X509_verify_cert_error_string(status));
	}

	X509* cert = SSL_get1_peer_certificate(ssl);
	if (cert)
	{
		X509_free(cert);
	}
	else if (verify_peer)
	{
		QERR("Peer presented no certificate");
		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
}

std::optional<Samurai::IO::Net::TlsFactory::Sha256Digest> Samurai::IO::Net::OpenSSL::getPeerCertificateSHA256()
{
	if (!ssl) return std::nullopt;

	X509* cert = SSL_get1_peer_certificate(ssl);
	if (!cert)
	{
		ERR_clear_error();
		return std::nullopt;
	}

	auto found = sha256_of(cert);
	X509_free(cert);
	return found;
}

Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::sendGoodbye() {

	if (!ssl) return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;

	ERR_clear_error();

	int ret = SSL_shutdown(ssl);

	/* 1 = peer replied, 0 = our notify is sent but the peer has not replied;
	   both mean we are done writing. */
	if (ret >= 0)
		return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
		case SSL_ERROR_WANT_READ:
			return Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;
		case SSL_ERROR_WANT_WRITE:
		case SSL_ERROR_WANT_CONNECT:
		case SSL_ERROR_WANT_ACCEPT:
			return Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;
		case SSL_ERROR_ZERO_RETURN:
			return Samurai::IO::Net::TlsFactory::TlsStatus::Closed;
		default:
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

}


/*
 * SSL_read()/SSL_write()/SSL_peek() take an int length, and SSL_get_error()
 * may only be consulted when the return value is <= 0.
 */
static int ssl_clamp_length(size_t length)
{
	if (length > (size_t) INT_MAX) return INT_MAX;
	return (int) length;
}

ssize_t Samurai::IO::Net::OpenSSL::read(char* data, size_t length, Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_read(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Closed;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL read error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			return -1;
		}
	}
}

ssize_t Samurai::IO::Net::OpenSSL::peek(char* data, size_t length, Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_peek(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Closed;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL peek error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			return -1;
		}
	}
}

size_t Samurai::IO::Net::OpenSSL::pending() const
{
	if (!ssl) return 0;

	int ret = SSL_pending(ssl);
	return (ret > 0) ? (size_t) ret : 0;
}

ssize_t Samurai::IO::Net::OpenSSL::write(const char* data, size_t length, Samurai::IO::Net::TlsFactory::TlsStatus& status)
{
	if (!ssl)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		return -1;
	}

	ERR_clear_error();

	int ret = SSL_write(ssl, data, ssl_clamp_length(length));

	if (ret > 0)
	{
		status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
		return ret;
	}

	int error = SSL_get_error(ssl, ret);
	switch (error) {
		case SSL_ERROR_NONE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
			return 0;

		case SSL_ERROR_WANT_READ:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantRead;
			return 0;

		case SSL_ERROR_WANT_WRITE:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::WantWrite;
			return 0;

		case SSL_ERROR_ZERO_RETURN:
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Closed;
			return 0;

		default:
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("SSL write error (%d): %s", error, msg);
			status = Samurai::IO::Net::TlsFactory::TlsStatus::Error;
			return -1;
		}
	}
}
