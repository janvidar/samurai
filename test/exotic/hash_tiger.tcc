#include "samurai/crypto/digest/hash.h"
#include "samurai/crypto/digest/tiger.h"
#include "samurai/crypto/digest/merkletree.h"

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


// EXO_TEST(hash_tth_8, {
// 	char buf[64];
// 	Samurai::Crypto::Digest::Tiger tiger;
// 	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
// 	
// 	uint8_t* temp = new uint8_t[100000];
// 	memset(temp, 'a', 100000);
// 	for (uint n = 0; n < 10; n++)
// 		merkle.update(temp, 100000);
// 	
// 	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
// 	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
// 	return strcasecmp(buf, "KEPTIGT4CQKF7S5EUVNJZSXXIPNMB3XSOAAQS4Y") == 0;
// 	return 0;
// });
// 
// EXO_TEST(hash_tth_9, {
// 	char buf[64];
// 	Samurai::Crypto::Digest::Tiger tiger;
// 	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
// 	for (size_t n = 0; n < 2000000; n++)
// 		merkle.update((uint8_t*) "a", 1);
// 	
// 	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
// 	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
// 	return strcasecmp(buf, "XD2AKUE5DFPBXNGML5P3QSOO7LOV2EKOICJWX3A") == 0;
// 	
// });
// 
// EXO_TEST(hash_tth_10, {
// 	char buf[64];
// 	Samurai::Crypto::Digest::Tiger tiger;
// 	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
// 	for (size_t n = 0; n < 4000000; n++)
// 		merkle.update((uint8_t*) "a", 1);
// 	
// 	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
// 	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
// 	return strcasecmp(buf, "4WDBG7VHOVUQ23TIDFPZCFJ3PGXUXVGJINVU4PI") == 0;
// 	
// });
// 
// EXO_TEST(hash_tth_11, {
// 	char buf[64];
// 	Samurai::Crypto::Digest::Tiger tiger;
// 	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
// 	for (size_t n = 0; n < 8000000; n++)
// 		merkle.update((uint8_t*) "a", 1);
// 	
// 	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
// 	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
// 	return strcasecmp(buf, "6L6GMENTBLX2UE5JIVSCPX4P5PQYB7YUIY6PJ6I") == 0;
// 	
// });
// 
// EXO_TEST(hash_tth_12, {
// 	char buf[64];
// 	Samurai::Crypto::Digest::Tiger tiger;
// 	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
// 	for (size_t n = 0; n < 12957194; n++)
// 		merkle.update((uint8_t*) "a", 1);
// 	
// 	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
// 	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
// 	return strcasecmp(buf, "T5E2GQZNS2YZCHOA2MLG6SL4C3P3ORSQZUZMGXI") == 0;
// 	
// });


// struct LeaveStore
// {
// 	uint64_t filesize;
// 	size_t leaves;
// 	Samurai::IO::Buffer data;
// };
// 
// static LeaveStore leaveStore;
// 
// EXO_TEST(hash_leaf_data_1,
// {
// 	char buf[64];
// 	Samurai::Crypto::Digest::Tiger tiger;
// 	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
// 
// 	uint8_t* temp = new uint8_t[80];
// 	memset(temp, 'a', 80);
// 	merkle.update(temp, 80);
// 
// 	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
// 	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
// 	
// 	leaveStore.filesize = merkle.getFileSize();
// 	leaveStore.leaves   = merkle.countLeaves();
// 	merkle.copyLeaves(leaveStore.data);
// 	
// 	delete[] temp;
// 	return strcasecmp(buf, "KEPTIGT4CQKF7S5EUVNJZSXXIPNMB3XSOAAQS4Y") == 0;
// });
// 
// EXO_TEST(hash_leaf_data_2,
// {
// 	char buf[64];
// 	Samurai::Crypto::Digest::Tiger tiger;
// 	Samurai::Crypto::Digest::MerkleTree merkle(&tiger, 0);
// 
// 	merkle.setLeaves(leaveStore.data, leaveStore.leaves, leaveStore.filesize);
// 	
// 	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
// 	value->getFormattedString(Samurai::Crypto::Digest::HashValue::FormatBase32, buf, 64);
// 	
// 	return strcasecmp(buf, "KEPTIGT4CQKF7S5EUVNJZSXXIPNMB3XSOAAQS4Y") == 0;
// });
// 
