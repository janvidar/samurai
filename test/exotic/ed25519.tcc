/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/crypto/ed25519.h>

#include <string>
#include <string_view>
#include <stdint.h>

/* RFC 8032 section 7.1, tests 1 and 2: the published vectors, not invented ones. */

namespace {

template<size_t N>
std::array<uint8_t, N> ed_hex(std::string_view text)
{
	std::array<uint8_t, N> out {};
	for (size_t n = 0; n < N && 2 * n + 1 < text.size(); n++)
		out[n] = (uint8_t) std::stoi(std::string(text.substr(2 * n, 2)), nullptr, 16);
	return out;
}

const auto ED_SEED_1 = ed_hex<32>("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
const auto ED_KEY_1  = ed_hex<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
const auto ED_SIG_1  = ed_hex<64>("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");

const auto ED_SEED_2 = ed_hex<32>("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
const auto ED_KEY_2  = ed_hex<32>("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
const auto ED_SIG_2  = ed_hex<64>("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
const std::string ED_MESSAGE_2("\x72", 1);

}

EXO_TEST(ed25519_the_published_signature_of_nothing_verifies,
{
	return Samurai::Crypto::ed25519Verify(ED_KEY_1, std::string_view(), ED_SIG_1);
});

EXO_TEST(ed25519_the_published_signature_of_one_byte_verifies,
{
	return Samurai::Crypto::ed25519Verify(ED_KEY_2, ED_MESSAGE_2, ED_SIG_2);
});

EXO_TEST(ed25519_a_seed_makes_its_published_key,
{
	return Samurai::Crypto::ed25519PublicKey(ED_SEED_1) == ED_KEY_1
		&& Samurai::Crypto::ed25519PublicKey(ED_SEED_2) == ED_KEY_2;
});

EXO_TEST(ed25519_signing_makes_the_published_signature,
{
	/* Ed25519 signatures are deterministic, so this is an exact comparison. */
	return Samurai::Crypto::ed25519Sign(ED_SEED_2, ED_MESSAGE_2) == ED_SIG_2;
});

EXO_TEST(ed25519_another_message_does_not_verify,
{
	return !Samurai::Crypto::ed25519Verify(ED_KEY_2, std::string_view("\x73", 1), ED_SIG_2);
});

EXO_TEST(ed25519_another_key_does_not_verify,
{
	return !Samurai::Crypto::ed25519Verify(ED_KEY_1, ED_MESSAGE_2, ED_SIG_2);
});

EXO_TEST(ed25519_a_changed_signature_does_not_verify,
{
	for (size_t n = 0; n < ED_SIG_2.size(); n++)
	{
		auto changed = ED_SIG_2;
		changed[n] ^= 0x01;
		if (Samurai::Crypto::ed25519Verify(ED_KEY_2, ED_MESSAGE_2, changed)) return false;
	}
	return true;
});

EXO_TEST(ed25519_what_it_signs_it_verifies,
{
	const std::string message = "quickdc-update 1\nchannel test\n";
	const auto signature = Samurai::Crypto::ed25519Sign(ED_SEED_1, message);
	return signature && Samurai::Crypto::ed25519Verify(ED_KEY_1, message, *signature);
});
