/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/util/string.h>

namespace {

constexpr bool is_space(unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

constexpr bool is_unreserved(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') ||
	       c == '-' || c == '.' || c == '_' || c == '~';
}

/** The value of one hexadecimal digit, or -1 if it is not one. */
constexpr int hex_value(unsigned char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

}


std::string_view Samurai::Util::trim(std::string_view text)
{
	size_t begin = 0;
	size_t end = text.size();

	while (begin < end && is_space((unsigned char) text[begin])) begin++;
	while (end > begin && is_space((unsigned char) text[end - 1])) end--;

	return text.substr(begin, end - begin);
}


std::vector<std::string_view> Samurai::Util::split(std::string_view text, char sep)
{
	std::vector<std::string_view> fields;

	size_t begin = 0;
	for (;;)
	{
		const size_t pos = text.find(sep, begin);
		if (pos == std::string_view::npos)
		{
			fields.push_back(text.substr(begin));
			return fields;
		}

		fields.push_back(text.substr(begin, pos - begin));
		begin = pos + 1;
	}
}


bool Samurai::Util::split_once(std::string_view text, char sep,
                               std::string_view& before, std::string_view& after)
{
	const size_t pos = text.find(sep);
	if (pos == std::string_view::npos) return false;

	before = text.substr(0, pos);
	after  = text.substr(pos + 1);
	return true;
}


bool Samurai::Util::percent_decode(std::string_view text, std::string& out)
{
	out.clear();
	out.reserve(text.size());

	for (size_t n = 0; n < text.size(); n++)
	{
		if (text[n] != '%')
		{
			out.push_back(text[n]);
			continue;
		}

		if (n + 2 >= text.size()) return false;

		const int hi = hex_value((unsigned char) text[n + 1]);
		const int lo = hex_value((unsigned char) text[n + 2]);
		if (hi < 0 || lo < 0) return false;

		out.push_back((char) ((hi << 4) | lo));
		n += 2;
	}

	return true;
}


std::string Samurai::Util::percent_encode(std::string_view text)
{
	static const char digits[] = "0123456789ABCDEF";

	std::string out;
	out.reserve(text.size());

	for (const char ch : text)
	{
		const unsigned char c = (unsigned char) ch;
		if (is_unreserved(c))
		{
			out.push_back(ch);
			continue;
		}

		out.push_back('%');
		out.push_back(digits[c >> 4]);
		out.push_back(digits[c & 0x0f]);
	}

	return out;
}


std::string Samurai::Util::xml_escape(std::string_view text)
{
	std::string out;
	out.reserve(text.size());

	for (const char ch : text)
	{
		switch (ch)
		{
			case '&':  out += "&amp;";  break;
			case '<':  out += "&lt;";   break;
			case '>':  out += "&gt;";   break;
			case '"':  out += "&quot;"; break;
			case '\'': out += "&apos;"; break;
			default:   out.push_back(ch); break;
		}
	}

	return out;
}


bool Samurai::Util::append_utf8(uint32_t codepoint, std::string& out)
{
	/* A surrogate has no UTF-8 encoding, and nothing above U+10FFFF exists. */
	if (codepoint > 0x10ffff) return false;
	if (codepoint >= 0xd800 && codepoint <= 0xdfff) return false;

	if (codepoint < 0x80)
	{
		out.push_back((char) codepoint);
	}
	else if (codepoint < 0x800)
	{
		out.push_back((char) (0xc0 | (codepoint >> 6)));
		out.push_back((char) (0x80 | (codepoint & 0x3f)));
	}
	else if (codepoint < 0x10000)
	{
		out.push_back((char) (0xe0 | (codepoint >> 12)));
		out.push_back((char) (0x80 | ((codepoint >> 6) & 0x3f)));
		out.push_back((char) (0x80 | (codepoint & 0x3f)));
	}
	else
	{
		out.push_back((char) (0xf0 | (codepoint >> 18)));
		out.push_back((char) (0x80 | ((codepoint >> 12) & 0x3f)));
		out.push_back((char) (0x80 | ((codepoint >> 6) & 0x3f)));
		out.push_back((char) (0x80 | (codepoint & 0x3f)));
	}

	return true;
}
