/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/eventloop.h>
#include <samurai/messagehandler.h>
#include <samurai/timer.h>
#include <samurai/io/file.h>
#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/dns/resolver-pool.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/tlsfactory.h>

#include "io/net/socketmonitor-backend.h"

#include <chrono>
#include <memory>
#include <string>

/*
 * Two loops in one process share nothing, and a loop that goes first leaves
 * what was made on it safe to destroy. Every case makes its own loops, so none
 * of them leans on what another case left behind.
 */

namespace {

using Samurai::EventLoop;
using Samurai::Timer;
using Samurai::IO::Net::InetSocketAddress;
using Samurai::IO::Net::ServerSocket;
using Samurai::IO::Net::Socket;

class Counter : public Samurai::TimerListener
{
	public:
		size_t fired = 0;

		void EventTimeout(Timer*) override { fired++; }
};

class Listener final : public Samurai::MessageListener
{
	public:
		Listener(EventLoop& loop, size_t id) : Samurai::MessageListener(loop), wanted(id) { }

		bool EventMessage(const Samurai::Message* msg) override
		{
			if (!msg || msg->getID() != wanted) return false;
			seen++;
			return true;
		}

		size_t seen = 0;

	private:
		size_t wanted;
};

class Events
	: public Samurai::IO::Net::SocketEventHandler
	, public Samurai::IO::Net::ServerSocketEventHandler
	, public Samurai::IO::Net::ResolveEventHandler
{
	public:
		bool error = false;
		bool found = false;
		bool host_error = false;

		void EventError(const Socket*, Samurai::IO::Net::SocketError, const char*) override
		{ error = true; }

		void EventAcceptError(const ServerSocket*, const char*) override { }
		void EventAcceptSocket(const ServerSocket*, std::shared_ptr<Socket>) override { }

		using Samurai::IO::Net::SocketEventHandler::EventHostFound;
		void EventHostFound(const Samurai::IO::Net::InetAddress*) override { found = true; }
		void EventHostError(Samurai::IO::Net::DNS::Resolver::Error) override { host_error = true; }
};

/* Bounded by time rather than passes: the poll backend returns at once from an
   empty set, so a count of passes says nothing about how long was waited. */
template<typename Predicate>
bool pump_until(EventLoop& loop, Predicate done, int timeout_ms = 2000)
{
	const Timer::clock::time_point deadline =
		Timer::clock::now() + std::chrono::milliseconds(timeout_ms);

	while (!done() && Timer::clock::now() < deadline)
		loop.wait(5);
	return done();
}

std::shared_ptr<ServerSocket> listening_server(EventLoop& loop, Events* events)
{
	InetSocketAddress any((uint16_t) 0);
	std::shared_ptr<ServerSocket> server = ServerSocket::create(loop, events, any);
	if (!server || !server->listen()) return nullptr;
	return server;
}

}

EXO_TEST(eventloop_is_valid_with_the_default_monitor,
{
	EventLoop loop;
	return loop.isValid() && loop.monitor().size() == 0 && loop.timers().size() == 0;
});

#ifdef SOCKET_NOTIFY_POLL
/* A loop over a backend of the caller's choosing drives its timers like any other. */
EXO_TEST(eventloop_runs_on_the_backend_it_is_given,
{
	EventLoop loop(std::make_unique<Samurai::IO::Net::PollSocketMonitor>());
	if (!loop.isValid()) return false;

	Counter counter;
	Timer timer(loop, &counter, std::chrono::milliseconds(1), true);
	return pump_until(loop, [&] { return counter.fired == 1; });
});
#endif

/* ------------------------------------------------------------------------- */
/* Two loops                                                                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(eventloop_a_timer_fires_only_on_its_own_loop,
{
	EventLoop a;
	EventLoop b;

	Counter counter;
	Timer timer(a, &counter, std::chrono::milliseconds(1), true);
	if (a.timers().size() != 1 || b.timers().size() != 0) return false;

	for (int n = 0; n < 20; n++) b.wait(5);
	if (counter.fired != 0) return false;

	return pump_until(a, [&] { return counter.fired == 1; });
});

EXO_TEST(eventloop_a_socket_is_registered_only_with_its_own_loop,
{
	EventLoop a;
	EventLoop b;

	Events events;
	std::shared_ptr<ServerSocket> server = listening_server(b, &events);
	if (!server) return false;

	return server->getLoop() == &b
		&& server->isMonitored()
		&& b.monitor().size() == 1
		&& a.monitor().size() == 0;
});

EXO_TEST(eventloop_a_message_reaches_only_listeners_on_its_own_loop,
{
	EventLoop a;
	EventLoop b;

	Listener on_a(a, 0x52000001);
	Listener on_b(b, 0x52000001);

	a.messages().postMessage(0x52000001, nullptr, 0, 0);
	b.wait(0);
	if (on_a.seen != 0 || on_b.seen != 0) return false;

	a.wait(0);
	return on_a.seen == 1 && on_b.seen == 0;
});

EXO_TEST(eventloop_each_loop_answers_its_own_lookups,
{
	EventLoop a;
	EventLoop b;
	if (&a.resolver() == &b.resolver()) return false;

	Events events;
	std::unique_ptr<Samurai::IO::Net::DNS::Resolver> lookup =
		Samurai::IO::Net::DNS::Resolver::getHostByName(b, &events, "localhost");

	/* The answer is reported through b's notifier, so pumping a alone never
	   delivers it, however long the worker took. */
	for (int n = 0; n < 40; n++) a.wait(5);
	if (events.found || events.host_error) return false;

	return pump_until(b, [&] { return events.found || events.host_error; })
		&& events.found;
});

/* ------------------------------------------------------------------------- */
/* A loop that goes first                                                    */
/*                                                                           */
/* What outlives its loop is detached by it, so its own destructor - which    */
/* the sanitizer build watches - finds nothing left to deregister from.      */
/* ------------------------------------------------------------------------- */

EXO_TEST(eventloop_a_timer_outliving_its_loop_is_safe,
{
	auto loop = std::make_unique<EventLoop>();

	Counter counter;
	auto timer = std::make_unique<Timer>(*loop, &counter, std::chrono::milliseconds(1), false);
	loop.reset();

	timer->reset();
	timer.reset();
	return counter.fired == 0;
});

EXO_TEST(eventloop_a_listener_outliving_its_loop_is_safe,
{
	auto loop = std::make_unique<EventLoop>();
	auto listener = std::make_unique<Listener>(*loop, 0x52000002);

	loop->messages().postMessage(0x52000002, nullptr, 0, 0);
	loop.reset();

	const bool silent = listener->seen == 0;
	listener.reset();
	return silent;
});

EXO_TEST(eventloop_a_socket_outliving_its_loop_is_safe,
{
	auto loop = std::make_unique<EventLoop>();

	Events events;
	std::shared_ptr<ServerSocket> server = listening_server(*loop, &events);
	if (!server) return false;

	/* Connecting, so it is monitored and has its connect timer armed. */
	std::shared_ptr<Socket> client =
		Socket::create(*loop, &events, std::string("127.0.0.1"), server->getLocalPort());
	client->connect();

	std::shared_ptr<Socket> idle =
		Socket::create(*loop, &events, std::string("127.0.0.1"), server->getLocalPort());

	loop.reset();

	if (server->getLoop() || client->getLoop() || idle->getLoop()) return false;
	if (server->isMonitored() || client->isMonitored()) return false;

	/* Nothing left to register with, and saying so rather than crashing. */
	server->setMonitor(Samurai::IO::Net::SocketMonitor::Triggers::Read);
	idle->connect();
	if (!events.error || idle->isMonitored()) return false;

	client.reset();
	idle.reset();
	server.reset();
	return true;
});

/* The resolver a lookup holds belongs to the loop's pool, which goes with it. */
EXO_TEST(eventloop_a_lookup_outliving_its_loop_is_safe,
{
	auto loop = std::make_unique<EventLoop>();

	Events events;
	std::shared_ptr<Socket> client =
		Socket::create(*loop, &events, std::string("localhost"), (uint16_t) 1);
	client->connect();

	loop.reset();
	client.reset();
	return true;
});

/* ------------------------------------------------------------------------- */
/* TLS                                                                       */
/*                                                                           */
/* The TLS library's state is the process's. Tearing it down frees the key    */
/* material, so the configured keys are what show whether one loop going took */
/* it from another that is still running.                                    */
/* ------------------------------------------------------------------------- */

EXO_TEST(eventloop_the_tls_state_outlives_one_of_two_loops,
{
	using Tls = Samurai::IO::Net::TlsFactory;

	/* setKeys() is process-wide, so whatever was there is put back. */
	struct Restore
	{
		std::string key, cert;
		bool saved = false;

		Restore()
		{
			if (Tls::getPrivateKey() && Tls::getCertificate())
			{
				key = Tls::getPrivateKey()->getName();
				cert = Tls::getCertificate()->getName();
				saved = true;
			}
		}

		~Restore() { if (saved) Tls::setKeys(key.c_str(), cert.c_str()); }
	} restore;

	auto first = std::make_unique<EventLoop>();
	EventLoop second;

	Tls::setKeys("eventloop-test-key.pem", "eventloop-test-cert.pem");
	first.reset();

	return Tls::getPrivateKey() != nullptr && Tls::getCertificate() != nullptr;
});
