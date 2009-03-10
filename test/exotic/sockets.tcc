
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
		Samurai::IO::Net::Socket* accepted;
		char* message;
		bool debug_enabled;

	public:
		SocketListener() {
			reset_flags();
			accepted = 0;
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
		
		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*, Samurai::IO::Net::Socket* socket)
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
		
		void EventError(const Samurai::IO::Net::Socket*, enum Samurai::IO::Net::SocketError error, const char* msg) {
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
			server = 0;
			mh = Samurai::MessageHandler::getInstance();
		}
		
		~SocketVariables()
		{
			delete mh;
			delete monitor;
			delete listener;
		}
		
	public:
		int magic;
		SocketListener* listener;
		Samurai::IO::Net::SocketAddress* server_address;
		
		Samurai::IO::Net::Socket* client;
		Samurai::IO::Net::ServerSocket* server;
		Samurai::IO::Net::DatagramSocket* server_udp;
		Samurai::IO::Net::DatagramSocket* client_udp;
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
	vars->server = new Samurai::IO::Net::ServerSocket(vars->listener, *vars->server_address);
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
	vars->client = new Samurai::IO::Net::Socket(0, "localhost", LOCALPORT);
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
	ssize_t n = vars->listener->accepted->read(buf, 64);
	return n == (ssize_t) strlen(vars->listener->message) && strncmp(buf, vars->listener->message, n) == 0;
});

EXO_TEST(sockets_client_write_2, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	vars->listener->message = (char*) "Reply from the other end\n";
	
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

EXO_TEST(sockets_server_udp_create, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	vars->server_udp = new Samurai::IO::Net::DatagramSocket(vars->listener, LOCALPORT);
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
	vars->client_udp = new Samurai::IO::Net::DatagramSocket(vars->listener, Samurai::IO::Net::InetAddress::IPv4);
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
	delete vars->listener->accepted;
	vars->listener->accepted = 0;
	vars->listener->reset_flags();
	vars->monitor->wait(25);
	return vars->listener->flag_disconnected;
});

EXO_TEST(sockets_monitor_count_5, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	delete vars->client;
	vars->client = 0;
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
	
	delete vars->server;
	delete vars->server_address;
	vars->server = 0;
	vars->server_address = 0;
	vars->listener->reset_flags();
	
	vars->monitor->wait(25);
	return vars->monitor->size() == 2;
});


EXO_TEST(sockets_monitor_count_7, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	delete vars->server_udp;
	vars->server_udp = 0;
	vars->listener->reset_flags();
	
	vars->monitor->wait(25);
	return vars->monitor->size() == 1;
});


EXO_TEST(sockets_monitor_count_8, {
	SocketVariables* vars = socket_tests_create();
	if (!vars) return false;
	
	delete vars->client_udp;
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
