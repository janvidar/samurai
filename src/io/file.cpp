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

Samurai::IO::File::File(const char* path) : info(0), temp(""), fd(-1)
{
	filename = std::string(resolvePath(path));
}


Samurai::IO::File::File(const std::string& path) : info(0), temp(""), fd(-1)
{
	filename = std::string(resolvePath(path.c_str()));
}


Samurai::IO::File::File(const Samurai::IO::File& copy) : info(0), temp(""), fd(-1)
{
	filename = std::string(copy.filename);
	if (copy.info)
	{
		info = new struct stat;
		memcpy(info, copy.info, sizeof(struct stat));
	}
}


Samurai::IO::File::File(const Samurai::IO::File* copy) : info(0), temp(""), fd(-1)
{
	filename = std::string(copy->filename);
	if (copy->info)
	{
		info = new struct stat;
		memcpy(info, copy->info, sizeof(struct stat));
	}
}


Samurai::IO::File::~File()
{
	close();
	delete info;
	info = 0;
}


void Samurai::IO::File::getInfo() const {
	delete info;
	info = 0;
	info = new struct stat;
	int retval = stat(filename.c_str(), info);
	if (retval == -1) {
		delete info;
		info = 0;
	}
}


bool Samurai::IO::File::open(int mode)
{
	int flags = 0;
	mode_t fmode = 0666;
	
	if (mode & Write && mode & Read)
	{
		flags |= O_RDWR;
	}
	else if (mode & Read)
	{
		flags |= O_RDONLY;
	}
	else if (mode & Write || mode & Append)
	{
		flags |= O_WRONLY;
		flags |= O_CREAT;
		
		if (mode & Append)
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
	std::string new_filename = std::string(resolvePath(new_name.c_str()));
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
	if (!info) getInfo();
	if (!info) return 0;
	return info->st_size;
}

mode_t Samurai::IO::File::getPermissions() const
{
	if (!info) getInfo();
	if (!info) return 0;
	return info->st_mode;
}

// This is Unix-specific
gid_t Samurai::IO::File::getOwner() const
{
	if (!info) getInfo();
	if (!info) return 0;
	return info->st_uid;
}

uid_t Samurai::IO::File::getGroup() const
{
	if (!info) getInfo();
	if (!info) return 0;
	return info->st_gid;
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
	if (!info) return 0;
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
	if (!info) getInfo();
	if (!info) return 0;
	return (S_ISREG(info->st_mode) == 1);
}

bool Samurai::IO::File::isSymlink() const
{
#ifndef SAMURAI_WINDOWS
	if (!info) getInfo();
	if (!info) return 0;
	return (S_ISLNK(info->st_mode) == 1);
#else
	return false;
#endif
}

bool Samurai::IO::File::isDirectory() const
{
	if (!info) getInfo();
	if (!info) return 0;
	return (S_ISDIR(info->st_mode) == 1);
}

bool Samurai::IO::File::exists(const char* path)
{
	struct stat info;
	int retval = stat(path, &info);
	return (retval != -1);
}

bool Samurai::IO::File::exists() const
{
	if (!info)
	{
		getInfo();
	}
	return (info != 0);
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
	if (!info) getInfo();
	if (!info) return 0;
	return Samurai::TimeStamp(info->st_ctime);
}


Samurai::TimeStamp Samurai::IO::File::getTimeModified() const
{
	if (!info) getInfo();
	if (!info) return 0;
	return Samurai::TimeStamp(info->st_mtime);
}


Samurai::TimeStamp Samurai::IO::File::getTimeAccessed() const
{
	if (!info) getInfo();
	if (!info) return 0;
	return Samurai::TimeStamp(info->st_atime);
}


int Samurai::IO::File::mkdir(const char* dirname, int mode)
{
	const char* dir = resolvePath(dirname);
#ifdef SAMURAI_UNIX
	return ::mkdir(dir, mode);
#endif

#ifdef SAMURAI_WINDOWS
	(void) mode; // Ignore mode
	return ::mkdir(dir);
#endif
}

int Samurai::IO::File::rmdir(const char* dirname)
{
	const char* dir = resolvePath(dirname);
	return ::rmdir(dir);
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

#define MAX_FILE_NAME 1024

#define SQUEEZE_LEFT(str, atpos, num) { \
	size_t t_len = strlen(str); \
	size_t n = atpos; \
	for (; n < t_len-num; n++) \
		str[n] = str[n+num]; \
	str[n] = '\0'; \
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

const char* Samurai::IO::File::resolvePath(const char* oldpath) {

// 	printf("Samurai::IO::File::resolvePath(): oldpath=%s\n", oldpath);

	static char path[MAX_FILE_NAME+2] = { 0, }; // FIXME: static
	static char copy[MAX_FILE_NAME+2] = { 0, }; // FIXME: static
	path[0] = '\0';
	copy[0] = '\0';

	size_t len = strlen(oldpath);
	if (len > MAX_FILE_NAME) len = MAX_FILE_NAME;
	strncat(path, oldpath, len);
	strcat(path, PATHSEP2);

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
			strcat(copy, prepend);
			strcat(copy, &path[1]);
			strcpy(path, copy);
		} else {
			SQUEEZE_LEFT(path, 0, 1);
		}
	}
	else if (path[0] != '/')
	{ // path is relative to working directory
		if (getcwd(copy, MAX_FILE_NAME)) {
			strcat(copy, PATHSEP2);
			strcat(copy, path);
			strcpy(path, copy);
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
			strcat(copy, tmp);
			strcat(copy, &path[1]);
			strcpy(path, copy);
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
		if (getcwd(copy, MAX_FILE_NAME)) {
			fix_backslash(copy);
			strcat(copy, PATHSEP2);
			strcat(copy, path);
			strcpy(path, copy);
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
	copy[0] = '\0';
	strcat(copy, PATHSEP2);
	strcat(copy, path);
	strcpy(path, copy);

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
	strcpy(copy, path);
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
			path[0] = '\0';
			strcat(path, copy);
			strcat(path, &pos[1]);
		} else {
			path[0] = '\0';
			strcat(path, &pos[1]);
		}
		
// 		printf("sqeeze    5: '%s'\n", path);
		strcpy(copy, path);
	}
	
	// remove any trailing /
	size_t n = strlen(path);
	if (path[n-1] == PATHSEP) path[n-1] = '\0';

	// make sure path is at least "/" if empty.
	if (!strlen(path)) {
		strcat(path, PATHSEP2);
	}

#ifdef SAMURAI_WINDOWS
	copy[0] = drive;
	copy[1] = ':';
	copy[2] = '\0';
	strcat(copy, path);
	// fix_slash(copy);
	strcpy(path, copy);
#endif

// 	printf("   -- result: %s\n", path);

	return path;
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


