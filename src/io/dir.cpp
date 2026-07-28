/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/dir.h>
#include <samurai/io/file.h>

#include <filesystem>
#include <string>
#include <system_error>

Samurai::IO::Directory::Directory(const Samurai::IO::File* file_) : opened(false) {
	path = file_ ? file_->getName() : std::string();
}


Samurai::IO::Directory::Directory(const std::string& path_) : opened(false) {
	path = Samurai::IO::File(path_).getName();
}


Samurai::IO::Directory::Directory(const char* path_) : opened(false) {
	path = Samurai::IO::File(path_ ? path_ : "").getName();
}


Samurai::IO::Directory::~Directory() {
	close();
}


bool Samurai::IO::Directory::open() {
	close();

	std::error_code ec;
	iter = std::filesystem::directory_iterator(path, ec);
	if (ec) return false;

	opened = true;
	return true;
}


void Samurai::IO::Directory::close() {
	iter = std::filesystem::directory_iterator();
	opened = false;
}


bool Samurai::IO::Directory::first(Samurai::IO::File& entry) {
	if (!opened) return false;

	std::error_code ec;
	iter = std::filesystem::directory_iterator(path, ec);
	if (ec) return false;

	return next(entry);
}


bool Samurai::IO::Directory::next(Samurai::IO::File& entry) {
	if (!opened) return false;

	const std::filesystem::directory_iterator end;
	if (iter == end) return false;

	/* directory_iterator never yields "." or "..", so the name filtering the
	   readdir() loop needed is gone too. */
	entry = Samurai::IO::File(iter->path().string());

	std::error_code ec;
	iter.increment(ec);
	if (ec) iter = end;

	return true;
}


std::optional<Samurai::IO::File> Samurai::IO::Directory::first() {
	if (!opened) return std::nullopt;

	std::error_code ec;
	iter = std::filesystem::directory_iterator(path, ec);
	if (ec) return std::nullopt;

	return next();
}


std::optional<Samurai::IO::File> Samurai::IO::Directory::next() {
	Samurai::IO::File entry;
	if (!next(entry)) return std::nullopt;
	return entry;
}
