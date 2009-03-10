/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <stdio.h>
#include <string.h>
#include <samurai/crypto/digest/tigertree.h>
#include <samurai/crypto/digest/merkletree.h>
#include <samurai/io/file.h>
#include <samurai/util/base32.h>

#define HASH 40

void hash_tiger(uint8_t* buffer, size_t length, uint8_t* hash) {
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update((uint8_t*) buffer, (uint64_t) length);
	Samurai::Crypto::Digest::HashValue* value = tiger.digest();
	memcpy(hash, value->getData(), value->size());
}

void hash_tth_old(uint8_t* buffer, size_t length, uint8_t* hash) {
	TT_CONTEXT tigerCtx;
	tt_init(&tigerCtx);
	tt_update(&tigerCtx, buffer, length);
	tt_digest(&tigerCtx, hash);
}

void hash_tth_new(uint8_t* buffer, size_t length, uint8_t* hash, bool tthl)
{
	(void) length;
	(void) buffer;
	
	/* QDBG("Buffer size: %d (buf=%p)", (int) length, buffer); */
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger);
	merkle.update((uint8_t*) buffer, length);
	Samurai::Crypto::Digest::HashValue* value = merkle.digest();
	memcpy(hash, value->getData(), value->size());
	
	if (tthl)
	{
		printf("TTHL Leaf data: %zd/%zd leaf nodes\n", merkle.countLeaves(), merkle.maxLeaves());
	}
}


bool use_tiger   = false;
bool use_old_tth = false;
bool flag_tthl   = false;

int main(int argc, char* argv[]) {
	int n = 1;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s [-tiger|-old|-new|-tthl] file(s)\n", argv[0]);
		return -1;
	}
	
	if (strcmp(argv[n], "-tiger") == 0) {
		use_tiger = true;
		n++;
	} else {
		if (strcmp(argv[n], "-old") == 0) {
			use_old_tth = true;
			use_tiger = false;
			n++;
		} else if (strcmp(argv[n], "-new") == 0) {
			use_old_tth = false;
			use_tiger = false;
			n++;
			
		} else if (strcmp(argv[n], "-tthl") == 0) {
			use_old_tth = false;
			use_tiger = false;
			flag_tthl = true;
			n++;
			
		} else {
			if (strncmp(argv[n], "-", 1) == 0) {
				fprintf(stderr, "Usage: %s [-tiger|-old|-new] file(s)\n", argv[0]);
				return -2;
			}
		}
	}
	
	for (int i = n; i < argc; i++) {
		uint8_t hash[TIGERSIZE];
		char* digest = new char[HASH];

		Samurai::IO::File file(argv[i]);
		if (file.isDirectory()) {
			fprintf(stderr, "File is a directory: %s\n", file.getName().c_str());
			continue;
		}

		if (!file.exists())
		{
			fprintf(stderr, "File does not exist: %s\n", file.getName().c_str());
			continue;
		}

		size_t size = (size_t) file.size();
		if (!file.open(Samurai::IO::File::Read)) {
			fprintf(stderr, "Cannot open file: %s\n", argv[i]);
			continue;
		}
		
		uint8_t* buffer = new uint8_t[size];
		ssize_t sz = file.read((char*) buffer, size);
		
		if (use_tiger)
			hash_tiger(buffer, sz, hash);
		else {
			if (use_old_tth)
				hash_tth_old(buffer, sz, hash);
			else
				hash_tth_new(buffer, sz, hash, flag_tthl);
		}
		
		delete[] buffer;

		base32_encode(hash, TIGERSIZE, digest);
		printf("%s  %s\n", digest, argv[i]);
	}
	
}



// eof
