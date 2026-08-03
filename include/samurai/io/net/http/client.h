/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_HTTP_CLIENT_H
#define HAVE_SAMURAI_HTTP_CLIENT_H

#include <samurai/io/buffer.h>
#include <samurai/io/net/http/response.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/url.h>
#include <samurai/timer.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace Samurai {
namespace IO {
namespace Net {
namespace HTTP {

class Client;

/** Implement this to receive the outcome of a request. */
class RequestEventHandler : public Samurai::IO::Net::EventHandler
{
	public:
		RequestEventHandler() { }
		~RequestEventHandler() override { }

	protected:
		/**
		 * A complete response arrived.
		 *
		 * Called for a 500 as much as for a 200. A UPnP fault is a 500 whose
		 * body carries the error code, so the status is something the caller
		 * branches on rather than a failure this should hide.
		 */
		virtual void EventHttpResponse(Client*, const Response&) = 0;

		/** The request produced no response. */
		virtual void EventHttpError(Client*, Status, const char* msg) = 0;

	friend class Client;
};

/**
 * A minimal asynchronous HTTP/1.1 client: one request per connection.
 *
 * Scope is what a UPnP gateway needs and no more. Not supported, each for a
 * reason:
 *
 *  - TLS. An IGD speaks plain HTTP on the local network, always.
 *  - Redirects. Following a Location: from an unauthenticated device on the LAN
 *    is a request-forgery primitive; a 3xx is reported with its body instead.
 *  - Connection reuse and pipelining. Every request sends Connection: close. A
 *    port mapping is two requests, so reuse buys nothing and costs a class of
 *    stale-connection faults.
 *  - Content codings. No Accept-Encoding is sent, so a conforming server must
 *    not apply one.
 *  - Chunked requests, cookies, authentication, proxies, Expect: 100-continue,
 *    and bodies too large to hold in memory.
 *
 * Not a SocketBase, so there is no create() factory: it owns a Socket, which
 * has one.
 */
class Client final
	: public Samurai::IO::Net::SocketEventHandler
	, public Samurai::TimerListener
{
	public:
		struct Options
		{
			/** Covers the whole exchange, not just the connect. */
			std::chrono::milliseconds timeout{ 10000 };
			ResponseParser::Limits limits;
			/** Sent as User-Agent when set. */
			std::string userAgent;
		};

		explicit Client(RequestEventHandler* eh);
		Client(RequestEventHandler* eh, const Options& options);
		~Client() override;

		/* Owns a socket and a timer, and is registered as their handler, so a
		 * copy would leave two objects answering for one connection. */
		Client(const Client&) = delete;
		Client& operator=(const Client&) = delete;

		void get(const URL& url);
		void post(const URL& url, std::string_view contentType, std::string_view body);
		void head(const URL& url);

		/**
		 * The general form.
		 *
		 * 'extra' is sent after the headers this class supplies, with the case
		 * it was given: SOAPAction is matched case-sensitively by more than one
		 * router. A field named here replaces the one that would have been
		 * supplied.
		 */
		void request(Method method, const URL& url, const Headers& extra,
		             std::string_view body);

		/** Abandon the request. No further event is delivered. */
		void abort();

		bool isBusy() const { return busy; }

		const Options& getOptions() const { return options; }
		void setOptions(const Options& opts) { options = opts; }

		/**
		 * The local address the connection was made from.
		 *
		 * This is how a UPnP mapping learns which of its addresses faces the
		 * gateway - NetworkInterface cannot answer it, having one address per
		 * interface and only for Version::IPv4. Null until connected.
		 */
		const InetAddress* getLocalAddress() const;

	private:
		void EventConnected(const Socket*) override;
		void EventDataAvailable(const Socket*) override;
		void EventCanWrite(const Socket*) override;
		void EventDisconnected(const Socket*) override;
		void EventTimeout(const Socket*) override;
		void EventError(const Socket*, SocketError, const char*) override;

		void EventTimeout(Samurai::Timer* timer) override;

		void internal_send();
		void internal_read();
		void internal_complete();
		void internal_fail(Status why, const char* msg);
		void internal_teardown();

		RequestEventHandler* eventHandler;
		Options options;

		std::shared_ptr<Socket> socket;
		std::unique_ptr<Samurai::Timer> deadline;

		/* The assembled request, and what of it still has to go out. */
		Samurai::IO::Buffer outgoing;
		Samurai::IO::Buffer incoming;

		ResponseParser parser;
		bool busy = false;
		bool reported = false;

	friend class Socket;
};

/**
 * Perform one request, driving the socket monitor until it completes.
 *
 * MUST NOT be called from inside an event handler, nor from a thread already
 * running the event loop: it dispatches that loop's events re-entrantly, and a
 * handler running inside another handler can destroy the socket whose callback
 * is on the stack.
 */
Status fetch(Method method, const URL& url, const Headers& extra,
             std::string_view body, Response& response,
             std::chrono::milliseconds timeout = std::chrono::seconds(10));

Status get(const URL& url, Response& response,
           std::chrono::milliseconds timeout = std::chrono::seconds(10));

}
}
}
}

#endif // HAVE_SAMURAI_HTTP_CLIENT_H
