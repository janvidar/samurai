/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_IO_DEVICE_H
#define HAVE_SAMURAI_IO_DEVICE_H

// #if 0

#include <samurai/samurai.h>
#include <string>

namespace Samurai {
namespace IO {

class Exception : public Samurai::Exception
{
	public:
		Exception() : Samurai::Exception("Samurai::IO::Exception") { }
	protected:
};

class Buffer;

class Device
{
	public:
		virtual ~Device() { }
		
	public:
		enum BinaryMode { BigEndian, LittleEndian, NativeEndian };
		
		/* Low-level read/write operations */
		virtual ssize_t read(uint8_t* data, size_t length) throw (Samurai::IO::Exception) = 0;
		virtual ssize_t write(const uint8_t* data, size_t length) throw (Samurai::IO::Exception) = 0;
		
	public:
		
	public:
		virtual ssize_t write(const char* string) throw (Samurai::IO::Exception);
		virtual void write(const std::string& string) throw (Samurai::IO::Exception);
		virtual void write(const Buffer& buffer, size_t len) throw (Samurai::IO::Exception);
		virtual void write(const Buffer* buffer, size_t len) throw (Samurai::IO::Exception);
		virtual void writeBinary(uint16_t number, BinaryMode endiannes = NativeEndian) throw (Samurai::IO::Exception);
		virtual void writeBinary(uint32_t number, BinaryMode endiannes = NativeEndian) throw (Samurai::IO::Exception);
		virtual void writeBinary(uint64_t number, BinaryMode endiannes = NativeEndian) throw (Samurai::IO::Exception);
		virtual void writeBinary(int16_t number, BinaryMode endiannes = NativeEndian) throw (Samurai::IO::Exception);
		virtual void writeBinary(int32_t number, BinaryMode endiannes = NativeEndian) throw (Samurai::IO::Exception);
		virtual void writeBinary(int64_t number, BinaryMode endiannes = NativeEndian) throw (Samurai::IO::Exception);
		virtual void writeText(uint8_t n) throw (Samurai::IO::Exception);
		virtual void writeText(uint16_t n) throw (Samurai::IO::Exception);
		virtual void writeText(uint32_t n) throw (Samurai::IO::Exception);
		virtual void writeText(uint64_t n) throw (Samurai::IO::Exception);
		virtual void writeText(int8_t n) throw (Samurai::IO::Exception);
		virtual void writeText(int16_t n) throw (Samurai::IO::Exception);
		virtual void writeText(int32_t n) throw (Samurai::IO::Exception);
		virtual void writeText(int64_t n) throw (Samurai::IO::Exception);
		
		
	public: /* Properties */
		/**
		 * @return true if we can write to this device.
		 */
		virtual bool isWritable() = 0;
		
		/**
		 * @return true if we can read from this device.
		 */
		virtual bool isReadable() = 0;
		
		/**
		 * @return true if we can obtain the size of this device. Typically only for files.
		 */
		virtual bool isSizeable();
		
		/**
		 * @return true if we can obtain seek this device. Typically only for files.
		 */
		virtual bool isSeekable();
	
		/**
		 * @return true if device is open (or socket is connected).
		 */
		virtual bool isOpen();
	
	
	protected:

};

}
}

// #endif // 0

#endif // HAVE_SAMURAI_IO_DEVICE_H

