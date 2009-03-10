/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_HASH_VALUE_H
#define HAVE_QUICKDC_HASH_VALUE_H

#include <samurai/samurai.h>

namespace Samurai {
namespace Crypto {
namespace Digest {

/**
 * This represents a binary form of a hash value.
 * This type of object is created by a hash algorithm.
 * It can conveniently be converted to printable form.
 */
class HashValue
{
	public:
		enum Format { FormatHex, FormatBase32 };
	
	public:
		HashValue(HashValue* copy);
		HashValue(const HashValue& copy);
		HashValue(size_t size);
		HashValue(size_t size, uint8_t* data);
		virtual ~HashValue();
		
		size_t size() const;
		uint8_t* getData();
		void setData(const uint8_t*);
		
		/**
		 * This will return a printable string of the hash.
		 *
		 * @return false if buf/buflen is not large enough to hold the string.
		 */
		bool getFormattedString(enum Format, char* buf, size_t buflen);
		
		
		HashValue& operator=(const HashValue& copy);
		bool operator==(const HashValue&);
		bool operator!=(const HashValue&);
	
	protected:
		HashValue();
		
		uint8_t* m_data;
		size_t m_size;
		
		
	friend class Hash;
	friend class Tiger;
};

}
}
}

#endif // HAVE_QUICKDC_HASH_VALUE_H
