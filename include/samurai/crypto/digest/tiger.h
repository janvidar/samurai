/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_HASH_TIGER_H
#define HAVE_QUICKDC_HASH_TIGER_H

#include <samurai/samurai.h>
#include <samurai/crypto/digest/hash.h>

namespace Samurai {
namespace Crypto {
namespace Digest {

#define TIGER_HASH_SIZE 192/8

class Tiger : public Hash {
	public:
		Tiger();
		Tiger(size_t passes, size_t block_size);
		virtual ~Tiger();
		
		void reset();
		HashValue* digest();
		
	private:
		void pass(uint64_t& a, uint64_t& b, uint64_t& c, uint8_t mul);
		void round(uint64_t& a, uint64_t& b, uint64_t& c, uint64_t x, uint8_t mul);
		void key_schedule();
		void hash(uint8_t* data, size_t length);
		void internal_finalize();
		
	private:
		static uint64_t sboxes[1024];
		uint64_t result[3];
		const size_t m_passes;
		uint64_t X[8];
};

}
}
}




#endif // HAVE_QUICKDC_HASH_TIGER_H

