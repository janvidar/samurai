/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_IO_STREAM_H
#define HAVE_SAMURAI_IO_STREAM_H

#include <samurai/samurai.h>

namespace Samurai {
namespace IO {

class Stream
{
	public:
		enum Type
		{
			File,
			Socket,
			Pipe,
			Buffer,
		};
		
		enum Flags
		{
			Read      = 0x01,
			Write     = 0x02,
			ReadWrite = Read | Write,
			Seekable  = 0x04,
		};
	
		enum BinaryMode
		{
			BigEndian,
			LittleEndian,
			NativeEndian
		};

		
	protected:
		Stream(int flags);
		virtual ~Stream();
		
	public:
		
		/* IO */
		virtual ssize_t read(char* data, size_t length);
		virtual ssize_t write(const char* data, size_t length);
		
		
		/* Seeking */
		virtual size_t size();
		virtual bool setPosition(uint64_t offset);
		virtual uint64_t getPosition();
		
		/*
		 * Some devices cannot be flushed, such as sockets, and
		 * thus always returns false.
		 */
		virtual bool flush();
		
		/*
		 * Returns true if the stream can be seeked.
		 * If a device is not seekable getPosition() and size() will always return 0,
		 * and setPosition() will always return false.
		 */
		virtual bool isSeekable() const;
		
		/*
		 * Returns true if open for reading
		 */
		virtual bool isReadable() const;
		
		/*
		 * Returns true if the stream is open for writing.
		 */
		virtual bool isWritable() const;
		
	public:
		/* Writing */
		ssize_t write(const char* str);
		ssize_t write(const std::string& str);
		ssize_t write(char chr);
		ssize_t writeText(uint8_t num);
		ssize_t writeText(uint16_t num);
		ssize_t writeText(uint32_t num);
		ssize_t writeText(uint64_t num);
		ssize_t writeText(int8_t num);
		ssize_t writeText(int16_t num);
		ssize_t writeText(int32_t num);
		ssize_t writeText(int64_t num);
		ssize_t writeBinary(uint16_t num, BinaryMode endian = NativeEndian);
		ssize_t writeBinary(uint32_t num, BinaryMode endian = NativeEndian);
		ssize_t writeBinary(uint64_t num, BinaryMode endian = NativeEndian);
		ssize_t writeBinary(int16_t num, BinaryMode endian = NativeEndian);
		ssize_t writeBinary(int32_t num, BinaryMode endian = NativeEndian);
		ssize_t writeBinary(int64_t num, BinaryMode endian = NativeEndian);
		
		/* Reading */
		
		
		/**
		 * Append data binary as little endian
		 */
		
	protected:
		int flags;
		
};

}
}

#endif // HAVE_SAMURAI_IO_STREAM_H
