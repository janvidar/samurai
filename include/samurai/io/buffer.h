/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_IO_BUFFER_H
#define HAVE_SAMURAI_IO_BUFFER_H

#include <samurai/samurai.h>
#include <string>

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
		virtual ~Buffer();
	
		enum BinaryMode { BigEndian, LittleEndian, NativeEndian };
		
	public:
	
		void append(const char* data, size_t len);
		void append(const char* string);
		void append(const std::string& string);
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
	
		/**
		 * Copy the first 'len' bytes into a char array, 
		 * or string.
		 */
		void pop(char* data, size_t len);
		
		/**
		 * Returns a string with the 'len' first bytes of the buffer.
		 */
		std::string pop(size_t len);

		void pop(char* data, size_t offset, size_t len);

		bool popBinary(size_t offset, uint8_t& number);
		bool popBinary(size_t offset, uint16_t& number, BinaryMode endianness = NativeEndian);
		bool popBinary(size_t offset, uint32_t& number, BinaryMode endianness = NativeEndian);
		bool popBinary(size_t offset, uint64_t& number, BinaryMode endianness = NativeEndian);
		
		// search/find
		int find(char achar, size_t offset = 0);
		int rfind(char achar);
		int find(const char* str, size_t offset = 0);

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
		size_t size() const { return len; }
		
		/**
		 * Remove the first n bytes from the buffer.
		 */ 
		void remove(size_t n);

		/**
		 * Returns the capasity of the buffer.
		 * The buffer is automatically resized, so this might not be very
		 * important.
		 */
		size_t capasity() const { return bufsize; }
		
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

		char at(size_t offset) const;
		
		
	protected:
		// grow buffer if capacity is exceeded
		void resize(size_t newsize = 0);
		
	protected:
		char* buf;
		size_t len;             // bytes stored in the buffer
		size_t bufsize;         // the buffer capasity
		size_t initialCapasity; // the buffer's initial capasity
};

}
}
#endif // HAVE_SAMURAI_IO_BUFFER_H

