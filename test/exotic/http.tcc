/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "testkeys.h"

#include <samurai/io/buffer.h>
#include <samurai/io/net/http/client.h>
#include <samurai/io/net/http/response.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/url.h>
#include <samurai/timer.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/*
 * Most of this drives ResponseParser directly, which needs no socket: it is
 * handed a Buffer, which is also how an SSDP search reply gets read by the same
 * code rather than by a second header parser.
 *
 * The Client cases stand up a server on the loopback that answers from a canned
 * string, so they are deterministic on any machine and reach no network.
 */

namespace {

using Samurai::IO::Buffer;
using Samurai::IO::Net::SocketMonitor;
using Samurai::IO::Net::URL;
using namespace Samurai::IO::Net::HTTP;

/* ------------------------------------------------------------------------- */
/* Parser helpers                                                            */
/* ------------------------------------------------------------------------- */

/** Feed the whole message at once. */
ResponseParser::Result feed(ResponseParser& parser, std::string_view message)
{
	Buffer input;
	input.append(message.data(), message.size());
	return parser.parse(input);
}

/** Feed at once, then report the close. */
ResponseParser::Result feed_and_close(ResponseParser& parser, std::string_view message)
{
	const ResponseParser::Result result = feed(parser, message);
	if (result != ResponseParser::Result::NeedMore) return result;
	return parser.finish();
}

bool parses(std::string_view message, Response& out)
{
	ResponseParser parser;
	if (feed(parser, message) != ResponseParser::Result::Complete) return false;
	out = parser.getResponse();
	return true;
}

bool refuses_with(std::string_view message, Status expected)
{
	ResponseParser parser;
	Buffer input;
	input.append(message.data(), message.size());

	if (parser.parse(input) != ResponseParser::Result::Error)
	{
		/* Not an error yet; the close may settle it. */
		if (parser.finish() != ResponseParser::Result::Error) return false;
	}
	return parser.getStatus() == expected;
}

const char* LENGTH_RESPONSE =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/xml\r\n"
	"Content-Length: 11\r\n"
	"\r\n"
	"hello world";

const char* CHUNKED_RESPONSE =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/xml\r\n"
	"Transfer-Encoding: chunked\r\n"
	"\r\n"
	"5\r\nhello\r\n"
	"1\r\n \r\n"
	"5\r\nworld\r\n"
	"0\r\n"
	"\r\n";

const char* CLOSE_RESPONSE =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/xml\r\n"
	"\r\n"
	"hello world";

/**
 * Feed one byte per call and compare against feeding it all at once.
 *
 * This is where an incremental parser fails, so it is the case worth having:
 * every partial state has to survive being resumed.
 */
bool byte_at_a_time_matches(std::string_view message, bool close_to_finish)
{
	ResponseParser whole;
	const ResponseParser::Result at_once = close_to_finish
		? feed_and_close(whole, message)
		: feed(whole, message);
	if (at_once != ResponseParser::Result::Complete) return false;

	ResponseParser drip;
	Buffer input;
	ResponseParser::Result result = ResponseParser::Result::NeedMore;

	for (size_t n = 0; n < message.size(); n++)
	{
		input.append(message.data() + n, 1);
		result = drip.parse(input);
		if (result == ResponseParser::Result::Error) return false;
		if (result == ResponseParser::Result::Complete) break;
	}

	if (result != ResponseParser::Result::Complete && close_to_finish)
		result = drip.finish();

	if (result != ResponseParser::Result::Complete) return false;

	const Response& a = whole.getResponse();
	const Response& b = drip.getResponse();

	return a.getStatusCode() == b.getStatusCode()
		&& a.getReasonPhrase() == b.getReasonPhrase()
		&& a.getBody() == b.getBody()
		&& a.getHeaders().size() == b.getHeaders().size();
}

/* ------------------------------------------------------------------------- */
/* A server on the loopback that answers from a canned string                 */
/* ------------------------------------------------------------------------- */

class CannedServer
	: public Samurai::IO::Net::ServerSocketEventHandler
	, public Samurai::IO::Net::SocketEventHandler
{
	public:
		std::string reply;
		/* Everything the client sent, so a case can assert on the request. */
		std::string request;
		bool answer = true;
		bool close_without_answering = false;
		int connections = 0;
		int concurrent = 0;
		int most_concurrent = 0;

		explicit CannedServer(std::string canned) : reply(std::move(canned))
		{
			Samurai::IO::Net::InetSocketAddress any((uint16_t) 0);
			server = Samurai::IO::Net::ServerSocket::create(this, any);
			if (!server || server->getFD() == -1) return;
			if (!server->listen()) return;
			port = server->getLocalPort();
		}

		~CannedServer() override
		{
			accepted.reset();
			server.reset();
		}

		CannedServer(const CannedServer&) = delete;
		CannedServer& operator=(const CannedServer&) = delete;

		bool ready() const { return server && port != 0; }
		uint16_t getPort() const { return port; }

		URL url(const char* path) const
		{
			return URL("http://127.0.0.1:" + std::to_string(port) + path);
		}

	protected:
		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char*) override
		{ }

		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*,
			std::shared_ptr<Samurai::IO::Net::Socket> socket) override
		{
			connections++;
			concurrent++;
			if (concurrent > most_concurrent) most_concurrent = concurrent;

			accepted = socket;
			accepted->setEventHandler(this);

			/* Each connection is answered from the start of the reply. */
			sent = 0;
			sending = false;
		}

		void EventDataAvailable(const Samurai::IO::Net::Socket* which) override
		{
			char scratch[4096];
			size_t got = 0;
			std::error_code ec;

			Samurai::IO::Net::Socket* socket = const_cast<Samurai::IO::Net::Socket*>(which);
			if (socket->read(scratch, sizeof(scratch), got, ec) !=
				Samurai::IO::ReadResult::Ok) return;

			request.append(scratch, got);

			/* The head is all this needs to decide to answer. */
			if (request.find("\r\n\r\n") == std::string::npos) return;

			if (close_without_answering)
			{
				concurrent--;
				accepted.reset();
				return;
			}

			if (!answer) return;

			sending = true;
			pump_reply(socket);
		}

		void EventCanWrite(const Samurai::IO::Net::Socket* which) override
		{
			if (!sending) return;
			pump_reply(const_cast<Samurai::IO::Net::Socket*>(which));
		}

		void EventDisconnected(const Samurai::IO::Net::Socket*) override
		{ }

	private:
		/*
		 * A reply larger than the socket buffer does not go out in one write, so
		 * what is left is finished under the write notifier. Without this a big
		 * body is silently truncated and the case that needed it passes or fails
		 * for the wrong reason.
		 */
		void pump_reply(Samurai::IO::Net::Socket* socket)
		{
			while (sent < reply.size())
			{
				const ssize_t wrote = socket->write(reply.data() + sent, reply.size() - sent);
				if (wrote <= 0)
				{
					socket->toggleWriteNotifier(true);
					return;
				}
				sent += (size_t) wrote;
			}

			sending = false;
			concurrent--;
			/* Closing is what delimits a body with no Content-Length. */
			accepted.reset();
		}

		std::shared_ptr<Samurai::IO::Net::ServerSocket> server;
		std::shared_ptr<Samurai::IO::Net::Socket> accepted;
		uint16_t port = 0;
		size_t sent = 0;
		bool sending = false;
};

/*
 * Answers each connection with the next reply in a list, so a redirect chain can
 * be laid out in the order the client will walk it. The last reply is repeated
 * once the list runs out, which is what makes a chain longer than the follow
 * limit easy to write.
 */
class SequenceServer
	: public Samurai::IO::Net::ServerSocketEventHandler
	, public Samurai::IO::Net::SocketEventHandler
{
	public:
		std::vector<std::string> replies;
		/* Every request line seen, in order, so a case can assert the path and
		   the method the client used after a redirect. */
		std::vector<std::string> requests;

		explicit SequenceServer(std::vector<std::string> canned)
			: replies(std::move(canned))
		{
			Samurai::IO::Net::InetSocketAddress any((uint16_t) 0);
			server = Samurai::IO::Net::ServerSocket::create(this, any);
			if (!server || server->getFD() == -1) return;
			if (!server->listen()) return;
			port = server->getLocalPort();
		}

		~SequenceServer() override { accepted.reset(); server.reset(); }

		SequenceServer(const SequenceServer&) = delete;
		SequenceServer& operator=(const SequenceServer&) = delete;

		bool ready() const { return server && port != 0 && !replies.empty(); }
		uint16_t getPort() const { return port; }

		URL url(const char* path) const
		{
			return URL("http://127.0.0.1:" + std::to_string(port) + path);
		}

		/** An absolute URL back to this server, for a Location header. */
		std::string absolute(const char* path) const
		{
			return "http://127.0.0.1:" + std::to_string(port) + path;
		}

	protected:
		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char*) override { }

		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*,
			std::shared_ptr<Samurai::IO::Net::Socket> socket) override
		{
			accepted = socket;
			pending.clear();
			accepted->setEventHandler(this);
		}

		void EventDataAvailable(const Samurai::IO::Net::Socket* which) override
		{
			char scratch[4096];
			size_t got = 0;
			std::error_code ec;

			Samurai::IO::Net::Socket* socket = const_cast<Samurai::IO::Net::Socket*>(which);
			if (socket->read(scratch, sizeof(scratch), got, ec) !=
				Samurai::IO::ReadResult::Ok) return;

			pending.append(scratch, got);
			const size_t end = pending.find("\r\n\r\n");
			if (end == std::string::npos) return;

			requests.push_back(pending.substr(0, pending.find("\r\n")));

			const size_t which_reply = (served < replies.size())
				? served : replies.size() - 1;
			served++;

			socket->write(replies[which_reply].data(), replies[which_reply].size(), ec);
			accepted.reset();
		}

		void EventDisconnected(const Samurai::IO::Net::Socket*) override { }

	private:
		std::shared_ptr<Samurai::IO::Net::ServerSocket> server;
		std::shared_ptr<Samurai::IO::Net::Socket> accepted;
		std::string pending;
		size_t served = 0;
		uint16_t port = 0;
};

/*
 * The same canned server over TLS, so the client's https path is exercised end
 * to end rather than only as far as the scheme check.
 *
 * The certificate is self-signed and names "localhost", so no trust store can
 * verify it against a loopback address. Both ends are therefore given the
 * process-wide allow-untrusted default while initialize() reads it, and it is
 * put back straight away - the point of the cases below is what the *client's*
 * own option does, not what this default does.
 */
class TlsCannedServer
	: public Samurai::IO::Net::ServerSocketEventHandler
	, public Samurai::IO::Net::SocketEventHandler
{
	public:
		std::string reply;
		std::string request;

		explicit TlsCannedServer(std::string canned) : reply(std::move(canned))
		{
			if (!tls_test_keys().ready) return;

			Samurai::IO::Net::InetSocketAddress any((uint16_t) 0);
			server = Samurai::IO::Net::ServerSocket::create(this, any);
			if (!server || server->getFD() == -1) return;
			if (!server->listen()) return;
			port = server->getLocalPort();
		}

		~TlsCannedServer() override { accepted.reset(); server.reset(); }

		TlsCannedServer(const TlsCannedServer&) = delete;
		TlsCannedServer& operator=(const TlsCannedServer&) = delete;

		bool ready() const { return server && port != 0 && tls_test_keys().ready; }
		uint16_t getPort() const { return port; }

		URL url(const char* path) const
		{
			return URL("https://127.0.0.1:" + std::to_string(port) + path);
		}

	protected:
		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char*) override { }

		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*,
			std::shared_ptr<Samurai::IO::Net::Socket> socket) override
		{
			accepted = socket;
			accepted->setEventHandler(this);

			const bool untrusted = Samurai::IO::Net::TlsFactory::defaultAllowUntrusted();
			Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(true);
			const bool initialized = accepted->TLSInitialize(true);
			Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(untrusted);

			if (!initialized) { accepted.reset(); return; }
			accepted->TLSsendHandshake();
		}

		void EventTLSConnected(const Samurai::IO::Net::Socket*) override { }

		void EventDataAvailable(const Samurai::IO::Net::Socket* which) override
		{
			char scratch[4096];
			size_t got = 0;
			std::error_code ec;

			Samurai::IO::Net::Socket* socket = const_cast<Samurai::IO::Net::Socket*>(which);
			if (socket->read(scratch, sizeof(scratch), got, ec) !=
				Samurai::IO::ReadResult::Ok) return;

			request.append(scratch, got);
			if (request.find("\r\n\r\n") == std::string::npos) return;

			socket->write(reply.data(), reply.size(), ec);
			accepted.reset();
		}

		void EventDisconnected(const Samurai::IO::Net::Socket*) override { }

	private:
		std::shared_ptr<Samurai::IO::Net::ServerSocket> server;
		std::shared_ptr<Samurai::IO::Net::Socket> accepted;
		uint16_t port = 0;
};

/** Records one exchange. */
class ClientRecorder : public RequestEventHandler
{
	public:
		bool done = false;
		bool ok = false;
		Status status = Status::Ok;
		Response response;

	protected:
		void EventHttpResponse(Client*, const Response& answer) override
		{
			response = answer;
			ok = true;
			done = true;
		}

		void EventHttpError(Client*, Status why, const char*) override
		{
			status = why;
			done = true;
		}
};

template<typename Fn>
bool http_pump(Fn done, int timeout_ms = 5000)
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	Samurai::TimerManager* timers = Samurai::TimerManager::getInstance();

	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(timeout_ms);

	while (!done())
	{
		if (std::chrono::steady_clock::now() > deadline) return done();
		monitor->wait(5);
		/* Driven here too, so these pass whether or not wait() does it. */
		timers->process();
	}
	return true;
}

}

/* ------------------------------------------------------------------------- */
/* Headers                                                                   */
/* ------------------------------------------------------------------------- */

EXO_TEST(http_headers_lookup_is_case_insensitive,
{
	Headers headers;
	headers.add("Content-Length", "42");
	return headers.get("content-length") == "42"
		&& headers.get("CONTENT-LENGTH") == "42"
		&& headers.has("Content-Length");
});

EXO_TEST(http_headers_get_of_an_absent_field_is_empty,
{
	Headers headers;
	headers.add("Host", "x");
	return !headers.get("Content-Length").has_value() && !headers.has("Nonesuch");
});

EXO_TEST(http_headers_add_keeps_duplicates,
{
	Headers headers;
	headers.add("Set-Cookie", "a");
	headers.add("Set-Cookie", "b");
	return headers.count("Set-Cookie") == 2 && headers.get("set-cookie") == "a";
});

EXO_TEST(http_headers_set_replaces_every_duplicate,
{
	Headers headers;
	headers.add("X", "a");
	headers.add("x", "b");
	headers.set("X", "c");
	return headers.count("X") == 1 && headers.get("X") == "c";
});

EXO_TEST(http_headers_set_adds_when_absent,
{
	Headers headers;
	headers.set("Host", "router");
	return headers.size() == 1 && headers.get("Host") == "router";
});

/* ------------------------------------------------------------------------- */
/* Framing                                                                   */
/* ------------------------------------------------------------------------- */

EXO_TEST(http_parses_a_content_length_response,
{
	Response response;
	if (!parses(LENGTH_RESPONSE, response)) return false;

	return response.getStatusCode() == 200
		&& response.getReasonPhrase() == "OK"
		&& response.getBody() == "hello world"
		&& response.getHeaders().get("Content-Type") == "text/xml"
		&& response.isSuccess();
});

EXO_TEST(http_parses_an_empty_body,
{
	Response response;
	if (!parses("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", response)) return false;
	return response.getBody().empty() && response.getStatusCode() == 200;
});

EXO_TEST(http_parses_a_response_with_no_reason_phrase,
{
	Response response;
	if (!parses("HTTP/1.1 204\r\n\r\n", response)) return false;
	return response.getStatusCode() == 204 && response.getReasonPhrase().empty();
});

EXO_TEST(http_parses_a_chunked_response,
{
	Response response;
	if (!parses(CHUNKED_RESPONSE, response)) return false;
	return response.getBody() == "hello world";
});

EXO_TEST(http_parses_a_chunked_response_with_extensions,
{
	Response response;
	const char* message =
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
		"5;name=value\r\nhello\r\n"
		"0\r\n\r\n";
	if (!parses(message, response)) return false;
	return response.getBody() == "hello";
});

EXO_TEST(http_parses_a_chunked_response_with_trailers,
{
	Response response;
	const char* message =
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
		"5\r\nhello\r\n"
		"0\r\n"
		"X-Checksum: 1234\r\n"
		"\r\n";
	if (!parses(message, response)) return false;
	return response.getBody() == "hello";
});

EXO_TEST(http_parses_uppercase_chunk_sizes,
{
	Response response;
	const char* message =
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
		"A\r\n0123456789\r\n0\r\n\r\n";
	if (!parses(message, response)) return false;
	return response.getBody() == "0123456789";
});

/* A body with neither framing is delimited by the close. Embedded HTTP servers
   send them constantly, so this has to work rather than being malformed. */
EXO_TEST(http_close_delimited_body_needs_finish,
{
	ResponseParser parser;
	if (feed(parser, CLOSE_RESPONSE) != ResponseParser::Result::NeedMore) return false;
	if (parser.finish() != ResponseParser::Result::Complete) return false;
	return parser.getResponse().getBody() == "hello world";
});

EXO_TEST(http_close_delimited_empty_body_is_complete,
{
	ResponseParser parser;
	if (feed(parser, "HTTP/1.1 200 OK\r\n\r\n") != ResponseParser::Result::NeedMore)
		return false;
	return parser.finish() == ResponseParser::Result::Complete
		&& parser.getResponse().getBody().empty();
});

/* A truncated Content-Length body is not complete just because the peer went
   away: the difference between that and a close-delimited body is why finish()
   exists. */
EXO_TEST(http_truncated_length_body_is_a_disconnect,
{
	ResponseParser parser;
	feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort");
	return parser.finish() == ResponseParser::Result::Error
		&& parser.getStatus() == Status::Disconnected;
});

EXO_TEST(http_head_response_has_no_body,
{
	ResponseParser parser;
	parser.expectNoBody();
	if (feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n")
		!= ResponseParser::Result::Complete) return false;
	return parser.getResponse().getBody().empty();
});

EXO_TEST(http_status_204_and_304_have_no_body,
{
	Response response;
	if (!parses("HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n", response))
		return false;
	if (!response.getBody().empty()) return false;

	return parses("HTTP/1.1 304 Not Modified\r\n\r\n", response)
		&& response.getBody().empty();
});

/* Chunked wins over Content-Length when both appear; disagreeing about which to
   believe is the whole of request smuggling. */
EXO_TEST(http_chunked_wins_over_content_length,
{
	Response response;
	const char* message =
		"HTTP/1.1 200 OK\r\n"
		"Content-Length: 3\r\n"
		"Transfer-Encoding: chunked\r\n\r\n"
		"5\r\nhello\r\n0\r\n\r\n";
	if (!parses(message, response)) return false;
	return response.getBody() == "hello";
});

EXO_TEST(http_accepts_http_1_0,
{
	Response response;
	if (!parses("HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nhi", response)) return false;
	return response.getStatusCode() == 200;
});

/* Cheap routers emit bare LF, and refusing them means not talking to them. */
EXO_TEST(http_accepts_bare_lf_line_endings,
{
	Response response;
	if (!parses("HTTP/1.1 200 OK\nContent-Length: 2\n\nhi", response)) return false;
	return response.getBody() == "hi" && response.getStatusCode() == 200;
});

/* The requirement that decides how a SOAP fault is delivered: a 500 carries a
   body naming the error, so it is a response and not a transport failure. */
EXO_TEST(http_reports_a_five_hundred_as_a_response_not_an_error,
{
	Response response;
	const char* message =
		"HTTP/1.1 500 Internal Server Error\r\n"
		"Content-Length: 9\r\n\r\n"
		"<s:Fault>";
	if (!parses(message, response)) return false;
	return response.getStatusCode() == 500
		&& !response.isSuccess()
		&& response.getBody() == "<s:Fault>";
});

/* A pipelined second message must be left alone. */
EXO_TEST(http_leaves_trailing_bytes_in_the_buffer,
{
	ResponseParser parser;
	Buffer input;
	const std::string two = std::string(LENGTH_RESPONSE) + "HTTP/1.1 200 OK\r\n";
	input.append(two.data(), two.size());

	if (parser.parse(input) != ResponseParser::Result::Complete) return false;
	return input.size() == 17;
});

EXO_TEST(http_reset_allows_the_parser_to_be_reused,
{
	ResponseParser parser;
	if (feed(parser, LENGTH_RESPONSE) != ResponseParser::Result::Complete) return false;

	parser.reset();
	if (feed(parser, "HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\n\r\nno")
		!= ResponseParser::Result::Complete) return false;

	return parser.getResponse().getStatusCode() == 404
		&& parser.getResponse().getBody() == "no";
});

/* ------------------------------------------------------------------------- */
/* Byte at a time                                                            */
/* ------------------------------------------------------------------------- */

EXO_TEST(http_byte_at_a_time_parses_a_length_body_identically,
{
	return byte_at_a_time_matches(LENGTH_RESPONSE, false);
});

EXO_TEST(http_byte_at_a_time_parses_a_chunked_body_identically,
{
	return byte_at_a_time_matches(CHUNKED_RESPONSE, false);
});

EXO_TEST(http_byte_at_a_time_parses_a_close_delimited_body_identically,
{
	return byte_at_a_time_matches(CLOSE_RESPONSE, true);
});

EXO_TEST(http_byte_at_a_time_parses_trailers_identically,
{
	const char* message =
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
		"5\r\nhello\r\n0\r\nX-T: 1\r\n\r\n";
	return byte_at_a_time_matches(message, false);
});

/* ------------------------------------------------------------------------- */
/* What is refused                                                           */
/* ------------------------------------------------------------------------- */

EXO_TEST(http_rejects_a_bad_status_line,
{
	return refuses_with("nonsense\r\n\r\n", Status::MalformedResponse)
		&& refuses_with("HTTP/1.1\r\n\r\n", Status::MalformedResponse)
		&& refuses_with("HTTP/9.9 200 OK\r\n\r\n", Status::MalformedResponse)
		&& refuses_with("HTTP/1.1 xyz OK\r\n\r\n", Status::MalformedResponse)
		&& refuses_with("HTTP/1.1 20 OK\r\n\r\n", Status::MalformedResponse)
		&& refuses_with("HTTP/1.1 099 OK\r\n\r\n", Status::MalformedResponse);
});

/*
 * An obsolete line folding is refused rather than un-folded: RFC 7230
 * deprecated it, and joining the pieces is how a value smuggles in something
 * the sender split deliberately.
 */
EXO_TEST(http_rejects_an_obs_fold_header,
{
	return refuses_with(
		"HTTP/1.1 200 OK\r\nX: a\r\n  b\r\nContent-Length: 0\r\n\r\n",
		Status::MalformedResponse);
});

EXO_TEST(http_rejects_a_header_with_no_colon,
{
	return refuses_with("HTTP/1.1 200 OK\r\nbroken\r\n\r\n", Status::MalformedResponse);
});

EXO_TEST(http_rejects_space_before_the_colon,
{
	return refuses_with("HTTP/1.1 200 OK\r\nX : a\r\n\r\n", Status::MalformedResponse);
});

EXO_TEST(http_rejects_a_non_numeric_content_length,
{
	return refuses_with("HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n",
	                    Status::MalformedResponse);
});

EXO_TEST(http_rejects_conflicting_content_lengths,
{
	return refuses_with(
		"HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\nabc",
		Status::MalformedResponse);
});

EXO_TEST(http_accepts_agreeing_content_lengths,
{
	Response response;
	return parses("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc",
	              response) && response.getBody() == "abc";
});

EXO_TEST(http_rejects_an_unknown_transfer_encoding,
{
	return refuses_with("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n",
	                    Status::MalformedResponse);
});

EXO_TEST(http_rejects_a_non_hex_chunk_size,
{
	return refuses_with(
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n",
		Status::MalformedResponse);
});

EXO_TEST(http_rejects_a_chunk_not_followed_by_a_terminator,
{
	return refuses_with(
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhelloXX\r\n0\r\n\r\n",
		Status::MalformedResponse);
});

EXO_TEST(http_rejects_a_body_over_the_limit,
{
	ResponseParser::Limits limits;
	limits.maxBody = 4;

	ResponseParser parser(limits);
	if (feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n")
		!= ResponseParser::Result::Error) return false;
	return parser.getStatus() == Status::ResponseTooLarge;
});

EXO_TEST(http_rejects_a_chunk_size_over_the_limit,
{
	ResponseParser::Limits limits;
	limits.maxChunkSize = 4;

	ResponseParser parser(limits);
	if (feed(parser, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFF\r\n")
		!= ResponseParser::Result::Error) return false;
	return parser.getStatus() == Status::ResponseTooLarge;
});

EXO_TEST(http_rejects_a_close_delimited_body_over_the_limit,
{
	ResponseParser::Limits limits;
	limits.maxBody = 4;

	ResponseParser parser(limits);
	return feed(parser, CLOSE_RESPONSE) == ResponseParser::Result::Error
		&& parser.getStatus() == Status::ResponseTooLarge;
});

EXO_TEST(http_rejects_too_many_headers,
{
	ResponseParser::Limits limits;
	limits.maxHeaderCount = 2;

	std::string message = "HTTP/1.1 200 OK\r\n";
	for (int n = 0; n < 8; n++) message += "X" + std::to_string(n) + ": 1\r\n";
	message += "\r\n";

	ResponseParser parser(limits);
	return feed(parser, message) == ResponseParser::Result::Error
		&& parser.getStatus() == Status::ResponseTooLarge;
});

EXO_TEST(http_rejects_a_header_block_over_the_limit,
{
	ResponseParser::Limits limits;
	limits.maxHeaderBlock = 32;

	std::string message = "HTTP/1.1 200 OK\r\nX: ";
	message += std::string(200, 'a');
	message += "\r\n\r\n";

	ResponseParser parser(limits);
	return feed(parser, message) == ResponseParser::Result::Error
		&& parser.getStatus() == Status::ResponseTooLarge;
});

/* A peer that never sends a line terminator must not be able to make the parser
   buffer without bound. */
EXO_TEST(http_rejects_an_unterminated_header_line,
{
	ResponseParser::Limits limits;
	limits.maxHeaderBlock = 64;

	ResponseParser parser(limits);
	Buffer input;
	const std::string endless(200, 'a');
	input.append(endless.data(), endless.size());

	return parser.parse(input) == ResponseParser::Result::Error
		&& parser.getStatus() == Status::ResponseTooLarge;
});

EXO_TEST(http_parse_after_an_error_stays_an_error,
{
	ResponseParser parser;
	if (feed(parser, "nonsense\r\n\r\n") != ResponseParser::Result::Error) return false;
	return feed(parser, LENGTH_RESPONSE) == ResponseParser::Result::Error;
});

/*
 * Walked over the enumerators rather than a list written out here, which a new
 * status would not be added to - and checked against the fallback rather than
 * against empty, since a value with no case of its own is what returns that. So
 * this fails when a status is added and left unnamed.
 *
 * Aborted is last; anything inserted before it is covered by construction.
 */
EXO_TEST(http_tostring_names_every_status,
{
	for (int n = 0; n <= (int) Status::Aborted; n++)
	{
		const char* name = toString((Status) n);
		if (!name || !*name) return false;
		if (std::string(name) == "unknown") return false;
	}

	return toString(Method::Get) == std::string("GET")
		&& toString(Method::Post) == std::string("POST")
		&& toString(Method::Head) == std::string("HEAD");
});

/* ------------------------------------------------------------------------- */
/* The client, against a server on the loopback                              */
/* ------------------------------------------------------------------------- */

EXO_TEST(http_client_rejects_a_bad_url,
{
	ClientRecorder events;
	Client client(&events);
	client.get(URL("not a url"));

	return events.done && !events.ok && events.status == Status::InvalidUrl;
});

/* https is supported now; a scheme this client does not speak is not. */
EXO_TEST(http_client_rejects_a_non_http_scheme,
{
	ClientRecorder events;
	Client client(&events);
	client.get(URL("ftp://example.org/"));

	return events.done && events.status == Status::InvalidUrl;
});

EXO_TEST(http_client_accepts_the_https_scheme,
{
	/*
	 * Refused for want of a reachable server rather than for the scheme: port 1
	 * on the loopback interface has nothing listening, so this gets as far as
	 * trying to connect, which a rejected scheme never would.
	 */
	ClientRecorder events;
	Client client(&events);
	client.get(URL("https://127.0.0.1:1/"));

	http_pump([&] { return events.done; });
	return events.done && events.status != Status::InvalidUrl;
});

EXO_TEST(http_client_fetches_over_tls,
{
	TlsCannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.allowUntrustedCertificate = true;
	Client client(&events, options);
	client.get(server.url("/over-tls"));

	http_pump([&] { return events.done; });

	return events.ok
		&& events.status == Status::Ok
		&& events.response.getStatusCode() == 200
		&& events.response.getBody() == "hello world"
		&& server.request.find("GET /over-tls ") != std::string::npos;
});

/*
 * The one that matters: the same server, with the option left alone. A
 * self-signed certificate is what an attacker presents, so the default has to
 * refuse it rather than encrypt a conversation with whoever answered.
 */
EXO_TEST(http_client_refuses_an_unverifiable_certificate_by_default,
{
	TlsCannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/over-tls"));

	http_pump([&] { return events.done; });

	return events.done && !events.ok && events.status == Status::TlsFailed;
});

/*
 * The case that verification exists for: a certificate that does check out, over
 * a real handshake, with nothing relaxed.
 *
 * This needs the fixture's own certificate offered as a trust anchor, because it
 * is signed by nothing the system store has heard of - and it needs the
 * subjectAltName that testkeys.h now puts in it, since the peer name being
 * checked is the address 127.0.0.1 rather than a hostname. The anchor is cleared
 * again so that the case above, which requires the same certificate to be
 * refused, does not depend on the order the two run in.
 */
EXO_TEST(http_client_accepts_a_verifiable_certificate,
{
	TlsCannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	Samurai::IO::Net::TlsFactory::clearTrustAnchors();
	Samurai::IO::Net::TlsFactory::addTrustAnchor(
		tls_test_keys().cert_path.c_str());

	ClientRecorder events;
	/* Default options: allowUntrustedCertificate is false. */
	Client client(&events);
	client.get(server.url("/verified"));

	http_pump([&] { return events.done; });

	Samurai::IO::Net::TlsFactory::clearTrustAnchors();

	return events.ok
		&& events.response.getStatusCode() == 200
		&& events.response.getBody() == "hello world";
});

/* And the anchor is what did it, not the verification having been skipped. */
EXO_TEST(http_client_still_refuses_the_same_certificate_without_the_anchor,
{
	TlsCannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	Samurai::IO::Net::TlsFactory::clearTrustAnchors();

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/verified"));

	http_pump([&] { return events.done; });

	return events.done && !events.ok && events.status == Status::TlsFailed;
});

/*
 * https against a plain-HTTP port gives up on the deadline rather than failing
 * the handshake: the server reads the ClientHello, finds no end of headers in
 * it, and says nothing. What matters is that it neither hangs for ever nor
 * mistakes the reply for a response - so the timeout is shortened here and the
 * outcome asserted to be a failure with no response.
 */
EXO_TEST(http_client_over_https_to_a_plain_server_times_out,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.allowUntrustedCertificate = true;
	options.timeout = std::chrono::milliseconds(300);
	Client client(&events, options);
	client.get(URL("https://127.0.0.1:" + std::to_string(server.getPort()) + "/"));

	http_pump([&] { return events.done; });

	return events.done && !events.ok && events.status == Status::RequestTimeout;
});

EXO_TEST(http_client_fetches_from_a_local_server,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/rootDesc.xml"));

	if (!http_pump([&] { return events.done; })) return false;
	if (!events.ok) return false;

	return events.response.getStatusCode() == 200
		&& events.response.getBody() == "hello world";
});

EXO_TEST(http_client_sends_the_request_target_and_a_host_header,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/upnp/rootDesc.xml?x=1"));

	if (!http_pump([&] { return events.done; })) return false;

	const std::string expected_host =
		"Host: 127.0.0.1:" + std::to_string(server.getPort()) + "\r\n";

	return server.request.find("GET /upnp/rootDesc.xml?x=1 HTTP/1.1\r\n") == 0
		&& server.request.find(expected_host) != std::string::npos
		&& server.request.find("Connection: close\r\n") != std::string::npos;
});

EXO_TEST(http_client_posts_a_body_with_a_content_length,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.post(server.url("/ctl/IPConn"), "text/xml; charset=\"utf-8\"", "<envelope/>");

	if (!http_pump([&] { return events.done; })) return false;
	if (!events.ok) return false;

	return server.request.find("POST /ctl/IPConn HTTP/1.1\r\n") == 0
		&& server.request.find("Content-Length: 11\r\n") != std::string::npos
		&& server.request.find("Content-Type: text/xml; charset=\"utf-8\"\r\n")
			!= std::string::npos
		&& server.request.find("\r\n\r\n<envelope/>") != std::string::npos;
});

/* Sent with the case it was given: SOAPAction is matched case-sensitively by
   more than one router. */
EXO_TEST(http_client_sends_an_extra_header_verbatim,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	Headers extra;
	extra.set("SOAPAction", "\"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\"");

	ClientRecorder events;
	Client client(&events);
	client.request(Method::Post, server.url("/ctl"), extra, "<x/>");

	if (!http_pump([&] { return events.done; })) return false;

	return server.request.find(
		"SOAPAction: \"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\"\r\n")
		!= std::string::npos;
});

EXO_TEST(http_client_reads_a_close_delimited_body,
{
	CannedServer server(CLOSE_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/x"));

	if (!http_pump([&] { return events.done; })) return false;
	return events.ok && events.response.getBody() == "hello world";
});

EXO_TEST(http_client_reads_a_chunked_body,
{
	CannedServer server(CHUNKED_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/x"));

	if (!http_pump([&] { return events.done; })) return false;
	return events.ok && events.response.getBody() == "hello world";
});

EXO_TEST(http_client_reports_a_five_hundred_as_a_response,
{
	CannedServer server(
		"HTTP/1.1 500 Internal Server Error\r\nContent-Length: 5\r\n\r\nfault");
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.post(server.url("/ctl"), "text/xml", "<x/>");

	if (!http_pump([&] { return events.done; })) return false;
	return events.ok
		&& events.response.getStatusCode() == 500
		&& events.response.getBody() == "fault";
});

EXO_TEST(http_client_reports_a_disconnect_before_the_response,
{
	CannedServer server("");
	server.close_without_answering = true;
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/x"));

	if (!http_pump([&] { return events.done; })) return false;
	return !events.ok && events.status == Status::Disconnected;
});

EXO_TEST(http_client_times_out_against_a_server_that_never_replies,
{
	CannedServer server("");
	server.answer = false;
	if (!server.ready()) return false;

	Client::Options options;
	options.timeout = std::chrono::milliseconds(300);

	ClientRecorder events;
	Client client(&events, options);
	client.get(server.url("/x"));

	if (!http_pump([&] { return events.done; })) return false;
	return !events.ok && events.status == Status::RequestTimeout;
});

EXO_TEST(http_client_reports_a_refused_connection,
{
	uint16_t unused = 0;
	{
		/* A port that was listening and is not any more: nothing else on the
		   machine is likely to have taken it in between. */
		CannedServer probe("");
		if (!probe.ready()) return false;
		unused = probe.getPort();
	}

	Client::Options options;
	options.timeout = std::chrono::milliseconds(1000);

	ClientRecorder events;
	Client client(&events, options);
	client.get(URL("http://127.0.0.1:" + std::to_string(unused) + "/x"));

	if (!http_pump([&] { return events.done; })) return false;
	return !events.ok;
});

/* An abandoned request reports nothing, so a caller that has torn down its own
   state cannot be called back into it. */
EXO_TEST(http_client_abort_delivers_no_event,
{
	CannedServer server("");
	server.answer = false;
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/x"));
	client.abort();

	http_pump([&] { return false; }, 200);
	return !events.done && !client.isBusy();
});

/* Destroying a client mid-flight must not leave the socket calling back into
   freed memory - which is what the sanitizer build is watching for here. */
EXO_TEST(http_client_destroyed_in_flight_is_safe,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	{
		Client client(&events);
		client.get(server.url("/x"));
		/* One pass, so the connect is under way but the answer has not come. */
		SocketMonitor::getInstance()->wait(1);
	}

	http_pump([&] { return false; }, 200);
	return !events.done;
});

EXO_TEST(http_client_can_be_reused_for_a_second_request,
{
	CannedServer first(LENGTH_RESPONSE);
	if (!first.ready()) return false;

	ClientRecorder events;
	Client client(&events);

	client.get(first.url("/one"));
	if (!http_pump([&] { return events.done; })) return false;
	if (!events.ok) return false;

	events.done = false;
	events.ok = false;

	CannedServer second("HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\n\r\nno");
	if (!second.ready()) return false;

	client.get(second.url("/two"));
	if (!http_pump([&] { return events.done; })) return false;

	return events.ok && events.response.getStatusCode() == 404
		&& events.response.getBody() == "no";
});

EXO_TEST(http_client_reports_the_local_address_while_connected,
{
	CannedServer server("");
	server.answer = false;
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/x"));

	/* Waiting for the request to have gone out, which means connected. */
	if (!http_pump([&] { return !server.request.empty(); })) return false;

	const Samurai::IO::Net::InetAddress* local = client.getLocalAddress();
	return local && local->toString() == "127.0.0.1";
});

/* One request per connection, and the server must never see two at once. */
EXO_TEST(http_client_uses_one_connection_per_request,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);

	for (int n = 0; n < 3; n++)
	{
		events.done = false;
		client.get(server.url("/x"));
		if (!http_pump([&] { return events.done; })) return false;
		if (!events.ok) return false;
	}

	return server.connections == 3 && server.most_concurrent == 1;
});

EXO_TEST(http_blocking_fetch_returns_the_body,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	Response response;
	const Status status = get(server.url("/x"), response,
	                          std::chrono::milliseconds(3000));

	return status == Status::Ok
		&& response.getStatusCode() == 200
		&& response.getBody() == "hello world";
});

EXO_TEST(http_blocking_fetch_reports_a_timeout,
{
	CannedServer server("");
	server.answer = false;
	if (!server.ready()) return false;

	Response response;
	const Status status = get(server.url("/x"), response,
	                          std::chrono::milliseconds(300));

	return status == Status::RequestTimeout;
});

/* ------------------------------------------------------------------------- */
/* Redirects                                                                 */
/* ------------------------------------------------------------------------- */

/* Not followed unless asked for, which is what a UPnP gateway relies on. */
EXO_TEST(http_client_does_not_follow_a_redirect_by_default,
{
	SequenceServer server({ "HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\n"
	                        "Content-Length: 0\r\n\r\n", LENGTH_RESPONSE });
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/start"));

	http_pump([&] { return events.done; });

	return events.ok
		&& events.response.getStatusCode() == 302
		&& server.requests.size() == 1;
});

EXO_TEST(http_client_follows_a_redirect_when_allowed,
{
	SequenceServer server({ "", LENGTH_RESPONSE });
	if (!server.ready()) return false;
	server.replies[0] = "HTTP/1.1 302 Found\r\nLocation: "
		+ server.absolute("/final") + "\r\nContent-Length: 0\r\n\r\n";

	ClientRecorder events;
	Client::Options options;
	options.maxRedirects = 5;
	Client client(&events, options);
	client.get(server.url("/start"));

	http_pump([&] { return events.done; });

	return events.ok
		&& events.response.getStatusCode() == 200
		&& events.response.getBody() == "hello world"
		&& server.requests.size() == 2
		&& server.requests[1].find("GET /final ") != std::string::npos;
});

/* A Location is commonly a bare path, resolved against the URL it came from. */
EXO_TEST(http_client_resolves_a_relative_redirect,
{
	SequenceServer server({ "HTTP/1.1 301 Moved\r\nLocation: /moved/here\r\n"
	                        "Content-Length: 0\r\n\r\n", LENGTH_RESPONSE });
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.maxRedirects = 5;
	Client client(&events, options);
	client.get(server.url("/a/b"));

	http_pump([&] { return events.done; });

	return events.ok
		&& events.response.getBody() == "hello world"
		&& server.requests.size() == 2
		&& server.requests[1].find("GET /moved/here ") != std::string::npos;
});

EXO_TEST(http_client_stops_after_the_redirect_limit,
{
	/* Every reply redirects, so the limit is the only thing that ends it. */
	SequenceServer server({ "HTTP/1.1 302 Found\r\nLocation: /again\r\n"
	                        "Content-Length: 0\r\n\r\n" });
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.maxRedirects = 3;
	Client client(&events, options);
	client.get(server.url("/start"));

	http_pump([&] { return events.done; });

	return events.done && !events.ok
		&& events.status == Status::TooManyRedirects
		/* The first request plus the three that were followed. */
		&& server.requests.size() == 4;
});

/* Nothing to follow: handed back as it stands, since the body may explain. */
EXO_TEST(http_client_returns_a_redirect_with_no_location,
{
	SequenceServer server({ "HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n" });
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.maxRedirects = 5;
	Client client(&events, options);
	client.get(server.url("/start"));

	http_pump([&] { return events.done; });

	return events.ok
		&& events.response.getStatusCode() == 302
		&& server.requests.size() == 1;
});

/* 303 says to fetch the result with GET whatever was sent originally. */
EXO_TEST(http_client_turns_a_post_into_a_get_on_303,
{
	SequenceServer server({ "HTTP/1.1 303 See Other\r\nLocation: /result\r\n"
	                        "Content-Length: 0\r\n\r\n", LENGTH_RESPONSE });
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.maxRedirects = 5;
	Client client(&events, options);
	client.post(server.url("/submit"), "text/plain", "a body");

	http_pump([&] { return events.done; });

	return events.ok
		&& server.requests.size() == 2
		&& server.requests[0].find("POST /submit ") != std::string::npos
		&& server.requests[1].find("GET /result ") != std::string::npos;
});

/* 307 exists to say "again, exactly as before", so the method survives. */
EXO_TEST(http_client_keeps_the_method_on_307,
{
	SequenceServer server({ "HTTP/1.1 307 Temporary Redirect\r\nLocation: /again\r\n"
	                        "Content-Length: 0\r\n\r\n", LENGTH_RESPONSE });
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.maxRedirects = 5;
	Client client(&events, options);
	client.post(server.url("/submit"), "text/plain", "a body");

	http_pump([&] { return events.done; });

	return events.ok
		&& server.requests.size() == 2
		&& server.requests[1].find("POST /again ") != std::string::npos;
});

/*
 * The one that matters most: having verified a certificate, being sent to plain
 * http hands the rest of the exchange to anyone on the path. Refused whatever
 * the follow limit says.
 */
EXO_TEST(http_client_refuses_a_redirect_from_https_to_http,
{
	CannedServer plain(LENGTH_RESPONSE);
	if (!plain.ready()) return false;

	TlsCannedServer secure("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:"
		+ std::to_string(plain.getPort()) + "/\r\nContent-Length: 0\r\n\r\n");
	if (!secure.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.maxRedirects = 5;
	options.allowUntrustedCertificate = true;
	Client client(&events, options);
	client.get(secure.url("/start"));

	http_pump([&] { return events.done; });

	return events.done && !events.ok
		&& events.status == Status::InsecureRedirect
		/* And it really did not go there. */
		&& plain.connections == 0;
});

EXO_TEST(http_headers_remove_drops_every_duplicate,
{
	Headers headers;
	headers.add("X-Thing", "one");
	headers.add("Other", "keep");
	headers.add("x-thing", "two");

	const size_t dropped = headers.remove("X-THING");
	return dropped == 2 && headers.size() == 1 && headers.has("Other")
		&& !headers.has("X-Thing") && headers.remove("absent") == 0;
});

/* ------------------------------------------------------------------------- */
/* The client's limits                                                       */
/* ------------------------------------------------------------------------- */

EXO_TEST(http_client_honours_a_lowered_body_limit,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.limits.maxBody = 4;
	Client client(&events, options);
	client.get(server.url("/"));

	http_pump([&] { return events.done; });

	return events.done && !events.ok && events.status == Status::ResponseTooLarge;
});

/*
 * And the direction that matters to a caller with something big to fetch: the
 * parser's own default stops at a megabyte, so a raised limit only means
 * anything if it actually reaches the parser.
 */
EXO_TEST(http_client_honours_a_raised_body_limit,
{
	const std::string body(2 * 1024 * 1024, 'x');
	CannedServer server("HTTP/1.1 200 OK\r\nContent-Length: "
		+ std::to_string(body.size()) + "\r\n\r\n" + body);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client::Options options;
	options.limits.maxBody = 4 * 1024 * 1024;
	Client client(&events, options);
	client.get(server.url("/"));

	http_pump([&] { return events.done; }, 15000);

	return events.ok && events.response.getBody() == body;
});

/* setOptions between requests has to be what the next one is held to. */
EXO_TEST(http_client_uses_the_limits_in_force_at_request_time,
{
	CannedServer server(LENGTH_RESPONSE);
	if (!server.ready()) return false;

	ClientRecorder events;
	Client client(&events);
	client.get(server.url("/"));
	http_pump([&] { return events.done; });
	if (!events.ok) return false;

	Client::Options tightened;
	tightened.limits.maxBody = 4;
	client.setOptions(tightened);

	events = ClientRecorder();
	client.get(server.url("/"));
	http_pump([&] { return events.done; });

	return events.done && !events.ok && events.status == Status::ResponseTooLarge;
});
