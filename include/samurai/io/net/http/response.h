/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_HTTP_RESPONSE_H
#define HAVE_SAMURAI_HTTP_RESPONSE_H

#include <stddef.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Samurai {
namespace IO {

class Buffer;

namespace Net {
namespace HTTP {

enum class Method { Get, Post, Head };

/** The method as it goes on the request line. */
const char* toString(Method method);

/**
 * Why a request did not produce a response, or Ok when it did.
 *
 * Note that Ok says a response arrived, not that the server liked the request:
 * a 500 is Ok with a status code of 500. A UPnP fault is exactly that - a 500
 * whose body has to be read - so an HTTP status is not a transport failure.
 */
enum class Status
{
	Ok,
	InvalidUrl,
	ConnectFailed,
	ConnectTimeout,
	RequestTimeout,     /**< no complete response before the deadline */
	Disconnected,       /**< the peer closed before the response was complete */
	MalformedResponse,
	ResponseTooLarge,
	Aborted
};

const char* toString(Status status);

/**
 * A set of header fields, matched without regard to case.
 *
 * A vector rather than a map: a header block is a handful of fields, order is
 * worth keeping for a request, and duplicates are legal.
 */
class Headers final
{
	public:
		void add(std::string_view name, std::string_view value);

		/** Replace every field with this name, or add one if there is none. */
		void set(std::string_view name, std::string_view value);

		/** The first field with this name. */
		std::optional<std::string_view> get(std::string_view name) const;

		bool has(std::string_view name) const;

		/** How many fields carry this name. */
		size_t count(std::string_view name) const;

		const std::vector<std::pair<std::string, std::string>>& all() const
		{ return fields; }

		size_t size() const { return fields.size(); }
		void clear() { fields.clear(); }

	private:
		std::vector<std::pair<std::string, std::string>> fields;
};

/** A parsed response. */
class Response final
{
	public:
		/** 0 until a status line has been read. */
		int getStatusCode() const { return status_code; }

		const std::string& getReasonPhrase() const { return reason; }
		const Headers& getHeaders() const { return headers; }

		/** The body, with any chunked framing removed. */
		const std::string& getBody() const { return body; }

		/** True for 200 through 299. */
		bool isSuccess() const { return status_code >= 200 && status_code <= 299; }

		void clear();

	private:
		int status_code = 0;
		std::string reason;
		Headers headers;
		std::string body;

	friend class ResponseParser;
};

/**
 * An incremental HTTP/1.1 response parser.
 *
 * Deliberately knows nothing about sockets: it is handed a Buffer and consumes
 * what it can. That is what lets an SSDP search reply - an HTTP-shaped message
 * in one datagram - be read by this rather than by a second header parser
 * written beside it.
 */
class ResponseParser final
{
	public:
		enum class Result { NeedMore, Complete, Error };

		struct Limits
		{
			size_t maxHeaderBlock = 32 * 1024;
			size_t maxHeaderCount = 100;
			size_t maxBody        = 1024 * 1024;
			size_t maxChunkSize   = 1024 * 1024;
		};

		ResponseParser();
		explicit ResponseParser(const Limits& limits);

		/**
		 * Consume what 'input' holds, removing the bytes taken from it.
		 *
		 * Whatever is left behind belongs to the next message and is not
		 * touched. Buffer::remove() advances a read index rather than moving the
		 * remainder, so feeding this one byte at a time costs the same as
		 * feeding it all at once.
		 */
		Result parse(Samurai::IO::Buffer& input);

		/**
		 * Tell the parser the peer has closed.
		 *
		 * A response carrying neither a Content-Length nor a chunked encoding
		 * is delimited by exactly that, and embedded HTTP servers send them:
		 * without this such a body would never be reported complete.
		 */
		Result finish();

		/**
		 * Say that this response cannot carry a body whatever its headers
		 * claim, which is true of a reply to HEAD and of 204 and 304. The
		 * parser cannot know the request, so the caller has to.
		 */
		void expectNoBody() { no_body = true; }

		/** Why parse() or finish() returned Error. */
		Status getStatus() const { return status; }

		const Response& getResponse() const { return response; }

		/** Forget everything, so the parser can read another response. */
		void reset();

	private:
		enum class State
		{
			StatusLine,
			HeaderBlock,
			BodyLength,
			BodyChunkSize,
			BodyChunkData,
			BodyChunkCrlf,
			BodyTrailers,
			BodyUntilClose,
			Done
		};

		Result fail(Status why);
		Result parseStatusLine(Samurai::IO::Buffer& input);
		Result parseHeaderBlock(Samurai::IO::Buffer& input);
		Result chooseBodyFraming();
		Result parseBodyLength(Samurai::IO::Buffer& input);
		Result parseChunkSize(Samurai::IO::Buffer& input);
		Result parseChunkData(Samurai::IO::Buffer& input);
		Result parseChunkCrlf(Samurai::IO::Buffer& input);
		Result parseTrailers(Samurai::IO::Buffer& input);
		Result parseBodyUntilClose(Samurai::IO::Buffer& input);

		/**
		 * Take one line, terminated by CRLF or by a bare LF.
		 *
		 * A bare LF is not conforming, and is accepted because cheap embedded
		 * servers emit it and the alternative is failing to talk to them.
		 */
		bool takeLine(Samurai::IO::Buffer& input, std::string& line, bool& too_long);

		Limits limits;
		State state = State::StatusLine;
		Status status = Status::Ok;
		Response response;

		size_t header_bytes = 0;
		size_t body_remaining = 0;
		size_t chunk_remaining = 0;
		bool no_body = false;
		bool closed = false;
};

}
}
}
}

#endif // HAVE_SAMURAI_HTTP_RESPONSE_H
