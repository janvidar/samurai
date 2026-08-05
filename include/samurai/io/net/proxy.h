/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_PROXY_H
#define HAVE_SAMURAI_PROXY_H

#include <samurai/samurai.h>

#include <string>

namespace Samurai {
namespace IO {
namespace Net {

class URL;

/**
 * Where a Socket goes to reach its peer, when it does not go there directly.
 *
 * Only SOCKS5 (RFC 1928), and only its CONNECT command. BIND and UDP ASSOCIATE
 * are not implemented and there is no way to ask for them: a stream is the only
 * thing that can be arranged through a proxy here, so nothing can leave as a
 * datagram by way of one. That matters for Tor, which carries no datagrams at
 * all - a UDP association would have to be made with something else, and would
 * report the host's own address to whatever it reached.
 *
 * A proxied Socket never resolves its peer's name. The name travels to the
 * proxy inside the CONNECT request and is resolved on the far side, because a
 * name handed to the system resolver is a plaintext question to whatever
 * resolv.conf names, and that question names the peer whether or not the
 * connection itself is tunnelled. There is therefore no equivalent of the
 * distinction curl draws between socks5:// and socks5h://: this is always the
 * latter.
 *
 * Settings are taken from the process default when a Socket is constructed, so
 * moving the default never disturbs a connection already under way. See
 * Socket::setProxy() for the per-connection override.
 *
 * What is NOT proxied, and cannot be: DatagramSocket, MulticastSocket, the UPnP
 * gateway (which is SSDP over multicast and then HTTP to an address on the local
 * network), ServerSocket, and the local resolver unless getTorExtensions() is
 * on. A program that needs every packet to go through the proxy has to leave
 * those alone.
 */
class ProxySettings {
	public:
		enum class Kind { None, Socks5 };

		/** No proxy. */
		ProxySettings() = default;

		ProxySettings(const std::string& host, uint16_t port);

		/** Tor's default SocksPort, 127.0.0.1:9050. */
		static ProxySettings tor();

		/**
		 * socks5://[user[:password]@]host[:port], with socks5h:// accepted as
		 * the same thing and 1080 assumed when no port is given.
		 *
		 * @return a ProxySettings whose getKind() is Kind::None when the URL is
		 *         not one of those schemes, names no host, or carries a
		 *         credential too long for RFC 1929.
		 */
		static ProxySettings fromURL(const URL& url);

		/** The same from text, which is what a command line hands over. */
		static ProxySettings fromString(const std::string& spec);

		Kind getKind() const { return kind; }
		bool isEnabled() const { return kind != Kind::None; }

		const std::string& getHost() const { return host; }
		uint16_t getPort() const { return port; }

		/**
		 * RFC 1929 username and password. Neither may exceed 255 bytes, which
		 * is what the length octet in front of each can express; a longer one
		 * is refused rather than truncated, since a truncated password is a
		 * different password.
		 *
		 * Tor reads a distinct pair as a stream isolation key when
		 * IsolateSOCKSAuth is on, which is how two connections are kept off one
		 * circuit. That is a use for this even where the proxy demands nothing.
		 *
		 * @return false if either is too long, in which case nothing is set.
		 */
		bool setCredentials(const std::string& user, const std::string& password);

		bool hasCredentials() const { return !username.empty() || !password.empty(); }
		const std::string& getUsername() const { return username; }
		const std::string& getPassword() const { return password; }

		/**
		 * Whether to use Tor's RESOLVE and RESOLVE_PTR commands, which is what
		 * lets DNS::Resolver answer a lookup without asking the system resolver.
		 *
		 * Off, because a proxy that is not Tor answers an unknown command with
		 * command-not-supported: asking one costs a round trip and then fails a
		 * lookup that would have succeeded. Turning it on redirects every
		 * lookup in the process, including the ones a caller makes by hand
		 * through InetAddress::lookup().
		 */
		void setTorExtensions(bool toggle) { tor_extensions = toggle; }
		bool getTorExtensions() const { return tor_extensions; }

		/**
		 * Whether a peer on the local network may be asked for at all.
		 *
		 * False. A request for 10.0.0.1, 127.0.0.1, 169.254.x or fe80:: through
		 * Tor cannot succeed - no exit relay routes those - and asking is worse
		 * than useless: on a proxy that is not Tor, a name or address the
		 * program did not choose for itself turns the proxy into a way to reach
		 * machines the program can see and its user cannot. So the request is
		 * refused here, before it is sent, rather than left to the far side.
		 *
		 * Turn it on for a SOCKS5 proxy that exists precisely to reach a network
		 * segment - a bastion host on the far side of one. It has no effect on a
		 * name: only the proxy learns what a name resolves to, which is the
		 * point, so a name that resolves to a private address on the far side is
		 * the far side's business.
		 */
		void setAllowPrivateTargets(bool toggle) { allow_private = toggle; }
		bool getAllowPrivateTargets() const { return allow_private; }

		/** host:port, or "none". */
		std::string toString() const;

		bool operator==(const ProxySettings& other) const;
		bool operator!=(const ProxySettings& other) const { return !(*this == other); }

		/**
		 * The settings a Socket starts with. Changing the default affects
		 * sockets constructed after the call, never one already under way.
		 */
		static const ProxySettings& getDefault();
		static void setDefault(const ProxySettings& proxy);
		static void clearDefault();

	private:
		Kind kind = Kind::None;
		std::string host;
		uint16_t port = 0;
		std::string username;
		std::string password;
		bool tor_extensions = false;
		bool allow_private = false;
};

}
}
}

#endif // HAVE_SAMURAI_PROXY_H
