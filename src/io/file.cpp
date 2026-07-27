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
	RETURN_IF_NOT_OPEN(fd, false);
	return true;
}


bool Samurai::IO::File::close()
{
	if (fd != -1)
	{
		int retval = ::close(fd);
		if (retval == -1) return false;
		fd = -1;
		return true;
	}
	return false;
}


bool Samurai::IO::File::rename(const std::string& new_name)
{
	std::string new_filename = resolvePath(new_name);
	int ret = ::rename(filename.c_str(), new_filename.c_str());
	if (ret == 0)
	{
		filename = new_filename;
		return true;
	}
	
	return false;
}



bool Samurai::IO::File::seek(off_t offset)
{
	RETURN_IF_NOT_OPEN(fd, false);

	if (SAMURAI_SEEK(fd, offset, SEEK_SET) == (off_t)-1)
		return false;
	return true;
}


off_t Samurai::IO::File::getCurrentPosition()
{
	RETURN_IF_NOT_OPEN(fd, 0);
	return SAMURAI_SEEK(fd, 0, SEEK_CUR);
}


bool Samurai::IO::File::flush()
{
	RETURN_IF_NOT_OPEN(fd, false);
	return (SAMURAI_FSYNC(fd) != -1);
}


ssize_t Samurai::IO::File::read(char* data, size_t length)
{
	RETURN_IF_NOT_OPEN(fd, -1);
	int status = ::read(fd, data, length);
	if (status == -1) return -1;
	if (status == 0)  return 0;
	return status;
}


ssize_t Samurai::IO::File::write(const char* data, size_t length)
{
	RETURN_IF_NOT_OPEN(fd, -1);
	int status = ::write(fd, data, length);
	if (status == -1) return (errno == EAGAIN) ? 0 : -1;

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
#ifdef SAMURAI_WINDOWS
	return (matchExtension("exe") || matchExtension("bat") || matchExtension("com"));
#endif

#ifdef SAMURAI_UNIX
	return (access(getName().c_str(), X_OK) == 0);
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

bool Samurai::IO::File::isRegular() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return (S_ISREG(info.st_mode) == 1);
}

bool Samurai::IO::File::isSymlink() const
{
#ifndef SAMURAI_WINDOWS
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return (S_ISLNK(info.st_mode) == 1);
#else
	return false;
#endif
}

bool Samurai::IO::File::isDirectory() const
{
	if (!info_valid) getInfo();
	if (!info_valid) return 0;
	return (S_ISDIR(info.st_mode) == 1);
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
	return (SAMURAI_UNLINK(filename.c_str()) != -1);
}


bool Samurai::IO::File::remove(const char* path)
{
	return (unlink(path) != -1);
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


int Samurai::IO::File::mkdir(const char* dirname, int mode)
{
	const std::string dir = resolvePath(dirname ? dirname : "");
#ifdef SAMURAI_UNIX
	return ::mkdir(dir.c_str(), mode);
#endif

#ifdef SAMURAI_WINDOWS
	(void) mode; // Ignore mode
	return ::mkdir(dir.c_str());
#endif
}

int Samurai::IO::File::rmdir(const char* dirname)
{
	const std::string dir = resolvePath(dirname ? dirname : "");
	return ::rmdir(dir.c_str());
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

#define PATHSEP '/'
#define PATHSEP2 "/"
#define PATHSEP_DOUBLE "//"
#define PATHSEP_NULL "/./"
#define PATHSEP_UP "/../"

#define MAX_FILE_NAME 4096

// The working buffers hold the input plus a getcwd() or HOME prefix plus a
// separator, so they need room for two full names rather than one.
#define PATH_BUF_SIZE (MAX_FILE_NAME * 2 + 16)

// t_len < num underflows and runs off the end of the buffer.
#define SQUEEZE_LEFT(str, atpos, num) { \
	size_t t_len = strlen(str); \
	size_t n = atpos; \
	if (t_len >= (size_t) (num)) { \
		for (; n < t_len-(num); n++) \
			str[n] = str[n+(num)]; \
		str[n] = '\0'; \
	} \
}

// Bounded replacements for the strcat()/strcpy() calls in resolvePath().
// Every one of those appended caller or environment supplied data to a fixed
// buffer without checking the room left in it.
static void path_append(char* dst, size_t dstsize, const char* src)
{
	if (!dstsize || !src) return;

	size_t used = strlen(dst);
	if (used + 1 >= dstsize) return;

	size_t avail = dstsize - used - 1;
	size_t len = strlen(src);
	if (len > avail) len = avail;

	memcpy(&dst[used], src, len);
	dst[used + len] = '\0';
}

static void path_set(char* dst, size_t dstsize, const char* src)
{
	if (!dstsize) return;
	dst[0] = '\0';
	path_append(dst, dstsize, src);
}

#ifdef SAMURAI_WINDOWS
char* fix_backslash(char* path)
{
	for (size_t n = 0; n < strlen(path); n++)
	{
		if (path[n] == '\\')
			path[n] = '/';
	}
	return path;
}

char* fix_slash(char* path)
{
	for (size_t n = 0; n < strlen(path); n++)
	{
		if (path[n] == '/')
			path[n] = '\\';
	}
	return path;
}

#endif

std::string Samurai::IO::File::resolvePath(const std::string& input) {

// 	printf("Samurai::IO::File::resolvePath(): oldpath=%s\n", oldpath);

	/* NOTE: These were static, so the returned pointer aliased across calls
	   and the function was neither reentrant nor thread safe. They are locals
	   now and the result is returned by value. */
	char path[PATH_BUF_SIZE] = { 0, };
	char copy[PATH_BUF_SIZE] = { 0, };

	const char* oldpath = input.c_str();

	size_t len = input.size();
	if (len > MAX_FILE_NAME) len = MAX_FILE_NAME;
	memcpy(path, oldpath, len);
	path[len] = '\0';
	path_append(path, sizeof(path), PATHSEP2);

#ifdef SAMURAI_WINDOWS
	char drive = 0;
	fix_backslash(path);
#endif

#ifdef SAMURAI_UNIX
	// If the path starts with a '~', replace it with the home directory.
	if (path[0] == '~' && (path[1] == '/' || path[1] == PATHSEP))
	{
		char* prepend = getenv("HOME");
		if (prepend) {
			path_set(copy, sizeof(copy), prepend);
			path_append(copy, sizeof(copy), &path[1]);
			path_set(path, sizeof(path), copy);
		} else {
			SQUEEZE_LEFT(path, 0, 1);
		}
	}
	else if (path[0] != '/')
	{ // path is relative to working directory
		if (getcwd(copy, sizeof(copy))) {
			path_append(copy, sizeof(copy), PATHSEP2);
			path_append(copy, sizeof(copy), path);
			path_set(path, sizeof(path), copy);
		}
	}
	
#endif
#ifdef SAMURAI_WINDOWS
	// If the path starts with a '~', replace it with the home directory.
	if (path[0] == '~' && (path[1] == '/' || path[1] == PATHSEP))
	{
		char* prepend = getenv("USERPROFILE");
		if (prepend)
		{
			char* tmp = strdup(prepend);
			fix_backslash(tmp);
			path_set(copy, sizeof(copy), tmp);
			path_append(copy, sizeof(copy), &path[1]);
			path_set(path, sizeof(path), copy);
			free(tmp);
		} else {
			SQUEEZE_LEFT(path, 0, 1);
		}
	}
// 	if (path[1] == ':' && (path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z'))
// 	{
// 		drive = path[0];
// 		SQUEEZE_LEFT(path, 0, 2);	
// 	}
	else if (path[0] != '/' && path[1] != ':')
	{
		if (getcwd(copy, sizeof(copy))) {
			fix_backslash(copy);
			path_append(copy, sizeof(copy), PATHSEP2);
			path_append(copy, sizeof(copy), path);
			path_set(path, sizeof(path), copy);
		}
	}

#endif


#ifdef SAMURAI_WINDOWS
 	if (path[1] == ':' && (path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z'))
 	{
 		drive = path[0];
 		SQUEEZE_LEFT(path, 0, 2);	
	} else {
		char* sysdrive = getenv("SamuraiDrive");
		if (sysdrive)
		{
			drive = sysdrive[0];
		} else {
			QERR("Unable to detect system drive. Assuming C:");
			drive = 'C'; // Last resort
		}
	}
#endif

	// Add a leading '/' (bodge)
	path_set(copy, sizeof(copy), PATHSEP2);
	path_append(copy, sizeof(copy), path);
	path_set(path, sizeof(path), copy);

	// replace any '//' with '/'.
	char* pos = 0;
	while ((pos = strstr(path, PATHSEP_DOUBLE))) {
		SQUEEZE_LEFT(pos, 0, 1);
	}
	
	// replace any '/./' with '/'.
	while ((pos = strstr(path, PATHSEP_NULL))) {
		SQUEEZE_LEFT(pos, 0, 2);
	}

	// figure out the real path when we have "/../" in the path.
	// printf("sqeeze    0: '%s'\n", oldpath);
	path_set(copy, sizeof(copy), path);
	while ((pos = strstr(copy, PATHSEP_UP))) {
//		printf("sqeeze    1: '%s'\n", pos);
		SQUEEZE_LEFT(pos, 0, 3); /* Remove "/.." keep the '/' */
// 		printf("sqeeze    2: '%s'\n", pos);
		pos[0] = '\0';           /* Turn the '/' into '\0'    */
		
// 		printf("sqeeze    3: '%s'\n", copy);
		
		char* npos = strrchr(copy, PATHSEP);
		if (npos) {
// 			printf("sqeeze 1  4: '%s'\n", npos);
			npos[1] = '\0';
			path_set(path, sizeof(path), copy);
			path_append(path, sizeof(path), &pos[1]);
		} else {
			path_set(path, sizeof(path), &pos[1]);
		}
		
// 		printf("sqeeze    5: '%s'\n", path);
		path_set(copy, sizeof(copy), path);
	}
	
	// remove any trailing /  (n == 0 reads and writes path[-1])
	size_t n = strlen(path);
	if (n && path[n-1] == PATHSEP) path[n-1] = '\0';

	// make sure path is at least "/" if empty.
	if (!strlen(path)) {
		path_append(path, sizeof(path), PATHSEP2);
	}

#ifdef SAMURAI_WINDOWS
	copy[0] = drive;
	copy[1] = ':';
	copy[2] = '\0';
	path_append(copy, sizeof(copy), path);
	// fix_slash(copy);
	path_set(path, sizeof(path), copy);
#endif

// 	printf("   -- result: %s\n", path);

	return std::string(path);
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


