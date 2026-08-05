/*
 *
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/tlsfactory-openssl.h>
#include <samurai/io/file.h>

#include <openssl/ssl.h>
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

bool Samurai::IO::Net::TlsFactory::global_init()
{
	TlsFactory::priv_init();
	/* The library initializes itself on first use. */
	return true;
}

bool Samurai::IO::Net::TlsFactory::global_deinit()
{
	TlsFactory::priv_fini();
	return true;
}


Samurai::IO::Net::OpenSSL::OpenSSL()
{
	ssl = nullptr;
	ctx = nullptr;
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

	/* The method accessors return a const pointer. */
	const SSL_METHOD* method;

	if (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client)
		method = TLS_client_method();
	else
		method = TLS_server_method();

	ctx = SSL_CTX_new(method);
	ssl = nullptr;
	if (!ctx)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("SSL error: %s", msg);

		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	/*
	 * SSLv2/v3 and TLS 1.0/1.1 are obsolete and rejected by modern peers, so
	 * 1.2 is the floor. The ceiling is set explicitly rather than left at the
	 * default so that a system configuration file carrying MaxProtocol cannot
	 * quietly take TLS 1.3 away from us.
	 */
	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

	const bool client = (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client);

	/*
	 * A client always wants the server's certificate. A server only wants the
	 * client's for mutual TLS, and demanding one otherwise rejects every client
	 * that has none - which is nearly all of them, and would make verifying
	 * connections unusable for a server.
	 */
	verify_peer = !allowUntrusted() && (client || requireClientCertificate());

	if (verify_peer)
	{
		/* SSL_VERIFY_FAIL_IF_NO_PEER_CERT is a server-side flag; OpenSSL
		   ignores it for a client, which fails the handshake on a missing
		   certificate regardless. */
		int flags = SSL_VERIFY_PEER;
		if (!client) flags |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

		SSL_CTX_set_verify(ctx, flags, nullptr);

		/* Without a trust store SSL_VERIFY_PEER can never succeed. */
		if (SSL_CTX_set_default_verify_paths(ctx) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load the system certificate store: %s", msg);
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}

		/*
		 * And whatever else was named, for a peer signed by an authority the
		 * system store does not carry. A file that cannot be read is fatal rather
		 * than skipped: silently carrying on would verify against the system
		 * store alone and refuse every peer the caller added the anchor for,
		 * which reads as the peer being untrustworthy rather than as a path being
		 * wrong.
		 */
		for (const std::string& anchor : getTrustAnchors())
		{
			if (SSL_CTX_load_verify_locations(ctx, anchor.c_str(), nullptr) == 1)
				continue;

			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load the trust anchor '%s': %s", anchor.c_str(), msg);
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}
	}
	else
	{
		SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
	}

	Samurai::IO::File* cert = TlsFactory::getCertificate();
	if (cert && cert->exists())
	{
		if (SSL_CTX_use_certificate_file(ctx, cert->getName().c_str(), SSL_FILETYPE_PEM) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load certificate: %s", msg);
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}
	}

	Samurai::IO::File* pkey = TlsFactory::getPrivateKey();
	if (pkey && pkey->exists())
	{
		if (SSL_CTX_use_PrivateKey_file(ctx, pkey->getName().c_str(), SSL_FILETYPE_PEM) != 1)
		{
			char msg[SSL_ERRBUF_SIZE];
			ssl_error_string(msg, sizeof(msg));
			QERR("Unable to load private key: %s", msg);
			return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
		}
	}

	ssl = SSL_new(ctx);
	if (!ssl)
	{
		char msg[SSL_ERRBUF_SIZE];
		ssl_error_string(msg, sizeof(msg));
		QERR("Unable to create SSL session: %s", msg);
		return Samurai::IO::Net::TlsFactory::TlsStatus::Error;
	}

	/*
	 * A valid certificate chain proves nothing unless we also check that the
	 * certificate was issued for the host we asked for. Without this, any
	 * peer holding any CA-signed certificate can impersonate any server.
	 */
	if (mode == Samurai::IO::Net::TlsFactory::TlsOperation::Client && !peer_name.empty())
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

	SSL_set_fd(ssl, sd);
	return Samurai::IO::Net::TlsFactory::TlsStatus::Ok;
}

Samurai::IO::Net::TlsFactory::TlsStatus Samurai::IO::Net::OpenSSL::deinitialize()
{
	if (ssl) SSL_free(ssl);
	if (ctx) SSL_CTX_free(ctx);
	ssl = nullptr;
	ctx = nullptr;
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
