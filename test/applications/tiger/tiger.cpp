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

#include <span>

#include <algorithm>

#define HASH 40

void hash_tiger(std::span<const uint8_t> buffer, std::span<uint8_t, Samurai::Crypto::Digest::TIGERSIZE> hash) {
	Samurai::Crypto::Digest::Tiger tiger;
	tiger.update(buffer);
	std::ranges::copy(tiger.digest()->bytes(), hash.begin());
}

void hash_tth_old(std::span<const uint8_t> buffer, std::span<uint8_t, Samurai::Crypto::Digest::TIGERSIZE> hash) {
	Samurai::Crypto::Digest::TT_CONTEXT tigerCtx;
	Samurai::Crypto::Digest::tt_init(&tigerCtx);
	Samurai::Crypto::Digest::tt_update(&tigerCtx, buffer);
	Samurai::Crypto::Digest::tt_digest(&tigerCtx, hash);
}

void hash_tth_new(std::span<const uint8_t> buffer, std::span<uint8_t, Samurai::Crypto::Digest::TIGERSIZE> hash, bool tthl)
{
	Samurai::Crypto::Digest::Tiger tiger;
	Samurai::Crypto::Digest::MerkleTree merkle(&tiger);
	merkle.update(buffer);
	std::ranges::copy(merkle.digest()->bytes(), hash.begin());
	
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
		uint8_t hash[Samurai::Crypto::Digest::TIGERSIZE];
		/* On the stack so that sizeof() below is the buffer's length rather
		 * than a pointer's, and so the 'continue' paths do not leak it. */
		char digest[HASH];

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
		if (!file.open(Samurai::IO::File::Mode::Read)) {
			fprintf(stderr, "Cannot open file: %s\n", argv[i]);
			continue;
		}
		
		uint8_t* buffer = new uint8_t[size];
		ssize_t sz = file.read((char*) buffer, size);
		
		if (use_tiger)
			hash_tiger(std::span(buffer, sz), hash);
		else {
			if (use_old_tth)
				hash_tth_old(std::span(buffer, sz), hash);
			else
				hash_tth_new(std::span(buffer, sz), hash, flag_tthl);
		}
		
		delete[] buffer;

		Samurai::Util::base32_encode(std::span<const unsigned char>(hash, Samurai::Crypto::Digest::TIGERSIZE), std::span<char>(digest, sizeof(digest)));
		printf("%s  %s\n", digest, argv[i]);
	}
	
}



// eof
