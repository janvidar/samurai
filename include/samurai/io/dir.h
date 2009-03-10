/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_DIRECTORY_H
#define HAVE_QUICKDC_DIRECTORY_H

#include <samurai/samurai.h>

// FIXME: Remove all platform specifcs here!

#ifdef SAMURAI_UNIX
#include <sys/types.h>
#include <dirent.h>
#endif

#ifdef SAMURAI_OS_WINDOWS
#include <io.h>
#endif

namespace Samurai {
namespace IO {

class File;

/**
 * This is a directory iterator
 */
class Directory {
	public:
		Directory(const Samurai::IO::File* file);
		Directory(const std::string& path);
		Directory(const char* path);
		virtual ~Directory();

	public:
		bool open();
		void close();
	
		/* Returns the first file entry of the directory */
		Samurai::IO::File* first();
		/* Returns the last file entry of the directory */
		Samurai::IO::File* next();

	protected:
		Samurai::IO::File* file;
		Samurai::IO::File* iterator;
#ifdef 	SAMURAI_UNIX
		DIR* dir;
		struct dirent* entry;
#endif

#ifdef SAMURAI_OS_WINDOWS
		struct _finddata_t* dir;
#endif

};

}
}

#endif // HAVE_QUICKDC_DIRECTORY_H
