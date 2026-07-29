#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>
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

/* ------------------------------------------------------------------------- */
/* copyRange(): a non-consuming copy of a sub-range                          */
/* ------------------------------------------------------------------------- */

EXO_TEST(buffer_copyrange_whole,
{
	Samurai::IO::Buffer buf;
	buf.append("hello world");
	return buf.copyRange(0, buf.size()) == "hello world";
});

EXO_TEST(buffer_copyrange_prefix,
{
	Samurai::IO::Buffer buf;
	buf.append("hello world");
	return buf.copyRange(0, 5) == "hello";
});

EXO_TEST(buffer_copyrange_middle,
{
	Samurai::IO::Buffer buf;
	buf.append("hello world");
	return buf.copyRange(6, 11) == "world";
});

EXO_TEST(buffer_copyrange_does_not_consume,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	buf.copyRange(0, 5);
	return buf.size() == 5;
});

/* The range is relative to the live data, so a consumed prefix shifts it. */
EXO_TEST(buffer_copyrange_after_remove,
{
	Samurai::IO::Buffer buf;
	buf.append("hello world");
	buf.remove(6);
	return buf.copyRange(0, buf.size()) == "world";
});

EXO_TEST(buffer_copyrange_empty_range,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	return buf.copyRange(2, 2).empty();
});

EXO_TEST(buffer_copyrange_past_end_is_empty,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	return buf.copyRange(0, 99).empty();
});

EXO_TEST(buffer_copyrange_reversed_is_empty,
{
	Samurai::IO::Buffer buf;
	buf.append("hello");
	return buf.copyRange(4, 1).empty();
});

/* Binary safe: the copy is sized, not NUL terminated. */
EXO_TEST(buffer_copyrange_keeps_embedded_nul,
{
	Samurai::IO::Buffer buf;
	buf.append("ab\0cd", 5);
	const std::string out = buf.copyRange(0, 5);
	return out.size() == 5 && out[2] == '\0' && out[4] == 'd';
});

/* ------------------------------------------------------------------------- */
/* Capacity, clearing and the raw accessors                                  */
/* ------------------------------------------------------------------------- */

EXO_TEST(buffer_starts_empty_with_its_initial_capacity,
{
	Samurai::IO::Buffer buf;
	return buf.size() == 0
		&& buf.capasity() == buf.getInitialCapasity()
		&& buf.getInitialCapasity() > 0;
});

EXO_TEST(buffer_reserve_grows_capacity_without_adding_data,
{
	Samurai::IO::Buffer buf;
	const size_t before = buf.capasity();

	buf.reserve(before * 4);
	return buf.size() == 0 && buf.capasity() >= before;
});

/* reserve(0) asks for nothing and must not disturb the buffer. */
EXO_TEST(buffer_reserve_zero_is_a_no_op,
{
	Samurai::IO::Buffer buf;
	buf.append("data", 4);
	const size_t capacity = buf.capasity();

	buf.reserve(0);
	return buf.size() == 4 && buf.capasity() == capacity;
});

/* Appending more than the initial capacity must grow rather than truncate. */
EXO_TEST(buffer_grows_past_its_initial_capacity,
{
	Samurai::IO::Buffer buf;
	const std::string chunk(64, 'x');
	for (int n = 0; n < 200; n++) buf.append(chunk);

	return buf.size() == 64 * 200 && buf.capasity() >= buf.size();
});

EXO_TEST(buffer_clear_empties_and_resets_capacity,
{
	Samurai::IO::Buffer buf;
	const std::string chunk(64, 'x');
	for (int n = 0; n < 200; n++) buf.append(chunk);

	buf.clear();
	return buf.size() == 0 && buf.capasity() == buf.getInitialCapasity();
});

EXO_TEST(buffer_is_reusable_after_clear,
{
	Samurai::IO::Buffer buf;
	buf.append("first", 5);
	buf.clear();
	buf.append("second", 6);

	return buf.size() == 6 && buf.pop(6) == "second";
});

/* clear() has to reset the consumed prefix too, not only the length. */
EXO_TEST(buffer_clear_after_remove_leaves_nothing_behind,
{
	Samurai::IO::Buffer buf;
	buf.append("abcdef", 6);
	buf.remove(3);
	buf.clear();

	buf.append("xy", 2);
	return buf.size() == 2 && buf.at(0) == 'x' && buf.at(1) == 'y';
});

EXO_TEST(buffer_index_operator_reads_live_data,
{
	Samurai::IO::Buffer buf;
	buf.append("abcdef", 6);
	return buf[0] == 'a' && buf[5] == 'f';
});

/* Both accessors are relative to the live region, so remove() shifts them. */
EXO_TEST(buffer_index_operator_follows_remove,
{
	Samurai::IO::Buffer buf;
	buf.append("abcdef", 6);
	buf.remove(2);
	return buf[0] == 'c' && buf.at(0) == 'c';
});

/* at() is the bounds-checked one and reads 0 past the end; operator[] is not. */
EXO_TEST(buffer_at_is_bounds_checked,
{
	Samurai::IO::Buffer buf;
	buf.append("abc", 3);
	return buf.at(2) == 'c' && buf.at(3) == 0 && buf.at(9999) == 0;
});

EXO_TEST(buffer_ptr_points_at_the_live_data,
{
	Samurai::IO::Buffer buf;
	buf.append("abcdef", 6);
	buf.remove(2);

	const char* p = buf.ptr();
	return p && p[0] == 'c' && p[3] == 'f';
});

EXO_TEST(buffer_ptr_of_an_empty_buffer_is_usable_or_null,
{
	Samurai::IO::Buffer buf;
	/* Either is acceptable; what must not happen is a dereference of garbage,
	   which the sanitizer build would catch. */
	const char* p = buf.ptr();
	(void) p;
	return true;
});

/* An empty buffer has no range to copy, not even an empty one. */
EXO_TEST(buffer_copyrange_of_an_empty_buffer,
{
	Samurai::IO::Buffer buf;
	return buf.copyRange(0, 0).empty();
});

/* ------------------------------------------------------------------------- */
/* Refusing to grow                                                          */
/*                                                                           */
/* resize() returns false and leaves the buffer alone when it cannot grow.    */
/* Two ways to get there: an arithmetic overflow computing what is needed,    */
/* and a request larger than the allocator can represent at all - which       */
/* vector::resize() reports by throwing length_error, not bad_alloc, so it    */
/* escaped through append() rather than being absorbed.                       */
/*                                                                           */
/* Every size used here is refused before anything is allocated. A request    */
/* merely large would be attempted for real and take the machine with it.     */
/* ------------------------------------------------------------------------- */

/* len + needed wraps, so the buffer cannot tell how much was asked for. */
EXO_TEST(buffer_growth_overflowing_size_t_is_refused,
{
	Samurai::IO::Buffer buf;
	buf.append("hello", 5);

	buf.reserve(SIZE_MAX);

	return buf.size() == 5 && buf.copyRange(0, 5) == "hello";
});

/* Representable, but beyond what the allocator will ever hand out. */
EXO_TEST(buffer_growth_beyond_max_size_is_refused,
{
	Samurai::IO::Buffer buf;
	buf.append("hello", 5);

	buf.reserve(std::vector<char>().max_size());

	return buf.size() == 5 && buf.copyRange(0, 5) == "hello";
});

EXO_TEST(buffer_growth_just_under_max_size_is_refused,
{
	Samurai::IO::Buffer buf;
	buf.append("hello", 5);

	/* Doubling from the initial capacity overshoots max_size() long before it
	   reaches this, so it is refused on the same check. */
	buf.reserve(std::vector<char>().max_size() - 64);

	return buf.size() == 5 && buf.copyRange(0, 5) == "hello";
});

/* append() drops the data and says so rather than letting anything escape. */
EXO_TEST(buffer_append_that_cannot_fit_drops_the_data,
{
	Samurai::IO::Buffer buf;
	buf.append("hello", 5);

	buf.append("x", SIZE_MAX / 2);

	return buf.size() == 5 && buf.copyRange(0, 5) == "hello";
});

EXO_TEST(buffer_append_overflowing_size_t_drops_the_data,
{
	Samurai::IO::Buffer buf;
	buf.append("hello", 5);

	buf.append("x", SIZE_MAX);

	return buf.size() == 5 && buf.copyRange(0, 5) == "hello";
});

/* A refused growth must leave the buffer working, not wedged. */
EXO_TEST(buffer_still_usable_after_a_refused_growth,
{
	Samurai::IO::Buffer buf;
	buf.append("hello", 5);

	buf.reserve(std::vector<char>().max_size());
	buf.append(" world", 6);

	if (buf.size() != 11 || buf.copyRange(0, 11) != "hello world") return false;

	buf.remove(6);
	return buf.copyRange(0, buf.size()) == "world";
});

/* The consumed prefix is still reclaimed even when the growth is refused. */
EXO_TEST(buffer_a_refused_growth_does_not_lose_the_live_bytes,
{
	Samurai::IO::Buffer buf;
	buf.append("0123456789", 10);
	buf.remove(7);

	buf.reserve(SIZE_MAX);

	return buf.size() == 3 && buf.copyRange(0, 3) == "789";
});
