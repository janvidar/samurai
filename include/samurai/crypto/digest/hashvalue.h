/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_HASH_VALUE_H
#define HAVE_SAMURAI_HASH_VALUE_H

#include <samurai/samurai.h>
#include <vector>

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
		enum class Format { Hex, Base32 };

	public:
		explicit HashValue(HashValue* copy);
		explicit HashValue(size_t size);
		HashValue(size_t size, const uint8_t* data);

		/*
		 * The vector owns the digest bytes and supplies copy, move and
		 * destruction, so none of them has to be written out here.
		 */
		HashValue(const HashValue& copy) = default;
		HashValue(HashValue&& other) noexcept = default;
		HashValue& operator=(const HashValue& copy) = default;
		HashValue& operator=(HashValue&& other) noexcept = default;
		virtual ~HashValue() = default;

		size_t size() const;
		uint8_t* getData();
		const uint8_t* getData() const;

		/** Overwrites the existing bytes; the size is fixed at construction. */
		void setData(const uint8_t*);

		/**
		 * This will return a printable string of the hash.
		 *
		 * @return false if buf/buflen is not large enough to hold the string.
		 */
		bool getFormattedString(enum Format, char* buf, size_t buflen) const;

		bool operator==(const HashValue&) const;

	protected:
		HashValue();

		std::vector<uint8_t> m_data;

	friend class Hash;
	friend class Tiger;
};

}
}
}

#endif // HAVE_SAMURAI_HASH_VALUE_H
