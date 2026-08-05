#include <memory>
/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include <samurai/io/net/proxy.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/url.h>
#include <samurai/io/buffer.h>

static bool running = true;

class Connection : public Samurai::IO::Net::SocketEventHandler {
	protected:
		std::shared_ptr<Samurai::IO::Net::Socket> socket;
			
	public:
		Connection(const Samurai::IO::Net::URL& url) {
			/* getHostname() rather than getHost().toString(), which reformats a
			   literal, and getEffectivePort() rather than getPort(), which is 0
			   for an "https://host" that names no port. */
			socket = Samurai::IO::Net::Socket::create(this, url.getHostname(),
			                                          url.getEffectivePort());
		}
		
		virtual ~Connection() {
			socket.reset();
		}
		
		void connect() {
			socket->connect();	
		}
		
		void disconnect() {
			socket.reset();
			socket = 0;
		}

		void write(char* buffer, size_t size) {
			socket->write(buffer, size);
		}


	protected:
		void status(const char* msg) {
			printf("*** %s\n", msg);
		}
		
		void EventHostLookup(const Samurai::IO::Net::Socket*) {
			status("Looking up host...");
		}
		
		void EventHostFound(const Samurai::IO::Net::Socket*) {
			status("Host found.");
		}
		
		void EventConnecting(const Samurai::IO::Net::Socket*) {
			status("SocketState::Connecting ...");
		}
		
		void EventConnected(const Samurai::IO::Net::Socket*) {
			status("SocketState::Connected.");
			if (socket->TLSInitialize(false)) {
				socket->TLSsendHandshake();
			} else {
				status("Unable to initialize SSL socket");
				running = false;
			}
		}
		
		void EventTLSConnected(const Samurai::IO::Net::Socket*) {
			status("TLS SocketState::Connected -- Secure connection established.");
			socket->write("HEAD / HTTP/1.0\r\n\r\n", 20);
		}
	
		void EventTLSDisconnected(const Samurai::IO::Net::Socket*) {
			status("TLS disconnect -- No longer secure connection.");
		}

		
		void EventTimeout(const Samurai::IO::Net::Socket*) {
			status("Connection timed out...");
			running = false;
		}
		
		void EventDisconnected(const Samurai::IO::Net::Socket*) {
			status("SocketState::Disconnected...");
			running = false;
		}
		
		void EventDataAvailable(const Samurai::IO::Net::Socket*) {
			char* buffer = new char[1024];
			size_t bytes = socket->read(buffer, 1024);
			buffer[bytes] = 0;
			printf("%s\n", buffer);
			delete[] buffer;
		}

		void EventCanWrite(const Samurai::IO::Net::Socket*)
		{
			/* can write */
		}
		
		void EventError(const Samurai::IO::Net::Socket*, Samurai::IO::Net::SocketError error, const char* msg)
		{
			(void) error;
			printf(" ERROR: %s\n", msg);
			running = false;
		}
	
};

int main(int argc, char** argv) {
	const char* arg_url = 0;
	const char* arg_socks5 = 0;
	bool arg_tor = false;

	for (int n = 1; n < argc; n++)
	{
		if (!strcmp(argv[n], "--tor")) { arg_tor = true; continue; }
		if (!strcmp(argv[n], "--socks5") && n + 1 < argc) { arg_socks5 = argv[++n]; continue; }
		if (argv[n][0] != '-' && !arg_url) { arg_url = argv[n]; continue; }
		arg_url = 0;
		break;
	}

	if (!arg_url) {
		printf("Usage: %s [--socks5 host:port] [--tor] url\n", argv[0]);
		printf("A simple HTTP file fetcher\n");
		exit(-1);
	}

	/* Before the socket is constructed, which is when it reads the default. */
	if (arg_socks5 || arg_tor)
	{
		Samurai::IO::Net::ProxySettings proxy = arg_socks5
			? Samurai::IO::Net::ProxySettings::fromString(arg_socks5)
			: Samurai::IO::Net::ProxySettings::tor();

		if (!proxy.isEnabled()) {
			printf("Not a usable SOCKS5 proxy: %s\n", arg_socks5);
			exit(-1);
		}

		if (arg_tor) proxy.setTorExtensions(true);
		Samurai::IO::Net::ProxySettings::setDefault(proxy);
		printf("Proxy: '%s'\n", proxy.toString().c_str());
	}

	Samurai::IO::Net::URL url(arg_url);

	printf("Url: '%s'\n", url.toString().c_str());

	Connection con(url);
	con.connect();
	while (running)
	{
		Samurai::IO::Net::SocketMonitor::getInstance()->wait(10000);
	}
	
}

