/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/http/response.h>
#include <samurai/io/buffer.h>
#include <samurai/util/string.h>

namespace {

using Samurai::Util::iequals;
using Samurai::Util::trim;

constexpr bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

/** All digits, and fits. False leaves 'out' untouched. */
bool parse_size(std::string_view text, size_t& out)
{
	if (text.empty()) return false;

	size_t value = 0;
	for (const char digit : text)
	{
		if (!is_digit((unsigned char) digit)) return false;

		/* Refused before the multiply wraps and takes the length with it. */
		if (value > (SIZE_MAX - 9) / 10) return false;
		value = value * 10 + (size_t) (digit - '0');
	}

	out = value;
	return true;
}

/** A hexadecimal chunk size, stopping at a chunk extension. */
bool parse_chunk_size(std::string_view text, size_t& out)
{
	/* Extensions after a ';' say nothing this needs. */
	const size_t semi = text.find(';');
	if (semi != std::string_view::npos) text = text.substr(0, semi);

	text = trim(text);
	if (text.empty()) return false;

	size_t value = 0;
	for (const char digit : text)
	{
		const unsigned char c = (unsigned char) digit;
		int d = -1;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		if (d < 0) return false;

		if (value > (SIZE_MAX - 15) / 16) return false;
		value = value * 16 + (size_t) d;
	}

	out = value;
	return true;
}

}


const char* Samurai::IO::Net::HTTP::toString(Samurai::IO::Net::HTTP::Method method)
{
	switch (method)
	{
		case Method::Get:  return "GET";
		case Method::Post: return "POST";
		case Method::Head: return "HEAD";
	}
	return "GET";
}


const char* Samurai::IO::Net::HTTP::toString(Samurai::IO::Net::HTTP::Status status)
{
	switch (status)
	{
		case Status::Ok:                return "ok";
		case Status::InvalidUrl:        return "not a usable URL";
		case Status::ConnectFailed:     return "could not connect";
		case Status::ConnectTimeout:    return "connection timed out";
		case Status::RequestTimeout:    return "no response before the deadline";
		case Status::Disconnected:      return "the peer closed early";
		case Status::MalformedResponse: return "malformed response";
		case Status::ResponseTooLarge:  return "response too large";
		case Status::TlsFailed:         return "the TLS handshake failed";
		case Status::TooManyRedirects:  return "too many redirects";
		case Status::InsecureRedirect:  return "redirected from https to http";
		case Status::Aborted:           return "abandoned";
	}
	return "unknown";
}


void Samurai::IO::Net::HTTP::Headers::add(std::string_view name, std::string_view value)
{
	fields.emplace_back(std::string(name), std::string(value));
}


void Samurai::IO::Net::HTTP::Headers::set(std::string_view name, std::string_view value)
{
	for (auto it = fields.begin(); it != fields.end(); )
	{
		if (iequals(it->first, name)) it = fields.erase(it);
		else ++it;
	}

	add(name, value);
}


size_t Samurai::IO::Net::HTTP::Headers::remove(std::string_view name)
{
	size_t dropped = 0;

	for (auto it = fields.begin(); it != fields.end(); )
	{
		if (iequals(it->first, name)) { it = fields.erase(it); dropped++; }
		else ++it;
	}

	return dropped;
}


std::optional<std::string_view>
Samurai::IO::Net::HTTP::Headers::get(std::string_view name) const
{
	for (const auto& [key, value] : fields)
		if (iequals(key, name)) return std::string_view(value);

	return std::nullopt;
}


bool Samurai::IO::Net::HTTP::Headers::has(std::string_view name) const
{
	return get(name).has_value();
}


size_t Samurai::IO::Net::HTTP::Headers::count(std::string_view name) const
{
	size_t found = 0;
	for (const auto& [key, value] : fields)
		if (iequals(key, name)) found++;

	return found;
}


void Samurai::IO::Net::HTTP::Response::clear()
{
	status_code = 0;
	reason.clear();
	headers.clear();
	body.clear();
}


Samurai::IO::Net::HTTP::ResponseParser::ResponseParser()
{
}


Samurai::IO::Net::HTTP::ResponseParser::ResponseParser(const Limits& bounds)
	: limits(bounds)
{
}


void Samurai::IO::Net::HTTP::ResponseParser::reset()
{
	state = State::StatusLine;
	status = Status::Ok;
	response.clear();
	header_bytes = 0;
	body_remaining = 0;
	chunk_remaining = 0;
	no_body = false;
	closed = false;
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::fail(Status why)
{
	status = why;
	return Result::Error;
}


bool Samurai::IO::Net::HTTP::ResponseParser::takeLine(Samurai::IO::Buffer& input,
                                                     std::string& line,
                                                     bool& too_long)
{
	too_long = false;

	const size_t lf = input.find('\n');
	if (lf == Samurai::IO::Buffer::npos)
	{
		/* No terminator yet. What has arrived still counts against the limit,
		   or a peer could hold the connection open sending a header forever. */
		if (input.size() > limits.maxHeaderBlock) too_long = true;
		return false;
	}

	if (lf > limits.maxHeaderBlock) { too_long = true; return false; }

	size_t end = lf;
	if (end > 0 && input.at(end - 1) == '\r') end--;

	line = input.copyRange(0, end);
	input.remove(lf + 1);

	header_bytes += lf + 1;
	if (header_bytes > limits.maxHeaderBlock) too_long = true;

	return true;
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parseStatusLine(Samurai::IO::Buffer& input)
{
	std::string line;
	bool too_long = false;
	if (!takeLine(input, line, too_long))
		return too_long ? fail(Status::ResponseTooLarge) : Result::NeedMore;
	if (too_long) return fail(Status::ResponseTooLarge);

	/* HTTP-version SP status-code [ SP reason-phrase ] */
	if (!Samurai::Util::istarts_with(line, "HTTP/")) return fail(Status::MalformedResponse);

	const size_t first = line.find(' ');
	if (first == std::string::npos) return fail(Status::MalformedResponse);

	const std::string_view version = std::string_view(line).substr(5, first - 5);
	if (version != "1.0" && version != "1.1") return fail(Status::MalformedResponse);

	size_t after = first;
	while (after < line.size() && line[after] == ' ') after++;

	const size_t second = line.find(' ', after);
	const std::string_view code = (second == std::string::npos)
		? std::string_view(line).substr(after)
		: std::string_view(line).substr(after, second - after);

	if (code.size() != 3) return fail(Status::MalformedResponse);

	size_t value = 0;
	if (!parse_size(code, value)) return fail(Status::MalformedResponse);
	if (value < 100 || value > 599) return fail(Status::MalformedResponse);

	response.status_code = (int) value;
	if (second != std::string::npos)
		response.reason = std::string(trim(std::string_view(line).substr(second + 1)));

	state = State::HeaderBlock;
	return Result::NeedMore;
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parseHeaderBlock(Samurai::IO::Buffer& input)
{
	for (;;)
	{
		std::string line;
		bool too_long = false;
		if (!takeLine(input, line, too_long))
			return too_long ? fail(Status::ResponseTooLarge) : Result::NeedMore;
		if (too_long) return fail(Status::ResponseTooLarge);

		if (line.empty()) return chooseBodyFraming();

		/*
		 * A line beginning with whitespace is an obsolete continuation of the
		 * one before it. Refused rather than un-folded: RFC 7230 deprecated it,
		 * and joining the pieces is how a header value gets to smuggle in
		 * something the sender split on purpose.
		 */
		if (line[0] == ' ' || line[0] == '\t') return fail(Status::MalformedResponse);

		std::string_view name;
		std::string_view value;
		if (!Samurai::Util::split_once(line, ':', name, value))
			return fail(Status::MalformedResponse);

		/* No space is allowed between the name and the colon. */
		if (name.empty() || trim(name) != name) return fail(Status::MalformedResponse);

		if (response.headers.size() >= limits.maxHeaderCount)
			return fail(Status::ResponseTooLarge);

		response.headers.add(name, trim(value));
	}
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::chooseBodyFraming()
{
	const int code = response.status_code;

	/* A 1xx, 204 or 304 never has a body, nor does a reply to HEAD. */
	if (no_body || (code >= 100 && code < 200) || code == 204 || code == 304)
	{
		state = State::Done;
		return Result::Complete;
	}

	const std::optional<std::string_view> encoding =
		response.headers.get("Transfer-Encoding");

	/* Chunked wins over a Content-Length when both are present, which is what
	   RFC 7230 requires - and disagreeing about which to believe is the whole
	   of request smuggling. */
	if (encoding.has_value())
	{
		if (iequals(trim(*encoding), "chunked"))
		{
			state = State::BodyChunkSize;
			return Result::NeedMore;
		}

		/* Any other coding is one this cannot undo. */
		if (!iequals(trim(*encoding), "identity"))
			return fail(Status::MalformedResponse);
	}

	if (response.headers.has("Content-Length"))
	{
		/* Two that disagree is unresolvable; two that agree is merely rude. */
		std::optional<std::string_view> seen;
		for (const auto& [key, value] : response.headers.all())
		{
			if (!iequals(key, "Content-Length")) continue;
			if (seen.has_value() && trim(*seen) != trim(value))
				return fail(Status::MalformedResponse);
			seen = value;
		}

		size_t length = 0;
		if (!parse_size(trim(*seen), length)) return fail(Status::MalformedResponse);
		if (length > limits.maxBody) return fail(Status::ResponseTooLarge);

		body_remaining = length;
		if (!body_remaining)
		{
			state = State::Done;
			return Result::Complete;
		}

		state = State::BodyLength;
		return Result::NeedMore;
	}

	/*
	 * Neither framing: the body runs to the close. Embedded HTTP servers do
	 * this constantly, so it has to work rather than being treated as
	 * malformed.
	 */
	state = State::BodyUntilClose;
	return Result::NeedMore;
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parseBodyLength(Samurai::IO::Buffer& input)
{
	if (!input.size()) return Result::NeedMore;

	const size_t take = (input.size() < body_remaining) ? input.size() : body_remaining;
	response.body += input.copyRange(0, take);
	input.remove(take);
	body_remaining -= take;

	if (body_remaining) return Result::NeedMore;

	state = State::Done;
	return Result::Complete;
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parseChunkSize(Samurai::IO::Buffer& input)
{
	std::string line;
	bool too_long = false;
	if (!takeLine(input, line, too_long))
		return too_long ? fail(Status::ResponseTooLarge) : Result::NeedMore;
	if (too_long) return fail(Status::ResponseTooLarge);

	size_t size = 0;
	if (!parse_chunk_size(line, size)) return fail(Status::MalformedResponse);
	if (size > limits.maxChunkSize) return fail(Status::ResponseTooLarge);
	if (response.body.size() + size > limits.maxBody) return fail(Status::ResponseTooLarge);

	if (!size)
	{
		state = State::BodyTrailers;
		return Result::NeedMore;
	}

	chunk_remaining = size;
	state = State::BodyChunkData;
	return Result::NeedMore;
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parseChunkData(Samurai::IO::Buffer& input)
{
	if (!input.size()) return Result::NeedMore;

	const size_t take = (input.size() < chunk_remaining) ? input.size() : chunk_remaining;
	response.body += input.copyRange(0, take);
	input.remove(take);
	chunk_remaining -= take;

	if (chunk_remaining) return Result::NeedMore;

	state = State::BodyChunkCrlf;
	return Result::NeedMore;
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parseChunkCrlf(Samurai::IO::Buffer& input)
{
	std::string line;
	bool too_long = false;
	if (!takeLine(input, line, too_long))
		return too_long ? fail(Status::ResponseTooLarge) : Result::NeedMore;
	if (too_long) return fail(Status::ResponseTooLarge);

	/* Exactly the terminator, and nothing else. */
	if (!line.empty()) return fail(Status::MalformedResponse);

	state = State::BodyChunkSize;
	return Result::NeedMore;
}


/* Read and discarded: nothing this serves reads a trailer. */
Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parseTrailers(Samurai::IO::Buffer& input)
{
	for (;;)
	{
		std::string line;
		bool too_long = false;
		if (!takeLine(input, line, too_long))
			return too_long ? fail(Status::ResponseTooLarge) : Result::NeedMore;
		if (too_long) return fail(Status::ResponseTooLarge);

		if (line.empty())
		{
			state = State::Done;
			return Result::Complete;
		}
	}
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parseBodyUntilClose(Samurai::IO::Buffer& input)
{
	if (input.size())
	{
		if (response.body.size() + input.size() > limits.maxBody)
			return fail(Status::ResponseTooLarge);

		response.body += input.copyRange(0, input.size());
		input.remove(input.size());
	}

	if (!closed) return Result::NeedMore;

	state = State::Done;
	return Result::Complete;
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::parse(Samurai::IO::Buffer& input)
{
	if (status != Status::Ok) return Result::Error;

	for (;;)
	{
		/*
		 * What the pass has to change for another to be worth running. A state
		 * that neither consumed a byte nor moved on is waiting for input that
		 * has not arrived - a partial chunk-size line, say - and running it
		 * again would spin rather than block.
		 */
		const State before = state;
		const size_t available = input.size();

		Result result = Result::NeedMore;

		switch (state)
		{
			case State::StatusLine:     result = parseStatusLine(input); break;
			case State::HeaderBlock:    result = parseHeaderBlock(input); break;
			case State::BodyLength:     result = parseBodyLength(input); break;
			case State::BodyChunkSize:  result = parseChunkSize(input); break;
			case State::BodyChunkData:  result = parseChunkData(input); break;
			case State::BodyChunkCrlf:  result = parseChunkCrlf(input); break;
			case State::BodyTrailers:   result = parseTrailers(input); break;
			case State::BodyUntilClose: result = parseBodyUntilClose(input); break;
			case State::Done:           return Result::Complete;
		}

		if (result != Result::NeedMore) return result;

		if (state == before && input.size() == available) return Result::NeedMore;
	}
}


Samurai::IO::Net::HTTP::ResponseParser::Result
Samurai::IO::Net::HTTP::ResponseParser::finish()
{
	if (status != Status::Ok) return Result::Error;
	if (state == State::Done) return Result::Complete;

	closed = true;

	if (state == State::BodyUntilClose)
	{
		state = State::Done;
		return Result::Complete;
	}

	/* Anything else was still waiting for bytes that will not arrive. */
	return fail(Status::Disconnected);
}
