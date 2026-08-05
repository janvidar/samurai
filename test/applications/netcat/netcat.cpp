/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <memory>

#include <stdio.h>
#include <stdlib.h>

#ifdef SAMURAI_POSIX
#include <poll.h>
#include <sys/poll.h>
#endif

#ifdef SAMURAI_WINDOWS
#include <conio.h>
#endif

#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <samurai/io/net/proxy.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/buffer.h>

static bool running = true;

static bool  arg_listen     = false;
static bool  arg_nodns      = false;
static bool  arg_udp        = false;
static bool  arg_verbose    = false;
static bool  arg_zero_io    = false;
static bool  arg_ssl        = false;
static bool  arg_randomize  = false;
static bool  arg_broadcast  = false;
static bool  arg_keepalive  = false;
static bool  arg_ipv6       = false;
static int   arg_timeout    = 300; // 300 seconds
static char* arg_hexdump    = 0;
static int   arg_local_port = 0;
static char* arg_local_addr = 0;
static char* arg_addr       = 0;
static int   arg_port       = 0;
static char* arg_socks5     = 0;
static char* arg_socks_user = 0;
static char* arg_socks_pass = 0;
static bool  arg_tor        = false;

class Connection :
	public Samurai::IO::Net::SocketEventHandler,
	public Samurai::IO::Net::ServerSocketEventHandler,
	public Samurai::IO::Net::DatagramEventHandler
{
	protected:
		std::shared_ptr<Samurai::IO::Net::SocketBase> socket;
		std::shared_ptr<Samurai::IO::Net::ServerSocket> server;
		std::shared_ptr<Samurai::IO::Net::DatagramSocket> udp;

	public:
		Connection(char* host, int port)
		{
			socket = Samurai::IO::Net::Socket::create(this, host, port);
			server = 0;
			udp = 0;
		}

		Connection() {
			socket = 0;
			server = 0;
			udp = 0;
		}

		virtual ~Connection() {
			socket.reset();
			server.reset();
			udp.reset();
		}

		void listen(int localport, const char* bindaddr = 0)
		{
			if (!bindaddr)
				bindaddr = arg_ipv6 ? "::" : "0.0.0.0";

			Samurai::IO::Net::InetAddress localAddr(bindaddr);

			if (arg_udp)
			{
				udp.reset();
				udp = Samurai::IO::Net::DatagramSocket::create(this, localAddr, localport);
				if (!udp->listen())
				{
					error("could not bind to port. Disabling service");
				}
			}
			else
			{
				Samurai::IO::Net::InetSocketAddress bindAddr(localAddr, localport);

				server.reset();
				server = Samurai::IO::Net::ServerSocket::create(this, localAddr, localport);
				if (!server->listen())
				{
					error("could not bind to port. Disabling service");
				}
			}
		}


		void connect()
		{
			std::shared_ptr<Samurai::IO::Net::Socket> tcp_sock = std::dynamic_pointer_cast<Samurai::IO::Net::Socket>(socket);
			if (tcp_sock) {
				tcp_sock->connect();
			}
		}

		void disconnect()
		{
			socket.reset();
		}

		void write(char* buffer, size_t size)
		{
			if (arg_udp)
			{

			}
			else
			{
				std::shared_ptr<Samurai::IO::Net::Socket> tcp_sock = std::dynamic_pointer_cast<Samurai::IO::Net::Socket>(socket);
				if (tcp_sock)
				{
					tcp_sock->write(buffer, size);
				}
			}
		}

		void status(const char* msg)
		{
			printf("*** %s\n", msg);
		}

		void error(const char* msg)
		{
			printf("*** ERROR: %s\n", msg);
			running = false;
		}

		void do_connected()
		{
			// socket->write("HEAD / HTTP/1.0\r\n\r\n", 20);
		}

		void do_accepted()
		{
			// socket->write("HEAD / HTTP/1.0\r\n\r\n", 20);
		}

	protected:

		void EventHostLookup(const Samurai::IO::Net::Socket*)
		{
			if (arg_verbose) status("Looking up host...");
		}

		void EventHostFound(const Samurai::IO::Net::Socket*)
		{
			if (arg_verbose) status("Host found.");
		}

		void EventConnecting(const Samurai::IO::Net::Socket*)
		{
			if (arg_verbose) status("SocketState::Connecting ...");
		}

		void EventConnected(const Samurai::IO::Net::Socket*)
		{
			if (arg_verbose) status("SocketState::Connected.");

			if (arg_ssl)
			{
				std::shared_ptr<Samurai::IO::Net::Socket> sock = std::dynamic_pointer_cast<Samurai::IO::Net::Socket>(socket);
				if (sock->TLSInitialize(false))
				{
					sock->TLSsendHandshake();
				}
				else
				{
					error("Unable to initialize SSL socket");
				}
			}
			else
			{
				void do_connected();
			}
		}

		void EventTLSConnected(const Samurai::IO::Net::Socket*)
		{
			if (arg_listen)
			{
				if (arg_verbose) status("TLS Accepted -- Secure connection established.");
				do_accepted();
			}
			else
			{
				if (arg_verbose) status("TLS SocketState::Connected -- Secure connection established.");
				do_connected();
			}
		}

		void EventTLSDisconnected(const Samurai::IO::Net::Socket*)
		{
			if (arg_verbose) status("TLS disconnect -- No longer secure connection.");
		}


		void EventTimeout(const Samurai::IO::Net::Socket*)
		{
			error("Connection timed out...");
		}

		void EventDisconnected(const Samurai::IO::Net::Socket*)
		{
			if (arg_verbose) status("SocketState::Disconnected...");
			running = false;
		}

		void EventDataAvailable(const Samurai::IO::Net::Socket*)
		{
			char* buffer = 0;
			ssize_t bytes = 0;

			std::shared_ptr<Samurai::IO::Net::Socket> tcp_sock = std::dynamic_pointer_cast<Samurai::IO::Net::Socket>(socket);
			if (tcp_sock) {
				buffer = new char[tcp_sock->getReceiveBufferSize()];
				bytes = tcp_sock->read(buffer, 1024);
				buffer[bytes] = 0;
			}

			printf("%s", buffer);
			delete[] buffer;
		}

		void EventCanWrite(const Samurai::IO::Net::Socket*)
		{
			/* can write */
		}

		void EventError(const Samurai::IO::Net::Socket*, Samurai::IO::Net::SocketError /*error*/, const char* msg)
		{
			error(msg);
		}


		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char* msg)
		{
			error(msg);
		}

		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*, std::shared_ptr<Samurai::IO::Net::Socket> sock)
		{
			if (arg_verbose)
			{
				char buf[100];
				snprintf(buf, sizeof(buf), "Accepted connection from: %s", sock->getAddress()->toString().c_str());
				status(buf);
			}

			if (socket)
				socket.reset();

			sock->setNonBlocking(true);
			sock->setEventHandler(this);
			sock->setMonitor(Samurai::IO::Net::SocketMonitor::Triggers::Read);
			socket = sock;

			if (arg_ssl)
			{
				if (sock->TLSInitialize(true))
				{
					sock->TLSsendHandshake();
				}
				else
				{
					error("Unable to initialize SSL socket");
				}
			}
			else
			{
				void do_accepted();
			}
		}

		void EventGotDatagram(Samurai::IO::Net::DatagramSocket*, Samurai::IO::Net::DatagramPacket* packet)
		{
			if (arg_verbose)
			{
				printf("Got datagram from: %s\n", packet->getAddress()->toString().c_str());
			}

			char* buffer = new char[packet->size()+1];
			buffer[packet->size()] = 0;
			packet->getBuffer()->pop(buffer, packet->size());

			printf("%s", buffer);
			delete[] buffer;
		}

		void EventDatagramError(const Samurai::IO::Net::DatagramSocket*, const char* msg)
		{
			fprintf(stderr, "Error: %s\n", msg);
		}

};

void print_usage(const char* cmd)
{
	fprintf(stdout, "connect to somewhere:   %s [-options] hostname port[s] [ports] ...\n", cmd);
	fprintf(stdout, "listen for inbound:     %s -l -p port [-options] [hostname] [port]\n", cmd);

	fprintf(stdout,
"options:\n"
// "        -c shell commands       as `-e'; use /bin/sh to exec [dangerous!!]\n"
// "        -e filename             program to exec after connect [dangerous!!]\n"
"        -b                      allow broadcasts\n"
"        -6                      Use Version::IPv6\n"
// "        -g gateway              source-routing hop point[s], up to 8\n"
// "        -G num                  source-routing pointer: 4, 8, 12, ...\n"
"        -h                      this cruft\n"
// "        -i secs                 delay interval for lines sent, ports scanned\n"
"        -k                      set keepalive option on socket\n"
"        -l                      listen mode, for inbound connects\n"
"        -n                      numeric-only IP addresses, no DNS\n"
"        -o file                 hex dump of traffic\n"
"        -p port                 local port number\n"
"        -r                      randomize local and remote ports\n"
// "        -q secs                 quit after EOF on stdin and delay of secs\n"
"        -s addr                 local source address\n"
// "        -t                      answer TELNET negotiation\n"
"        -u                      UDP mode\n"
"        -v                      verbose [use twice to be more verbose]\n"
"        -w secs                 timeout for connects and final net reads\n"
// "        -x tos                  set Type Of Service\n"
"        -z                      zero-I/O mode [used for scanning]\n"
"        --socks5 host:port      reach the peer through a SOCKS5 proxy. The\n"
"                                peer's name is sent to the proxy and never\n"
"                                resolved here.\n"
"        --socks5-user name      SOCKS5 username (RFC 1929)\n"
"        --socks5-pass word      SOCKS5 password\n"
"        --tor                   as --socks5 127.0.0.1:9050, and enables Tor's\n"
"                                RESOLVE extension so -n is not needed\n"
// "port numbers can be individual or ranges: lo-hi [inclusive];\n"
// "hyphens in port names must be backslash escaped (e.g. 'ftp\-data').\n"
);

	exit(0);
}


void parse_args(int argc, char** argv)
{
#define CHECK_BOOL_ARG(TOGGLE, VAR) if (!strcmp(argv[n], TOGGLE) ) {VAR = true; n++; continue;}
#define CHECK_INT_ARG(TOGGLE, VAR) if (!strcmp(argv[n], TOGGLE) && n+1 < argc) { VAR=atoi(argv[n+1]); n+=2; continue;}
#define CHECK_STR_ARG(TOGGLE, VAR) if (!strcmp(argv[n], TOGGLE) && n+1 < argc) { VAR=argv[n+1]; n+=2; continue;}

	int n = 1;
	int rem;

	while (rem = argc - n, rem > 0)
	{
		if (argv[n][0] == '-')
		{
			if ( !strcmp(argv[n], "-h") || !strcmp(argv[n], "--help") )
			{
				print_usage(argv[0]);
			}
			CHECK_BOOL_ARG("-v", arg_verbose);
			CHECK_BOOL_ARG("-z", arg_zero_io);
			CHECK_BOOL_ARG("-l", arg_listen);
			CHECK_BOOL_ARG("-n", arg_nodns);
			CHECK_BOOL_ARG("-u", arg_udp);
			CHECK_BOOL_ARG("-X", arg_ssl);
			CHECK_BOOL_ARG("-k", arg_keepalive);
			CHECK_BOOL_ARG("-b", arg_broadcast);
			CHECK_BOOL_ARG("-r", arg_randomize);
			CHECK_BOOL_ARG("-6", arg_ipv6);

			CHECK_BOOL_ARG("--tor", arg_tor);

			CHECK_INT_ARG("-p", arg_local_port);
			CHECK_STR_ARG("-s", arg_local_addr);
			CHECK_INT_ARG("-w", arg_timeout);
			CHECK_STR_ARG("-o", arg_hexdump);
			CHECK_STR_ARG("--socks5", arg_socks5);
			CHECK_STR_ARG("--socks5-user", arg_socks_user);
			CHECK_STR_ARG("--socks5-pass", arg_socks_pass);

			fprintf(stderr, "Unknown argument: %s\n", argv[n]);
			exit(1);
		}
		else
		{
			if (n < argc && !arg_addr) arg_addr = argv[n++];
			else print_usage(argv[0]);

			if (n < argc && !arg_port) arg_port = atoi(argv[n++]);
			else print_usage(argv[0]);
		}
	}

	if (arg_listen) {
		if (!arg_local_port)
			print_usage(argv[0]);
	} else {
		if (!arg_port || !arg_addr)
			print_usage(argv[0]);
	}
}


/**
 * Turn the proxy arguments into the process-wide default.
 *
 * Set before any socket is constructed, which is when a socket reads it.
 */
static bool apply_proxy_args(const char* cmd)
{
	if (!arg_socks5 && !arg_tor) return true;

	Samurai::IO::Net::ProxySettings proxy = arg_socks5
		? Samurai::IO::Net::ProxySettings::fromString(arg_socks5)
		: Samurai::IO::Net::ProxySettings::tor();

	if (!proxy.isEnabled())
	{
		fprintf(stderr, "%s: not a usable SOCKS5 proxy: %s\n", cmd, arg_socks5);
		return false;
	}

	if (arg_socks_user || arg_socks_pass)
	{
		if (!proxy.setCredentials(arg_socks_user ? arg_socks_user : "",
		                          arg_socks_pass ? arg_socks_pass : ""))
		{
			fprintf(stderr, "%s: SOCKS5 credentials are too long\n", cmd);
			return false;
		}
	}

	/* Only for --tor: on any other proxy an unknown command costs a round trip
	   and then fails a lookup that the system resolver would have answered. */
	if (arg_tor) proxy.setTorExtensions(true);

	Samurai::IO::Net::ProxySettings::setDefault(proxy);

	if (arg_verbose)
		fprintf(stderr, "%s: using SOCKS5 proxy %s\n", cmd, proxy.toString().c_str());

	return true;
}

#define MAXLINE 1024

int main(int argc, char** argv) {
	parse_args(argc, argv);
	if (!apply_proxy_args(argv[0])) return 1;

	Samurai::IO::Net::SocketMonitor* monitor = Samurai::IO::Net::SocketMonitor::getInstance();

	Connection* con = 0;

	if (!arg_listen)
	{
		con = new Connection(arg_addr, arg_port);
		con->connect();
	} else {
		con = new Connection();
		con->listen(arg_local_port, arg_local_addr);
	}

#ifdef SAMURAI_POSIX
	struct pollfd pfd;
	pfd.fd = fileno(stdin);
	pfd.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;

	if (fcntl(fileno(stdin), F_SETFL, O_NONBLOCK) == -1) {
		printf("Unable to set stdin non-blocking\n");
		return -1;
	}
#endif // SAMURAI_POSIX


#ifdef SAMURAI_WINDOWS
	char* line = new char[MAXLINE];
	int chars = 0;
#endif

	while (running)
	{
		monitor->wait(10);
		Samurai::MessageHandler::getInstance()->process();

#ifdef SAMURAI_POSIX
		int pollret = poll(&pfd, 1, 50);
		if (pollret == 0) continue;
		else if (pollret == -1) break;
		else {
			char* buffer = new char[MAXLINE];
			fgets(buffer, MAXLINE, stdin);
			size_t size = strlen(buffer);
			con->write(buffer, size);
			delete[] buffer;
		}
#endif

#ifdef SAMURAI_WINDOWS
		if (_kbhit()) {
			int ch = _getche();
			if (ch == '\n' || ch == '\r' || chars == MAXLINE-2) {
				line[chars++] = '|';
				line[chars++] = '\0';

				con->write(line, chars);
				chars = 0;
				line[0] = '\0';
			} else {
				line[chars++] = ch;
			}
		}
#endif
	}

#ifdef SAMURAI_WINDOWS
	delete[] line;
#endif
	delete con;
}
