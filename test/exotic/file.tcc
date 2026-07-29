/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <system_error>
#include <vector>
#include <string>
#include <samurai/io/file.h>
#include <samurai/io/dir.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <utility>
#include <span>

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
	Samurai::IO::File f(PATH_PREFIX "//home////.jalla////../janv/../janv/../janvidar/.samurai//..");  // expecting: /home/janvidar
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
	if (!a.open(Samurai::IO::File::Mode::Read)) return false;

	Samurai::IO::File b;
	b = std::move(a);

	char buf[8] = { 0 };
	ssize_t n = b.read(buf, 4);
	b.close();
	return n > 0;
});

EXO_TEST(dir_iterate_out_param, {
	Samurai::IO::Directory dir(EXOTIC_DATA_PATH("data"));
	if (!dir.open()) return false;

	size_t count = 0;
	Samurai::IO::File entry;
	for (bool ok = dir.first(entry); ok; ok = dir.next(entry))
		count++;
	return count > 0;
});

EXO_TEST(dir_iterate_optional, {
	Samurai::IO::Directory dir(EXOTIC_DATA_PATH("data"));
	if (!dir.open()) return false;

	size_t count = 0;
	for (auto entry = dir.first(); entry; entry = dir.next())
		count++;
	return count > 0;
});

EXO_TEST(dir_iterate_counts_agree, {
	Samurai::IO::Directory a(EXOTIC_DATA_PATH("data"));
	Samurai::IO::Directory b(EXOTIC_DATA_PATH("data"));
	if (!a.open() || !b.open()) return false;

	size_t na = 0;
	size_t nb = 0;
	Samurai::IO::File entry;
	for (bool ok = a.first(entry); ok; ok = a.next(entry)) na++;
	for (auto e = b.first(); e; e = b.next()) nb++;
	return na == nb && na > 0;
});

EXO_TEST(dir_iterate_unopened, {
	Samurai::IO::Directory dir(EXOTIC_DATA_PATH("data/nosuchdir"));
	return !dir.first().has_value();
});

EXO_TEST(file_read_span, {
	Samurai::IO::File f(EXOTIC_DATA_PATH("data/file1"));
	if (!f.open(Samurai::IO::File::Mode::Read)) return false;

	char buf[16];
	ssize_t n = f.read(std::span<char>(buf, sizeof(buf)));
	f.close();
	return n > 0;
});

EXO_TEST(file_write_span_roundtrip, {
	const std::string path = std::string(EXOTIC_DATA_PATH("data")) + "/span-test";
	const char payload[] = "span round trip";

	Samurai::IO::File w(path);
	if (!w.open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate)) return false;
	ssize_t written = w.write(std::span<const char>(payload, sizeof(payload) - 1));
	w.close();

	Samurai::IO::File r(path);
	if (!r.open(Samurai::IO::File::Mode::Read)) return false;
	char buf[32] = { 0 };
	ssize_t got = r.read(std::span<char>(buf, sizeof(buf) - 1));
	r.close();
	Samurai::IO::File::remove(path.c_str());

	return written == (ssize_t) (sizeof(payload) - 1) && got == written && strcmp(buf, payload) == 0;
});

/* ------------------------------------------------------------------------- */
/* Open modes, seeking, size and the directory operations                    */
/*                                                                           */
/* Everything here works inside a scratch directory created per case, so the  */
/* suite leaves nothing behind and cases do not collide.                      */
/* ------------------------------------------------------------------------- */

namespace {

using FileMode = Samurai::IO::File::Mode;

/* A unique scratch path that removes itself. Named by counter rather than by
   time, so a run is reproducible. */
class Scratch
{
	public:
		explicit Scratch(const char* tag)
		{
			static int counter = 0;
			dir = std::string("samurai-file-test-") + tag + "-" + std::to_string(counter++);
			Samurai::IO::File::mkdir(dir.c_str());
		}

		~Scratch()
		{
			/* Best effort: a case that failed part way may have left files. */
			for (const std::string& created : files)
				Samurai::IO::File::remove(created.c_str());
			Samurai::IO::File::rmdir(dir.c_str());
		}

		std::string path(const char* leaf)
		{
			std::string p = dir + "/" + leaf;
			files.push_back(p);
			return p;
		}

		const std::string& directory() const { return dir; }

		Scratch(const Scratch&) = delete;
		Scratch& operator=(const Scratch&) = delete;

	private:
		std::string dir;
		std::vector<std::string> files;
};

static bool write_file(const std::string& path, const std::string& content)
{
	Samurai::IO::File f(path);
	if (!f.open(FileMode::Write | FileMode::Truncate)) return false;
	const bool ok = f.write(content.data(), content.size()) == (ssize_t) content.size();
	f.close();
	return ok;
}

static std::string read_file(const std::string& path)
{
	Samurai::IO::File f(path);
	if (!f.open(FileMode::Read)) return std::string();

	char buf[256] = { 0 };
	const ssize_t n = f.read(buf, sizeof(buf));
	f.close();
	return n > 0 ? std::string(buf, (size_t) n) : std::string();
}

}

EXO_TEST(file_mkdir_and_rmdir,
{
	Scratch scratch("mkdir");
	Samurai::IO::File dir(scratch.directory());
	return dir.exists() && dir.isDirectory() && !dir.isRegular();
});

EXO_TEST(file_write_then_read_round_trip,
{
	Scratch scratch("roundtrip");
	const std::string path = scratch.path("data.txt");

	return write_file(path, "hello file") && read_file(path) == "hello file";
});

EXO_TEST(file_size_reports_what_was_written,
{
	Scratch scratch("size");
	const std::string path = scratch.path("sized.bin");
	if (!write_file(path, std::string(1234, 'z'))) return false;

	Samurai::IO::File f(path);
	return f.size() == 1234;
});

EXO_TEST(file_is_regular_not_directory,
{
	Scratch scratch("regular");
	const std::string path = scratch.path("plain.txt");
	if (!write_file(path, "x")) return false;

	Samurai::IO::File f(path);
	return f.isRegular() && !f.isDirectory();
});

/* Truncate must discard what was there, not overwrite a prefix of it. */
EXO_TEST(file_truncate_discards_previous_content,
{
	Scratch scratch("truncate");
	const std::string path = scratch.path("t.txt");

	if (!write_file(path, "a long original line")) return false;
	if (!write_file(path, "short")) return false;

	return read_file(path) == "short";
});

/* Append must keep it. */
EXO_TEST(file_append_adds_to_the_end,
{
	Scratch scratch("append");
	const std::string path = scratch.path("a.txt");
	if (!write_file(path, "first")) return false;

	Samurai::IO::File f(path);
	if (!f.open(FileMode::Write | FileMode::Append)) return false;
	f.write("second", 6);
	f.close();

	return read_file(path) == "firstsecond";
});

/* Exclusive refuses to create a file that is already there. */
EXO_TEST(file_exclusive_refuses_an_existing_file,
{
	Scratch scratch("exclusive");
	const std::string path = scratch.path("e.txt");
	if (!write_file(path, "already here")) return false;

	Samurai::IO::File f(path);
	return !f.open(FileMode::Write | FileMode::Exclusive);
});

EXO_TEST(file_exclusive_creates_a_new_file,
{
	Scratch scratch("exclusive-new");
	const std::string path = scratch.path("fresh.txt");

	Samurai::IO::File f(path);
	const bool opened = f.open(FileMode::Write | FileMode::Exclusive);
	if (opened) f.close();
	return opened;
});

/* ------------------------------------------------------------------------- */
/* Seeking                                                                   */
/* ------------------------------------------------------------------------- */

EXO_TEST(file_seek_moves_the_read_position,
{
	Scratch scratch("seek");
	const std::string path = scratch.path("s.txt");
	if (!write_file(path, "0123456789")) return false;

	Samurai::IO::File f(path);
	if (!f.open(FileMode::Read)) return false;
	if (!f.seek(4)) { f.close(); return false; }

	char buf[4] = { 0 };
	const ssize_t n = f.read(buf, 3);
	f.close();
	return n == 3 && memcmp(buf, "456", 3) == 0;
});

EXO_TEST(file_current_position_follows_reads_and_seeks,
{
	Scratch scratch("tell");
	const std::string path = scratch.path("p.txt");
	if (!write_file(path, "0123456789")) return false;

	Samurai::IO::File f(path);
	if (!f.open(FileMode::Read)) return false;

	const bool at_start = f.getCurrentPosition() == 0;

	char buf[4];
	f.read(buf, 4);
	const bool after_read = f.getCurrentPosition() == 4;

	f.seek(2);
	const bool after_seek = f.getCurrentPosition() == 2;

	f.close();
	return at_start && after_read && after_seek;
});

EXO_TEST(file_seek_to_the_end_reads_nothing,
{
	Scratch scratch("seek-end");
	const std::string path = scratch.path("e.txt");
	if (!write_file(path, "abcdef")) return false;

	Samurai::IO::File f(path);
	if (!f.open(FileMode::Read)) return false;
	f.seek(6);

	char buf[4];
	const ssize_t n = f.read(buf, sizeof(buf));
	f.close();
	return n == 0;
});

/* ------------------------------------------------------------------------- */
/* rename, remove, flush                                                     */
/* ------------------------------------------------------------------------- */

EXO_TEST(file_rename_moves_the_content,
{
	Scratch scratch("rename");
	const std::string from = scratch.path("from.txt");
	const std::string to = scratch.path("to.txt");
	if (!write_file(from, "carried across")) return false;

	Samurai::IO::File f(from);
	if (!f.rename(to)) return false;

	return !Samurai::IO::File::exists(from.c_str())
		&& read_file(to) == "carried across";
});

EXO_TEST(file_remove_deletes_it,
{
	Scratch scratch("remove");
	const std::string path = scratch.path("gone.txt");
	if (!write_file(path, "temporary")) return false;
	if (!Samurai::IO::File::exists(path.c_str())) return false;

	Samurai::IO::File f(path);
	return f.remove() && !Samurai::IO::File::exists(path.c_str());
});

EXO_TEST(file_flush_makes_content_readable,
{
	Scratch scratch("flush");
	const std::string path = scratch.path("f.txt");

	Samurai::IO::File writer(path);
	if (!writer.open(FileMode::Write | FileMode::Truncate)) return false;
	writer.write("flushed", 7);
	const bool flushed = writer.flush();

	const std::string seen = read_file(path);
	writer.close();

	return flushed && seen == "flushed";
});

/* ------------------------------------------------------------------------- */
/* The std::error_code overloads                                             */
/*                                                                           */
/* The whole point of these is telling a missing file from a permission       */
/* problem, which the bool cannot.                                           */
/* ------------------------------------------------------------------------- */

EXO_TEST(file_open_missing_reports_no_such_file,
{
	Scratch scratch("enoent");
	Samurai::IO::File f(scratch.directory() + "/definitely-not-here.txt");

	std::error_code ec;
	const bool opened = f.open(FileMode::Read, ec);
	return !opened && ec == std::errc::no_such_file_or_directory;
});

EXO_TEST(file_open_success_clears_the_error_code,
{
	Scratch scratch("ok-ec");
	const std::string path = scratch.path("good.txt");
	if (!write_file(path, "fine")) return false;

	Samurai::IO::File f(path);
	std::error_code ec = std::make_error_code(std::errc::io_error);
	const bool opened = f.open(FileMode::Read, ec);
	if (opened) f.close();

	return opened && !ec;
});

EXO_TEST(file_exclusive_reports_file_exists,
{
	Scratch scratch("eexist");
	const std::string path = scratch.path("there.txt");
	if (!write_file(path, "here")) return false;

	Samurai::IO::File f(path);
	std::error_code ec;
	const bool opened = f.open(FileMode::Write | FileMode::Exclusive, ec);
	return !opened && ec == std::errc::file_exists;
});

EXO_TEST(file_read_and_write_error_code_overloads_succeed,
{
	Scratch scratch("rw-ec");
	const std::string path = scratch.path("rw.txt");

	Samurai::IO::File writer(path);
	std::error_code ec;
	if (!writer.open(FileMode::Write | FileMode::Truncate, ec) || ec) return false;
	if (writer.write("payload", 7, ec) != 7 || ec) return false;
	writer.close();

	Samurai::IO::File reader(path);
	if (!reader.open(FileMode::Read, ec) || ec) return false;

	char buf[8] = { 0 };
	const ssize_t n = reader.read(buf, 7, ec);
	reader.close();

	return n == 7 && !ec && memcmp(buf, "payload", 7) == 0;
});

EXO_TEST(file_remove_missing_reports_no_such_file,
{
	Scratch scratch("rm-ec");
	Samurai::IO::File f(scratch.directory() + "/never-existed.txt");

	std::error_code ec;
	return !f.remove(ec) && ec == std::errc::no_such_file_or_directory;
});

/* size() of something that is not there is not a valid length. */
EXO_TEST(file_size_of_missing_file_is_not_positive,
{
	Scratch scratch("size-missing");
	Samurai::IO::File f(scratch.directory() + "/absent.bin");
	return f.size() <= 0;
});

/* ------------------------------------------------------------------------- */
/* The stat cache follows the file                                            */
/*                                                                            */
/* The accessors answer from a cached stat, which every operation through the  */
/* descriptor drops, so a size() taken at any point describes the file as it   */
/* is then and not as it was when first asked.                                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(file_size_reflects_a_write_after_an_earlier_size,
{
	Scratch scratch("stat-cache");
	Samurai::IO::File f(scratch.path("grows.bin"));

	if (!f.open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate))
		return false;

	/* Caches the stat of the empty file. */
	if (f.size() != 0) return false;

	if (f.write("hello", 5) != 5) return false;

	return f.size() == 5;
});

EXO_TEST(file_size_keeps_up_across_several_writes,
{
	Scratch scratch("stat-cache-2");
	Samurai::IO::File f(scratch.path("grows-more.bin"));

	if (!f.open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate))
		return false;

	for (int n = 0; n < 4; n++)
	{
		if (f.write("abcd", 4) != 4) return false;
		if (f.size() != (off_t) ((n + 1) * 4)) return false;
	}
	return true;
});

/* The accessors describe the file the descriptor holds, so renaming the path
   out from under an open File does not change what size() reports. */
EXO_TEST(file_size_follows_the_descriptor_not_the_path,
{
	Scratch scratch("stat-fd");
	const std::string original = scratch.path("held.bin");
	const std::string decoy    = scratch.path("decoy.bin");

	Samurai::IO::File f(original);
	if (!f.open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate))
		return false;
	if (f.write("0123456789", 10) != 10) return false;

	/* Something else puts a shorter file at the same path. */
	if (::rename(original.c_str(), decoy.c_str()) != 0) return false;
	Samurai::IO::File other(original);
	if (!other.open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate))
		return false;
	other.write("xy", 2);
	other.close();

	const off_t held = f.size();
	f.close();
	return held == 10;
});

/* exists() is a question about the name, so an open file whose path is gone
   answers false even though the descriptor is still good. */
EXO_TEST(file_exists_asks_about_the_path,
{
	Scratch scratch("exists-path");
	const std::string path = scratch.path("vanishes.bin");

	Samurai::IO::File f(path);
	if (!f.open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate))
		return false;
	if (!f.exists()) return false;

	if (::unlink(path.c_str()) != 0) return false;

	const bool gone = !f.exists();
	f.close();
	return gone;
});

/* rmdir() removes directories. A regular file at the same name is not one, and
   must survive the call. */
EXO_TEST(file_rmdir_refuses_a_regular_file,
{
	Scratch scratch("rmdir-file");
	const std::string path = scratch.path("not-a-directory.txt");

	Samurai::IO::File f(path);
	if (!f.open(Samurai::IO::File::Mode::Write | Samurai::IO::File::Mode::Truncate))
		return false;
	f.write("keep me", 7);
	f.close();

	const int ret = Samurai::IO::File::rmdir(path.c_str());

	Samurai::IO::File check(path);
	return ret == -1 && check.exists() && check.size() == 7;
});

EXO_TEST(file_rmdir_removes_an_empty_directory,
{
	Scratch scratch("rmdir-dir");
	const std::string dir = scratch.directory() + "/inner";

	if (Samurai::IO::File::mkdir(dir.c_str()) != 0) return false;

	const int ret = Samurai::IO::File::rmdir(dir.c_str());

	Samurai::IO::File check(dir);
	return ret == 0 && !check.exists();
});

/* ------------------------------------------------------------------------- */
/* A signal is not an I/O error                                               */
/*                                                                            */
/* read() returning EINTR means the call was cut short before anything could  */
/* be transferred, so it is reissued rather than reported.                    */
/*                                                                            */
/* A fifo opened for both reading and writing gives a descriptor that blocks  */
/* on an empty read without blocking on open. The handler supplies the byte,  */
/* so the reissued call has something to return and the case cannot hang.     */
/* ------------------------------------------------------------------------- */

namespace {

int g_fifo_write_end = -1;

extern "C" void samurai_test_file_alarm(int)
{
	if (g_fifo_write_end != -1)
	{
		[[maybe_unused]] ssize_t ignored = ::write(g_fifo_write_end, "x", 1);
	}
}

static bool arm_writing_alarm()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = samurai_test_file_alarm;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0; /* not SA_RESTART: the call must fail with EINTR */
	if (sigaction(SIGALRM, &sa, nullptr) != 0) return false;

	struct itimerval timer;
	memset(&timer, 0, sizeof(timer));
	timer.it_value.tv_usec = 100000;
	return setitimer(ITIMER_REAL, &timer, nullptr) == 0;
}

}

EXO_TEST(file_read_interrupted_by_a_signal_is_reissued,
{
	Scratch scratch("eintr");
	const std::string path = scratch.path("fifo");

	if (::mkfifo(path.c_str(), 0600) != 0) return false;

	Samurai::IO::File f(path);
	if (!f.open(Samurai::IO::File::Mode::Read | Samurai::IO::File::Mode::Write))
		return false;

	/* A second handle for the signal handler to write through; the fifo
	   already has a reader, so this does not block. */
	g_fifo_write_end = ::open(path.c_str(), O_WRONLY);
	if (g_fifo_write_end == -1) return false;

	if (!arm_writing_alarm()) { ::close(g_fifo_write_end); g_fifo_write_end = -1; return false; }

	char buf[1] = { 0 };
	std::error_code ec;
	const ssize_t got = f.read(buf, sizeof(buf), ec);

	::close(g_fifo_write_end);
	g_fifo_write_end = -1;
	f.close();

	return got == 1 && buf[0] == 'x' && !ec;
});


/* ------------------------------------------------------------------------- */
/* Predicates and the advisory open modes                                    */
/*                                                                           */
/* isDeleteable() used to answer false for everything, and isExecutable() was */
/* spelled isExcecutable in an installed header. Neither had a caller, which  */
/* is how both survived.                                                     */
/* ------------------------------------------------------------------------- */

EXO_TEST(file_readable_and_writable,
{
	Scratch scratch("perm");
	const std::string path = scratch.path("plain.txt");
	if (!write_file(path, "x")) return false;

	Samurai::IO::File f(path);
	return f.isReadable() && f.isWritable();
});

EXO_TEST(file_predicates_are_false_for_what_is_not_there,
{
	Samurai::IO::File f("samurai-file-test-no-such-file");
	return !f.exists()
		&& !f.isReadable()
		&& !f.isWritable()
		&& !f.isExecutable()
		&& !f.isSymlink()
		&& !f.isDirectory();
});

/* A file the umask left non-executable, and the same file with the bit set. */
EXO_TEST(file_executable_follows_the_mode,
{
	Scratch scratch("exec");
	const std::string path = scratch.path("script.sh");
	if (!write_file(path, "#!/bin/sh\nexit 0\n")) return false;

	Samurai::IO::File f(path);
	if (f.isExecutable()) return false;

	if (chmod(path.c_str(), 0755) != 0) return false;
	return f.isExecutable();
});

EXO_TEST(file_a_directory_is_executable_because_it_is_searchable,
{
	Scratch scratch("execdir");
	Samurai::IO::File dir(scratch.directory());
	return dir.isDirectory() && dir.isExecutable();
});

/*
 * Deleting writes to the directory, not to the file, so a read-only file in a
 * writable directory can still go.
 */
EXO_TEST(file_deleteable_reads_the_directory_not_the_file,
{
	Scratch scratch("del");
	const std::string path = scratch.path("readonly.txt");
	if (!write_file(path, "x")) return false;
	if (chmod(path.c_str(), 0444) != 0) return false;

	Samurai::IO::File f(path);
	if (f.isWritable()) return false;

	const bool deleteable = f.isDeleteable();
	chmod(path.c_str(), 0644);
	return deleteable;
});

EXO_TEST(file_not_deleteable_from_a_read_only_directory,
{
	Scratch scratch("delro");
	const std::string path = scratch.path("locked.txt");
	if (!write_file(path, "x")) return false;

	if (chmod(scratch.directory().c_str(), 0500) != 0) return false;

	Samurai::IO::File f(path);
	const bool deleteable = f.isDeleteable();

	/* Put it back, or the scratch directory cannot clean itself up. */
	chmod(scratch.directory().c_str(), 0755);

	/* Nothing to stop root, so only assert the refusal where it applies. */
	if (geteuid() == 0) return true;
	return !deleteable;
});

/* What it says has to match what actually happens. */
EXO_TEST(file_deleteable_agrees_with_remove,
{
	Scratch scratch("delreal");
	const std::string path = scratch.path("goes.txt");
	if (!write_file(path, "x")) return false;

	Samurai::IO::File f(path);
	if (!f.isDeleteable()) return false;

	return f.remove() && !f.exists();
});

EXO_TEST(file_symlink_is_recognised,
{
	Scratch scratch("symlink");
	const std::string target = scratch.path("target.txt");
	const std::string link = scratch.path("link.txt");
	if (!write_file(target, "pointed at")) return false;
	if (symlink("target.txt", link.c_str()) != 0) return false;

	Samurai::IO::File l(link);
	Samurai::IO::File t(target);

	/* isSymlink() asks about the link; everything else follows it. */
	return l.isSymlink() && !t.isSymlink() && l.exists() && !l.isDirectory();
});

EXO_TEST(file_symlink_to_a_directory_is_both,
{
	Scratch scratch("symdir");
	const std::string link = scratch.path("dirlink");
	if (symlink(".", link.c_str()) != 0) return false;

	Samurai::IO::File l(link);
	return l.isSymlink() && l.isDirectory();
});

/*
 * Paranoid maps to O_NOFOLLOW, so a symlink must not open through it. This is
 * what the mode is for: a path that was a regular file a moment ago and is a
 * link to something else by the time it is opened.
 */
EXO_TEST(file_paranoid_refuses_to_follow_a_symlink,
{
	Scratch scratch("paranoid");
	const std::string target = scratch.path("real.txt");
	const std::string link = scratch.path("sneaky.txt");
	if (!write_file(target, "the real thing")) return false;
	if (symlink("real.txt", link.c_str()) != 0) return false;

	Samurai::IO::File plain(link);
	if (!plain.open(FileMode::Read)) return false;
	plain.close();

	Samurai::IO::File guarded(link);
	std::error_code ec;
	const bool opened = guarded.open(FileMode::Read | FileMode::Paranoid, ec);
	if (opened) guarded.close();

	return !opened && ec;
});

EXO_TEST(file_paranoid_opens_a_regular_file,
{
	Scratch scratch("paranoidok");
	const std::string path = scratch.path("real.txt");
	if (!write_file(path, "content")) return false;

	Samurai::IO::File f(path);
	if (!f.open(FileMode::Read | FileMode::Paranoid)) return false;

	char buf[32] = { 0 };
	const ssize_t n = f.read(buf, sizeof(buf) - 1);
	f.close();

	return n == 7 && std::string(buf) == "content";
});

/*
 * NoAccess maps to O_NOATIME, which only Linux has. Everywhere else the flag is
 * dropped, so all that can be asserted is that asking for it is harmless.
 */
EXO_TEST(file_no_access_mode_is_harmless,
{
	Scratch scratch("noatime");
	const std::string path = scratch.path("timed.txt");
	if (!write_file(path, "content")) return false;

	Samurai::IO::File f(path);
	if (!f.open(FileMode::Read | FileMode::NoAccess)) return false;

	char buf[32] = { 0 };
	const ssize_t n = f.read(buf, sizeof(buf) - 1);
	f.close();

	return n == 7 && std::string(buf) == "content";
});

EXO_TEST(file_no_access_combines_with_paranoid,
{
	Scratch scratch("bothmodes");
	const std::string path = scratch.path("both.txt");
	if (!write_file(path, "content")) return false;

	Samurai::IO::File f(path);
	const bool ok = f.open(FileMode::Read | FileMode::Paranoid | FileMode::NoAccess);
	if (ok) f.close();
	return ok;
});
