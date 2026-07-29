#include <memory>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <samurai/messagehandler.h>
#include <samurai/io/net/socketbase.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/inetaddress.h>


#define LOCALPORT 65500

// Samurai::IO::Net::Socket* socket = new Samurai::IO::Net::Socket(0, 0);
class SocketListener :
	public Samurai::IO::Net::ServerSocketEventHandler,
	public Samurai::IO::Net::DatagramEventHandler,
	public Samurai::IO::Net::SocketEventHandler
{

	public:
		bool flag_accept_error;
		const char* accept_error;
		bool flag_accept_socket;
		bool flag_host_lookup;
		bool flag_host_found;
		bool flag_connecting;
		bool flag_connected;
		bool flag_timeout;
		bool flag_disconnected;
		bool flag_data_available;
		bool flag_can_write;
		bool flag_tls_connected;
		bool flag_tls_disconnected;
		bool flag_error;
		bool flag_udp_msg;
		bool flag_udp_error;
		char* udp_msg;
		char* udp_error;
		const char* client_error;
		std::shared_ptr<Samurai::IO::Net::Socket> accepted;
		char* message;
		bool debug_enabled;

	public:
		SocketListener() {
			reset_flags();
			accepted.reset();
			message = 0;
			debug_enabled = false;
		}
		
		void reset_flags()
		{
			flag_accept_error = false;
			flag_accept_socket = false;
			flag_host_lookup = false;
			flag_host_found = false;
			flag_connecting = false;
			flag_connected = false;
			flag_timeout = false;
			flag_disconnected = false;
			flag_data_available = false;
			flag_can_write = false;
			flag_tls_connected = false;
			flag_tls_disconnected = false;
			flag_error = false;
			client_error = 0;
			accept_error = 0;
			flag_error = false;
			flag_udp_msg = false;
			flag_udp_error = false;
			udp_msg = 0;
			udp_error = 0;
		}

	public:
		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char* msg)
		{
			(void) msg;
			accept_error = msg;
			flag_accept_error = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*, std::shared_ptr<Samurai::IO::Net::Socket> socket)
		{
			flag_accept_socket = true;
			accepted = socket;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventHostLookup(const Samurai::IO::Net::Socket*) {
			flag_host_lookup = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventHostFound(const Samurai::IO::Net::Socket*) {
			flag_host_found = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventConnecting(const Samurai::IO::Net::Socket*) {
			flag_connecting = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventConnected(const Samurai::IO::Net::Socket*) {
			flag_connected = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventTimeout(const Samurai::IO::Net::Socket*) {
			flag_timeout = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventDisconnected(const Samurai::IO::Net::Socket*) {
			flag_disconnected = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventDataAvailable(const Samurai::IO::Net::Socket*) {
			flag_data_available = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventCanWrite(const Samurai::IO::Net::Socket*) {
			flag_can_write = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventTLSConnected(const Samurai::IO::Net::Socket*) {
			flag_tls_connected = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventTLSDisconnected(const Samurai::IO::Net::Socket*) {
			flag_tls_disconnected = true;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventError(const Samurai::IO::Net::Socket*, Samurai::IO::Net::SocketError error, const char* msg) {
			(void) error;
			(void) msg;
			flag_error = true;
			client_error = msg;
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventGotDatagram(Samurai::IO::Net::DatagramSocket*, Samurai::IO::Net::DatagramPacket* packet)
		{
			(void) packet;
			flag_udp_msg = true;
			/* udp_msg = packet */
			debug(__PRETTY_FUNCTION__);
		}
		
		void EventDatagramError(const Samurai::IO::Net::DatagramSocket*, const char* msg)
		{
			flag_udp_error = false;
			udp_error = (char*) msg;
			debug(__PRETTY_FUNCTION__);
		}
		
		void debug(const char* str)
		{
			if (debug_enabled)
			{
				puts(str);
			}
		}
		
		void debugEnable()
		{
			debug_enabled = true;
		}
		
		void debugDisable()
		{
			debug_enabled = false;
		}
		
};

class SocketVariables
{
	public:
		SocketVariables() :
			magic(0),
			listener(0),
			server_address(0),
			server_udp(0),
			client_udp(0),
			monitor(0),
			mh(0)
		{
			magic = 0xdeadbeef;
			listener = new SocketListener();
			monitor = Samurai::IO::Net::SocketMonitor::getInstance();
			server_address = 0;
			server.reset();
			mh = Samurai::MessageHandler::getInstance();
		}
		
		~SocketVariables()
		{
			/*
			 * Only the listener is ours. The message handler and the socket
			 * monitor are singletons owned by the library, and releasing them
			 * here left its static pointer dangling.
			 */
			delete listener;
		}
		
	public:
		int magic;
		SocketListener* listener;
		Samurai::IO::Net::SocketAddress* server_address;
		
		std::shared_ptr<Samurai::IO::Net::Socket> client;
		std::shared_ptr<Samurai::IO::Net::ServerSocket> server;
		std::shared_ptr<Samurai::IO::Net::DatagramSocket> server_udp;
		std::shared_ptr<Samurai::IO::Net::DatagramSocket> client_udp;
		Samurai::IO::Net::SocketMonitor* monitor;
		Samurai::MessageHandler* mh;
		
};

static SocketVariables* g_socket_test_vars = 0;


SocketVariables* socket_tests_create()
{
	if (!g_socket_test_vars)
		g_socket_test_vars = new SocketVariables();
		
	return g_socket_test_vars;
}

void socket_tests_destroy()
{
	delete g_socket_test_vars;
	g_socket_test_vars = 0;
}

EXO_TEST(sockets_create_monitor, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	return vars->monitor && vars->monitor->size() == 0;
});






EXO_TEST(sockets_create_server, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->server_address = new Samurai::IO::Net::InetSocketAddress(LOCALPORT);
	vars->server = Samurai::IO::Net::ServerSocket::create(vars->listener, *vars->server_address);
	return vars->server->getFD() != -1;
});

EXO_TEST(sockets_server_listen, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	return vars->server->listen();
});

EXO_TEST(sockets_monitor_count_1, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	return vars->monitor->size() == 1;
});

EXO_TEST(sockets_client_create, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->client = Samurai::IO::Net::Socket::create((Samurai::IO::Net::SocketEventHandler*) 0, std::string("localhost"), (uint16_t) LOCALPORT);
	vars->client->setEventHandler(vars->listener);
	return vars->client != 0;
});

EXO_TEST(sockets_client_connect_1, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->client->connect();
	return vars->client->getFD() != -1;
});

EXO_TEST(sockets_monitor_count_2, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	return vars->monitor->size() == 2;
});

EXO_TEST(sockets_client_connect_2, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	bool ok = vars->listener->flag_host_lookup && vars->listener->flag_host_found && (vars->listener->flag_connecting || vars->listener->flag_connected);
	
	printf("ok=%d, flag_host_lookup=%d, flag_host_found=%d, flag_connecting=%d, flag_connected=%d\n", ok, vars->listener->flag_host_lookup, vars->listener->flag_host_found, vars->listener->flag_connecting, vars->listener->flag_connected);
	
	bool immediate = vars->listener->flag_connected;
	vars->listener->reset_flags();
	
	
	if (immediate)
	{
		/* In case the socket was immediately connected, set the connected
		   flag to true.
		   This happens on FreeBSD, at least.
		 */
		vars->listener->flag_connected = true;
	}
	return ok;
});

EXO_TEST(sockets_monitor_poll_1, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	for (int i = 0; i < 10; i++)
		vars->monitor->wait(25);
	printf("flag_connected=%d, flag_accept_socket=%d\n",  vars->listener->flag_connected, vars->listener->flag_accept_socket);
	bool ok = vars->listener->flag_connected && vars->listener->flag_accept_socket;
	return ok;
});

EXO_TEST(sockets_client_write_1, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->listener->message = (char*) "Hello, there!\n";
	ssize_t n = vars->client->write(vars->listener->message, strlen(vars->listener->message));
	printf("n=%d\n", (int) n);
	return n == (ssize_t) strlen(vars->listener->message);
});

EXO_TEST(sockets_client_read_1, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->monitor->wait(25);
	
	char buf[64];
	if (!vars->listener->accepted) return false;
	ssize_t n = vars->listener->accepted->read(buf, 64);
	return n == (ssize_t) strlen(vars->listener->message) && strncmp(buf, vars->listener->message, n) == 0;
});

EXO_TEST(sockets_client_write_2, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->listener->message = (char*) "Reply from the other end\n";
	
	if (!vars->listener->accepted) return false;
	ssize_t n = vars->listener->accepted->write(vars->listener->message, strlen(vars->listener->message));
	return n == (ssize_t) strlen(vars->listener->message);
});


EXO_TEST(sockets_client_read_2, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->monitor->wait(25);
	
	char buf[64];
	ssize_t n = vars->client->read(buf, 64);
	return n == (ssize_t) strlen(vars->listener->message) && strncmp(buf, vars->listener->message, n) == 0;
});

EXO_TEST(sockets_client_write_vectored, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;

	vars->listener->message = (char*) "GET /index.html HTTP/1.0\r\n\r\n";

	ssize_t n = vars->client->write({"GET /index.html", " HTTP/1.0\r\n", "", "\r\n"});
	return n == (ssize_t) strlen(vars->listener->message);
});

EXO_TEST(sockets_client_read_vectored, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;

	vars->monitor->wait(25);

	char buf[64];
	if (!vars->listener->accepted) return false;
	ssize_t n = vars->listener->accepted->read(buf, 64);
	return n == (ssize_t) strlen(vars->listener->message) && strncmp(buf, vars->listener->message, n) == 0;
});

EXO_TEST(sockets_server_udp_create, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->server_udp = Samurai::IO::Net::DatagramSocket::create(vars->listener, (uint16_t) LOCALPORT);
	return vars->server_udp && vars->server_udp->listen();
});

EXO_TEST(sockets_monitor_count_3, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	return vars->monitor->size() == 3;
});

EXO_TEST(sockets_client_udp_create, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->client_udp = Samurai::IO::Net::DatagramSocket::create(vars->listener, Samurai::IO::Net::InetAddress::Version::IPv4);
	return vars->client_udp != 0;
});


EXO_TEST(sockets_monitor_count_4, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	return vars->monitor->size() == 4;
});


EXO_TEST(sockets_client_disconnect, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->listener->accepted.reset();
	vars->listener->accepted.reset();
	vars->listener->reset_flags();
	vars->monitor->wait(25);
	return vars->listener->flag_disconnected;
});

EXO_TEST(sockets_monitor_count_5, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->client.reset();
	vars->client.reset();
	vars->listener->reset_flags();
	vars->monitor->wait(25);
	return vars->monitor->size() == 3;
});

EXO_TEST(sockets_client_udp_write, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->listener->message = (char*) "Hello, there!\n";
	Samurai::IO::Net::DatagramPacket packet((uint8_t*) vars->listener->message, strlen(vars->listener->message));
	Samurai::IO::Net::InetSocketAddress target(std::string("127.0.0.1"), LOCALPORT);
	packet.setAddress(&target);
	
	int n = vars->client_udp->send(&packet);
	return n == (ssize_t) strlen(vars->listener->message);
});

EXO_TEST(sockets_monitor_poll_2, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->monitor->wait(25);
	bool ok = vars->listener->flag_udp_msg;
	return ok;
});


EXO_TEST(sockets_monitor_count_6, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->server.reset();
	delete vars->server_address;
	vars->server.reset();
	vars->server_address = 0;
	vars->listener->reset_flags();
	
	vars->monitor->wait(25);
	return vars->monitor->size() == 2;
});


EXO_TEST(sockets_monitor_count_7, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->server_udp.reset();
	vars->server_udp = 0;
	vars->listener->reset_flags();
	
	vars->monitor->wait(25);
	return vars->monitor->size() == 1;
});


EXO_TEST(sockets_monitor_count_8, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->client_udp.reset();
	vars->client_udp = 0;
	vars->listener->reset_flags();
	
	vars->monitor->wait(25);
	return vars->monitor->size() == 0;
});


EXO_TEST(sockets_monitor_poll_3, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->monitor->wait(25);
	return 1;
});


EXO_TEST(sockets_monitor_shutdown, {
	socket_tests_destroy();
	return !g_socket_test_vars;
});


/* ------------------------------------------------------------------------- */
/* A refused connect leaves the socket reusable                               */
/*                                                                            */
/* A connection that was refused or timed out is one that can be attempted     */
/* again, so the socket ends up Disconnected: a state connect() accepts, and   */
/* one the Error trigger recognises as already handled, so a single failure    */
/* is reported once.                                                          */
/* ------------------------------------------------------------------------- */

namespace {

/* Nothing listens on port 1 of the loopback interface without privileges, so a
   connect there is refused immediately rather than left to time out. */
const uint16_t REFUSED_PORT = 1;

class ConnectFailureListener : public Samurai::IO::Net::SocketEventHandler
{
	public:
		size_t errors = 0;
		size_t connects = 0;
		size_t disconnects = 0;

		void EventConnected(const Samurai::IO::Net::Socket*) { connects++; }
		void EventDisconnected(const Samurai::IO::Net::Socket*) { disconnects++; }
		void EventError(const Samurai::IO::Net::Socket*, Samurai::IO::Net::SocketError, const char*) { errors++; }
};

/* Drive the monitor until the handler has seen an error, or the budget runs
   out. The refusal arrives on the first or second pass in practice. */
static bool pump_until_error(ConnectFailureListener& listener)
{
	Samurai::IO::Net::SocketMonitor* monitor = Samurai::IO::Net::SocketMonitor::getInstance();
	for (int n = 0; n < 40 && listener.errors == 0; n++)
		monitor->wait(25);
	return listener.errors > 0;
}

}

EXO_TEST(sockets_refused_connect_reports_one_error, {
	ConnectFailureListener listener;
	std::shared_ptr<Samurai::IO::Net::Socket> sock =
		Samurai::IO::Net::Socket::create(&listener, std::string("127.0.0.1"), REFUSED_PORT);
	if (!sock) return false;

	sock->connect();
	if (!pump_until_error(listener)) return false;

	/* The Write and Error triggers arrive together for a refused connect, so
	   pump once more: one refusal is still one error. */
	Samurai::IO::Net::SocketMonitor::getInstance()->wait(25);
	return listener.errors == 1 && listener.connects == 0;
});

EXO_TEST(sockets_refused_connect_can_be_retried, {
	ConnectFailureListener listener;
	std::shared_ptr<Samurai::IO::Net::Socket> sock =
		Samurai::IO::Net::Socket::create(&listener, std::string("127.0.0.1"), REFUSED_PORT);
	if (!sock) return false;

	sock->connect();
	if (!pump_until_error(listener)) return false;

	/* The failed attempt released the descriptor, so a retry that is really
	   made allocates a new one. */
	sock->connect();
	return sock->getFD() != INVALID_SOCKET;
});

/* NOTE: that disconnect() stays quiet for a connection which never came up has
   no assertion here. A refused connect reaches Disconnected, which disconnect()
   ignores for the same reason Invalid is ignored, so the two are
   indistinguishable from outside. Only the connect timeout separates them, and
   that needs an address which blackholes rather than refuses. */


/* ------------------------------------------------------------------------- */
/* Writing to a peer that has gone away                                       */
/*                                                                            */
/* A platform with no MSG_NOSIGNAL relies on SO_NOSIGPIPE being set on the     */
/* descriptor, without which this write raises the signal instead of failing.  */
/* Reaching the assertion at all is the test.                                  */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_write_to_closed_peer_returns_an_error_not_a_signal, {
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;

	/* Close the reader, so the first or second write to sv[0] gets EPIPE. */
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


/* ------------------------------------------------------------------------- */
/* Failures that arrive without a return value                                */
/*                                                                            */
/* A constructor cannot report that the descriptor was never created, so the   */
/* state carries it and listen() refuses. A readiness notification that turns  */
/* out to have nothing behind it is not an error either, and must not reach    */
/* the handler as one.                                                        */
/* ------------------------------------------------------------------------- */

EXO_TEST(sockets_server_without_an_address_family_is_not_created, {
	/* An unset address has no family, so socket() cannot be asked for one. */
	Samurai::IO::Net::InetAddress unset_addr;
	Samurai::IO::Net::InetSocketAddress unset(unset_addr, (uint16_t) 0);
	std::shared_ptr<Samurai::IO::Net::ServerSocket> server =
		Samurai::IO::Net::ServerSocket::create(
			(Samurai::IO::Net::ServerSocketEventHandler*) nullptr, unset);

	/* A constructor cannot return a failure, so the factory does it. */
	return server == nullptr;
});

EXO_TEST(sockets_datagram_read_with_nothing_pending_is_not_an_error, {
	std::shared_ptr<Samurai::IO::Net::DatagramSocket> sock =
		Samurai::IO::Net::DatagramSocket::create(
			(Samurai::IO::Net::DatagramEventHandler*) nullptr, (uint16_t) 0);
	if (!sock || !sock->listen()) return false;

	Samurai::IO::Net::DatagramPacket packet;

	/* Nothing has been sent here, and the socket is non-blocking, so recvfrom()
	   reports EAGAIN. That is "no datagram", not "the socket failed". */
	return sock->read(&packet) == 0;
});


/* ------------------------------------------------------------------------- */
/* A signal is not a read error                                               */
/*                                                                            */
/* recv() returning EINTR means the call was cut short, not that the peer or  */
/* the descriptor is gone, so the connection stays usable and the caller is    */
/* expected to ask again.                                                     */
/*                                                                            */
/* Socket::create() forwards to the descriptor-adopting constructor, so a      */
/* socketpair() gives a connected Socket with no port, no listener and no      */
/* timing to depend on.                                                       */
/* ------------------------------------------------------------------------- */

namespace {

extern "C" void samurai_test_alarm_handler(int) { }

/* Arrange for SIGALRM to arrive shortly, without SA_RESTART: the default
   disposition would resume the call rather than fail it with EINTR. */
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

EXO_TEST(sockets_peek_interrupted_by_a_signal_keeps_the_connection, {
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;

	Samurai::IO::Net::InetSocketAddress dummy((uint16_t) 0);
	std::shared_ptr<Samurai::IO::Net::Socket> sock =
		Samurai::IO::Net::Socket::create(sv[0], dummy);
	if (!sock) { close(sv[0]); close(sv[1]); return false; }

	if (!arm_interrupting_alarm()) { close(sv[1]); return false; }

	/* Nothing has been sent, so this blocks until the alarm cuts it short. */
	char buf[1] = { 0 };
	if (sock->peek(buf, sizeof(buf)) != 0) { close(sv[1]); return false; }

	/* The connection is still there, so a peek that has something to look at
	   answers with it. */
	if (::send(sv[1], "x", 1, 0) != 1) { close(sv[1]); return false; }

	const ssize_t got = sock->peek(buf, sizeof(buf));
	close(sv[1]);
	return got == 1 && buf[0] == 'x';
});
