/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include <samurai/io/net/socket.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/url.h>
#include <samurai/io/buffer.h>

static bool running = true;

class Connection : public Samurai::IO::Net::SocketEventHandler {
	protected:
		Samurai::IO::Net::Socket* socket;
			
	public:
		Connection(const Samurai::IO::Net::URL& url) {
			socket = new Samurai::IO::Net::Socket(this, url.getHost().toString(), url.getPort());
		}
		
		virtual ~Connection() {
			delete socket;
		}
		
		void connect() {
			socket->connect();	
		}
		
		void disconnect() {
			delete socket;
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
			status("Connecting ...");
		}
		
		void EventConnected(const Samurai::IO::Net::Socket*) {
			status("Connected.");
			if (socket->TLSInitialize(false)) {
				socket->TLSsendHandshake();
			} else {
				status("Unable to initialize SSL socket");
				running = false;
			}
		}
		
		void EventTLSConnected(const Samurai::IO::Net::Socket*) {
			status("TLS Connected -- Secure connection established.");
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
			status("Disconnected...");
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
		
		void EventError(const Samurai::IO::Net::Socket*, enum Samurai::IO::Net::SocketError error, const char* msg)
		{
			(void) error;
			printf(" ERROR: %s\n", msg);
			running = false;
		}
	
};

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: %s url\n", argv[0]);
		printf("A simple HTTP file fetcher\n");
		exit(-1);
	}

	Samurai::IO::Net::URL url(argv[1]);

	printf("Url: '%s'\n", url.toString().c_str());

	Connection con(url);
	con.connect();
	while (running)
	{
		Samurai::IO::Net::SocketMonitor::getInstance()->wait(10000);
	}
	
}

