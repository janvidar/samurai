#include "samurai/crypto/digest/hash.h"
#include "samurai/crypto/digest/tiger.h"
#include "samurai/crypto/digest/merkletree.h"
#include "samurai/crypto/digest/tigertree.h"
#include "samurai/util/base32.h"
#include <vector>

bool testTiger(const char* input, const char* expected) {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) input, strlen(input));
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatHex, buf, 64);
	return strcasecmp(buf, expected) == 0;
}

bool testTTH(const char* input, const char* expected) {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, strlen(input));
	merkle.update((uint8_t*) input, strlen(input));
	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
	return strcasecmp(buf, expected) == 0;
}


EXO_TEST(hash_tiger_1, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) "abc", 3);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
	return strcmp(buf, "FKVRJBHIYFMPFP5YYX7UDNL2KJISSEY4SV5V7EY") == 0;
});

EXO_TEST(hash_tiger_2, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) "a", 1);
	tiger.update((uint8_t*) "b", 1);
	tiger.update((uint8_t*) "c", 1);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
	return strcmp(buf, "FKVRJBHIYFMPFP5YYX7UDNL2KJISSEY4SV5V7EY") == 0;
});

EXO_TEST(hash_tiger_3, {
	return testTiger("", "3293AC630C13F0245F92BBB1766E16167A4E58492DDE73F3");
});

EXO_TEST(hash_tiger_4, {
	return testTiger("a", "77BEFBEF2E7EF8AB2EC8F93BF587A7FC613E247F5F247809");
});

EXO_TEST(hash_tiger_5, {
	return testTiger("abc", "2AAB1484E8C158F2BFB8C5FF41B57A525129131C957B5F93");
});

EXO_TEST(hash_tiger_6, {
	return testTiger("message digest", "D981F8CB78201A950DCF3048751E441C517FCA1AA55A29F6");
});

EXO_TEST(hash_tiger_7, {
	return testTiger("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "0F7BF9A19B9C58F2B7610DF7E84F0AC3A71C631E7B53F78E");
});

EXO_TEST(hash_tiger_8, {
	return testTiger("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "8DCEA680A17583EE502BA38A3C368651890FFBCCDC49A8CC");
});

EXO_TEST(hash_tiger_9, {
	return testTiger("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "1C14795529FD9F207A958F84C52F11E887FA0CABDFD91BFD");
});

EXO_TEST(hash_tiger_10, {
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) "Jan Vidar Krey", 3);
	Samurai::Crypto::Digest::HashValue* value1 = tiger.digest();
	Samurai::Crypto::Digest::HashValue* value2 = tiger.digest();
	bool ok = (*value1 == *value2);
	return ok;
});

EXO_TEST(hash_tiger_11, {
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) "a", 1);
	tiger.update((uint8_t*) "b", 1);
	tiger.update((uint8_t*) "c", 1);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	char buf[64];
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatHex, buf, 64);
	return strcasecmp(buf, "2AAB1484E8C158F2BFB8C5FF41B57A525129131C957B5F93") == 0;
});

EXO_TEST(hash_tiger_12, {
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) "ab", 2);
	tiger.update((uint8_t*) "c", 1);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	char buf[64];
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatHex, buf, 64);
	return strcasecmp(buf, "2AAB1484E8C158F2BFB8C5FF41B57A525129131C957B5F93") == 0;
});

EXO_TEST(hash_tiger_13, {
	Samurai::Crypto::Digest::Tiger tiger;
	for (size_t n = 0; n < 1000000; n++)
	{
		tiger.update((uint8_t*) "a", 1);
	}
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	char buf[64];
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatHex, buf, 64);
	return strcasecmp(buf, "6DB0E2729CBEAD93D715C6A7D36302E9B3CEE0D2BC314B41") == 0;
});

EXO_TEST(hash_tiger_14, {
	Samurai::Crypto::Digest::Tiger tiger;
	for (size_t n = 0; n < 8; n++)
	{
		tiger.update((uint8_t*) "1234567890", 10);
	}
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	char buf[64];
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatHex, buf, 64);
	return strcasecmp(buf, "1C14795529FD9F207A958F84C52F11E887FA0CABDFD91BFD") == 0;
});

EXO_TEST(hash_tth_1, {
	return testTTH("", "LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ");
});

EXO_TEST(hash_tth_2, {
	return testTTH("a", "CZQUWH3IYXBF5L3BGYUGZHASSMXU647IP2IKE4Y");
});

EXO_TEST(hash_tth_3, {
	return testTTH("abc", "ASD4UJSEH5M47PDYB46KBTSQTSGDKLBHYXOMUIA");
});

EXO_TEST(hash_tth_4, {
	return testTTH("message digest", "YM432MSOX5QILIH2L4TNO62E3O35WYGWSBSJOBA");
});

EXO_TEST(hash_tth_5, {
	return testTTH("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "VHRH2RUE3HSJ2SNATJ4AHWQVWJMOLXPASI4HB2I");
});

EXO_TEST(hash_tth_6, {
	return testTTH("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "TF74ENF7MF2WPDE35M23NRSVKJIRKYRMTLWAHWQ");
});

EXO_TEST(hash_tth_7, {
	return testTTH("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "NBKCANQ2ODNTSV4C7YJFF3JRAV7LKTFIPHQNBJY");
});


/* ------------------------------------------------------------------------- */
/* Streaming tiger tree hashes                                                */
/*                                                                            */
/* These pass 0 as the block size, which selects the default. That used to     */
/* reach Hash with a zero-sized block, where update() could not advance and    */
/* spun forever, which is why they were disabled.                             */
/* ------------------------------------------------------------------------- */

EXO_TEST(hash_tth_stream_1mb, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);

	std::vector<uint8_t> temp(100000, 'a');
	for (unsigned n = 0; n < 10; n++)
		merkle.update(temp.data(), temp.size());

	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
	return strcasecmp(buf, "KEPTIGT4CQKF7S5EUVNJZSXXIPNMB3XSOAAQS4Y") == 0;
});

/* One byte at a time, so every block boundary is crossed mid-update. */
EXO_TEST(hash_tth_stream_2m_bytewise, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
	for (size_t n = 0; n < 2000000; n++)
		merkle.update((const uint8_t*) "a", 1);

	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
	return strcasecmp(buf, "XD2AKUE5DFPBXNGML5P3QSOO7LOV2EKOICJWX3A") == 0;
});

EXO_TEST(hash_tth_stream_4m_bytewise, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
	for (size_t n = 0; n < 4000000; n++)
		merkle.update((const uint8_t*) "a", 1);

	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
	return strcasecmp(buf, "4WDBG7VHOVUQ23TIDFPZCFJ3PGXUXVGJINVU4PI") == 0;
});

EXO_TEST(hash_tth_stream_8m_bytewise, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
	for (size_t n = 0; n < 8000000; n++)
		merkle.update((const uint8_t*) "a", 1);

	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
	return strcasecmp(buf, "6L6GMENTBLX2UE5JIVSCPX4P5PQYB7YUIY6PJ6I") == 0;
});

/*
 * A size that is not a whole number of leaves, checked against the other
 * tiger-tree implementation in the tree rather than against a recorded digest:
 * the two are independent, so agreeing on an awkward length is the property
 * worth asserting.
 */
EXO_TEST(hash_tth_stream_matches_reference_impl, {
	const size_t N = 12957194;
	std::vector<uint8_t> data(N, 'a');

	char from_tree[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
	merkle.update(data.data(), N);
	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, from_tree, 64);

	Samurai::Crypto::Digest::TT_CONTEXT ctx;
	Samurai::Crypto::Digest::tt_init(&ctx);
	Samurai::Crypto::Digest::tt_update(&ctx, data.data(), N);
	uint8_t raw[Samurai::Crypto::Digest::TIGERSIZE];
	Samurai::Crypto::Digest::tt_digest(&ctx, raw);

	char from_ref[64];
	base32_encode(raw, Samurai::Crypto::Digest::TIGERSIZE, from_ref, sizeof(from_ref));

	return strcasecmp(from_tree, from_ref) == 0;
});
