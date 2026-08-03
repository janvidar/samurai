/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/io/net/upnp/portmap.h>
#include <samurai/io/net/upnp/portmapper.h>
#include <samurai/timer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <memory>

/*
 * Ask the local gateway to forward a port, and the other way round.
 *
 * The add, delete and external-address commands go through the blocking helpers,
 * which is also what smoke-tests them; 'list' goes through the asynchronous API
 * so that path is exercised too.
 */

using namespace Samurai::IO::Net::UPnP;

namespace {

void usage(const char* argv0)
{
	printf("Usage: %s [options] add|del|external|list <port>\n", argv0);
	printf("Adds or removes a UPnP port mapping on the local gateway.\n\n");
	printf("  add <port>       forward this port to this host\n");
	printf("  del <port>       remove the mapping for this external port\n");
	printf("  external         print the address the world sees\n");
	printf("  list             print the gateway's whole mapping table\n\n");
	printf("  --udp            UDP rather than TCP\n");
	printf("  --external PORT  ask for this external port; 0 lets the gateway choose\n");
	printf("  --lease SECONDS  lease duration, 0 for permanent (default 7200)\n");
	printf("  --timeout MS     how long to wait (default 15000)\n");
	printf("  --url LOCATION   skip the search and use this description URL\n");
	printf("  --description S  what to call the mapping on the gateway\n");
}

/** Drives the asynchronous path, for 'list'. */
class Listing
	: public GatewayEventHandler
	, public PortMapperEventHandler
{
	public:
		bool finished = false;
		bool ok = false;

		void start(const Samurai::IO::Net::URL& location)
		{
			gateway = location.isValid()
				? Gateway::describe(this, location)
				: Gateway::discover(this);

			if (!gateway) finished = true;
		}

	protected:
		void EventGatewayFound(Gateway* found) override
		{
			mapper = PortMapper::create(this, found);
			if (!mapper) { finished = true; return; }
			mapper->listMappings();
		}

		void EventGatewayError(Gateway*, Error error) override
		{
			printf("No gateway: %s\n", toString(error));
			finished = true;
		}

		void EventMappingList(PortMapper*, std::span<const Mapping> list) override
		{
			if (list.empty()) printf("(no mappings)\n");

			for (const Mapping& mapping : list)
			{
				printf("%-3s %5u -> %s:%-5u  lease %-6lld  %s%s\n",
				       toString(mapping.protocol),
				       mapping.externalPort,
				       mapping.internalClient.toString().c_str(),
				       mapping.internalPort,
				       (long long) mapping.lease.count(),
				       mapping.description.c_str(),
				       mapping.enabled ? "" : " (disabled)");
			}

			ok = true;
			finished = true;
		}

		void EventMappingAdded(PortMapper*, const Mapping&) override { }
		void EventMappingRemoved(PortMapper*, const MappingKey&) override { }

		void EventMapperError(PortMapper*, Error error, DeviceError device,
		                      uint16_t code) override
		{
			if (error == Error::Device)
				printf("The gateway refused: %s (%u)\n", toString(device), code);
			else
				printf("Failed: %s\n", toString(error));

			finished = true;
		}

	private:
		std::unique_ptr<Gateway> gateway;
		std::unique_ptr<PortMapper> mapper;
};

void report(const MapResult& result)
{
	if (result.ok()) return;

	if (result.error == Error::Device)
		printf("The gateway refused: %s (%u)\n",
		       toString(result.deviceError), result.code);
	else
		printf("Failed: %s\n", toString(result.error));
}

int run_listing(const Samurai::IO::Net::URL& location, int timeout_ms)
{
	Listing listing;
	listing.start(location);

	Samurai::IO::Net::SocketMonitor* monitor =
		Samurai::IO::Net::SocketMonitor::getInstance();
	Samurai::TimerManager* timers = Samurai::TimerManager::getInstance();

	const Samurai::Timer::clock::time_point deadline =
		Samurai::Timer::clock::now() + std::chrono::milliseconds(timeout_ms);

	while (!listing.finished && Samurai::Timer::clock::now() < deadline)
	{
		monitor->wait(50);
		timers->process();
	}

	if (!listing.finished) { printf("Gave up waiting.\n"); return 1; }
	return listing.ok ? 0 : 1;
}

}

int main(int argc, char** argv)
{
	Protocol protocol = Protocol::TCP;
	uint16_t external = 0;
	bool external_given = false;
	long lease = 7200;
	int timeout_ms = 15000;
	const char* description = "samurai";
	Samurai::IO::Net::URL location("");

	int n = 1;
	for (; n < argc && argv[n][0] == '-'; n++)
	{
		if (!strcmp(argv[n], "--udp"))
		{
			protocol = Protocol::UDP;
		}
		else if (!strcmp(argv[n], "--external") && n + 1 < argc)
		{
			external = (uint16_t) atoi(argv[++n]);
			external_given = true;
		}
		else if (!strcmp(argv[n], "--lease") && n + 1 < argc)
		{
			lease = atol(argv[++n]);
		}
		else if (!strcmp(argv[n], "--timeout") && n + 1 < argc)
		{
			timeout_ms = atoi(argv[++n]);
			if (timeout_ms < 1000) timeout_ms = 1000;
		}
		else if (!strcmp(argv[n], "--description") && n + 1 < argc)
		{
			description = argv[++n];
		}
		else if (!strcmp(argv[n], "--url") && n + 1 < argc)
		{
			location = Samurai::IO::Net::URL(argv[++n]);
			if (!location.isValid()) { printf("Not a usable URL.\n"); return 1; }
		}
		else
		{
			usage(argv[0]);
			return strcmp(argv[n], "--help") ? 1 : 0;
		}
	}

	if (n >= argc) { usage(argv[0]); return 1; }

	const char* command = argv[n++];

	/* A description URL stands in for the search, for a gateway that does not
	   answer one or a caller that keeps the URL in its configuration. */
	if (location.isValid()) setBlockingGateway(location);

	if (!strcmp(command, "external"))
	{
		const MapResult result =
			getExternalAddressBlocking(std::chrono::milliseconds(timeout_ms));
		report(result);
		if (!result.ok()) return 1;

		printf("%s\n", result.externalAddress.toString().c_str());
		return 0;
	}

	if (!strcmp(command, "list")) return run_listing(location, timeout_ms);

	if (n >= argc) { usage(argv[0]); return 1; }

	const int port = atoi(argv[n]);
	if (port <= 0 || port > 65535) { printf("Not a usable port.\n"); return 1; }

	if (!strcmp(command, "add"))
	{
		const MapResult result = mapPortBlocking(
			protocol, (uint16_t) port,
			external_given ? external : (uint16_t) port,
			description, std::chrono::seconds(lease),
			std::chrono::milliseconds(timeout_ms));

		report(result);
		if (!result.ok()) return 1;

		printf("Mapped %s %u -> %s:%d\n", toString(protocol), result.externalPort,
		       result.internalClient.toString().c_str(), port);
		return 0;
	}

	if (!strcmp(command, "del"))
	{
		const MapResult result = unmapPortBlocking(protocol, (uint16_t) port,
			std::chrono::milliseconds(timeout_ms));

		report(result);
		if (!result.ok()) return 1;

		printf("Removed the %s mapping for port %d\n", toString(protocol), port);
		return 0;
	}

	usage(argv[0]);
	return 1;
}
