#include <samurai/io/buffer.h>
#include <utility>

EXO_TEST(buffer_append_size,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	return buf.size() == 5;
});

EXO_TEST(buffer_remove_advances,
{
	Samurai::IO::Buffer buf;
	buf.append("hello world");
	buf.remove(6);
	return buf.size() == 5 && buf.at(0) == 'w';
});

EXO_TEST(buffer_at_in_range,
{
	Samurai::IO::Buffer buf;
	buf.append("abc");
	return buf.at(0) == 'a' && buf.at(1) == 'b' && buf.at(2) == 'c';
});

EXO_TEST(buffer_at_past_end,
{
	Samurai::IO::Buffer buf;
	buf.append("abc");
	return buf.at(3) == 0 && buf.at(4096) == 0;
});

EXO_TEST(buffer_at_past_end_after_remove,
{
	Samurai::IO::Buffer buf;
	buf.append("abcdef");
	buf.remove(4);
	return buf.size() == 2 && buf.at(1) == 'f' && buf.at(2) == 0;
});

EXO_TEST(buffer_at_on_empty,
{
	Samurai::IO::Buffer buf;
	return buf.at(0) == 0;
});

EXO_TEST(buffer_pop_returns_count,
{
	Samurai::IO::Buffer buf;
	buf.append("abcdef");
	char out[16];
	return buf.pop(out, 4) == 4 && buf.size() == 6;
});

EXO_TEST(buffer_pop_clamps_to_available,
{
	Samurai::IO::Buffer buf;
	buf.append("abc");
	char out[16];
	return buf.pop(out, 16) == 3;
});

EXO_TEST(buffer_pop_on_empty,
{
	Samurai::IO::Buffer buf;
	char out[16];
	return buf.pop(out, 16) == 0;
});

EXO_TEST(buffer_pop_offset_past_end,
{
	Samurai::IO::Buffer buf;
	buf.append("abc");
	char out[16];
	return buf.pop(out, 8, 4) == 0;
});

EXO_TEST(buffer_find_char,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	return buf.find('l') == 2;
});

EXO_TEST(buffer_find_char_missing,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	return buf.find('z') == Samurai::IO::Buffer::npos;
});

EXO_TEST(buffer_rfind_char,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	return buf.rfind('l') == 3;
});

EXO_TEST(buffer_rfind_char_missing,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	return buf.rfind('z') == Samurai::IO::Buffer::npos;
});

EXO_TEST(buffer_find_string,
{
	Samurai::IO::Buffer buf;
	buf.append("one\r\ntwo\r\n");
	return buf.find("\r\n") == 3 && buf.find("\r\n", 4) == 8;
});

EXO_TEST(buffer_find_string_missing,
{
	Samurai::IO::Buffer buf;
	buf.append("one\r\ntwo\r\n");
	return buf.find("\n\r") == Samurai::IO::Buffer::npos;
});

EXO_TEST(buffer_find_after_remove,
{
	Samurai::IO::Buffer buf;
	buf.append("one\r\ntwo\r\n");
	buf.remove(5);
	return buf.find("\r\n") == 3;
});

EXO_TEST(buffer_assignment_copies,
{
	Samurai::IO::Buffer a;
	a.append("abcdef");
	a.remove(2);

	Samurai::IO::Buffer b;
	b = a;
	return b.size() == 4 && b.at(0) == 'c';
});

EXO_TEST(buffer_move_construct,
{
	Samurai::IO::Buffer a;
	a.append("abcdef");
	a.remove(2);

	Samurai::IO::Buffer b(std::move(a));
	return b.size() == 4 && b.at(0) == 'c' && a.size() == 0;
});

EXO_TEST(buffer_move_assign,
{
	Samurai::IO::Buffer a;
	a.append("abcdef");

	Samurai::IO::Buffer b;
	b.append("xyz");
	b = std::move(a);
	return b.size() == 6 && b.at(0) == 'a' && a.size() == 0;
});
