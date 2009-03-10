/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#define MAX_FILE_NAME 1024

#include <samurai/io/dir.h>
#include <samurai/io/file.h>

#ifdef SAMURAI_OS_WINDOWS
#include <io.h>
#endif

Samurai::IO::Directory::Directory(const Samurai::IO::File* file_) {
	file = new Samurai::IO::File(*file_);
	iterator = 0;

#ifdef SAMURAI_UNIX
	dir = 0;
	entry = 0;
#endif
}


Samurai::IO::Directory::Directory(const std::string& path) {
	file = new Samurai::IO::File(path);
	iterator = 0;

#ifdef SAMURAI_UNIX
	dir = 0;
	entry = 0;
#endif
}

Samurai::IO::Directory::Directory(const char* path) {
	file = new Samurai::IO::File(path);
	iterator = 0;

#ifdef SAMURAI_UNIX
	dir = 0;
	entry = 0;
#endif
}

Samurai::IO::Directory::~Directory() {
	close();
	delete file;
}

bool Samurai::IO::Directory::open() {
#ifdef SAMURAI_UNIX
	if (dir) {
		close();
	}

	dir = opendir(file->getName().c_str());
	if (!dir) return false;
	return true;
#endif

#ifdef SAMURAI_OS_WINDOWS
	// FIXME: Not implemented
	return false;
#endif
}

void Samurai::IO::Directory::close() {
#ifdef SAMURAI_UNIX
	if (dir)
		::closedir(dir);
	dir = 0;

	delete iterator;
	iterator = 0;
#endif

#ifdef SAMURAI_OS_WINDOWS
	// FIXME: Not implemented
#endif
}

Samurai::IO::File* Samurai::IO::Directory::first() {
	return next();
}

Samurai::IO::File* Samurai::IO::Directory::next() {
#ifdef SAMURAI_UNIX
	delete iterator; iterator = 0;	
	if (!dir) return 0;
	for (;;) {
		entry = readdir(dir);
		if (!entry) return 0;
		if (
#if !defined(SAMURAI_OS_SOLARIS)
			(entry->d_type == DT_DIR) &&
#endif
			((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
			)
			continue;

		char temp_filename[MAX_FILE_NAME + 1] = { 0, };
		strcat(temp_filename, file->getName().c_str());
		strcat(temp_filename, "/");
		strcat(temp_filename, entry->d_name);

		iterator = new Samurai::IO::File(temp_filename);
		return iterator;
	}
#endif

#ifdef SAMURAI_OS_WINDOWS
	// FIXME: Not implemented
	return 0;
#endif
}
