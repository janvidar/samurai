/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#if defined(SAMURAI_UNIX) && !defined(SAMURAI_OS_MACOSX)
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#endif

#include <samurai/samurai.h>

#include <stdio.h>
#include <samurai/io/file.h>
#include <samurai/error.h>
#include <filesystem>
#include <samurai/io/buffer.h>

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#ifdef SAMURAI_WINDOWS
#include <io.h>
#include <algorithm>
#include <cctype>
#endif


#if defined(SAMURAI_BSD) || defined(SAMURAI_OS_SOLARIS)
#define SAMURAI_SEEK lseek
#endif

#if defined(SAMURAI_POSIX) && !defined(SAMURAI_BSD) && !defined(SAMURAI_OS_SOLARIS)
#define SAMURAI_SEEK lseek64
#endif

#if defined(SAMURAI_WINDOWS)
#define SAMURAI_SEEK _lseeki64
#endif

#if defined(SAMURAI_WINDOWS)
#define SAMURAI_UNLINK _unlink
#define SAMURAI_FSYNC  _commit
#else
#define SAMURAI_UNLINK unlink
#define SAMURAI_FSYNC  fsync
#endif

#if 0
namespace Samurai {
namespace IO {
	
class FileBase
{
	public:
		FileBase();
		~FileBase();
		
};

}
}
#endif

#define RETURN_IF_NOT_OPEN(X, VAL) if (X == -1) return VAL;

Samurai::IO::File::File(const char* path) : info_valid(false), temp(""), fd(-1)
{
	filename = resolvePath(std::string(path));
}


Samurai::IO::File::File(const std::string& path) : info_valid(false), temp(""), fd(-1)
{
	filename = resolvePath(path);
}


Samurai::IO::File::File(const Samurai::IO::File& copy) : info_valid(false), temp(""), fd(-1)
{
	filename = std::string(copy.filename);
	info_valid = copy.info_valid;
	if (info_valid) info = copy.info;
}


Samurai::IO::File::File(const Samurai::IO::File* copy) : info_valid(false), temp(""), fd(-1)
{
	filename = std::string(copy->filename);
	info_valid = copy->info_valid;
	if (info_valid) info = copy->info;
}


Samurai::IO::File::~File()
{
	close();
}


void Samurai::IO::File::getInfo() const {
	info_valid = (stat(filename.c_str(), &info) == 0);
}


bool Samurai::IO::File::open(int mode)
{
	std::error_code ec;
	return open(mode, ec);
}


bool Samurai::IO::File::open(int mode, std::error_code& ec)
{
	ec.clear();

	int flags = 0;
	mode_t fmode = 0666;
	
	const bool wants_write  = (mode & Write) != 0;
	const bool wants_read   = (mode & Read) != 0;
	const bool wants_append = (mode & Append) != 0;

	if ((wants_write || wants_append) && wants_read)
		flags |= O_RDWR;
	else if (wants_write || wants_append)
		flags |= O_WRONLY;
	else
		flags |= O_RDONLY;

	if (wants_write || wants_append)
	{
		flags |= O_CREAT;

		if (wants_append)
			flags |= O_APPEND;

		if (mode & Truncate)
			flags |= O_TRUNC;

		if (mode & Exclusive)
			flags |= O_EXCL;
	}

#ifdef O_NOFOLLOW
	if (mode & Paranoid)
		flags |= O_NOFOLLOW;
#endif
	
#ifdef O_NOATIME
	if (mode & NoAccess)
		flags |= O_NOATIME;
#endif

#ifdef _LARGEFILE64_SOURCE
	flags |= O_LARGEFILE;
#endif

	fd = ::open(filename.c_str(), flags, fmode);
	if (fd == -1)
	{
		ec = Samurai::system_error(errno);
		return false;
	}

	/* A previous stat() no longer describes what is now open. */
	info_valid = false;
	return true;
}


bool Samurai::IO::File::close()
{
	std::error_code ec;
	return close(ec);
}


bool Samurai::IO::File::close(std::error_code& ec)
{
	ec.clear();
	if (fd == -1) { ec = Samurai::system_error(EBADF); return false; }

	if (::close(fd) == -1)
	{
		ec = Samurai::system_error(errno);
		/* The descriptor is gone either way; holding on to it would mean
		   closing someone else's file on a later attempt. */
		fd = -1;
		return false;
	}

	fd = -1;
	return true;
}


bool Samurai::IO::File::rename(const std::string& new_name)
{
	std::error_code ec;
	return rename(new_name, ec);
}


bool Samurai::IO::File::rename(const std::string& new_name, std::error_code& ec)
{
	ec.clear();
	std::string new_filename = resolvePath(new_name);

	if (::rename(filename.c_str(), new_filename.c_str()) != 0)
	{
		ec = Samurai::system_error(errno);
		return false;
	}

	filename = new_filename;
	baseName = "";
	info_valid = false;
	return true;
}



bool Samurai::IO::File::seek(off_t offset)
{
	std::error_code ec;
	return seek(offset, ec);
}


bool Samurai::IO::File::seek(off_t offset, std::error_code& ec)
{
	ec.clear();
	if (fd == -1) { ec = Samurai::system_error(EBADF); return false; }

	if (SAMURAI_SEEK(fd, offset, SEEK_SET) == (off_t) -1)
	{
		ec = Samurai::system_error(errno);
		return false;
	}
	return true;
}


off_t Samurai::IO::File::getCurrentPosition()
{
	RETURN_IF_NOT_OPEN(fd, 0);
	return SAMURAI_SEEK(fd, 0, SEEK_CUR);
}


bool Samurai::IO::File::flush()
{
	std::error_code ec;
	return flush(ec);
}


bool Samurai::IO::File::flush(std::error_code& ec)
{
	ec.clear();
	if (fd == -1) { ec = Samurai::system_error(EBADF); return false; }

	if (SAMURAI_FSYNC(fd) == -1)
	{
		ec = Samurai::system_error(errno);
		return false;
	}
	return true;
}


ssize_t Samurai::IO::File::read(char* data, size_t length)
{
	std::error_code ec;
	return read(data, length, ec);
}


ssize_t Samurai::IO::File::read(char* data, size_t length, std::error_code& ec)
{
	ec.clear();
	if (fd == -1) { ec = Samurai::system_error(EBADF); return -1; }

	/* NOTE: was 'int status', truncating a ssize_t result. */
	ssize_t status = ::read(fd, data, length);
	if (status == -1) { ec = Samurai::system_error(errno); return -1; }
	return status;
}


ssize_t Samurai::IO::File::write(const char* data, size_t length)
{
	std::error_code ec;
	return write(data, length, ec);
}


ssize_t Samurai::IO::File::write(const char* data, size_t length, std::error_code& ec)
{
	ec.clear();
	if (fd == -1) { ec = Samurai::system_error(EBADF); return -1; }

	ssize_t status = ::write(fd, data, length);
	if (status == -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
		ec = Samurai::system_error(errno);
		return -1;
	}
	return status;
}


ssize_t Samurai::IO::File::read(Samurai::IO::Buffer* data, size_t length) {
	RETURN_IF_NOT_OPEN(fd, -1);

	char* buf = new char[length];
	int status = ::read(fd, buf, length);
	if (status == -1) {
		delete[] buf;
		return -1;
	} if (status == 0) {
		delete[] buf;
		return 0;
	} else {
		data->append(buf, (size_t) status);
		delete[] buf;
		return status;
	}
}

ssize_t Samurai::IO::File::write(Samurai::IO::Buffer* data, size_t length, bool remove) {
	RETURN_IF_NOT_OPEN(fd, -1);
	
	size_t len = length;
	if (len > data->size()) len = data->size();

	char* buf = new char[len];

	data->pop(buf, length);
	int status = ::write(fd, buf, len);
	if (status == -1) {
		delete[] buf;
		return (errno == EAGAIN) ? 0 : -1;
	}

	if (remove) data->remove((size_t) status);

	delete[] buf;
	return status;
}


off_t Samurai::IO::File::size() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return info.st_size;
}

mode_t Samurai::IO::File::getPermissions() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return info.st_mode;
}

// This is Unix-specific
gid_t Samurai::IO::File::getOwner() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return info.st_uid;
}

uid_t Samurai::IO::File::getGroup() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return info.st_gid;
}

bool Samurai::IO::File::isReadable() const
{
	return (access(getName().c_str(), R_OK) == 0);
}

bool Samurai::IO::File::isWritable() const
{
	return (access(getName().c_str(), W_OK) == 0);
}

bool Samurai::IO::File::isDeleteable() const
{
	return false;
}

bool Samurai::IO::File::isExcecutable()  const
{
	/* NOTE: neither branch was followed by a return, so a platform that is
	   neither ran off the end of a non-void function. */
#ifdef SAMURAI_WINDOWS
	/* Windows has no execute bit; the extension is what decides. Compared
	   case-insensitively, since "PROGRAM.EXE" is just as executable. */
	std::string ext = getExtension();
	for (std::string::size_type n = 0; n < ext.size(); n++)
		ext[n] = (char) tolower((unsigned char) ext[n]);

	return (ext == "exe" || ext == "bat" || ext == "cmd" || ext == "com");
#elif defined(SAMURAI_UNIX)
	return (access(getName().c_str(), X_OK) == 0);
#else
	std::error_code ec;
	const std::filesystem::perms p = std::filesystem::status(filename, ec).permissions();
	if (ec) return false;
	return (p & (std::filesystem::perms::owner_exec |
	             std::filesystem::perms::group_exec |
	             std::filesystem::perms::others_exec)) != std::filesystem::perms::none;
#endif
}

const std::string& Samurai::IO::File::getExtension() const
{
	size_t pos = filename.rfind('.');
	if (pos != std::string::npos)
		temp = filename.substr(pos+1);
	else
		temp = "";
	return temp;
}

bool Samurai::IO::File::matchExtension(const std::string& other) const
{
	const std::string& ext = getExtension();
	if (!ext.size() || !other.size())
		return false;
	return ext == other;
}

/* NOTE: S_ISREG and friends are only guaranteed non-zero, not 1, so the
   '== 1' these used to do was wrong on platforms that return the masked
   value. std::filesystem answers the question directly. */
bool Samurai::IO::File::isRegular() const
{
	std::error_code ec;
	return std::filesystem::is_regular_file(filename, ec) && !ec;
}

/* NOTE: this could never return true. It tested the cached stat(), which
   follows symlinks, so a link always reported as whatever it pointed at.
   is_symlink() has lstat() semantics. */
bool Samurai::IO::File::isSymlink() const
{
	std::error_code ec;
	return std::filesystem::is_symlink(std::filesystem::symlink_status(filename, ec)) && !ec;
}

bool Samurai::IO::File::isDirectory() const
{
	std::error_code ec;
	return std::filesystem::is_directory(filename, ec) && !ec;
}

bool Samurai::IO::File::exists(const char* path)
{
	struct stat info;
	int retval = stat(path, &info);
	return (retval != -1);
}

bool Samurai::IO::File::exists() const
{
	if (!info_valid) getInfo();
	return info_valid;
}

bool Samurai::IO::File::remove() {
	std::error_code ec;
	return remove(ec);
}


bool Samurai::IO::File::remove(std::error_code& ec) {
	ec.clear();
	if (SAMURAI_UNLINK(filename.c_str()) == -1)
	{
		ec = Samurai::system_error(errno);
		return false;
	}
	info_valid = false;
	return true;
}


bool Samurai::IO::File::remove(const char* path)
{
	/* NOTE: this called unlink() directly while the member remove() went
	   through SAMURAI_UNLINK, so the static overload alone failed to build
	   on Windows, where the name is _unlink. */
	if (!path) return false;
	return (SAMURAI_UNLINK(path) != -1);
}


Samurai::TimeStamp Samurai::IO::File::getTimeCreated() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return Samurai::TimeStamp(info.st_ctime);
}


Samurai::TimeStamp Samurai::IO::File::getTimeModified() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return Samurai::TimeStamp(info.st_mtime);
}


Samurai::TimeStamp Samurai::IO::File::getTimeAccessed() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return Samurai::TimeStamp(info.st_atime);
}


/*
 * NOTE: these were split across #ifdefs that named the POSIX functions on
 * Windows - ::mkdir there is _mkdir and takes no mode, and rmdir had no
 * Windows branch at all, so neither compiled. std::filesystem covers both.
 * Return values keep the ::mkdir convention: 0 on success, -1 on failure.
 */
int Samurai::IO::File::mkdir(const char* dirname, int mode)
{
	const std::string dir = resolvePath(dirname ? dirname : "");

	std::error_code ec;
	if (!std::filesystem::create_directory(dir, ec) || ec)
		return -1;

	/* Permission bits are a POSIX notion; on Windows this reduces to the
	   read-only flag, which is the closest thing available. */
	std::filesystem::permissions(dir, static_cast<std::filesystem::perms>(mode), ec);

	return 0;
}

int Samurai::IO::File::rmdir(const char* dirname)
{
	const std::string dir = resolvePath(dirname ? dirname : "");

	std::error_code ec;
	if (!std::filesystem::remove(dir, ec) || ec)
		return -1;

	return 0;
}

/**
 * This will convert any path given to the absolute path
 * and even follow symlinks.
 * 
 * This handles a prepending '~' as the environment variable
 * 'HOME' (or / if not set).
 *
 * The path is always simplified so redundant path separators
 * are removed (example: /dir//file => /dir/file).
 * 
 * Also any /../ are followed, /./ are removed etc.
 *
 * This is acheived in the following steps:
 * 1) Fix prepending '~' if it exists (FIXME: Does not work on Win32)
 * 2) Remove any multiple path separators to one.
 * 3) Resolve any /./ to '/'.
 * 4) Reesolve any '/../'
 * 5) Remove trailing '/' (if any).
 */

/**
 * NOTE: This was ~150 lines of hand-rolled path arithmetic over two fixed
 * buffers - the source of a global-buffer-overflow, a SQUEEZE_LEFT length
 * underflow and an out-of-bounds read on the empty path. std::filesystem does
 * the normalisation; the only thing it does not do is '~', so that stays.
 */
std::string Samurai::IO::File::resolvePath(const std::string& input) {
	std::string work = input;

	// Expand a leading '~' to the home directory.
	if (!work.empty() && work[0] == '~' && (work.size() == 1 || work[1] == '/'))
	{
#ifdef SAMURAI_WINDOWS
		const char* home = getenv("USERPROFILE");
#else
		const char* home = getenv("HOME");
#endif
		if (home)
			work = std::string(home) + work.substr(1);
		else
			work = work.substr(1);
	}

	std::error_code ec;
	std::filesystem::path p(work);

	/* absolute() needs the current directory, which can fail (a deleted cwd,
	   or one we cannot read). Fall back to lexical normalisation rather than
	   returning something half-resolved. */
	if (p.is_relative())
	{
		std::filesystem::path abs = std::filesystem::absolute(p, ec);
		if (!ec) p = abs;
	}

	std::string out = p.lexically_normal().string();

	/* lexically_normal() leaves a trailing separator on directory-like paths;
	   the previous implementation stripped it and callers compare these. */
	while (out.size() > 1 && (out[out.size()-1] == '/' || out[out.size()-1] == '\\'))
		out.erase(out.size()-1);

	if (out.empty()) out = "/";
	return out;
}


const std::string& Samurai::IO::File::getBaseName() const
{
	if (baseName != "")
		return baseName;
	
	size_t pos = filename.rfind('/');
	if (pos == std::string::npos) return filename;
	baseName = filename.substr(pos+1);
	return baseName;
}


bool Samurai::IO::File::operator==(const File& file)
{
	if (&file == this) return true;
	return file.filename == filename;
}


