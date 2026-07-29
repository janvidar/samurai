/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_IO_FILE_H
#define HAVE_SAMURAI_IO_FILE_H

#include <samurai/samurai.h>
#include <samurai/bitmask.h>
#include <samurai/timestamp.h>
#include <samurai/error.h>
#include <sys/stat.h>
#include <optional>
#include <span>
#include <string>


namespace Samurai {
namespace IO {
class Buffer;

class FileBase;

class File final {

	public:
		File();
		File(const std::string& path);
		File(const char* path);
		File(const File& file);
		File(const File* file);
		File(File&& other) noexcept;
		File& operator=(File&& other) noexcept;
		~File();
		
		enum class Mode : unsigned
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
		bool open(Mode mode = Mode::Read);

		/**
		 * Open, reporting why on failure.
		 *
		 * NOTE: the bool overload above cannot tell a missing file from a
		 * permission problem from a full descriptor table.
		 */
		bool open(Mode mode, std::error_code& ec);
		
		/**
		 * @return true if the file is open.
		 */
		bool isOpen() const { return fd != -1; }
		
		/**
		 * @short Close the file
		 */
		bool close();
		bool close(std::error_code& ec);
		
		/**
		 * @short Flush any unwritten buffer data to disk.
		 */
		bool flush();
		bool flush(std::error_code& ec);
		
		/**
		 * @short Seek to a position inside an open file.
		 */
		bool seek(off_t offset);
		bool seek(off_t offset, std::error_code& ec);

		/**
		 * @short Get current position
		 */
		off_t getCurrentPosition();

		/**
		 * @short delete the file
		 */
		bool remove();
		bool remove(std::error_code& ec);
		
		/**
		 * @short delete a file
		 */
		static bool remove(const char* path);

		/**
		 * Rename file to the given new file name.
		 * @return true if OK
		 */
		bool rename(const std::string& new_name);
		bool rename(const std::string& new_name, std::error_code& ec);
		
		// IO
		ssize_t read(char* data, size_t length);
		ssize_t write(const char* data, size_t length);

		/**
		 * @return bytes transferred, 0 at end of file, or -1 with 'ec' set.
		 *
		 * read() may return less than asked for. write() places the whole
		 * extent unless the descriptor would block, so a short count from it
		 * means the caller should offer the remainder again.
		 */
		ssize_t read(char* data, size_t length, std::error_code& ec);
		ssize_t write(const char* data, size_t length, std::error_code& ec);
		ssize_t read(Samurai::IO::Buffer* data, size_t length = 65536);
		ssize_t write(Samurai::IO::Buffer* data, size_t length = 65536, bool remove = true);

		/** The same transfers with the extent carried by the argument. */
		ssize_t read(std::span<char> data)
		{ return read(data.data(), data.size()); }

		ssize_t write(std::span<const char> data)
		{ return write(data.data(), data.size()); }

		ssize_t read(std::span<char> data, std::error_code& ec)
		{ return read(data.data(), data.size(), ec); }

		ssize_t write(std::span<const char> data, std::error_code& ec)
		{ return write(data.data(), data.size(), ec); }
		
		// Returns the file size
		off_t size() const;
		mode_t getPermissions() const;
		
		// This is Unix-specific
		uid_t getOwner() const;
		gid_t getGroup() const;
		
		bool isReadable() const;
		bool isWritable() const;
		bool isDeleteable() const;
		bool isExcecutable() const;
		bool isRegular() const;
		bool isSymlink() const;
		bool isDirectory() const;
		
		static bool exists(const char* file);
		bool exists() const;
		
		Samurai::TimeStamp getTimeAccessed() const;
		Samurai::TimeStamp getTimeCreated() const;
		Samurai::TimeStamp getTimeModified() const;
		
		const std::string& getName() const { return filename; }
		const std::string& getBaseName() const;

		/**
		 * Returns the file extension or an empty
		 * string if no extension can be found.
		 */
		const std::string& getExtension() const;
		
		/**
		 * Returns true if the current file extension matches
		 * the supplied string. Matching is done case insensitive
		 * since file extensions most likely are.
		 * However, one exception is .c vs .C.
		 */
		bool matchExtension(const std::string& ext) const;
		
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
		mutable std::optional<struct stat> info;
		std::string filename;
		mutable std::string baseName;
		mutable std::string temp;
		int fd;
};

/* File::Mode is a flag set; see samurai/bitmask.h. */
SAMURAI_DECLARE_BITMASK(File::Mode)

}
}

#endif // HAVE_SAMURAI_IO_FILE_H

