/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/dns/dnsconfig.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/buffer.h>
#include <samurai/io/file.h>
#include <stdlib.h>

Samurai::IO::Net::DNS::ResolveConfiguration::ResolveConfiguration()
{
	for (size_t n = 0; n < MAXNS; n++) {
		nameservers[n] = 0;
	}

	num_nameservers  = 0;
	cur_nameserver   = 0;

	option_timeout   = 5;
	option_attempts  = 2;
	option_ndots     = 1;
	option_rotate    = false;
	option_ipv6      = false;
	option_debug     = false;

	parse();
}


Samurai::IO::Net::DNS::ResolveConfiguration::~ResolveConfiguration()
{
	for (size_t n = 0; n < num_nameservers; n++) {
		delete nameservers[n];
	}
}


void Samurai::IO::Net::DNS::ResolveConfiguration::skipNameServer() {
	cur_nameserver++;
}

Samurai::IO::Net::InetAddress* Samurai::IO::Net::DNS::ResolveConfiguration::getNameServer(size_t n)
{
	if (!num_nameservers) return 0;

	if (option_rotate) cur_nameserver = n % num_nameservers;
	if (cur_nameserver > num_nameservers) cur_nameserver = 0;
	return nameservers[cur_nameserver];
}


char* Samurai::IO::Net::DNS::ResolveConfiguration::getNameSearch()
{
	return 0;
}

size_t Samurai::IO::Net::DNS::ResolveConfiguration::getNDots() const { return option_ndots; }
size_t Samurai::IO::Net::DNS::ResolveConfiguration::getTimeout() const { return option_timeout; }
size_t Samurai::IO::Net::DNS::ResolveConfiguration::getAttempts() const { return option_attempts; }
bool   Samurai::IO::Net::DNS::ResolveConfiguration::isRotate() const { return option_rotate; }
bool   Samurai::IO::Net::DNS::ResolveConfiguration::isIPv6() const { return option_ipv6; }
bool   Samurai::IO::Net::DNS::ResolveConfiguration::isDebug() const { return option_debug; }

#define IS_SPACE(X) (X == ' ' || X == '\t')

void Samurai::IO::Net::DNS::ResolveConfiguration::parse() {
	
	QDBG("[DNS] Reading /etc/resolv.conf...");
	Samurai::IO::File conf("/etc/resolv.conf");
	if (conf.open(Samurai::IO::File::Read)) {
		Samurai::IO::Buffer buffer(conf.size());
		conf.read(&buffer, conf.size());
		QDBG("[DNS] Read %d bytes", (int) buffer.size());
		
		int offset = 0;
		int last = 0;
		offset = buffer.find("\n", offset);
		while (offset != -1) {
			char* line = buffer.memdup(last, offset);
			size_t len = strlen(line);
			if (len) {
				if (!strncmp(line, "nameserver", 10)) {
					size_t n = 10;
					for (; IS_SPACE(line[n]) && n < len; n++)
					{
						// do nothing
					}

					if (strlen(&line[n])) {
						addNameServer(&line[n]);
					}
				} else if (!strncmp(line, "search", 6)) {
					
				} else if (!strncmp(line, "domain", 6)) {
				
				} else if (!strncmp(line, "options", 7)) {
					if (strstr(line, "rotate")) {
						option_rotate = true;
					}

					if (strstr(line, "inet6")) {
						option_ipv6 = true;
					}

					if (strstr(line, "debug")) {
						option_debug = true;
					}

					if (strstr(line, "attempts:")) {
						char* pos = &(strstr(line, "attempts:"))[9];
						if (strlen(pos)) {
							int n = quickdc_atoi(pos);
							if (n > 0) option_attempts = (size_t) n;
						}
					}

					if (strstr(line, "timeout:")) {
						char* pos = &(strstr(line, "timeout:"))[8];
						if (strlen(pos)) {
							int n = quickdc_atoi(pos);
							if (n > 0) option_timeout = (size_t) n;
						}
					}

					if (strstr(line, "ndots:")) {
						char* pos = &(strstr(line, "ndots:"))[6];
						if (strlen(pos)) {
							int n = quickdc_atoi(pos);
							if (n > 0) option_ndots = (size_t) n;
						}
					}

				}
			}
			free(line);
			last = ++offset;
			offset = buffer.find("\n", ++offset);
		}
		conf.close();
	}
	QDBG("[DNS] Done. Found %d nameservers.", (int) num_nameservers);
}


void Samurai::IO::Net::DNS::ResolveConfiguration::addNameServer(const char* server)
{
	if (num_nameservers >= MAXNS) return; // too many found!
	Samurai::IO::Net::InetAddress* addr = new Samurai::IO::Net::InetAddress(server);
	if (addr->isValid()) {
		nameservers[num_nameservers++] = addr;
	}
}



