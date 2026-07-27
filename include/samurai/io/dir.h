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
	
		/**
		 * Rewind and read the first entry into 'entry'.
		 * @return false if the directory is not open or is empty.
		 */
		bool first(Samurai::IO::File& entry);

		/**
		 * Read the next entry into 'entry'.
		 * @return false when the directory is exhausted.
		 *
		 * NOTE: these used to return a File* that the Directory owned and
		 * deleted on the following call, so the caller's pointer was
		 * invalidated by the act of continuing to iterate.
		 */
		bool next(Samurai::IO::File& entry);

	protected:
		Samurai::IO::File* file;
#ifdef 	SAMURAI_UNIX
		DIR* dir;
		struct dirent* entry_data;
#endif

#ifdef SAMURAI_OS_WINDOWS
		struct _finddata_t* dir;
#endif


	private:
		/* Owns 'file' and 'iterator': copying would double free both. */
		Directory(const Directory&);
		Directory& operator=(const Directory&);
};

}
}

#endif // HAVE_QUICKDC_DIRECTORY_H
