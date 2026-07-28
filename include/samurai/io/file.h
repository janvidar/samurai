/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_IO_FILE_H
#define HAVE_SAMURAI_IO_FILE_H

#include <samurai/samurai.h>
#include <samurai/timestamp.h>
#include <samurai/error.h>
#include <sys/stat.h>
#include <string>


namespace Samurai {
namespace IO {
class Buffer;

class FileBase;

class File {

	public:
		File();
		File(const std::string& path);
		File(const char* path);
		File(const File& file);
		File(const File* file);
		File(File&& other) noexcept;
		File& operator=(File&& other) noexcept;
		virtual ~File();
		
		enum Mode
		{
			Write     = 0x01, /**<<< "Open writing, the file will be created if it does not exist." */
			Read      = 0x02, /**<<< "Open for reading." */
			Append    = 0x04, /**<<< "Write flag: Write to file by appending to it" */
			Truncate  = 0x08, /**<<< "Write flag: File is truncated when opened for writing" */
			Exclusive = 0x10, /**<<< "Write flag: opening will fail if file already exists when trying to create it" */
			Paranoid  = 0x20, /**<<< "Don't follow symbolic links (if supported by operating system)" */
			NoAccess  = 0x40, /**<<< "Don't update access time of file (if supported by operating system). This can cause significant speedups under heavy disk load." */
		};
		
	public:
		/**
		 * @short Open a file using a specified access mode.
		 */
		virtual bool open(int mode = Read);

		/**
		 * Open, reporting why on failure.
		 *
		 * NOTE: the bool overload above cannot tell a missing file from a
		 * permission problem from a full descriptor table.
		 */
		virtual bool open(int mode, std::error_code& ec);
		
		/**
		 * @return true if the file is open.
		 */
		bool isOpen() const { return fd != -1; }
		
		/**
		 * @short Close the file
		 */
		virtual bool close();
		virtual bool close(std::error_code& ec);
		
		/**
		 * @short Flush any unwritten buffer data to disk.
		 */
		virtual bool flush();
		virtual bool flush(std::error_code& ec);
		
		/**
		 * @short Seek to a position inside an open file.
		 */
		virtual bool seek(off_t offset);
		virtual bool seek(off_t offset, std::error_code& ec);

		/**
		 * @short Get current position
		 */
		virtual off_t getCurrentPosition();

		/**
		 * @short delete the file
		 */
		virtual bool remove();
		virtual bool remove(std::error_code& ec);
		
		/**
		 * @short delete a file
		 */
		static bool remove(const char* path);

		/**
		 * Rename file to the given new file name.
		 * @return true if OK
		 */
		virtual bool rename(const std::string& new_name);
		virtual bool rename(const std::string& new_name, std::error_code& ec);
		
		// IO
		virtual ssize_t read(char* data, size_t length);
		virtual ssize_t write(const char* data, size_t length);

		/** @return bytes transferred, 0 at end of file, or -1 with 'ec' set. */
		virtual ssize_t read(char* data, size_t length, std::error_code& ec);
		virtual ssize_t write(const char* data, size_t length, std::error_code& ec);
		virtual ssize_t read(Samurai::IO::Buffer* data, size_t length = 65536);
		virtual ssize_t write(Samurai::IO::Buffer* data, size_t length = 65536, bool remove = true);
		
		// Returns the file size
		virtual off_t size() const;
		virtual mode_t getPermissions() const;
		
		// This is Unix-specific
		virtual uid_t getOwner() const;
		virtual gid_t getGroup() const;
		
		virtual bool isReadable() const;
		virtual bool isWritable() const;
		virtual bool isDeleteable() const;
		virtual bool isExcecutable() const;
		virtual bool isRegular() const;
		virtual bool isSymlink() const;
		virtual bool isDirectory() const;
		
		static bool exists(const char* file);
		virtual bool exists() const;
		
		virtual Samurai::TimeStamp getTimeAccessed() const;
		virtual Samurai::TimeStamp getTimeCreated() const;
		virtual Samurai::TimeStamp getTimeModified() const;
		
		virtual const std::string& getName() const { return filename; }
		virtual const std::string& getBaseName() const;

		/**
		 * Returns the file extension or an empty
		 * string if no extension can be found.
		 */
		virtual const std::string& getExtension() const;
		
		/**
		 * Returns true if the current file extension matches
		 * the supplied string. Matching is done case insensitive
		 * since file extensions most likely are.
		 * However, one exception is .c vs .C.
		 */
		virtual bool matchExtension(const std::string& ext) const;
		
		static int mkdir(const char* dirname, int mode = 0755);
		static int rmdir(const char* dirname);

		/**
		 * This will convert, resolve and shorten any path.
		 * 
		 * This handles a prepending '~' as the environment variable
		 * 'HOME' (or / if not set).
		 *
		 * The path is always simplified so redundant path separators
		 * are removed (example: /dir//file => /dir/file).
		 * 
		 * Also any /../ are followed, /./ are removed etc.
		 */
		static std::string resolvePath(const std::string& path);
		
		bool operator==(const File& file);
		
	private:
		void getInfo() const;

	public:
		
	protected:
		/* NOTE: was a mutable struct stat* with no assignment operator, so
		   assigning one File to another double freed it. Held by value with a
		   validity flag instead. */
		mutable bool info_valid;
		mutable struct stat info;
		std::string filename;
		mutable std::string baseName;
		mutable std::string temp;
		int fd;
};

}
}

#endif // HAVE_SAMURAI_IO_FILE_H

