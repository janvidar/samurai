/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include "ssdp.h"

#include <samurai/io/buffer.h>
#include <samurai/io/net/datagram.h>
#include <samurai/io/net/http/response.h>
#include <samurai/io/net/interface.h>
#include <samurai/os.h>
#include <samurai/stdc.h>
#include <samurai/util/string.h>

namespace {

/*
 * The device types first, being the right question, then the two service types
 * as a safety net: a gateway advertising the wrong deviceType but the right
 * service is common enough that asking only the first question misses some.
 * WANIPConnection:2 is not listed because InternetGatewayDevice:2 covers it.
 */
constexpr const char* const SEARCH_TARGETS[] = {
	"urn:schemas-upnp-org:device:InternetGatewayDevice:2",
	"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
	"urn:schemas-upnp-org:service:WANIPConnection:1",
	"urn:schemas-upnp-org:service:WANPPPConnection:1",
};

using Version = Samurai::IO::Net::InetAddress::Version;

constexpr Samurai::IO::Net::UPnP::Ssdp::Group GROUPS[] = {
	{ "239.255.255.250", 1900, Version::IPv4 },
	{ "ff02::c",         1900, Version::IPv6 },
	{ "ff05::c",         1900, Version::IPv6 },
};

}


std::span<const char* const> Samurai::IO::Net::UPnP::Ssdp::searchTargets()
{
	return std::span<const char* const>(SEARCH_TARGETS, std::size(SEARCH_TARGETS));
}


std::span<const Samurai::IO::Net::UPnP::Ssdp::Group>
Samurai::IO::Net::UPnP::Ssdp::groups()
{
	return std::span<const Group>(GROUPS, std::size(GROUPS));
}


std::string Samurai::IO::Net::UPnP::Ssdp::buildSearch(std::string_view host,
                                                      std::string_view target,
                                                      uint8_t mx)
{
	std::string out;
	out += "M-SEARCH * HTTP/1.1\r\n";
	out += "HOST: ";
	out.append(host);
	out += "\r\n";
	/* The quotes are part of the value, and a gateway that checks will refuse
	   the search without them. */
	out += "MAN: \"ssdp:discover\"\r\n";
	out += "ST: ";
	out.append(target);
	out += "\r\n";
	out += "MX: ";
	out += std::to_string((unsigned) mx);
	out += "\r\n";
	out += "USER-AGENT: ";
	out += Samurai::OS::getName();
	out += "/";
	out += Samurai::OS::getVersion();
	out += " UPnP/1.1 Samurai/1.0\r\n";
	out += "\r\n";
	return out;
}


/*
 * The location, not the USN.
 *
 * What this is for is not fetching and parsing the same description twice, and
 * the location is exactly the thing that would be fetched - so two replies
 * naming one description are one candidate however they identify themselves.
 *
 * The USN cannot do that job. A device answers once per search target it
 * matches, and the USN carries the target with it, so one gateway replying to
 * all four targets produces four distinct USNs. MiniUPnPd goes further and uses
 * one UUID for its device announcements and another for its service ones, so
 * even the UUID inside the USN does not collapse them.
 */
std::string Samurai::IO::Net::UPnP::Ssdp::Reply::key() const
{
	return location.toString();
}


bool Samurai::IO::Net::UPnP::Ssdp::parseReply(std::string_view datagram,
                                              const Samurai::IO::Net::InetAddress& source,
                                              bool requireMatchingHost,
                                              Reply& out)
{
	out = Reply();

	/* A NOTIFY is an announcement, not an answer, and its start line is a
	   request line the response parser would rightly refuse. Rejected here so
	   the refusal is deliberate rather than incidental. */
	if (Samurai::Util::istarts_with(datagram, "NOTIFY")) return false;

	Samurai::IO::Buffer input;
	input.append(datagram.data(), datagram.size());

	Samurai::IO::Net::HTTP::ResponseParser parser;
	/* A search reply is a head and nothing else, so whatever framing it claims,
	   there is no body to wait for. */
	parser.expectNoBody();

	if (parser.parse(input) != Samurai::IO::Net::HTTP::ResponseParser::Result::Complete)
		return false;

	const Samurai::IO::Net::HTTP::Response& response = parser.getResponse();
	if (!response.isSuccess()) return false;

	const Samurai::IO::Net::HTTP::Headers& headers = response.getHeaders();

	const std::optional<std::string_view> location = headers.get("LOCATION");
	if (!location.has_value()) return false;

	const URL url(std::string(Samurai::Util::trim(*location)));
	if (!url.isValid() || url.getHostname().empty()) return false;
	if (!Samurai::Util::iequals(url.getScheme(), "http")) return false;

	/*
	 * Without this, any host on the local network can answer a search with a
	 * location naming somewhere else, and everything downstream - the fetch, the
	 * control URL check - is then anchored to an address the device chose.
	 */
	if (requireMatchingHost)
	{
		if (!Samurai::Util::iequals(url.getHostname(), source.toString())) return false;
	}

	const std::optional<std::string_view> usn = headers.get("USN");
	if (!usn.has_value()) return false;

	out.location = url;
	out.usn = std::string(Samurai::Util::trim(*usn));
	out.source = source;

	if (const std::optional<std::string_view> st = headers.get("ST"))
		out.st = std::string(Samurai::Util::trim(*st));

	if (const std::optional<std::string_view> server = headers.get("SERVER"))
		out.server = std::string(Samurai::Util::trim(*server));

	/* CACHE-CONTROL: max-age=1800, which says how long the answer is good for. */
	if (const std::optional<std::string_view> cache = headers.get("CACHE-CONTROL"))
	{
		for (const std::string_view field : Samurai::Util::split(*cache, ','))
		{
			std::string_view name;
			std::string_view value;
			if (!Samurai::Util::split_once(field, '=', name, value)) continue;
			if (!Samurai::Util::iequals(Samurai::Util::trim(name), "max-age")) continue;

			const uint64_t age =
				Samurai::Util::Convert::to_uint64(std::string(Samurai::Util::trim(value)));
			if (age && age < 0xffffffffu) out.maxAge = (unsigned) age;
			break;
		}
	}

	return true;
}


std::unique_ptr<Samurai::IO::Net::UPnP::Ssdp::Search>
Samurai::IO::Net::UPnP::Ssdp::Search::start(SearchEventHandler* eh,
                                            const Options& options)
{
	std::unique_ptr<Search> self(new Search(eh, options));
	if (!self->internal_open()) return nullptr;

	self->deadline = Samurai::Timer::clock::now() + options.timeout;
	self->internal_sendRound();

	/* One repeating timer drives both the retransmission rounds and the
	   deadline. The prototype's loop enforced neither. */
	self->ticker = std::make_unique<Samurai::Timer>(self.get(), options.interval, false);
	return self;
}


Samurai::IO::Net::UPnP::Ssdp::Search::Search(SearchEventHandler* eh,
                                             const Options& opts)
	: eventHandler(eh), options(opts)
{
}


Samurai::IO::Net::UPnP::Ssdp::Search::~Search()
{
	ticker.reset();

	/* Each socket names this as its handler, so they have to stop being
	   monitored before it goes away. */
	for (Leg& leg : legs)
	{
		if (!leg.socket) continue;
		leg.socket->setEventHandler(nullptr);
		leg.socket->disableMonitor();
	}
	legs.clear();
}


/*
 * One socket per interface per group, bound to an ephemeral port and joining
 * nothing: a reply comes back unicast to the port the search left from.
 *
 * Loopback is skipped - no gateway lives there - and an interface that cannot be
 * set up is passed over rather than failing the whole search, because a host
 * commonly has several of which only one is real.
 */
bool Samurai::IO::Net::UPnP::Ssdp::Search::internal_open()
{
	std::vector<std::unique_ptr<Samurai::IO::Net::NetworkInterface>> interfaces;
	if (!Samurai::IO::Net::NetworkInterface::getInterfaces(interfaces)) return false;

	for (const auto& iface : interfaces)
	{
		if (!iface->isEnabled() || !iface->isMulticast() || iface->isLoopback())
			continue;

		for (const Group& group : groups())
		{
			const bool is_ipv6 =
				group.version == Samurai::IO::Net::InetAddress::Version::IPv6;
			if (is_ipv6 && !options.searchIPv6) continue;
			if (!is_ipv6 && !options.searchIPv4) continue;

			const Samurai::IO::Net::InetAddress bind_address(is_ipv6 ? "::" : "0.0.0.0");
			std::shared_ptr<Samurai::IO::Net::MulticastSocket> socket =
				Samurai::IO::Net::MulticastSocket::create(this, bind_address, (uint16_t) 0);

			if (!socket || socket->getFD() == INVALID_SOCKET) continue;
			if (!socket->listen()) continue;

			/* Without this the search leaves by the default route, which on a
			   host with a VPN or a container bridge is very often not the
			   interface the gateway is on. */
			if (!socket->setInterface(*iface)) continue;

			socket->setMulticastTimeToLive(options.hops);
			/* Nothing here wants to hear its own search back. */
			socket->setLoopbackMode(false);

			const Samurai::IO::Net::InetAddress destination(group.address);
			if (!destination.isValid() || !destination.isMulticast()) continue;

			Leg leg;
			leg.socket = socket;
			leg.destination = Samurai::IO::Net::InetSocketAddress(destination, group.port);

			/* The HOST header names the group, bracketed for IPv6. */
			if (is_ipv6)
			{
				leg.host = std::string("[") + group.address + "]:"
					+ std::to_string(group.port);
			}
			else
			{
				leg.host = std::string(group.address) + ":" + std::to_string(group.port);
			}

			legs.push_back(std::move(leg));
		}
	}

	return !legs.empty();
}


void Samurai::IO::Net::UPnP::Ssdp::Search::internal_sendRound()
{
	for (Leg& leg : legs)
	{
		for (const char* target : searchTargets())
		{
			const std::string request = buildSearch(leg.host, target, options.mx);

			Samurai::IO::Net::DatagramPacket packet;
			packet.setData((const uint8_t*) request.data(), request.size());
			packet.setAddress(&leg.destination);

			/* A refusal on one interface says nothing about the others, so it is
			   passed over rather than reported. */
			leg.socket->send(&packet);
		}
	}

	round++;
}


void Samurai::IO::Net::UPnP::Ssdp::Search::EventGotDatagram(
	Samurai::IO::Net::DatagramSocket*, Samurai::IO::Net::DatagramPacket* packet)
{
	if (!packet || finished) return;

	Samurai::IO::Buffer* buffer = packet->getBuffer();
	if (!buffer || !buffer->size()) return;

	Samurai::IO::Net::SocketAddress* from = packet->getAddress();
	Samurai::IO::Net::InetSocketAddress* inet =
		dynamic_cast<Samurai::IO::Net::InetSocketAddress*>(from);
	if (!inet || !inet->getAddress()) return;

	const std::string datagram = buffer->copyRange(0, buffer->size());

	Reply reply;
	if (!parseReply(datagram, *inet->getAddress(),
	                options.requireLocationMatchesSource, reply)) return;

	/* Keyed on the USN, so a device answering on several interfaces or matching
	   several search targets is one candidate rather than many. */
	const std::string key = reply.key();
	if (!seen.insert(key).second) return;

	replies.push_back(reply);
	if (eventHandler) eventHandler->EventSsdpReply(reply);
}


void Samurai::IO::Net::UPnP::Ssdp::Search::EventDatagramError(
	const Samurai::IO::Net::DatagramSocket*, const char*)
{
	/* One interface failing says nothing about the others. */
}


/*
 * NOTE: the timer is released before anything is reported, because the handler
 * may destroy this search - and a timer callback, unlike a monitor dispatch,
 * holds nothing that would keep it alive.
 */
void Samurai::IO::Net::UPnP::Ssdp::Search::EventTimeout(Samurai::Timer*)
{
	if (finished) return;

	if (Samurai::Timer::clock::now() >= deadline)
	{
		finished = true;
		ticker.reset();
		if (eventHandler) eventHandler->EventSsdpFinished();
		return;
	}

	/* SSDP is unreliable by design, so the search is repeated - twice by
	   default, which is what the specification advises and as far as being a
	   good neighbour goes. */
	if (round < options.rounds) internal_sendRound();
}
