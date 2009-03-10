/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/file.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef SAMURAI_WINDOWS
#define PATH_PREFIX "C:"
#else
#define PATH_PREFIX ""
#endif

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
	Samurai::IO::File f("data");
	return f.exists();
});

EXO_TEST(file_exist_2, {
	Samurai::IO::File f("data/file1");
	return f.exists();
});

EXO_TEST(file_exist_3, {
	Samurai::IO::File f("data/file2");
	return !f.exists();
});

