/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UPNP_SSDP_H
#define HAVE_SAMURAI_UPNP_SSDP_H

#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/multicast.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/socketevent.h>
#include <samurai/io/net/url.h>
#include <samurai/timer.h>

#include <chrono>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Samurai {
namespace IO {
namespace Net {
namespace UPnP {
namespace Ssdp {

/**
 * The search targets a gateway is looked for under, in the order they are
 * tried.
 *
 * Not ssdp:all, which is what the earlier prototype asked for: that draws a
 * reply from every printer, television and media server on the network, each of
 * which would then have to be fetched and parsed to find out it is not a
 * gateway. The device types are the right question; the two service types are a
 * safety net for devices whose advertised deviceType is wrong, which is common.
 */
std::span<const char* const> searchTargets();

/** The address a search goes to. */
struct Group
{
	const char* address;
	uint16_t port;
	Samurai::IO::Net::InetAddress::Version version;
};

/**
 * The groups a search is sent to.
 *
 * 239.255.255.250 for Version::IPv4, and ff02::c and ff05::c for Version::IPv6.
 * The organisation and global scoped groups are deliberately absent: a gateway
 * is one hop away by definition, so searching them costs datagrams and finds
 * nothing.
 */
std::span<const Group> groups();

/** Build an M-SEARCH request body. */
std::string buildSearch(std::string_view host, std::string_view target, uint8_t mx);

/** What a search reply said. */
struct Reply
{
	Samurai::IO::Net::URL location{""};
	std::string st;
	std::string usn;
	std::string server;
	Samurai::IO::Net::InetAddress source;
	/** From CACHE-CONTROL: max-age, or 0 when it said nothing. */
	unsigned maxAge = 0;

	/** What identifies the device, for discarding a repeat of it. */
	std::string key() const;
};

/**
 * Read a search reply.
 *
 * Parsed through the HTTP response parser rather than by a header reader written
 * here: a reply is an HTTP-shaped message in one datagram, and having two
 * parsers would mean two sets of quirks to keep straight.
 *
 * A NOTIFY is refused: it is an announcement, not an answer to a search.
 *
 * @param source where the datagram came from.
 * @param requireMatchingHost when set, the LOCATION host has to be 'source'.
 *        Without that check any host on the network can name a third party.
 */
bool parseReply(std::string_view datagram, const Samurai::IO::Net::InetAddress& source,
                bool requireMatchingHost, Reply& out);

/** Told about each new reply, and about the search running out of time. */
class SearchEventHandler
{
	public:
		virtual ~SearchEventHandler() = default;

		/** A reply that had not been seen before. */
		virtual void EventSsdpReply(const Reply& reply) = 0;

		/** The search window closed. */
		virtual void EventSsdpFinished() = 0;
};

/**
 * An M-SEARCH across every multicast-capable interface.
 *
 * One socket per interface per family, bound to an ephemeral port and joining
 * nothing: a search reply is unicast back to the port the request came from, so
 * a group membership buys nothing here. Binding 1900 instead - which the earlier
 * prototype did - collides with any other SSDP client on the host for no gain.
 *
 * SSDP is unreliable by design, so each target is sent more than once and the
 * whole thing is bounded by a deadline rather than run until something answers.
 */
class Search final
	: public Samurai::IO::Net::DatagramEventHandler
	, public Samurai::TimerListener
{
	public:
		struct Options
		{
			std::chrono::milliseconds timeout{ 3000 };
			std::chrono::milliseconds interval{ 500 };
			uint8_t mx = 2;
			uint8_t rounds = 2;
			uint8_t hops = 2;
			bool searchIPv4 = true;
			bool searchIPv6 = true;
			bool requireLocationMatchesSource = true;
		};

		/**
		 * Start searching.
		 *
		 * @return null when not one interface could be searched, which is the
		 *         only outright failure - a host where some interfaces refuse
		 *         still searches the rest.
		 */
		static std::unique_ptr<Search> start(SearchEventHandler* eh, const Options& options);

		~Search() override;

		Search(const Search&) = delete;
		Search& operator=(const Search&) = delete;

		/** Replies so far, deduplicated, in the order they arrived. */
		const std::vector<Reply>& getReplies() const { return replies; }

		bool isFinished() const { return finished; }

	private:
		explicit Search(SearchEventHandler* eh, const Options& options);

		bool internal_open();
		void internal_sendRound();

		void EventGotDatagram(Samurai::IO::Net::DatagramSocket*,
		                      Samurai::IO::Net::DatagramPacket*) override;
		void EventDatagramError(const Samurai::IO::Net::DatagramSocket*,
		                        const char*) override;

		void EventTimeout(Samurai::Timer* timer) override;

		/** One socket, and the group it sends to. */
		struct Leg
		{
			std::shared_ptr<Samurai::IO::Net::MulticastSocket> socket;
			Samurai::IO::Net::InetSocketAddress destination;
			std::string host;
		};

		SearchEventHandler* eventHandler;
		Options options;

		std::vector<Leg> legs;
		std::vector<Reply> replies;
		std::set<std::string> seen;

		std::unique_ptr<Samurai::Timer> ticker;
		Samurai::Timer::clock::time_point deadline{};
		uint8_t round = 0;
		bool finished = false;
};

}
}
}
}
}

#endif // HAVE_SAMURAI_UPNP_SSDP_H
