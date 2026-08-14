/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/crypto/digest/sha.h>
#include <samurai/crypto/hmac.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <stdint.h>
#include <string.h>

/*
 * These are the published vectors, not cases invented here, because that is
 * the only thing that makes a hand-written digest trustworthy: FIPS 180-4 and
 * the NIST examples for SHA-1 and SHA-256, RFC 2202 for HMAC-SHA1, RFC 4231
 * for HMAC-SHA256.
 *
 * The block-boundary cases matter most. A padding mistake is invisible for a
 * short message and shows up only when the length lands exactly on, or one
 * either side of, the point where the count no longer fits in the final block.
 */

namespace {

using Samurai::Crypto::Digest::HashValue;
using Samurai::Crypto::Digest::Sha1;
using Samurai::Crypto::Digest::Sha256;
using Samurai::Crypto::Hmac;
using Samurai::Crypto::HmacAlgorithm;

std::span<const uint8_t> bytes(std::string_view text)
{
	return std::span<const uint8_t>(
		reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

std::string hex(std::span<const uint8_t> data)
{
	static const char* digits = "0123456789abcdef";
	std::string out;
	out.reserve(data.size() * 2);
	for (uint8_t b : data)
	{
		out.push_back(digits[b >> 4]);
		out.push_back(digits[b & 0x0f]);
	}
	return out;
}

std::string sha1Hex(std::string_view message)
{
	Sha1 h;
	h.update(bytes(message));
	return hex(h.digest().bytes());
}

std::string sha256Hex(std::string_view message)
{
	Sha256 h;
	h.update(bytes(message));
	return hex(h.digest().bytes());
}

/** A key or message given as hex, which is how RFC 4231 writes them. */
std::vector<uint8_t> unhex(std::string_view text)
{
	std::vector<uint8_t> out;
	auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	for (size_t i = 0; i + 1 < text.size(); i += 2)
	{
		const int hi = nibble(text[i]);
		const int lo = nibble(text[i + 1]);
		if (hi < 0 || lo < 0) break;
		out.push_back(static_cast<uint8_t>((hi << 4) | lo));
	}
	return out;
}

std::string hmacHex(HmacAlgorithm algorithm, std::span<const uint8_t> key,
                    std::span<const uint8_t> message)
{
	Hmac mac(algorithm, key);
	mac.update(message);
	return hex(mac.digest().bytes());
}

} // namespace

/* --- SHA-1: FIPS 180-4 and the NIST examples ----------------------------- */

EXO_TEST(sha1_empty, {
	return sha1Hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709";
});

EXO_TEST(sha1_abc, {
	return sha1Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d";
});

EXO_TEST(sha1_two_blocks, {
	/* 56 bytes: the length that forces the count into a second block. */
	return sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
	    == "84983e441c3bd26ebaae4aa1f95129e5e54670f1";
});

EXO_TEST(sha1_million_a, {
	/* The classic long-message vector; also the only case here that exercises
	   many consecutive compressions. */
	Sha1 h;
	const std::string chunk(1000, 'a');
	for (int i = 0; i < 1000; i++)
		h.update(bytes(chunk));
	return hex(h.digest().bytes()) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f";
});

/* --- SHA-256: FIPS 180-4 ------------------------------------------------- */

EXO_TEST(sha256_empty, {
	return sha256Hex("")
	    == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
});

EXO_TEST(sha256_abc, {
	return sha256Hex("abc")
	    == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
});

EXO_TEST(sha256_two_blocks, {
	return sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
	    == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
});

EXO_TEST(sha256_million_a, {
	Sha256 h;
	const std::string chunk(1000, 'a');
	for (int i = 0; i < 1000; i++)
		h.update(bytes(chunk));
	return hex(h.digest().bytes())
	    == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
});

/*
 * The lengths where padding decides between one final block and two. A
 * mistake here is invisible everywhere else.
 */
EXO_TEST(sha256_block_boundaries, {
	struct Case { size_t length; const char* expected; };
	const Case cases[] = {
		/* 55: the last length whose count still fits in the same block. */
		{ 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
		/* 56: the first that forces a second block. */
		{ 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
		/* 63, 64, 65: either side of exactly one block. */
		{ 63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34" },
		{ 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
		{ 65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0" },
	};

	for (const Case& c : cases)
	{
		if (sha256Hex(std::string(c.length, 'a')) != c.expected)
			return false;
	}
	return true;
});

/* Feeding a message in pieces must give the same answer as feeding it whole:
   the block accumulator is where that would break. */
EXO_TEST(sha256_incremental_matches_whole, {
	const std::string message(1000, 'x');

	Sha256 whole;
	whole.update(bytes(message));
	const std::string expected = hex(whole.digest().bytes());

	for (size_t chunk : { (size_t) 1, (size_t) 7, (size_t) 63, (size_t) 64, (size_t) 65 })
	{
		Sha256 drip;
		for (size_t i = 0; i < message.size(); i += chunk)
			drip.update(bytes(std::string_view(message).substr(i, chunk)));
		if (hex(drip.digest().bytes()) != expected)
			return false;
	}
	return true;
});

EXO_TEST(sha_reset_restores_the_initial_state, {
	Sha256 h;
	h.update(bytes("abc"));
	h.digest();

	h.reset();
	h.update(bytes("abc"));
	return hex(h.digest().bytes())
	    == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
});

EXO_TEST(sha_digest_is_stable_when_asked_twice, {
	Sha256 h;
	h.update(bytes("abc"));
	const std::string first = hex(h.digest().bytes());
	return first == hex(h.digest().bytes());
});

EXO_TEST(sha_sizes, {
	Sha1 a;
	Sha256 b;
	return a.size() == 20 && b.size() == 32
	    && a.digest().size() == 20 && b.digest().size() == 32;
});

/* --- HMAC-SHA1: RFC 2202 ------------------------------------------------- */

EXO_TEST(hmac_sha1_rfc2202_case1, {
	const std::vector<uint8_t> key(20, 0x0b);
	return hmacHex(HmacAlgorithm::Sha1, key, bytes("Hi There"))
	    == "b617318655057264e28bc0b6fb378c8ef146be00";
});

EXO_TEST(hmac_sha1_rfc2202_case2, {
	return hmacHex(HmacAlgorithm::Sha1, bytes("Jefe"),
	               bytes("what do ya want for nothing?"))
	    == "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79";
});

EXO_TEST(hmac_sha1_rfc2202_case3, {
	const std::vector<uint8_t> key(20, 0xaa);
	const std::vector<uint8_t> message(50, 0xdd);
	return hmacHex(HmacAlgorithm::Sha1, key, message)
	    == "125d7342b9ac11cd91a39af48aa17b4f63f175d3";
});

EXO_TEST(hmac_sha1_rfc2202_long_key, {
	/* 80 bytes, longer than the 64-byte block, so the key is hashed down. */
	const std::vector<uint8_t> key(80, 0xaa);
	return hmacHex(HmacAlgorithm::Sha1, key,
	               bytes("Test Using Larger Than Block-Size Key - Hash Key First"))
	    == "aa4ae5e15272d00e95705637ce8a3b55ed402112";
});

/* --- HMAC-SHA256: RFC 4231 ----------------------------------------------- */

EXO_TEST(hmac_sha256_rfc4231_case1, {
	const std::vector<uint8_t> key = unhex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
	return hmacHex(HmacAlgorithm::Sha256, key, bytes("Hi There"))
	    == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7";
});

EXO_TEST(hmac_sha256_rfc4231_case2, {
	return hmacHex(HmacAlgorithm::Sha256, bytes("Jefe"),
	               bytes("what do ya want for nothing?"))
	    == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843";
});

EXO_TEST(hmac_sha256_rfc4231_case3, {
	const std::vector<uint8_t> key(20, 0xaa);
	const std::vector<uint8_t> message(50, 0xdd);
	return hmacHex(HmacAlgorithm::Sha256, key, message)
	    == "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe";
});

EXO_TEST(hmac_sha256_rfc4231_long_key, {
	/* 131 bytes: RFC 4231 case 6, which exercises hashing the key down. */
	const std::vector<uint8_t> key(131, 0xaa);
	return hmacHex(HmacAlgorithm::Sha256, key,
	               bytes("Test Using Larger Than Block-Size Key - Hash Key First"))
	    == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54";
});

EXO_TEST(hmac_sha256_rfc4231_long_key_and_message, {
	/* Case 7: both the key and the message exceed the block size. */
	const std::vector<uint8_t> key(131, 0xaa);
	const std::string message =
		"This is a test using a larger than block-size key and a larger than "
		"block-size data. The key needs to be hashed before being used by the "
		"HMAC algorithm.";
	return hmacHex(HmacAlgorithm::Sha256, key, bytes(message))
	    == "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2";
});

/* --- HMAC behaviour ------------------------------------------------------ */

EXO_TEST(hmac_incremental_matches_whole, {
	const std::vector<uint8_t> key(20, 0x0b);
	const std::string message(500, 'z');

	const std::string expected = hmacHex(HmacAlgorithm::Sha256, key, bytes(message));

	Hmac mac(HmacAlgorithm::Sha256, key);
	for (size_t i = 0; i < message.size(); i += 37)
		mac.update(bytes(std::string_view(message).substr(i, 37)));
	return hex(mac.digest().bytes()) == expected;
});

EXO_TEST(hmac_reset_reuses_the_key, {
	const std::vector<uint8_t> key(20, 0x0b);

	Hmac mac(HmacAlgorithm::Sha1, key);
	mac.update(bytes("Hi There"));
	const std::string first = hex(mac.digest().bytes());

	mac.reset();
	mac.update(bytes("Hi There"));
	return hex(mac.digest().bytes()) == first
	    && first == "b617318655057264e28bc0b6fb378c8ef146be00";
});

EXO_TEST(hmac_empty_key_is_allowed, {
	/* Useless as a key, but a valid one rather than a mistake to reject. */
	const std::vector<uint8_t> none;
	const std::string mac = hmacHex(HmacAlgorithm::Sha256, none, bytes("message"));
	return mac.size() == 64 && mac != std::string(64, '0');
});

EXO_TEST(hmac_one_shot_matches_incremental, {
	const std::vector<uint8_t> key(20, 0x0b);
	const std::span<const uint8_t> message = bytes("Hi There");

	return hex(Samurai::Crypto::hmacSha1(key, message).bytes())
	         == hmacHex(HmacAlgorithm::Sha1, key, message)
	    && hex(Samurai::Crypto::hmacSha256(key, message).bytes())
	         == hmacHex(HmacAlgorithm::Sha256, key, message);
});

EXO_TEST(hmac_a_changed_key_changes_the_mac, {
	std::vector<uint8_t> a(20, 0x0b);
	std::vector<uint8_t> b(20, 0x0b);
	b[19] ^= 0x01;

	return hmacHex(HmacAlgorithm::Sha256, a, bytes("message"))
	    != hmacHex(HmacAlgorithm::Sha256, b, bytes("message"));
});

EXO_TEST(hmac_sizes, {
	const std::vector<uint8_t> key(20, 0x0b);
	Hmac a(HmacAlgorithm::Sha1, key);
	Hmac b(HmacAlgorithm::Sha256, key);
	return a.size() == 20 && b.size() == 32
	    && Samurai::Crypto::hmacSize(HmacAlgorithm::Sha1) == 20
	    && Samurai::Crypto::hmacSize(HmacAlgorithm::Sha256) == 32;
});
