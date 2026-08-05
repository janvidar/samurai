/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_HTTP_CLIENT_H
#define HAVE_SAMURAI_HTTP_CLIENT_H

#include <samurai/io/buffer.h>
#include <samurai/io/net/http/response.h>
#include <samurai/io/net/proxy.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/url.h>
#include <samurai/timer.h>

#include <chrono>
#include <memory>
#include <optional>
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
 * https is supported, with the certificate verified against the trust store and
 * the hostname checked against it - the name the URL asked for, not one derived
 * from the connection. A server whose certificate cannot be verified is refused
 * unless the caller sets Options::allowUntrustedCertificate, which exists for a
 * test server and for a peer authenticated some other way, and is off.
 *
 * Following a redirect is available but off, Options::maxRedirects being zero:
 * chasing a Location: from a device on the local network that has not been
 * authenticated - which is what this was first written to talk to - hands that
 * device a request-forgery primitive, so a 3xx is reported with its body instead
 * and a caller that wants one followed says so. An https response is never
 * followed to a plain http target whatever that option says.
 *
 * Not supported at all, each for a reason:
 *
 *  - Connection reuse and pipelining. Every request sends Connection: close. A
 *    port mapping is two requests, so reuse buys nothing and costs a class of
 *    stale-connection faults.
 *  - Content codings. No Accept-Encoding is sent, so a conforming server must
 *    not apply one.
 *  - Chunked requests, cookies, authentication, Expect: 100-continue, and bodies
 *    too large to hold in memory.
 *
 * A SOCKS5 proxy is supported, being a property of the socket rather than of
 * HTTP: the connection is tunnelled, the host in the URL is never resolved
 * locally, and https over a tunnel verifies the certificate against the name the
 * URL asked for. An HTTP proxy - the kind that takes an absolute-form request
 * target, or CONNECT - is not.
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

			/**
			 * Accept an https peer whose certificate the trust store cannot
			 * verify.
			 *
			 * Off, and worth leaving off: with it on, the connection is
			 * encrypted against a passive observer and against nobody else,
			 * since any certificate at all will do. It is here for a test
			 * server with a self-signed certificate, and for a protocol that
			 * authenticates the peer by fingerprint instead.
			 */
			bool allowUntrustedCertificate = false;

			/**
			 * How many redirects to follow. Zero does not follow any, which is
			 * what a UPnP gateway wants: chasing a Location: from an
			 * unauthenticated device on the local network is a request-forgery
			 * primitive, so a 3xx is handed back with its body instead.
			 *
			 * A client fetching from a named server on the internet is the other
			 * case, and five is the conventional bound. An https response is
			 * never followed to a plain http target whatever this says.
			 */
			unsigned maxRedirects = 0;

			/**
			 * Reach the server through a SOCKS5 proxy.
			 *
			 * Nothing means the process default, ProxySettings::getDefault(), so
			 * a program that proxies everything says so once. Set it to a
			 * default-constructed ProxySettings to insist on a direct connection
			 * where the process default is a proxy.
			 */
			std::optional<ProxySettings> proxy;
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
		 * interface and only for Version::IPv4.
		 *
		 * Null until connected, and remembered afterwards: the socket is closed
		 * before the response is delivered, so a handler asking where its answer
		 * came from would otherwise always get nothing.
		 */
		const InetAddress* getLocalAddress() const;

	private:
		void EventConnected(const Socket*) override;
		void EventTLSConnected(const Socket*) override;
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
		/* Kept past the socket, so it survives being asked for from a handler. */
		InetAddress local_address;
		bool busy = false;
		bool reported = false;

		/* Whether the request is over TLS, so that EventConnected knows to
		   start a handshake rather than write the request straight out. */
		bool secure = false;

		/* Set while the handshake is outstanding. A failed one closes the socket
		   and arrives as a plain socket error, so this is what tells a refused
		   certificate apart from a host that never answered. */
		bool handshaking = false;

		/*
		 * The request as it was asked for, kept so that a redirect can be
		 * re-issued against a new URL without the caller being involved.
		 */
		Method current_method = Method::Get;
		/* URL has no default constructor; an empty one is not valid, which is
		   the right state before a request has been made. */
		URL current_url{ "" };
		Headers current_extra;
		std::string current_body;
		unsigned redirects_followed = 0;

		/** Everything request() does once it has decided it may proceed. */
		void internal_start();

		/**
		 * @return true when the response was a redirect this followed, in which
		 *         case nothing has been reported and a new request is in flight.
		 */
		bool internal_follow_redirect();

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
