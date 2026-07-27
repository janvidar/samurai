/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/dir.h>
#include <samurai/io/file.h>

#include <string>

#ifdef SAMURAI_OS_WINDOWS
#include <io.h>
#endif

Samurai::IO::Directory::Directory(const Samurai::IO::File* file_) {
	file = new Samurai::IO::File(*file_);

#ifdef SAMURAI_UNIX
	dir = 0;
	entry_data = 0;
#endif
}


Samurai::IO::Directory::Directory(const std::string& path) {
	file = new Samurai::IO::File(path);

#ifdef SAMURAI_UNIX
	dir = 0;
	entry_data = 0;
#endif
}

Samurai::IO::Directory::Directory(const char* path) {
	file = new Samurai::IO::File(path);

#ifdef SAMURAI_UNIX
	dir = 0;
	entry_data = 0;
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
#endif

#ifdef SAMURAI_OS_WINDOWS
	// FIXME: Not implemented
#endif
}

bool Samurai::IO::Directory::first(Samurai::IO::File& entry) {
#ifdef SAMURAI_UNIX
	if (!dir) return false;
	::rewinddir(dir);
	return next(entry);
#else
	(void) entry;
	return false;
#endif
}

bool Samurai::IO::Directory::next(Samurai::IO::File& entry) {
#ifdef SAMURAI_UNIX
	if (!dir) return false;

	for (;;) {
		entry_data = readdir(dir);
		if (!entry_data) return false;

		if ((strcmp(entry_data->d_name, ".") == 0) || (strcmp(entry_data->d_name, "..") == 0))
			continue;

		entry = Samurai::IO::File(file->getName() + "/" + entry_data->d_name);
		return true;
	}
#endif

#ifdef SAMURAI_OS_WINDOWS
	// FIXME: Not implemented
	(void) entry;
	return false;
#endif
}
