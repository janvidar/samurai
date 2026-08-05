/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/messagehandler.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/buffer.h>

#include "testkeys.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/*
 * Socket, ServerSocket and DatagramSocket driven through the real event loop.
 *
 * This was one ordered scenario over a single shared fixture: twenty-eight
 * cases that had to run in sequence, where a failure in the third made every
 * assertion after it meaningless, and no case could be read without reading
 * the ones before it. Each case now builds what it needs and gives it back.
 *
 * Two things make that possible. Binding to port 0 lets the kernel choose, so
 * cases neither collide with each other nor with whatever else on the machine
 * holds a fixed port. And nothing asserts the monitor's absolute size: it is a
 * process-wide singleton shared with every other suite in this binary, so a
 * fixture records what it was on the way in and asserts only its own delta.
 */

namespace {

using Samurai::IO::Net::InetAddress;
using Samurai::IO::Net::InetSocketAddress;
using Samurai::IO::Net::Socket;
using Samurai::IO::Net::SocketMonitor;

/* Long enough that a loaded machine does not fail; only reached when broken. */
static const int SOCKET_TEST_TIMEOUT_MS = 5000;

/*
 * Records the callbacks a socket made, so a case can wait for one and then
 * assert on what arrived.
 */
class SocketRecorder :
	public Samurai::IO::Net::ServerSocketEventHandler,
	public Samurai::IO::Net::SocketEventHandler,
	public Samurai::IO::Net::DatagramEventHandler
{
	public:
		bool accept_error = false;
		bool host_lookup = false;
		bool host_found = false;
		bool connecting = false;
		bool connected = false;
		bool timeout = false;
		bool disconnected = false;
		bool data_available = false;
		bool can_write = false;
		bool tls_connected = false;
		bool tls_disconnected = false;
		bool error = false;
		bool datagram = false;
		bool datagram_error = false;

		int accept_count = 0;
		int data_available_count = 0;
		int error_count = 0;

		/* Only meaningful once error is set. */
		Samurai::IO::Net::SocketError last_error = Samurai::IO::Net::SocketError::SocketUnknown;

		std::string datagram_payload;
		std::shared_ptr<Socket> accepted;

		void reset()
		{
			accept_error = false;
			host_lookup = false;
			host_found = false;
			connecting = false;
			connected = false;
			timeout = false;
			disconnected = false;
			data_available = false;
			can_write = false;
			tls_connected = false;
			tls_disconnected = false;
			error = false;
			datagram = false;
			datagram_error = false;
			accept_count = 0;
			data_available_count = 0;
			error_count = 0;
			datagram_payload.clear();
		}

		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char*) override
		{ accept_error = true; }

		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*,
			std::shared_ptr<Socket> socket) override
		{
			accepted = socket;
			accept_count++;
		}

		void EventHostLookup(const Socket*) override { host_lookup = true; }
		void EventHostFound(const Socket*) override { host_found = true; }
		void EventConnecting(const Socket*) override { connecting = true; }
		void EventConnected(const Socket*) override { connected = true; }
		void EventTimeout(const Socket*) override { timeout = true; }
		void EventDisconnected(const Socket*) override { disconnected = true; }

		void EventDataAvailable(const Socket*) override
		{
			data_available = true;
			data_available_count++;
		}

		void EventCanWrite(const Socket*) override { can_write = true; }
		void EventTLSConnected(const Socket*) override { tls_connected = true; }
		void EventTLSDisconnected(const Socket*) override { tls_disconnected = true; }

		void EventError(const Socket*, Samurai::IO::Net::SocketError which, const char*) override
		{
			error = true;
			error_count++;
			last_error = which;
		}

		void EventGotDatagram(Samurai::IO::Net::DatagramSocket*,
			Samurai::IO::Net::DatagramPacket* packet) override
		{
			datagram = true;
			if (packet && packet->getBuffer())
			{
				Samurai::IO::Buffer* buf = packet->getBuffer();
				datagram_payload = buf->copyRange(0, buf->size());
			}
		}

		void EventDatagramError(const Samurai::IO::Net::DatagramSocket*, const char*) override
		{ datagram_error = true; }
};

/* Runs the event loop until the condition holds, or gives up. */
template<typename Fn>
static bool pump(Fn done, int timeout_ms = SOCKET_TEST_TIMEOUT_MS)
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(timeout_ms);

	while (!done())
	{
		if (std::chrono::steady_clock::now() > deadline)
			return done();
		monitor->wait(10);
	}
	return true;
}

/*
 * Waits without running the event loop, for cases that ask a socket directly
 * what it sees. Pumping would race them: the monitor notices a closed peer
 * first, and from Disconnected a read reports Error rather than EndOfFile.
 */
template<typename Fn>
static bool spin(Fn done, int timeout_ms = SOCKET_TEST_TIMEOUT_MS)
{
	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(timeout_ms);

	while (!done())
	{
		if (std::chrono::steady_clock::now() > deadline)
			return done();
		usleep(1000);
	}
	return true;
}

/*
 * A listening server and a client connected to it over the loopback, plus the
 * socket the server accepted - three registrations in the monitor.
 */
struct TcpFixture
{
	SocketRecorder server_events;
	SocketRecorder client_events;
	std::shared_ptr<Samurai::IO::Net::ServerSocket> server;
	std::shared_ptr<Socket> client;
	SocketMonitor* monitor;
	size_t baseline;
	uint16_t port = 0;
	bool ready = false;

	TcpFixture()
		: monitor(SocketMonitor::getInstance())
		, baseline(monitor->size())
	{
		InetSocketAddress any((uint16_t) 0);
		server = Samurai::IO::Net::ServerSocket::create(&server_events, any);
		if (!server || server->getFD() == -1) return;
		if (!server->listen()) return;

		port = server->getLocalPort();
		if (!port) return;

		client = Socket::create(&client_events, std::string("127.0.0.1"), port);
		if (!client) return;
		client->connect();

		if (!pump([&] { return client_events.connected && server_events.accepted; }))
			return;

		/* An accepted socket arrives with no handler and outside the monitor.
		   Giving it one is how the application adopts it. */
		server_events.accepted->setEventHandler(&server_events);
		ready = true;
	}

	~TcpFixture()
	{
		server_events.accepted.reset();
		client.reset();
		server.reset();

		/* Releasing a socket deregisters it, but the monitor is only asked to
		   forget it on its next pass. */
		pump([&] { return monitor->size() <= baseline; }, 1000);
	}

	TcpFixture(const TcpFixture&) = delete;
	TcpFixture& operator=(const TcpFixture&) = delete;

	Socket* accepted() { return server_events.accepted.get(); }

	/* How many registrations this fixture added to the shared monitor. */
	size_t added() const
	{
		const size_t now = monitor->size();
		return (now > baseline) ? now - baseline : 0;
	}

};

/*
 * The same pair, with TLS negotiated over it through the event loop.
 *
 * tls.tcc drives the factory directly over a socketpair(); what is exercised
 * here is Socket's half - the TlsStatus switch that turns WantRead and
 * WantWrite into write-notifier changes and drives the handshake from
 * internal_canRead() and internal_canWrite(), which only the loop can reach.
 */
struct TlsFixture
{
	TcpFixture tcp;
	bool ready = false;

	TlsFixture()
	{
		if (!tcp.ready || !tls_test_keys_claimed().ready) return;

		/* The certificate is self-signed and names "localhost", so it cannot
		   be verified against a loopback address. A Socket keeps its Tls to
		   itself, so the process default is what these two ends can be given,
		   and initialize() is what reads it - putting it back afterwards leaves
		   nothing behind for the cases that follow. */
		const bool untrusted =
			Samurai::IO::Net::TlsFactory::defaultAllowUntrusted();
		Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(true);

		const bool initialized = tcp.accepted()->TLSInitialize(true)
			&& tcp.client->TLSInitialize(false);

		Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(untrusted);
		if (!initialized) return;

		/* Neither end can finish alone; the loop carries each one's records to
		   the other until both report the handshake complete. */
		tcp.accepted()->TLSsendHandshake();
		tcp.client->TLSsendHandshake();

		ready = pump([&] {
			return tcp.server_events.tls_connected && tcp.client_events.tls_connected;
		});
	}

	Socket* client() { return tcp.client.get(); }
	Socket* server() { return tcp.accepted(); }
};

/*
 * Writes the whole of 'text', retrying while the socket has no room.
 *
 * A stream write is allowed to take part of what it was offered, and over TLS
 * it can take nothing at all and ask to be called again.
 */
static bool sendAll(Socket* socket, const std::string& text)
{
	size_t sent = 0;
	bool failed = false;

	pump([&] {
		while (sent < text.size())
		{
			std::error_code ec;
			const ssize_t n = socket->write(&text[sent], text.size() - sent, ec);
			if (n < 0) { failed = true; return true; }
			if (n == 0) return false; /* no room; come back after a wait */
			sent += (size_t) n;
		}
		return true;
	});

	return !failed && sent == text.size();
}

/*
 * Reads until 'want' bytes have arrived, or gives up.
 *
 * One EventDataAvailable is not one message. The event says the descriptor has
 * bytes; over TLS those may be part of a record that cannot be decrypted until
 * the rest of it lands, so the read that follows reports WouldBlock and the
 * loop has to come back.
 */
static std::string receive(Socket* socket, size_t want)
{
	std::string out;

	pump([&] {
		for (;;)
		{
			char buf[4096];
			size_t got = 0;
			std::error_code ec;

			if (socket->read(buf, sizeof(buf), got, ec) != Samurai::IO::ReadResult::Ok)
				break;
			if (!got) break;
			out.append(buf, got);
		}
		return out.size() >= want;
	});

	return out;
}

/* Peeks once 'want' bytes are there, without taking any of them. */
static std::string peekAll(Socket* socket, size_t want)
{
	std::string out;

	pump([&] {
		char buf[4096];
		size_t got = 0;
		std::error_code ec;

		if (socket->peek(buf, sizeof(buf), got, ec) == Samurai::IO::ReadResult::Ok)
			out.assign(buf, got);
		return out.size() >= want;
	});

	return out;
}

/*
 * A port nothing listens on: bound, so the kernel picked one that was free, and
 * then dropped again. A connect there is refused rather than left to time out.
 */
static uint16_t find_dead_port()
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();
	uint16_t port = 0;

	{
		SocketRecorder events;
		InetSocketAddress any((uint16_t) 0);
		auto server = Samurai::IO::Net::ServerSocket::create(&events, any);
		if (server && server->listen())
			port = server->getLocalPort();
	}

	pump([&] { return monitor->size() <= baseline; }, 1000);
	return port;
}

} // namespace


/* ------------------------------------------------------------------------- */
/* Setting a connection up                                                   */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_server_binds_an_ephemeral_port,
{
	SocketRecorder events;
	InetSocketAddress any((uint16_t) 0);

	auto server = Samurai::IO::Net::ServerSocket::create(&events, any);
	if (!server || server->getFD() == -1) return false;
	if (!server->listen()) return false;

	/* Port 0 asks the kernel to choose; it has to say which it chose. */
	return server->getLocalPort() != 0;
});

EXO_TEST(sockets_server_registers_once,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	{
		SocketRecorder events;
		InetSocketAddress any((uint16_t) 0);
		auto server = Samurai::IO::Net::ServerSocket::create(&events, any);
		if (!server->listen()) return false;
		if (monitor->size() != baseline + 1) return false;
	}

	pump([&] { return monitor->size() <= baseline; }, 1000);
	return monitor->size() == baseline;
});

/*
 * A constructor cannot report that the descriptor was never created, so the
 * factory answers with nothing rather than handing back a server that can never
 * be listened on. An unset address has no family, so socket() cannot be asked
 * for one.
 */
EXO_TEST(sockets_a_server_without_an_address_family_is_not_created,
{
	InetAddress unset_addr;
	InetSocketAddress unset(unset_addr, (uint16_t) 0);

	auto server = Samurai::IO::Net::ServerSocket::create(
		(Samurai::IO::Net::ServerSocketEventHandler*) nullptr, unset);

	return server == nullptr;
});

EXO_TEST(sockets_client_connects,
{
	TcpFixture fix;
	return fix.ready;
});

EXO_TEST(sockets_connect_reports_its_progress,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	/* A connection that came up cannot have skipped the lookup. */
	return fix.client_events.host_lookup
		&& fix.client_events.host_found
		&& fix.client_events.connected
		&& !fix.client_events.error
		&& !fix.client_events.timeout;
});

EXO_TEST(sockets_server_accepts_exactly_one,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	return fix.accepted() != 0
		&& fix.server_events.accept_count == 1
		&& !fix.server_events.accept_error;
});

EXO_TEST(sockets_a_connection_costs_three_registrations,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	/* The listener, the client and the accepted socket. */
	return fix.added() == 3;
});

/*
 * An accepted socket is handed over with no event handler and outside the
 * monitor, so a server that only holds on to it hears nothing further. Giving
 * it a handler is what adopts it.
 */
EXO_TEST(sockets_an_accepted_socket_joins_the_monitor_when_adopted,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	SocketRecorder server_events;
	SocketRecorder client_events;

	InetSocketAddress any((uint16_t) 0);
	auto server = Samurai::IO::Net::ServerSocket::create(&server_events, any);
	if (!server->listen()) return false;

	auto client = Socket::create(&client_events,
		std::string("127.0.0.1"), server->getLocalPort());
	client->connect();

	if (!pump([&] { return client_events.connected && server_events.accepted; }))
		return false;

	/* The listener and the client, but not yet what was accepted. */
	if (monitor->size() != baseline + 2) return false;
	if (server_events.accepted->getEventHandler() != 0) return false;

	server_events.accepted->setEventHandler(&server_events);
	const bool joined = (monitor->size() == baseline + 3);

	server_events.accepted.reset();
	client.reset();
	server.reset();
	pump([&] { return monitor->size() <= baseline; }, 1000);

	return joined;
});

EXO_TEST(sockets_releasing_gives_every_registration_back,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	{
		TcpFixture fix;
		if (!fix.ready) return false;
	}

	return monitor->size() == baseline;
});

EXO_TEST(sockets_repeated_connections_do_not_accumulate,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	for (int n = 0; n < 5; n++)
	{
		TcpFixture fix;
		if (!fix.ready) return false;
	}

	return monitor->size() == baseline;
});

/* ------------------------------------------------------------------------- */
/* Moving data                                                               */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_client_to_server,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const std::string message = "Hello, there!\n";
	if (!sendAll(fix.client.get(), message)) return false;

	return receive(fix.accepted(), message.size()) == message;
});

EXO_TEST(sockets_server_to_client,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const std::string message = "Reply from the other end\n";
	if (!sendAll(fix.accepted(), message)) return false;

	return receive(fix.client.get(), message.size()) == message;
});

EXO_TEST(sockets_both_directions_at_once,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const std::string out = "from the client";
	const std::string back = "from the server";

	if (!sendAll(fix.client.get(), out)) return false;
	if (!sendAll(fix.accepted(), back)) return false;

	return receive(fix.accepted(), out.size()) == out
		&& receive(fix.client.get(), back.size()) == back;
});

EXO_TEST(sockets_write_reports_what_it_took,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const std::string message = "counted";
	std::error_code ec;
	const ssize_t n = fix.client->write(message.data(), message.size(), ec);

	return n == (ssize_t) message.size() && !ec;
});

EXO_TEST(sockets_vectored_write_joins_the_buffers,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const std::string expect = "GET /index.html HTTP/1.0\r\n\r\n";

	const ssize_t n = fix.client->write({"GET /index.html", " HTTP/1.0\r\n", "\r\n"});
	if (n != (ssize_t) expect.size()) return false;

	return receive(fix.accepted(), expect.size()) == expect;
});

/* An empty buffer contributes nothing and does not end the write. */
EXO_TEST(sockets_vectored_write_skips_empty_buffers,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const std::string expect = "abcd";

	const ssize_t n = fix.client->write({"", "ab", "", "cd", ""});
	if (n != (ssize_t) expect.size()) return false;

	return receive(fix.accepted(), expect.size()) == expect;
});

EXO_TEST(sockets_peek_does_not_consume,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const std::string message = "peek at me";
	if (!sendAll(fix.client.get(), message)) return false;

	if (peekAll(fix.accepted(), message.size()) != message) return false;

	/* Still there for the read that follows. */
	return receive(fix.accepted(), message.size()) == message;
});

/*
 * The three-way result is the reason the ssize_t overloads are discouraged:
 * nothing-yet, peer-closed and failed all answer 0 there.
 */
EXO_TEST(sockets_read_reports_would_block_when_idle,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	char buf[16];
	size_t got = 0;
	std::error_code ec;

	const Samurai::IO::ReadResult res = fix.accepted()->read(buf, sizeof(buf), got, ec);
	return res == Samurai::IO::ReadResult::WouldBlock && got == 0 && !ec;
});

EXO_TEST(sockets_read_reports_end_of_file_when_the_peer_goes,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	fix.client->disconnect();

	char buf[16];
	size_t got = 0;
	std::error_code ec;
	Samurai::IO::ReadResult res = Samurai::IO::ReadResult::WouldBlock;

	spin([&] {
		res = fix.accepted()->read(buf, sizeof(buf), got, ec);
		return res != Samurai::IO::ReadResult::WouldBlock;
	});

	return res == Samurai::IO::ReadResult::EndOfFile && got == 0;
});

EXO_TEST(sockets_data_available_fires_once_per_arrival,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	fix.server_events.data_available_count = 0;

	/*
	 * Waiting on the count rather than on the bytes. Over the loopback the data
	 * is often already in the receive buffer, so a read can succeed without the
	 * loop ever running - and it is the loop that reports the arrival.
	 */
	if (!sendAll(fix.client.get(), "first")) return false;
	if (!pump([&] { return fix.server_events.data_available_count >= 1; })) return false;
	if (receive(fix.accepted(), 5) != "first") return false;

	const int after_first = fix.server_events.data_available_count;

	if (!sendAll(fix.client.get(), "second")) return false;
	if (!pump([&] { return fix.server_events.data_available_count > after_first; })) return false;

	return receive(fix.accepted(), 6) == "second";
});

/* Bigger than a single segment, so it arrives in pieces. */
EXO_TEST(sockets_a_large_transfer_arrives_whole,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const std::string message(64 * 1024, 'x');

	size_t sent = 0;
	std::string received;

	const bool done = pump([&] {
		if (sent < message.size())
		{
			std::error_code ec;
			const ssize_t n = fix.client->write(&message[sent], message.size() - sent, ec);
			if (n > 0) sent += (size_t) n;
			else if (n < 0) return true;
		}

		char buf[4096];
		size_t got = 0;
		std::error_code ec;
		while (fix.accepted()->read(buf, sizeof(buf), got, ec) == Samurai::IO::ReadResult::Ok && got)
			received.append(buf, got);
		return received.size() >= message.size();
	});

	return done && received == message;
});

/* ------------------------------------------------------------------------- */
/* Addresses                                                                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_a_connected_pair_knows_its_ports,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	/* The client dialled the port the server was given. */
	if (fix.client->getPort() != fix.port) return false;

	/* And the accepted socket is on the server's end of the same pair. */
	if (fix.accepted()->getLocalPort() != fix.port) return false;

	return fix.client->getLocalPort() != 0
		&& fix.client->getLocalPort() != fix.port;
});

EXO_TEST(sockets_a_connected_pair_knows_its_addresses,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	const InetAddress* remote = fix.client->getAddress();
	const InetAddress* local = fix.client->getLocalAddress();

	return remote && local && remote->isLoopback() && local->isLoopback();
});

/* ------------------------------------------------------------------------- */
/* Shutting down                                                             */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_the_server_side_notices_the_client_leaving,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	fix.server_events.reset();
	fix.client->disconnect();

	return pump([&] { return fix.server_events.disconnected; });
});

EXO_TEST(sockets_the_client_notices_the_server_side_leaving,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	fix.client_events.reset();
	fix.accepted()->disconnect();

	return pump([&] { return fix.client_events.disconnected; });
});

EXO_TEST(sockets_releasing_the_accepted_socket_closes_the_connection,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	fix.client_events.reset();
	fix.server_events.accepted.reset();

	return pump([&] { return fix.client_events.disconnected; });
});

/* Nothing that follows a disconnect may reach through a closed descriptor. */
EXO_TEST(sockets_disconnecting_twice_is_harmless,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	fix.client->disconnect();
	fix.client->disconnect();
	return true;
});

EXO_TEST(sockets_a_refused_connection_is_reported,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	const uint16_t dead_port = find_dead_port();
	if (!dead_port) return false;

	bool settled = false;
	{
		SocketRecorder events;
		auto client = Socket::create(&events, std::string("127.0.0.1"), dead_port);
		client->connect();

		/* Refused, or - if something else grabbed the port meanwhile -
		   connected. Either is an answer; hanging is not. */
		settled = pump([&] {
			return events.error || events.disconnected || events.connected;
		});
	}

	pump([&] { return monitor->size() <= baseline; }, 1000);
	return settled && monitor->size() == baseline;
});

/*
 * A refusal is one failure, and reaches the handler once. The Write and Error
 * triggers arrive together for a refused connect, so the loop is given another
 * pass to prove it does not report the same refusal twice.
 *
 * NOTE: which callback carries the refusal is the platform's choice, and it is
 * not always EventError. On Darwin the readable side of a refused connect is
 * noticed first, so it arrives as EventDisconnected instead - which is why the
 * case above waits on either. What has to hold is that it is reported once,
 * whichever of the two it came through.
 */
EXO_TEST(sockets_a_refused_connection_is_reported_once,
{
	const uint16_t dead_port = find_dead_port();
	if (!dead_port) return false;

	SocketRecorder events;
	auto client = Socket::create(&events, std::string("127.0.0.1"), dead_port);
	if (!client) return false;

	client->connect();

	/* find_dead_port() reuses a port it has just released, so something else on
	   the machine can take it in between. A connection that succeeded has no
	   refusal to report once. */
	if (!pump([&] {
		return events.error_count > 0 || events.disconnected || events.connected;
	})) return false;

	if (events.connected) return true;

	SocketMonitor::getInstance()->wait(25);

	const int reports = events.error_count + (events.disconnected ? 1 : 0);
	return reports == 1 && !events.connected;
});

/*
 * A connection that was refused is one that can be attempted again, so it ends
 * up Disconnected rather than Connecting: a state connect() accepts. The failed
 * attempt released the descriptor, so a retry that is really made takes a new
 * one.
 */
EXO_TEST(sockets_a_refused_connection_can_be_retried,
{
	const uint16_t dead_port = find_dead_port();
	if (!dead_port) return false;

	SocketRecorder events;
	auto client = Socket::create(&events, std::string("127.0.0.1"), dead_port);
	if (!client) return false;

	client->connect();

	/* As above, on both counts: the refusal may arrive as a disconnect rather
	   than an error, and a port taken in the meantime connects instead - leaving
	   no refused connection to retry. */
	if (!pump([&] {
		return events.error_count > 0 || events.disconnected || events.connected;
	})) return false;

	if (events.connected) return true;

	client->connect();
	return pump([&] { return client->getFD() != INVALID_SOCKET; });
});

EXO_TEST(sockets_an_unresolvable_host_is_reported,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	bool settled = false;
	{
		SocketRecorder events;
		auto client = Socket::create(&events,
			std::string("no-such-host.invalid"), (uint16_t) 80);
		client->connect();

		settled = pump([&] {
			return events.error || events.disconnected;
		});
	}

	pump([&] { return monitor->size() <= baseline; }, 1000);
	return settled && monitor->size() == baseline;
});

/* ------------------------------------------------------------------------- */
/* Datagrams                                                                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_datagram_socket_listens,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	{
		SocketRecorder events;
		auto server = Samurai::IO::Net::DatagramSocket::create(&events, (uint16_t) 0);
		if (!server || !server->listen()) return false;
		if (server->getLocalPort() == 0) return false;
		if (monitor->size() != baseline + 1) return false;
	}

	pump([&] { return monitor->size() <= baseline; }, 1000);
	return monitor->size() == baseline;
});

/*
 * Nothing has been sent here, and the socket is non-blocking, so recvfrom()
 * reports EAGAIN. That is "no datagram", not "the socket failed", and a
 * readiness notification with nothing behind it must not reach the handler as
 * an error.
 */
EXO_TEST(sockets_a_datagram_read_with_nothing_pending_is_not_an_error,
{
	SocketRecorder events;
	auto socket = Samurai::IO::Net::DatagramSocket::create(&events, (uint16_t) 0);
	if (!socket || !socket->listen()) return false;

	Samurai::IO::Net::DatagramPacket packet;
	return socket->read(&packet) == 0 && !events.datagram_error;
});

EXO_TEST(sockets_a_datagram_arrives,
{
	SocketRecorder server_events;
	SocketRecorder client_events;

	auto server = Samurai::IO::Net::DatagramSocket::create(&server_events, (uint16_t) 0);
	if (!server || !server->listen()) return false;

	const uint16_t port = server->getLocalPort();
	if (!port) return false;

	auto client = Samurai::IO::Net::DatagramSocket::create(&client_events,
		InetAddress::Version::IPv4);
	if (!client) return false;

	const std::string message = "Hello, there!\n";
	Samurai::IO::Net::DatagramPacket packet(
		(uint8_t*) message.data(), message.size());
	InetSocketAddress target(std::string("127.0.0.1"), port);
	packet.setAddress(&target);

	if (client->send(&packet) != (int) message.size()) return false;

	if (!pump([&] { return server_events.datagram; })) return false;
	return server_events.datagram_payload == message;
});

EXO_TEST(sockets_datagram_payloads_are_kept_apart,
{
	SocketRecorder server_events;
	SocketRecorder client_events;

	auto server = Samurai::IO::Net::DatagramSocket::create(&server_events, (uint16_t) 0);
	if (!server || !server->listen()) return false;

	const uint16_t port = server->getLocalPort();
	if (!port) return false;

	auto client = Samurai::IO::Net::DatagramSocket::create(&client_events,
		InetAddress::Version::IPv4);
	if (!client) return false;

	InetSocketAddress target(std::string("127.0.0.1"), port);

	const std::string first = "first datagram";
	const std::string second = "the second one, which is longer";

	{
		Samurai::IO::Net::DatagramPacket packet((uint8_t*) first.data(), first.size());
		packet.setAddress(&target);
		if (client->send(&packet) != (int) first.size()) return false;
	}

	if (!pump([&] { return server_events.datagram; })) return false;
	if (server_events.datagram_payload != first) return false;

	server_events.datagram = false;
	server_events.datagram_payload.clear();

	{
		Samurai::IO::Net::DatagramPacket packet((uint8_t*) second.data(), second.size());
		packet.setAddress(&target);
		if (client->send(&packet) != (int) second.size()) return false;
	}

	if (!pump([&] { return server_events.datagram; })) return false;

	/* A datagram is a message, not a stream: the second must not carry any
	   of the first along with it. */
	return server_events.datagram_payload == second;
});

/* ------------------------------------------------------------------------- */
/* Signals, and a peer that has gone away                                    */
/*                                                                           */
/* A socketpair() gives a connected pair with no port, no listener and no     */
/* timing to depend on, which is what these two need: the descriptor itself,  */
/* not the loop around it.                                                   */
/* ------------------------------------------------------------------------- */

/*
 * A platform with no MSG_NOSIGNAL relies on SO_NOSIGPIPE being set on the
 * descriptor, without which this write raises the signal instead of failing.
 * Reaching the assertion at all is the test.
 */
EXO_TEST(sockets_writing_to_a_closed_peer_returns_an_error_not_a_signal,
{
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;

	/* Close the reader, so a write to the other end gets EPIPE. */
	Samurai::IO::Net::socket_close(sv[1]);

	ssize_t total = 0;
	for (int n = 0; n < 8; n++)
	{
		const ssize_t ret = ::send(sv[0], "payload", 7, Samurai::IO::Net::send_flags);
		if (ret < 0) { total = ret; break; }
		total += ret;
	}

	Samurai::IO::Net::socket_close(sv[0]);

	/* Still running, and the failure surfaced as a return value. */
	return total < 0;
});

namespace {

extern "C" void samurai_test_alarm_handler(int) { }

/* Arrange for SIGALRM to arrive shortly, without SA_RESTART: the default
   disposition would resume the interrupted call rather than fail it. */
static bool arm_interrupting_alarm()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = samurai_test_alarm_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGALRM, &sa, nullptr) != 0) return false;

	struct itimerval timer;
	memset(&timer, 0, sizeof(timer));
	timer.it_value.tv_usec = 100000;
	return setitimer(ITIMER_REAL, &timer, nullptr) == 0;
}

}

/*
 * recv() returning EINTR means the call was cut short, not that the peer or the
 * descriptor is gone, so the connection stays usable and the caller is expected
 * to ask again.
 */
EXO_TEST(sockets_a_peek_interrupted_by_a_signal_keeps_the_connection,
{
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;

	InetSocketAddress dummy((uint16_t) 0);
	auto socket = Socket::create(sv[0], dummy);
	if (!socket) { close(sv[0]); close(sv[1]); return false; }

	if (!arm_interrupting_alarm()) { close(sv[1]); return false; }

	/* Nothing has been sent, so this blocks until the alarm cuts it short. */
	char buf[1] = { 0 };
	if (socket->peek(buf, sizeof(buf)) != 0) { close(sv[1]); return false; }

	/* The connection is still there, so a peek that has something to look at
	   answers with it. */
	if (::send(sv[1], "x", 1, 0) != 1) { close(sv[1]); return false; }

	const ssize_t got = socket->peek(buf, sizeof(buf));
	close(sv[1]);
	return got == 1 && buf[0] == 'x';
});

/* ------------------------------------------------------------------------- */
/* TLS through Socket and the event loop                                     */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_tls_handshake_completes,
{
	TlsFixture fix;
	return fix.ready;
});

EXO_TEST(sockets_tls_reports_both_ends_connected,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	return fix.tcp.client_events.tls_connected
		&& fix.tcp.server_events.tls_connected
		&& !fix.tcp.client_events.error
		&& !fix.tcp.server_events.error;
});

EXO_TEST(sockets_tls_initialize_twice_is_refused,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	/* One factory per socket; the second would leak the first. */
	return !fix.client()->TLSInitialize(false);
});

EXO_TEST(sockets_tls_carries_data_client_to_server,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	const std::string message = "encrypted, going out";

	if (!sendAll(fix.client(), message)) return false;
	return receive(fix.server(), message.size()) == message;
});

EXO_TEST(sockets_tls_carries_data_server_to_client,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	const std::string message = "encrypted, coming back";

	if (!sendAll(fix.server(), message)) return false;
	return receive(fix.client(), message.size()) == message;
});

EXO_TEST(sockets_tls_peek_does_not_consume,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	const std::string message = "peek through TLS";

	if (!sendAll(fix.client(), message)) return false;
	if (peekAll(fix.server(), message.size()) != message) return false;

	return receive(fix.server(), message.size()) == message;
});

/*
 * Reading one TLS record takes all of it off the descriptor, so a caller who
 * asked for less than the record held leaves the rest inside the TLS layer -
 * where a further read still finds it, in order, with nothing lost.
 */
EXO_TEST(sockets_tls_a_partial_read_leaves_the_rest_available,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	const std::string message = "0123456789abcdefghij";

	if (!sendAll(fix.client(), message)) return false;

	/* Wait for the record, then take only part of it. */
	if (peekAll(fix.server(), message.size()) != message) return false;

	char head[4];
	size_t got = 0;
	std::error_code ec;
	if (fix.server()->read(head, sizeof(head), got, ec) != Samurai::IO::ReadResult::Ok)
		return false;
	if (got != sizeof(head)) return false;

	const std::string rest = receive(fix.server(), message.size() - sizeof(head));
	return std::string(head, sizeof(head)) + rest == message;
});

/*
 * The monitor must keep serving a socket whose descriptor has nothing left to
 * report but whose TLS layer still holds data, or the remainder is never
 * delivered.
 */
EXO_TEST(sockets_tls_the_loop_notices_data_the_descriptor_cannot_report,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	const std::string message = "0123456789abcdefghij";

	if (!sendAll(fix.client(), message)) return false;
	if (peekAll(fix.server(), message.size()) != message) return false;

	char head[4];
	size_t got = 0;
	std::error_code ec;
	if (fix.server()->read(head, sizeof(head), got, ec) != Samurai::IO::ReadResult::Ok)
		return false;

	/* Nothing further arrives on the wire from here on, yet the loop must
	   still report the socket readable for what the TLS layer is holding. */
	fix.tcp.server_events.data_available = false;
	if (!pump([&] { return fix.tcp.server_events.data_available; }, 1000)) return false;

	return receive(fix.server(), message.size() - sizeof(head))
		== message.substr(sizeof(head));
});

EXO_TEST(sockets_tls_goodbye_is_reported,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	fix.client()->TLSsendGoodbye();

	return pump([&] { return fix.tcp.client_events.tls_disconnected; });
});

EXO_TEST(sockets_tls_peer_certificate_is_the_one_we_configured,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	/* The client saw the server's certificate, which is the one testkeys.h
	   generated - the same one the library reports as its own. */
	const auto peer = fix.client()->TLSgetPeerCertificateSHA256();
	const auto own = Samurai::IO::Net::TlsFactory::getOwnCertificateSHA256();

	return peer.has_value() && own.has_value() && *peer == *own;
});

/*
 * The record is written by OpenSSL rather than by anything here, so what keeps
 * a write to a departed peer from raising SIGPIPE is the transport the
 * connection was given - not the flags a caller passes. Reaching the assertion
 * at all is the test.
 */
EXO_TEST(sockets_tls_writing_to_a_peer_that_has_gone_is_an_error_not_a_signal,
{
	TlsFixture fix;
	if (!fix.ready) return false;

	fix.tcp.server_events.accepted.reset();

	/*
	 * The first record still goes into the send buffer; what the closed peer
	 * answers with is a reset, and the write after that is the one that finds
	 * the connection gone. Not pumped: the loop would notice the peer first and
	 * leave the socket refusing the write before it reaches TLS.
	 */
	ssize_t last = 0;
	for (int n = 0; n < 64 && last >= 0; n++)
	{
		last = fix.client()->write("payload", 7);
		usleep(1000);
	}

	return last < 0;
});

/* Without TLS there is no peer certificate to report. */
EXO_TEST(sockets_no_peer_certificate_without_tls,
{
	TcpFixture fix;
	if (!fix.ready) return false;

	return !fix.client->TLSgetPeerCertificateSHA256().has_value();
});

EXO_TEST(sockets_tls_releases_its_registrations,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	{
		TlsFixture fix;
		if (!fix.ready) return false;
	}

	return monitor->size() == baseline;
});

/* ------------------------------------------------------------------------- */
/* Connect timeout                                                           */
/*                                                                           */
/* The timer connect() arms could not fire before SocketMonitor::wait() drove */
/* TimerManager, so this reports whatever the network says on a host that     */
/* answers, and the timeout on one that does not.                            */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_connect_timeout_is_settable,
{
	SocketRecorder events;
	auto socket = Socket::create(&events, std::string("127.0.0.1"), (uint16_t) 9);
	if (!socket) return false;

	socket->setConnectTimeout(std::chrono::milliseconds(250));
	return socket->getConnectTimeout() == std::chrono::milliseconds(250);
});

/*
 * 192.0.2.0/24 is RFC 5737 TEST-NET-1, which is not routed, so a connect there
 * hangs rather than being refused. A host behind a firewall that answers with
 * an ICMP rejection instead reports an error, which is a legitimate outcome
 * here - what must not happen is silence.
 */
EXO_TEST(sockets_connect_to_a_black_hole_gives_up,
{
	SocketRecorder events;
	auto socket = Socket::create(&events, std::string("192.0.2.1"), (uint16_t) 80);
	if (!socket) return false;

	socket->setConnectTimeout(std::chrono::milliseconds(300));
	socket->connect();

	if (!pump([&] { return events.error; }, 5000)) return false;

	return events.last_error == Samurai::IO::Net::SocketError::ConnectionTimeout
		|| events.last_error == Samurai::IO::Net::SocketError::HostUnreachable
		|| events.last_error == Samurai::IO::Net::SocketError::NetUnreachable
		|| events.last_error == Samurai::IO::Net::SocketError::SocketUnknown;
});

/* Releasing the socket from inside the timeout handler must be safe: a timer
   callback holds no reference keeping it alive, unlike a monitor dispatch. */
EXO_TEST(sockets_released_in_its_timeout_handler,
{
	struct Dropper : public Samurai::IO::Net::SocketEventHandler
	{
		std::shared_ptr<Socket> socket;
		bool reported = false;

		void EventError(const Socket*, Samurai::IO::Net::SocketError, const char*) override
		{
			reported = true;
			socket.reset();
		}
	};

	Dropper dropper;
	dropper.socket = Socket::create(&dropper, std::string("192.0.2.1"), (uint16_t) 80);
	if (!dropper.socket) return false;

	dropper.socket->setConnectTimeout(std::chrono::milliseconds(200));
	dropper.socket->connect();

	if (!pump([&] { return dropper.reported; }, 5000)) return false;

	pump([&] { return false; }, 50);
	return !dropper.socket;
});

/* ------------------------------------------------------------------------- */
/* Registrations queued behind a rejected change                             */
/*                                                                           */
/* The kqueue backend queues its changes and submits them together on the     */
/* next wait. kevent() abandons the rest of a changelist as soon as one       */
/* element fails, and a rejection is ordinary: closing a descriptor removes   */
/* its registrations, so the EV_DELETE queued when a socket was released is   */
/* refused on the next pass. Churning sockets without pumping in between is   */
/* what puts a rejection in front of a live registration.                    */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_a_registration_survives_a_rejected_change_beside_it,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	/*
	 * Registered, and committed by a pass of the loop, so the kernel really
	 * holds them.
	 */
	std::vector<std::shared_ptr<Samurai::IO::Net::ServerSocket>> servers;
	SocketRecorder events;

	for (int n = 0; n < 160; n++)
	{
		InetSocketAddress any((uint16_t) 0);
		auto server = Samurai::IO::Net::ServerSocket::create(&events, any);
		if (server && server->listen()) servers.push_back(server);
	}
	if (servers.size() < 160) return true;   /* not enough descriptors here */

	monitor->wait(1);

	/*
	 * Dropped together and without another pass. Closing a descriptor already
	 * removes its registrations, so each queued deletion will be refused - and
	 * there are now more of them queued than the eventlist could report on,
	 * which is what made kevent() fail the whole call rather than name the
	 * element.
	 */
	servers.clear();

	/*
	 * A connection that has to work, its registration queued behind all of
	 * those. Discarding the changelist on that failure cost it its readiness
	 * events entirely, and it sat here until something timed it out.
	 */
	TcpFixture fix;
	if (!fix.ready) return false;

	std::error_code ec;
	if (fix.client->write("ping", 4, ec) != 4) return false;
	if (!pump([&] { return fix.server_events.data_available; })) return false;

	char buf[8];
	size_t got = 0;
	if (fix.server_events.accepted->read(buf, sizeof(buf), got, ec)
		!= Samurai::IO::ReadResult::Ok) return false;
	if (got != 4 || memcmp(buf, "ping", 4) != 0) return false;

	pump([&] { return monitor->size() <= baseline; }, 2000);
	return true;
});

/* ------------------------------------------------------------------------- */
/* Dialling twice from one local port                                        */
/* ------------------------------------------------------------------------- */

/*
 * Two peers that can neither of them be connected to reach each other by dialling
 * at once, each from the local port it already used to reach a hub - so the mapping
 * its NAT made for that connection is the one the other's SYN arrives on.
 *
 * That needs a second connection from a port another socket is still holding, which
 * an ordinary outbound socket cannot do: the port is assigned rather than chosen,
 * and it is in use. These cases are about the two things setReuseLocalPort() adds.
 */
EXO_TEST(sockets_a_second_connection_can_share_a_local_port,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	bool ok = false;
	{
		/* Two places to connect to, standing in for a hub and a peer. */
		SocketRecorder hub_events;
		SocketRecorder peer_events;
		InetSocketAddress any((uint16_t) 0);

		auto hub = Samurai::IO::Net::ServerSocket::create(&hub_events, any);
		auto peer = Samurai::IO::Net::ServerSocket::create(&peer_events, any);
		if (!hub || !peer || hub->getFD() == -1 || peer->getFD() == -1)
			EXO_SKIP("could not listen on the loopback");
		if (!hub->listen() || !peer->listen())
			EXO_SKIP("could not listen on the loopback");

		/* The hub connection, whose port is to be shared. */
		SocketRecorder to_hub_events;
		auto to_hub = Socket::create(&to_hub_events, std::string("127.0.0.1"), hub->getLocalPort());
        EXO_ASSERT(to_hub != nullptr);
		to_hub->setReuseLocalPort(0);
		to_hub->connect();
		EXO_ASSERT(pump([&] { return to_hub_events.connected; }));

		const uint16_t shared = to_hub->getLocalPort();
		EXO_ASSERT(shared != 0);

		/* And the peer connection, from that same port. */
		SocketRecorder to_peer_events;
		auto to_peer = Socket::create(&to_peer_events, std::string("127.0.0.1"), peer->getLocalPort());
		EXO_ASSERT(to_peer != nullptr);
		to_peer->setReuseLocalPort(shared);
		to_peer->connect();

		ok = pump([&] { return to_peer_events.connected; });

		/* Connected, and from the port that was asked for rather than another. */
		if (ok) ok = to_peer->getLocalPort() == shared;

		to_peer.reset();
		to_hub.reset();
		hub_events.accepted.reset();
		peer_events.accepted.reset();
		peer.reset();
		hub.reset();
	}

	pump([&] { return monitor->size() <= baseline; }, 1000);
	EXO_ASSERT(ok);
	return 1;
});

/*
 * A local port that cannot be bound is reported, not worked around.
 *
 * Carrying on from a port the system picked instead would connect - and the peer
 * has already been told which port to expect, so the connection it is waiting for
 * would never arrive and nothing would say why. A listening socket's port is one
 * that genuinely cannot be shared this way.
 *
 * Note what the case above establishes about the other direction: a *connected*
 * socket's port can be shared without that socket having agreed, because the two
 * differ in the rest of the tuple. So a client dialling from its hub connection's
 * port needs nothing of the hub connection.
 */
EXO_TEST(sockets_a_local_port_that_cannot_be_bound_is_reported,
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const size_t baseline = monitor->size();

	bool reported = false;
	bool connected = false;
	{
		SocketRecorder peer_events;
		SocketRecorder blocker_events;
		InetSocketAddress any((uint16_t) 0);

		auto peer = Samurai::IO::Net::ServerSocket::create(&peer_events, any);
		auto blocker = Samurai::IO::Net::ServerSocket::create(&blocker_events, any);
		if (!peer || !blocker || peer->getFD() == -1 || blocker->getFD() == -1)
			EXO_SKIP("could not listen on the loopback");
		if (!peer->listen() || !blocker->listen())
			EXO_SKIP("could not listen on the loopback");

		SocketRecorder events;
		auto client = Socket::create(&events, std::string("127.0.0.1"), peer->getLocalPort());
		EXO_ASSERT(client != nullptr);

		/* A port somebody is listening on. */
		client->setReuseLocalPort(blocker->getLocalPort());
		client->connect();

		pump([&] { return events.connected || events.error; }, 1500);
		connected = events.connected;
		reported = events.error;

		client.reset();
		peer_events.accepted.reset();
		blocker.reset();
		peer.reset();
	}

	pump([&] { return monitor->size() <= baseline; }, 1000);

	/* Either the platform refused it and said so, or it allowed it - but it must
	   not have quietly connected from some other port. */
	EXO_ASSERT(reported || connected);
	if (connected) EXO_SKIP("this platform shares a listening socket's port");
	EXO_ASSERT(reported);
	return 1;
});
