/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/file.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <utility>

#ifdef SAMURAI_WINDOWS
#define PATH_PREFIX "C:"
#else
#define PATH_PREFIX ""
#endif

/* The fixtures under data/ are reached through a directory baked in at compile
   time, so the suite passes regardless of the working directory it is started
   from. The fallback suits the legacy Makefile here, which builds and runs
   autotest inside this directory. */
#ifndef EXOTIC_DATA_DIR
#define EXOTIC_DATA_DIR "."
#endif

#define EXOTIC_DATA_PATH(relative) EXOTIC_DATA_DIR "/" relative

EXO_TEST(file_1, {
	Samurai::IO::File f(PATH_PREFIX "/path/to/file/jalla");
	
	printf("%s: expect %s, got=%s\n", __FUNCTION__, "jalla", f.getBaseName().c_str());
	return f.getBaseName() == "jalla";
});

EXO_TEST(file_2, {
	Samurai::IO::File f(PATH_PREFIX "///path///to///file///jalla");
	printf("%s: expect %s, got=%s\n", __FUNCTION__, PATH_PREFIX "/path/to/file/jalla", f.getName().c_str());
	return f.getName() == PATH_PREFIX "/path/to/file/jalla";
});

EXO_TEST(file_3, {
	Samurai::IO::File f(PATH_PREFIX "/path/to/file/jalla/../../");
	return f.getName() == PATH_PREFIX "/path/to";
});

EXO_TEST(file_4, {
	Samurai::IO::File f(PATH_PREFIX "/home/user/../../");
	return f.getName() == PATH_PREFIX "/";
});

EXO_TEST(file_5, {
	Samurai::IO::File f(PATH_PREFIX "//home////.jalla////../janv/../janv/../janvidar/.quickdc//..");  // expecting: /home/janvidar
	return f.getName() == PATH_PREFIX "/home/janvidar";
});

EXO_TEST(file_6, {
	Samurai::IO::File f(PATH_PREFIX "//home/~////.jalla/////");	// expecting: /home/~/.jalla
	return f.getName() == PATH_PREFIX "/home/~/.jalla";
});

EXO_TEST(file_7, {
	Samurai::IO::File f("~");
	char* home = getenv("HOME");
	if (!home) return false;
	return f.getName() == home;
});

EXO_TEST(file_8, {
	char pwd[1024] = { 0 };
	memset(pwd, 0, 1024);
	if (!getcwd(pwd, 1024)) return false;
	Samurai::IO::File f("home/../");
	return f.getName() == pwd;
});

EXO_TEST(file_exist_1, {
	Samurai::IO::File f(EXOTIC_DATA_PATH("data"));
	return f.exists();
});

EXO_TEST(file_exist_2, {
	Samurai::IO::File f(EXOTIC_DATA_PATH("data/file1"));
	return f.exists();
});

EXO_TEST(file_exist_3, {
	Samurai::IO::File f(EXOTIC_DATA_PATH("data/file2"));
	return !f.exists();
});


EXO_TEST(file_extension_1, {
	Samurai::IO::File f("/tmp/archive.tar.gz");
	return f.getExtension() == "gz";
});

EXO_TEST(file_extension_2, {
	Samurai::IO::File f("/home/user.name/file");
	return f.getExtension() == "";
});

EXO_TEST(file_extension_3, {
	Samurai::IO::File f("/home/user.name/file.txt");
	return f.getExtension() == "txt";
});

EXO_TEST(file_extension_4, {
	Samurai::IO::File f("/home/user/.bashrc");
	return f.getExtension() == "";
});

EXO_TEST(file_extension_5, {
	Samurai::IO::File f("noextension");
	return f.getExtension() == "";
});

EXO_TEST(file_match_extension_1, {
	Samurai::IO::File f("/home/user.name/file");
	return !f.matchExtension("name/file");
});

EXO_TEST(file_move_construct, {
	Samurai::IO::File a(EXOTIC_DATA_PATH("data/file1"));
	const std::string name = a.getName();
	Samurai::IO::File b(std::move(a));
	return b.getName() == name && b.exists();
});

EXO_TEST(file_move_assign_open, {
	Samurai::IO::File a(EXOTIC_DATA_PATH("data/file1"));
	if (!a.open(Samurai::IO::File::Read)) return false;

	Samurai::IO::File b;
	b = std::move(a);

	char buf[8] = { 0 };
	ssize_t n = b.read(buf, 4);
	b.close();
	return n > 0;
});
