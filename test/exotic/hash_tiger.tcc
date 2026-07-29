#include "samurai/crypto/digest/hash.h"
#include "samurai/crypto/digest/tiger.h"
#include "samurai/crypto/digest/merkletree.h"
#include "samurai/util/base32.h"
#include <vector>
#include <array>
#include <string>
#include "samurai/io/buffer.h"

/* Named here because a comma inside EXO_TEST's second argument would be
   read as an argument separator. */

bool testTiger(const char* input, const char* expected) {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) input, strlen(input));
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Hex, buf, 64);
	return strcasecmp(buf, expected) == 0;
}

bool testTTH(const char* input, const char* expected) {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, strlen(input));
	merkle.update((uint8_t*) input, strlen(input));
	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
	return strcasecmp(buf, expected) == 0;
}


EXO_TEST(hash_tiger_1, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) "abc", 3);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
	return strcmp(buf, "FKVRJBHIYFMPFP5YYX7UDNL2KJISSEY4SV5V7EY") == 0;
});

EXO_TEST(hash_tiger_2, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) "a", 1);
	tiger.update((uint8_t*) "b", 1);
	tiger.update((uint8_t*) "c", 1);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
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
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Hex, buf, 64);
	return strcasecmp(buf, "2AAB1484E8C158F2BFB8C5FF41B57A525129131C957B5F93") == 0;
});

EXO_TEST(hash_tiger_12, {
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) "ab", 2);
	tiger.update((uint8_t*) "c", 1);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	char buf[64];
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Hex, buf, 64);
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
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Hex, buf, 64);
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
	value->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Hex, buf, 64);
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

	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
	return strcasecmp(buf, "KEPTIGT4CQKF7S5EUVNJZSXXIPNMB3XSOAAQS4Y") == 0;
});

/* One byte at a time, so every block boundary is crossed mid-update. */
EXO_TEST(hash_tth_stream_2m_bytewise, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
	for (size_t n = 0; n < 2000000; n++)
		merkle.update((const uint8_t*) "a", 1);

	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
	return strcasecmp(buf, "XD2AKUE5DFPBXNGML5P3QSOO7LOV2EKOICJWX3A") == 0;
});

EXO_TEST(hash_tth_stream_4m_bytewise, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
	for (size_t n = 0; n < 4000000; n++)
		merkle.update((const uint8_t*) "a", 1);

	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
	return strcasecmp(buf, "4WDBG7VHOVUQ23TIDFPZCFJ3PGXUXVGJINVU4PI") == 0;
});

EXO_TEST(hash_tth_stream_8m_bytewise, {
	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
	for (size_t n = 0; n < 8000000; n++)
		merkle.update((const uint8_t*) "a", 1);

	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
	return strcasecmp(buf, "6L6GMENTBLX2UE5JIVSCPX4P5PQYB7YUIY6PJ6I") == 0;
});

/* ------------------------------------------------------------------------- */
/* Tiger tree known answers across leaf boundaries                            */
/*                                                                            */
/* Sizes either side of a 1024-byte leaf, and one that is nowhere near a whole */
/* number of leaves, so the odd-node handling in the tree build is exercised   */
/* rather than only the balanced case.                                        */
/*                                                                            */
/* Every digest below was produced by both this implementation and the Bitzi   */
/* reference, which the suite used to run side by side; the reference now sits */
/* beside the testtiger program. The empty-input value is the published TTH of */
/* the empty string, so the set is anchored outside this tree as well.         */
/* ------------------------------------------------------------------------- */

static bool tth_of_repeated_a(size_t count, const char* expected)
{
	const std::vector<uint8_t> data(count, 'a');

	char buf[64];
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
	if (count) merkle.update(data.data(), data.size());
	merkle.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
	return strcasecmp(buf, expected) == 0;
}

EXO_TEST(hash_tth_size_empty, {
	return tth_of_repeated_a(0, "LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ");
});

EXO_TEST(hash_tth_size_1, {
	return tth_of_repeated_a(1, "CZQUWH3IYXBF5L3BGYUGZHASSMXU647IP2IKE4Y");
});

EXO_TEST(hash_tth_size_1023, {
	return tth_of_repeated_a(1023, "YBJDV4HQU6LDJZMP36DEUZ7MMNXA6TBLMOX55PI");
});

EXO_TEST(hash_tth_size_1024, {
	return tth_of_repeated_a(1024, "BR4BVJBMHDFVCFI4WBPSL63W5TWXWVBSC574BLI");
});

EXO_TEST(hash_tth_size_1025, {
	return tth_of_repeated_a(1025, "CDYY2OW6F6DTGCH3Q6NMSDLSRV7PNMAL3CED3DA");
});

EXO_TEST(hash_tth_size_2048, {
	return tth_of_repeated_a(2048, "YPAYMUL6MIZR2X34IKJON6TN2KPYPNE7IHGP2MQ");
});

EXO_TEST(hash_tth_size_2049, {
	return tth_of_repeated_a(2049, "5ROSDZNI2SQAVSITIGLFULZQNFUGPAID2V45YFY");
});

EXO_TEST(hash_tth_size_65537, {
	return tth_of_repeated_a(65537, "SVNLUONR2CQSU2VDCKJJKORSZ5OTK6MEAE32D5Y");
});

EXO_TEST(hash_tth_size_100000, {
	return tth_of_repeated_a(100000, "WJYXXZ3KYQB2MQSOE42HLXJ5XE6OGXKFIDYLOQQ");
});

/* Not a whole number of leaves, and large enough that the stack collapses
   several times on the way. */
EXO_TEST(hash_tth_size_12957194, {
	return tth_of_repeated_a(12957194, "OKDA34GX7KFK5VGURZARYTKRJ6N26IJ4C5YGNFY");
});

/* ------------------------------------------------------------------------- */
/* TTHL leaf serialisation                                                    */
/*                                                                            */
/* copyLeaves*() writes the leaf digests out; setLeaves*() builds a tree from  */
/* them without the original data. A tree restored from its own leaves must    */
/* produce the root it started with - that is the whole point of shipping      */
/* TTHL separately from the file.                                             */
/* ------------------------------------------------------------------------- */

static std::string merkle_root_base32(Samurai::Crypto::Digest::MerkleTree& tree)
{
	char buf[64];
	tree.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Base32, buf, 64);
	return std::string(buf);
}

/* Hash 'size' bytes and leave the tree finalized, so its leaves can be read. */
static void merkle_hash_filler(Samurai::Crypto::Digest::MerkleTree& tree, size_t size)
{
	const std::vector<uint8_t> data(size, 'a');
	tree.update(data.data(), data.size());
	tree.finalize();
}

static const size_t leaf_test_size = 1000000;

EXO_TEST(merkle_leaves_are_produced,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	merkle_hash_filler(tree, leaf_test_size);

	/* More than one, or the ordering tests below prove nothing. */
	return tree.countLeaves() > 1 && tree.countLeaves() <= tree.maxLeaves();
});

EXO_TEST(merkle_copy_leaves_ltr_size_matches_count,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	merkle_hash_filler(tree, leaf_test_size);

	Samurai::IO::Buffer buffer;
	tree.copyLeavesLTR(buffer);
	return buffer.size() == tree.countLeaves() * tree.size();
});

EXO_TEST(merkle_leaf_round_trip_ltr,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	merkle_hash_filler(tree, leaf_test_size);

	const std::string expected = merkle_root_base32(tree);
	const size_t leaves = tree.countLeaves();

	Samurai::IO::Buffer buffer;
	tree.copyLeavesLTR(buffer);

	Samurai::Crypto::Digest::Tiger tiger2;
	Samurai::Crypto::Digest::MerkleTree restored(&tiger2, 0);
	restored.setLeavesLTR(buffer, leaves, leaf_test_size);

	return merkle_root_base32(restored) == expected;
});

EXO_TEST(merkle_leaf_round_trip_rtl,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	merkle_hash_filler(tree, leaf_test_size);

	const std::string expected = merkle_root_base32(tree);
	const size_t leaves = tree.countLeaves();

	Samurai::IO::Buffer buffer;
	tree.copyLeavesRTL(buffer);

	Samurai::Crypto::Digest::Tiger tiger2;
	Samurai::Crypto::Digest::MerkleTree restored(&tiger2, 0);
	restored.setLeavesRTL(buffer, leaves, leaf_test_size);

	return merkle_root_base32(restored) == expected;
});

/* The two directions must genuinely differ, or neither is reversing and both
   round trips above would pass for the wrong reason. */
EXO_TEST(merkle_copy_leaves_ltr_and_rtl_differ,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	merkle_hash_filler(tree, leaf_test_size);

	Samurai::IO::Buffer ltr;
	Samurai::IO::Buffer rtl;
	tree.copyLeavesLTR(ltr);
	tree.copyLeavesRTL(rtl);

	if (ltr.size() != rtl.size()) return false;

	const size_t leaf_size = tree.size();
	/* The last leaf out of RTL is the first leaf out of LTR. */
	for (size_t n = 0; n < leaf_size; n++)
		if (ltr.at(n) != rtl.at(rtl.size() - leaf_size + n)) return false;

	return ltr.at(0) != rtl.at(0);
});

/* Feeding LTR bytes to the RTL setter reverses the leaves, which must change
   the root. This is what proves the round trips are order sensitive. */
EXO_TEST(merkle_leaf_order_changes_the_root,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	merkle_hash_filler(tree, leaf_test_size);

	const std::string expected = merkle_root_base32(tree);
	const size_t leaves = tree.countLeaves();

	Samurai::IO::Buffer buffer;
	tree.copyLeavesLTR(buffer);

	Samurai::Crypto::Digest::Tiger tiger2;
	Samurai::Crypto::Digest::MerkleTree reversed(&tiger2, 0);
	reversed.setLeavesRTL(buffer, leaves, leaf_test_size);

	return merkle_root_base32(reversed) != expected;
});

/* setLeaves*() resets first, so a tree that already hashed something else must
   end up with only the leaves it was given. */
EXO_TEST(merkle_set_leaves_discards_previous_content,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	merkle_hash_filler(tree, leaf_test_size);

	const std::string expected = merkle_root_base32(tree);
	const size_t leaves = tree.countLeaves();

	Samurai::IO::Buffer buffer;
	tree.copyLeavesLTR(buffer);

	Samurai::Crypto::Digest::Tiger tiger2;
	Samurai::Crypto::Digest::MerkleTree dirty(&tiger2, 0);
	const std::vector<uint8_t> other(50000, 'z');
	dirty.update(other.data(), other.size());

	dirty.setLeavesLTR(buffer, leaves, leaf_test_size);
	return merkle_root_base32(dirty) == expected && dirty.countLeaves() == leaves;
});

/* A tree restored from leaves can hand the same leaves back out again. */
EXO_TEST(merkle_leaves_survive_a_second_round_trip,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	merkle_hash_filler(tree, leaf_test_size);

	const size_t leaves = tree.countLeaves();
	Samurai::IO::Buffer first;
	tree.copyLeavesLTR(first);

	Samurai::Crypto::Digest::Tiger tiger2;
	Samurai::Crypto::Digest::MerkleTree restored(&tiger2, 0);
	restored.setLeavesLTR(first, leaves, leaf_test_size);
	restored.finalize();

	Samurai::IO::Buffer second;
	restored.copyLeavesLTR(second);

	if (first.size() != second.size()) return false;
	for (size_t n = 0; n < first.size(); n++)
		if (first.at(n) != second.at(n)) return false;

	return true;
});

/*
 * reset() has to clear the inherited accumulator as well as the tree's own
 * state. Hash::reset() is pure virtual, so nothing else does it: a partial
 * block left over from earlier input used to survive into the next use and
 * finalize() hashed it as an extra leaf.
 */
EXO_TEST(merkle_reset_discards_a_partial_block,
{
	Samurai::Crypto::Digest::Tiger reference_hasher;
	Samurai::Crypto::Digest::MerkleTree reference(&reference_hasher, 0);
	merkle_hash_filler(reference, leaf_test_size);
	const std::string expected = merkle_root_base32(reference);

	/* Not a whole number of blocks, so an accumulator is left behind. */
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	const std::vector<uint8_t> junk(50123, 'z');
	tree.update(junk.data(), junk.size());

	tree.reset();
	merkle_hash_filler(tree, leaf_test_size);

	return merkle_root_base32(tree) == expected;
});

EXO_TEST(merkle_reset_after_finalize_allows_reuse,
{
	Samurai::Crypto::Digest::Tiger reference_hasher;
	Samurai::Crypto::Digest::MerkleTree reference(&reference_hasher, 0);
	merkle_hash_filler(reference, leaf_test_size);
	const std::string expected = merkle_root_base32(reference);

	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	const std::vector<uint8_t> junk(50123, 'z');
	tree.update(junk.data(), junk.size());
	tree.finalize();

	tree.reset();
	merkle_hash_filler(tree, leaf_test_size);

	return merkle_root_base32(tree) == expected;
});

/* ------------------------------------------------------------------------- */
/* Finalization is once and for all                                          */
/*                                                                           */
/* Nothing set m_finalized for Tiger - the only assignment in the tree was in */
/* merkletree.cpp - so internal_finalize()'s own guard never fired. A second  */
/* digest() padded the already padded state and returned a different, wrong   */
/* value, and update() after finalize was absorbed instead of ignored.        */
/* ------------------------------------------------------------------------- */

static std::string tiger_hex(Samurai::Crypto::Digest::Hash& hash)
{
	char buf[64];
	hash.digest()->getFormattedString(Samurai::Crypto::Digest::HashValue::Format::Hex, buf, 64);
	return std::string(buf);
}

EXO_TEST(hash_tiger_digest_is_stable_across_calls,
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "abc", 3);

	const std::string first = tiger_hex(tiger);
	const std::string second = tiger_hex(tiger);
	const std::string third = tiger_hex(tiger);

	return first == second && second == third
		&& first == "2aab1484e8c158f2bfb8c5ff41b57a525129131c957b5f93";
});

EXO_TEST(hash_tiger_update_after_finalize_is_ignored,
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "abc", 3);
	const std::string before = tiger_hex(tiger);

	tiger.update((const uint8_t*) "def", 3);
	return tiger_hex(tiger) == before;
});

/* reset() is the documented way back, and must restore a usable hasher. */
EXO_TEST(hash_tiger_reset_allows_reuse,
{
	Samurai::Crypto::Digest::Tiger reference;
	reference.update((const uint8_t*) "abc", 3);
	const std::string expected = tiger_hex(reference);

	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "something else entirely", 23);
	tiger_hex(tiger);

	tiger.reset();
	tiger.update((const uint8_t*) "abc", 3);
	return tiger_hex(tiger) == expected;
});

EXO_TEST(hash_merkle_digest_is_stable_across_calls,
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree tree(&tiger, 0);
	const std::vector<uint8_t> data(50000, 'a');
	tree.update(data.data(), data.size());

	const std::string first = tiger_hex(tree);
	return first == tiger_hex(tree) && first == tiger_hex(tree);
});

/* ------------------------------------------------------------------------- */
/* getFormattedString bounds                                                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(hashvalue_hex_needs_room_for_the_terminator,
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "abc", 3);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();

	const size_t exact = value->size() * 2 + 1;
	std::vector<char> just_enough(exact);
	std::vector<char> one_short(exact - 1);

	return value->getFormattedString(
			Samurai::Crypto::Digest::HashValue::Format::Hex, just_enough.data(), exact)
		&& !value->getFormattedString(
			Samurai::Crypto::Digest::HashValue::Format::Hex, one_short.data(), exact - 1);
});

/* A refused call still leaves a terminated, empty string behind. */
EXO_TEST(hashvalue_short_buffer_is_left_empty,
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "abc", 3);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();

	char buf[8];
	memset(buf, 'x', sizeof(buf));
	const bool refused = !value->getFormattedString(
		Samurai::Crypto::Digest::HashValue::Format::Hex, buf, sizeof(buf));

	return refused && buf[0] == '\0';
});

EXO_TEST(hashvalue_base32_needs_room_for_the_terminator,
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "abc", 3);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();

	const size_t exact = ((value->size() * 8 + 4) / 5) + 1;
	std::vector<char> just_enough(exact);
	std::vector<char> one_short(exact - 1);

	return value->getFormattedString(
			Samurai::Crypto::Digest::HashValue::Format::Base32, just_enough.data(), exact)
		&& !value->getFormattedString(
			Samurai::Crypto::Digest::HashValue::Format::Base32, one_short.data(), exact - 1);
});

EXO_TEST(hashvalue_rejects_null_and_zero_length,
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "abc", 3);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();

	char buf[64];
	return !value->getFormattedString(
			Samurai::Crypto::Digest::HashValue::Format::Hex, nullptr, sizeof(buf))
		&& !value->getFormattedString(
			Samurai::Crypto::Digest::HashValue::Format::Hex, buf, 0);
});

/* ------------------------------------------------------------------------- */
/* HashValue move semantics                                                  */
/* ------------------------------------------------------------------------- */

EXO_TEST(hashvalue_move_transfers_the_digest,
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "abc", 3);

	Samurai::Crypto::Digest::HashValue original(*tiger.digest());
	char before[64];
	original.getFormattedString(
		Samurai::Crypto::Digest::HashValue::Format::Hex, before, sizeof(before));

	Samurai::Crypto::Digest::HashValue moved(std::move(original));
	char after[64];
	moved.getFormattedString(
		Samurai::Crypto::Digest::HashValue::Format::Hex, after, sizeof(after));

	return strcmp(before, after) == 0 && moved.size() == 24;
});

EXO_TEST(hashvalue_copy_is_independent,
{
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((const uint8_t*) "abc", 3);

	Samurai::Crypto::Digest::HashValue a(*tiger.digest());
	Samurai::Crypto::Digest::HashValue b(a);

	return a == b && a.size() == b.size();
});
