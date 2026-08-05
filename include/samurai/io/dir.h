/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_DIRECTORY_H
#define HAVE_SAMURAI_DIRECTORY_H

#include <samurai/samurai.h>

#include <filesystem>
#include <optional>
#include <string>

#include <samurai/io/file.h>

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
		~Directory();

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
		 */
		bool next(Samurai::IO::File& entry);

		/**
		 * The same iteration without an out-parameter: the result is empty
		 * when the directory is not open, or exhausted.
		 */
		std::optional<Samurai::IO::File> first();
		std::optional<Samurai::IO::File> next();

	protected:
		std::string path;
		std::filesystem::directory_iterator iter;
		bool opened;
};

}
}

#endif // HAVE_SAMURAI_DIRECTORY_H
