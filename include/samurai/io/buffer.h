/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_IO_BUFFER_H
#define HAVE_SAMURAI_IO_BUFFER_H

#include <samurai/samurai.h>
#include <string>
#include <string_view>
#include <vector>

#define INITBUFSIZE 8192

namespace Samurai {
namespace IO {

/**
 * A simple FIFO buffer class
 */
class Buffer {
	public:
		Buffer(size_t bufsize = INITBUFSIZE);
		Buffer(const Buffer* copy);
		Buffer(const Buffer& copy);
		Buffer(Buffer&& other) noexcept;
		Buffer& operator=(const Buffer& copy);
		Buffer& operator=(Buffer&& other) noexcept;
		virtual ~Buffer();
	
		enum BinaryMode { BigEndian, LittleEndian, NativeEndian };
		
	public:
	
		void append(const char* data, size_t len);
		void append(const char* string);
		void append(const std::string& string);
		void append(const std::string_view string);
		void append(char c);
		void append(int number);
		void append(uint64_t number);
		void append(Buffer* buffer, size_t len);
		void append(const Buffer& buffer);
		void append(const Buffer& buffer, size_t len);
		
		/**
		 * Append data binary as little endian
		 */
		void appendBinary(uint16_t number, BinaryMode endiannes = NativeEndian);
		void appendBinary(uint32_t number, BinaryMode endiannes = NativeEndian);
		void appendBinary(uint64_t number, BinaryMode endiannes = NativeEndian);
	
		/** Returned by the find() family when there is no match. */
		static const size_t npos = (size_t) -1;

		/**
		 * Copy the first 'len' bytes into a char array, or string.
		 * These copy without consuming; remove() is the consuming half.
		 *
		 * @return the number of bytes copied, which is less than 'len' when
		 *         the buffer holds fewer.
		 */
		size_t pop(char* data, size_t len);

		/**
		 * NOTE: The popBinary() family returns false and leaves 'number'
		 * untouched if fewer than sizeof(number) bytes are available at
		 * 'offset'. Always check the return value.
		 */
		
		/**
		 * Returns a string with the 'len' first bytes of the buffer.
		 */
		std::string pop(size_t len);

		size_t pop(char* data, size_t offset, size_t len);

		bool popBinary(size_t offset, uint8_t& number);
		bool popBinary(size_t offset, uint16_t& number, BinaryMode endianness = NativeEndian);
		bool popBinary(size_t offset, uint32_t& number, BinaryMode endianness = NativeEndian);
		bool popBinary(size_t offset, uint64_t& number, BinaryMode endianness = NativeEndian);
		
		// search/find, returning npos when there is no match.
		size_t find(char achar, size_t offset = 0);
		size_t rfind(char achar);
		size_t find(const char* str, size_t offset = 0);

		/**
		 * Allocate memory and return a chunk based on the
		 * given offset and length.
		 */
		char* memdup(size_t offset, size_t end);

		/**
		 * Returns a pointer to the data area.
		 * NOTE: Use with care!
		 */
		char* ptr();

		/**
		 * Returns the size of the buffer
		 */
		size_t size() const { return len - head; }

		/** Ensure room for 'n' more bytes without reallocating on append. */
		void reserve(size_t n);
		
		/**
		 * Remove the first n bytes from the buffer.
		 */ 
		void remove(size_t n);

		/**
		 * Returns the capasity of the buffer.
		 * The buffer is automatically resized, so this might not be very
		 * important.
		 */
		size_t capasity() const { return buf.size(); }
		
		/**
		 * Return the initial capasity of the buffer.
		 */
		size_t getInitialCapasity() const { return initialCapasity; }
		
		/**
		 * Clear the buffer and reset it to initial capasity
		 */
		void clear();

		/**
		 * Use with care, no boundary checks
		 */
		char operator[](size_t offset) const;

		/** Bounds checked; returns 0 past the end of the live data. */
		char at(size_t offset) const;
		
		
	protected:
		// Grow the buffer so that 'needed' more bytes fit.
		// Returns false (leaving the buffer untouched) if allocation fails.
		bool resize(size_t needed = 0);
		
	protected:
		/* Compact the consumed prefix away, so that head becomes 0. */
		void compact();

	protected:
		/* NOTE: was a malloc'd char* with no assignment operator, so assigning
		   one Buffer to another double freed. The vector owns the storage and
		   supplies the copy and move semantics. */
		std::vector<char> buf;

		/*
		 * Live bytes are [head, len). 'head' is what remove() advances instead
		 * of memmove()ing the remainder down: a protocol loop that consumes one
		 * message at a time used to move the whole rest of the buffer on every
		 * message, which is quadratic in the number of messages buffered.
		 */
		size_t head;
		size_t len;
		size_t initialCapasity; // the buffer's initial capasity
};

}
}
#endif // HAVE_SAMURAI_IO_BUFFER_H

