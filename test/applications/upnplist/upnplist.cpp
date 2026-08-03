/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/upnp/gateway.h>
#include <samurai/io/net/upnp/portmapper.h>
#include <samurai/timer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <memory>

/*
 * List what answers a UPnP gateway search, and what the gateway that answered
 * says about itself.
 *
 * This used to be the SSDP implementation as well as the tool: a packet builder,
 * a worker per interface and a loop that ran forever. All of that is now in the
 * library, so what is left is the part that prints.
 */

using namespace Samurai::IO::Net::UPnP;

namespace {

class Lister
	: public GatewayEventHandler
	, public PortMapperEventHandler
{
	public:
		bool finished = false;

		void run(const Samurai::IO::Net::URL& location, int seconds)
		{
			Gateway::Options options;
			options.searchTimeout = std::chrono::seconds(seconds);

			gateway = location.isValid()
				? Gateway::describe(this, location, options)
				: Gateway::discover(this, options);

			if (!gateway)
			{
				printf("Unable to search: no multicast-capable interface.\n");
				finished = true;
				return;
			}

			if (!location.isValid())
				printf("Searching for an internet gateway...\n\n");
		}

	protected:
		void EventGatewayCandidate(Gateway*, const Candidate& candidate) override
		{
			printf("Reply from %s\n", candidate.source.toString().c_str());
			printf("  LOCATION: %s\n", candidate.location.toString().c_str());
			if (!candidate.st.empty())     printf("  ST:       %s\n", candidate.st.c_str());
			if (!candidate.usn.empty())    printf("  USN:      %s\n", candidate.usn.c_str());
			if (!candidate.server.empty()) printf("  SERVER:   %s\n", candidate.server.c_str());
			printf("\n");
		}

		void EventGatewayFound(Gateway* found) override
		{
			printf("Gateway: %s\n", found->getFriendlyName().c_str());
			printf("  Version:      IGD %d\n", found->getDeviceVersion());

			const GatewayInfo info = found->info();
			printf("  Service:      %s\n", info.serviceType.c_str());
			printf("  Control URL:  %s\n", info.controlURL.toString().c_str());
			printf("  Description:  %s\n", info.descriptionURL.toString().c_str());
			printf("  Reached from: %s\n",
			       found->getLocalAddress()
			           ? found->getLocalAddress()->toString().c_str() : "unknown");

			if (found->hasService(ServiceKind::WanIpv6FirewallControl))
				printf("  Also offers IPv6 firewall control.\n");

			printf("\n");

			mapper = PortMapper::create(this, found);
			if (!mapper) { finished = true; return; }

			mapper->getExternalAddress();
		}

		void EventGatewayError(Gateway*, Error error) override
		{
			printf("No gateway: %s\n", toString(error));
			finished = true;
		}

		void EventExternalAddress(PortMapper*,
			const Samurai::IO::Net::InetAddress& address) override
		{
			printf("External address: %s\n\n", address.toString().c_str());
			printf("Port mappings:\n");
			mapper->listMappings();
		}

		void EventMappingList(PortMapper*, std::span<const Mapping> list) override
		{
			if (list.empty()) printf("  (none)\n");

			for (const Mapping& mapping : list)
			{
				printf("  %-5s %5u -> %s:%u  %s%s\n",
				       toString(mapping.protocol),
				       mapping.externalPort,
				       mapping.internalClient.toString().c_str(),
				       mapping.internalPort,
				       mapping.description.c_str(),
				       mapping.enabled ? "" : " (disabled)");
			}

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

void usage(const char* argv0)
{
	printf("Usage: %s [--url LOCATION] [--timeout SECONDS]\n", argv0);
	printf("Lists the UPnP internet gateways on the local network.\n\n");
	printf("  --url LOCATION    skip the search and read this description\n");
	printf("  --timeout SECONDS how long to search for (default 3)\n");
}

}

int main(int argc, char** argv)
{
	Samurai::IO::Net::URL location("");
	int seconds = 3;

	for (int n = 1; n < argc; n++)
	{
		if (!strcmp(argv[n], "--url") && n + 1 < argc)
		{
			location = Samurai::IO::Net::URL(argv[++n]);
			if (!location.isValid()) { printf("Not a usable URL.\n"); return 1; }
		}
		else if (!strcmp(argv[n], "--timeout") && n + 1 < argc)
		{
			seconds = atoi(argv[++n]);
			if (seconds < 1) seconds = 1;
		}
		else
		{
			usage(argv[0]);
			return strcmp(argv[n], "--help") ? 1 : 0;
		}
	}

	Lister lister;
	lister.run(location, seconds);

	Samurai::IO::Net::SocketMonitor* monitor =
		Samurai::IO::Net::SocketMonitor::getInstance();
	Samurai::TimerManager* timers = Samurai::TimerManager::getInstance();

	/* A bound wait, so a gateway that stops answering mid-exchange does not
	   leave this running for ever - which is what the old loop did. */
	const Samurai::Timer::clock::time_point deadline =
		Samurai::Timer::clock::now() + std::chrono::seconds(seconds + 20);

	while (!lister.finished && Samurai::Timer::clock::now() < deadline)
	{
		monitor->wait(50);
		timers->process();
	}

	if (!lister.finished) printf("Gave up waiting.\n");
	return lister.finished ? 0 : 1;
}
