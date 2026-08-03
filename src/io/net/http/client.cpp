/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/http/client.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/util/string.h>

#include <string.h>

namespace {

/** How much is read off the socket per readable event. */
constexpr size_t READ_CHUNK = 8192;

/**
 * The Host: field for a URL.
 *
 * The port is left off when it is the scheme's default: embedded HTTP servers
 * have been seen to refuse "Host: 192.168.1.1:80". An Version::IPv6 literal is
 * bracketed again, having been stored without.
 */
std::string host_header(const Samurai::IO::Net::URL& url)
{
	const std::string name = url.getHostname();
	std::string out;

	if (name.find(':') != std::string::npos)
	{
		out += '[';
		out += name;
		out += ']';
	}
	else
	{
		out += name;
	}

	const uint16_t port = url.getEffectivePort();
	if (port && port != Samurai::IO::Net::URL::getDefaultPort(url.getScheme()))
	{
		out += ':';
		out += std::to_string(port);
	}

	return out;
}

}


Samurai::IO::Net::HTTP::Client::Client(RequestEventHandler* eh)
	: eventHandler(eh)
{
}


Samurai::IO::Net::HTTP::Client::Client(RequestEventHandler* eh, const Options& opts)
	: eventHandler(eh), options(opts)
{
}


Samurai::IO::Net::HTTP::Client::~Client()
{
	/* The socket holds a raw pointer to this as its handler, so it has to stop
	   being monitored before this object goes away. */
	internal_teardown();
}


void Samurai::IO::Net::HTTP::Client::internal_teardown()
{
	deadline.reset();

	if (socket)
	{
		socket->setEventHandler(nullptr);
		socket->disableMonitor();
		socket.reset();
	}

	busy = false;
}


const Samurai::IO::Net::InetAddress*
Samurai::IO::Net::HTTP::Client::getLocalAddress() const
{
	if (local_address.getType() == InetAddress::Version::Unspecified) return nullptr;
	return &local_address;
}


void Samurai::IO::Net::HTTP::Client::get(const URL& url)
{
	const Headers none;
	request(Method::Get, url, none, std::string_view());
}


void Samurai::IO::Net::HTTP::Client::head(const URL& url)
{
	const Headers none;
	request(Method::Head, url, none, std::string_view());
}


void Samurai::IO::Net::HTTP::Client::post(const URL& url,
                                          std::string_view contentType,
                                          std::string_view body)
{
	Headers extra;
	extra.set("Content-Type", contentType);
	request(Method::Post, url, extra, body);
}


void Samurai::IO::Net::HTTP::Client::request(Method method, const URL& url,
                                             const Headers& extra,
                                             std::string_view body)
{
	if (busy) { internal_fail(Status::Aborted, "a request is already in flight"); return; }

	internal_teardown();
	parser.reset();
	incoming.clear();
	outgoing.clear();
	local_address = InetAddress();
	reported = false;
	secure = false;
	handshaking = false;

	if (!url.isValid() || url.getHostname().empty() || !url.getEffectivePort())
	{
		internal_fail(Status::InvalidUrl, "not a usable URL");
		return;
	}

	const bool is_http  = Samurai::Util::iequals(url.getScheme(), "http");
	const bool is_https = Samurai::Util::iequals(url.getScheme(), "https");

	if (!is_http && !is_https)
	{
		internal_fail(Status::InvalidUrl, "only http and https are supported");
		return;
	}

	secure = is_https;

	if (method == Method::Head) parser.expectNoBody();

	/*
	 * Assembled once into a buffer rather than written as a vector of pieces: a
	 * partial write has to be resumed from somewhere, and the request is a few
	 * hundred bytes, so holding it costs less than tracking a position across
	 * several spans would.
	 */
	Headers headers;
	headers.set("Host", host_header(url));
	headers.set("Connection", "close");
	if (!options.userAgent.empty()) headers.set("User-Agent", options.userAgent);

	const bool has_body = !body.empty();
	if (has_body || method == Method::Post)
		headers.set("Content-Length", std::to_string(body.size()));

	/* Whatever the caller named replaces what was supplied above. */
	for (const auto& [name, value] : extra.all())
		headers.set(name, value);

	std::string head;
	head += toString(method);
	head += ' ';
	head += url.getFile();
	head += " HTTP/1.1\r\n";

	for (const auto& [name, value] : headers.all())
	{
		head += name;
		head += ": ";
		head += value;
		head += "\r\n";
	}
	head += "\r\n";

	outgoing.append(head);
	if (has_body) outgoing.append(body.data(), body.size());

	socket = Socket::create(this, url.getHostname(), url.getEffectivePort());
	if (!socket)
	{
		internal_fail(Status::ConnectFailed, "could not create a socket");
		return;
	}

	busy = true;

	/* One deadline over the whole exchange - lookup, connect, request and
	   response - because a caller cares how long it waits, not which stage it
	   waited in. The socket's own connect timeout is set alongside so a stalled
	   connect does not have to wait out the longer one. */
	socket->setConnectTimeout(options.timeout);
	deadline = std::make_unique<Samurai::Timer>(this, options.timeout, true);

	socket->connect();
}


void Samurai::IO::Net::HTTP::Client::abort()
{
	/* Deliberately silent: an abandoned request reports nothing, so a caller
	   that has torn down its own state cannot be called back into it. */
	reported = true;
	internal_teardown();
}


void Samurai::IO::Net::HTTP::Client::internal_fail(Status why, const char* msg)
{
	if (reported) return;
	reported = true;

	internal_teardown();

	if (eventHandler) eventHandler->EventHttpError(this, why, msg);
}


void Samurai::IO::Net::HTTP::Client::internal_complete()
{
	if (reported) return;
	reported = true;

	/* Copied out before the socket goes: the handler is entitled to start
	   another request on this client, which resets the parser. */
	const Response answer = parser.getResponse();
	internal_teardown();

	if (eventHandler) eventHandler->EventHttpResponse(this, answer);
}


void Samurai::IO::Net::HTTP::Client::EventConnected(const Socket*)
{
	/* Taken now rather than when it is asked for: the socket is closed before
	   the response is delivered, and this is the answer to which of the host's
	   addresses reaches the peer. */
	if (socket)
	{
		if (const InetAddress* local = socket->getLocalAddress())
			local_address = *local;
	}

	if (!secure)
	{
		internal_send();
		return;
	}

	/*
	 * TCP is up; nothing may be written until TLS is. The peer name comes from
	 * the socket, which took it from the URL, so the certificate is checked
	 * against the host that was asked for rather than whatever the connection
	 * resolved to.
	 */
	if (!socket)
	{
		internal_fail(Status::TlsFailed, "no socket to negotiate TLS on");
		return;
	}

	socket->TLSsetAllowUntrusted(options.allowUntrustedCertificate);

	if (!socket->TLSInitialize(false))
	{
		internal_fail(Status::TlsFailed, "could not initialise TLS");
		return;
	}

	handshaking = true;
	socket->TLSsendHandshake();
}


/*
 * The handshake is done and verified - a failed one closes the socket and
 * arrives as EventError instead. This is the first point at which the request
 * may go out.
 */
void Samurai::IO::Net::HTTP::Client::EventTLSConnected(const Socket*)
{
	handshaking = false;
	internal_send();
}


void Samurai::IO::Net::HTTP::Client::EventCanWrite(const Socket*)
{
	internal_send();
}


void Samurai::IO::Net::HTTP::Client::internal_send()
{
	if (!socket || !outgoing.size()) return;

	std::error_code ec;
	const ssize_t sent = socket->write(outgoing.ptr(), outgoing.size(), ec);

	if (sent < 0)
	{
		internal_fail(Status::Disconnected, ec.message().c_str());
		return;
	}

	outgoing.remove((size_t) sent);

	/* Ask to be told when there is room for the rest, and stop asking once
	   there is no rest: a write notifier left armed fires on every pass. */
	socket->toggleWriteNotifier(outgoing.size() != 0);
}


void Samurai::IO::Net::HTTP::Client::EventDataAvailable(const Socket*)
{
	internal_read();
}


void Samurai::IO::Net::HTTP::Client::internal_read()
{
	if (!socket) return;

	for (;;)
	{
		char scratch[READ_CHUNK];
		size_t transferred = 0;
		std::error_code ec;

		const Samurai::IO::ReadResult result =
			socket->read(scratch, sizeof(scratch), transferred, ec);

		if (result == Samurai::IO::ReadResult::Error)
		{
			internal_fail(Status::Disconnected, ec.message().c_str());
			return;
		}

		if (result == Samurai::IO::ReadResult::EndOfFile)
		{
			/* A body with no framing ends here, and is complete rather than
			   truncated; anything else was waiting for bytes that will not
			   arrive. */
			if (parser.finish() == ResponseParser::Result::Complete)
				internal_complete();
			else
				internal_fail(parser.getStatus(), toString(parser.getStatus()));
			return;
		}

		if (result == Samurai::IO::ReadResult::WouldBlock || !transferred) return;

		incoming.append(scratch, transferred);

		const ResponseParser::Result parsed = parser.parse(incoming);
		if (parsed == ResponseParser::Result::Error)
		{
			internal_fail(parser.getStatus(), toString(parser.getStatus()));
			return;
		}

		if (parsed == ResponseParser::Result::Complete)
		{
			internal_complete();
			return;
		}
	}
}


void Samurai::IO::Net::HTTP::Client::EventDisconnected(const Socket*)
{
	if (parser.finish() == ResponseParser::Result::Complete)
		internal_complete();
	else
		internal_fail(parser.getStatus() == Status::Ok
			? Status::Disconnected : parser.getStatus(), "the peer closed early");
}


void Samurai::IO::Net::HTTP::Client::EventTimeout(const Socket*)
{
	internal_fail(Status::ConnectTimeout, "connection timed out");
}


void Samurai::IO::Net::HTTP::Client::EventError(const Socket*, SocketError which,
                                                const char* msg)
{
	/*
	 * A refused certificate or a failed handshake arrives here, because the
	 * socket closes itself and reports an error rather than a TLS-specific
	 * event. Telling it apart matters to a caller: a name that does not resolve
	 * and a certificate that does not verify want different answers.
	 */
	Status why;
	if (which == SocketError::ConnectionTimeout)
		why = Status::ConnectTimeout;
	else if (handshaking)
		why = Status::TlsFailed;
	else
		why = Status::ConnectFailed;

	internal_fail(why, msg ? msg : "socket error");
}


/*
 * NOTE: the timer is released before the handler runs. internal_fail() calls
 * into the application, which may destroy this client, and a timer callback -
 * unlike a monitor dispatch - holds nothing that would keep it alive.
 */
void Samurai::IO::Net::HTTP::Client::EventTimeout(Samurai::Timer*)
{
	deadline.reset();
	internal_fail(Status::RequestTimeout, "no response before the deadline");
}


namespace {

/** Collects one response for the blocking form below. */
class BlockingHandler : public Samurai::IO::Net::HTTP::RequestEventHandler
{
	public:
		bool done = false;
		Samurai::IO::Net::HTTP::Status status = Samurai::IO::Net::HTTP::Status::Ok;
		Samurai::IO::Net::HTTP::Response response;

	protected:
		void EventHttpResponse(Samurai::IO::Net::HTTP::Client*,
		                       const Samurai::IO::Net::HTTP::Response& answer) override
		{
			response = answer;
			status = Samurai::IO::Net::HTTP::Status::Ok;
			done = true;
		}

		void EventHttpError(Samurai::IO::Net::HTTP::Client*,
		                    Samurai::IO::Net::HTTP::Status why, const char*) override
		{
			status = why;
			done = true;
		}
};

}


Samurai::IO::Net::HTTP::Status
Samurai::IO::Net::HTTP::fetch(Method method, const URL& url, const Headers& extra,
                              std::string_view body, Response& response,
                              std::chrono::milliseconds timeout)
{
	BlockingHandler handler;

	Client::Options options;
	options.timeout = timeout;

	Client client(&handler, options);
	client.request(method, url, extra, body);

	Samurai::IO::Net::SocketMonitor* monitor =
		Samurai::IO::Net::SocketMonitor::getInstance();
	Samurai::TimerManager* timers = Samurai::TimerManager::getInstance();

	const Samurai::Timer::clock::time_point limit =
		Samurai::Timer::clock::now() + timeout + std::chrono::milliseconds(500);

	/*
	 * TimerManager is driven here as well as by wait(), which is what makes this
	 * work whether or not the caller's loop would have. The slice is capped so
	 * the outer bound is honoured even with no timer armed.
	 */
	while (!handler.done && Samurai::Timer::clock::now() < limit)
	{
		const int next = timers->timeToNext();
		const int slice = (next < 0 || next > 50) ? 50 : next;
		monitor->wait(slice);
		timers->process();
	}

	if (!handler.done)
	{
		client.abort();
		return Status::RequestTimeout;
	}

	response = handler.response;
	return handler.status;
}


Samurai::IO::Net::HTTP::Status
Samurai::IO::Net::HTTP::get(const URL& url, Response& response,
                            std::chrono::milliseconds timeout)
{
	const Headers none;
	return fetch(Method::Get, url, none, std::string_view(), response, timeout);
}
