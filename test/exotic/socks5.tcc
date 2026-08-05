/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/dns/resolver.h>
#include <samurai/io/net/proxy.h>
#include <samurai/io/net/socks5.h>
#include <samurai/io/net/socket.h>
#include <samurai/io/net/serversocket.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/url.h>
#include <samurai/io/net/tlsfactory.h>

#include "testkeys.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

/*
 * SOCKS5, at two levels.
 *
 * The codec cases drive Socks5Handshake with no socket at all, which is the only
 * way to say anything about a reply arriving one byte at a time, or about a
 * request that must never be sent. The socket cases run a real Socket through
 * the real event loop against a stub proxy on the loopback, which is the only
 * way to say anything about the order events are delivered in.
 *
 * The stub speaks the server half of RFC 1928 by hand rather than through this
 * library, so the two halves are not the same code checked against itself.
 *
 * ProxySettings::setDefault() is process-wide state shared with every other
 * suite in this binary, so every case that touches it restores it - see
 * ProxyDefaultGuard.
 */

namespace {

using Samurai::IO::Net::InetAddress;
using Samurai::IO::Net::InetSocketAddress;
using Samurai::IO::Net::ProxySettings;
using Samurai::IO::Net::Socket;
using Samurai::IO::Net::SocketError;
using Samurai::IO::Net::SocketMonitor;
using Samurai::IO::Net::Socks5Handshake;
using Status = Samurai::IO::Net::Socks5Handshake::Status;
using Command = Samurai::IO::Net::Socks5Handshake::Command;

/* Long enough that a loaded machine does not fail; only reached when broken. */
static const int SOCKS_TEST_TIMEOUT_MS = 5000;

/* Puts the process-wide proxy default back, whatever a case did to it. */
struct ProxyDefaultGuard
{
	ProxySettings saved;
	ProxyDefaultGuard() : saved(ProxySettings::getDefault()) { }
	~ProxyDefaultGuard() { ProxySettings::setDefault(saved); }

	ProxyDefaultGuard(const ProxyDefaultGuard&) = delete;
	ProxyDefaultGuard& operator=(const ProxyDefaultGuard&) = delete;
};

template<typename Fn>
static bool pump(Fn done, int timeout_ms = SOCKS_TEST_TIMEOUT_MS)
{
	SocketMonitor* monitor = SocketMonitor::getInstance();
	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::milliseconds(timeout_ms);

	while (!done())
	{
		if (std::chrono::steady_clock::now() > deadline) return done();
		monitor->wait(10);
	}
	return true;
}

/* ------------------------------------------------------------------------- */
/* Driving the codec with no socket under it                                  */
/* ------------------------------------------------------------------------- */

/*
 * Collects everything the handshake wanted to send, in order, and feeds it
 * whatever a case decides the proxy said.
 */
struct Codec
{
	std::unique_ptr<Socks5Handshake> handshake;
	/* Retired output, so a case can assert on the bytes that went out. */
	std::string sent;
	/* Retire output one byte per call, which is what a full socket buffer
	   forces. What ends up in 'sent' must not depend on it. */
	bool one_byte_writes = false;

	Codec(const ProxySettings& proxy, Command command,
	      const std::string& target, uint16_t port, bool byte_writes = false)
		: one_byte_writes(byte_writes)
	{
		handshake = std::make_unique<Socks5Handshake>(proxy, command, target, port);
		drain();
	}

	/** Accept everything queued, as a socket with room would. */
	void drain()
	{
		while (handshake->getStatus() == Status::NeedWrite)
		{
			const std::string_view out = handshake->outgoing();
			if (out.empty()) break;

			const size_t take = one_byte_writes ? 1 : out.size();
			sent.append(out.substr(0, take));
			handshake->consumeOutgoing(take);
		}
	}

	Status feed(const std::string& bytes, size_t* consumed_out = nullptr)
	{
		size_t consumed = 0;
		handshake->feed(bytes.data(), bytes.size(), consumed);
		if (consumed_out) *consumed_out = consumed;
		drain();
		return handshake->getStatus();
	}

	/** Feed in single bytes, never more than wanted() asks for. */
	Status feedByteAtATime(const std::string& bytes)
	{
		for (char c : bytes)
		{
			if (handshake->getStatus() != Status::NeedRead) break;

			size_t consumed = 0;
			handshake->feed(&c, 1, consumed);
			drain();
		}
		return handshake->getStatus();
	}

	Status status() const { return handshake->getStatus(); }

	SocketError error() const
	{
		const char* message = nullptr;
		return handshake->getError(message);
	}

	const char* message() const
	{
		const char* text = nullptr;
		handshake->getError(text);
		return text;
	}
};

static std::string bytes(std::initializer_list<int> values)
{
	std::string out;
	for (int v : values) out += (char) (uint8_t) v;
	return out;
}

/* VER METHOD */
static std::string methodReply(uint8_t method)
{
	return bytes({ 0x05, method });
}

/* VER REP RSV ATYP=IPv4 0.0.0.0 port - what Tor answers a CONNECT with. */
static std::string connectReply(uint8_t reply, uint16_t port = 0)
{
	std::string out = bytes({ 0x05, reply, 0x00, 0x01, 0, 0, 0, 0 });
	out += (char) (uint8_t) (port >> 8);
	out += (char) (uint8_t) (port & 0xFF);
	return out;
}

static ProxySettings testProxy()
{
	return ProxySettings("192.0.2.1", 1080);
}

/*
 * Table data lives out here rather than inside a case.
 *
 * EXO_TEST() takes the whole body as one macro argument, and the preprocessor
 * only protects commas inside parentheses - so a braced initialiser written in a
 * case body is read as a second argument to the macro.
 */

/* Peers the proxy must not be asked for: private, loopback, link-local,
   unspecified, multicast, and the names that can only mean this machine. */
static const char* const socks5_local_peers[] = {
	"10.1.2.3", "172.16.0.1", "192.168.1.1", "127.0.0.1",
	"169.254.1.1", "fc00::1", "fe80::1", "::1", "0.0.0.0",
	"239.1.2.3", "localhost", "printer.local", "host.localhost"
};

/* And ones it must be: "localhost.example.com" is a name that merely starts
   with one of the above. */
static const char* const socks5_public_peers[] = {
	"198.51.100.7", "2001:db8::1", "example.com", "localhost.example.com"
};

struct ReplyExpectation
{
	uint8_t reply;
	SocketError code;
};

static const ReplyExpectation socks5_reply_table[] = {
	{ 0x01, SocketError::SocketUnknown },
	{ 0x02, SocketError::ProxyRefused },
	{ 0x03, SocketError::NetUnreachable },
	{ 0x04, SocketError::HostUnreachable },
	{ 0x05, SocketError::ConnectionRefused },
	{ 0x06, SocketError::ConnectionTimeout },
	{ 0x07, SocketError::ProxyRefused },
	{ 0x08, SocketError::ProxyRefused },
	/* Not one the RFC defines, so still a refusal rather than a success
	   nobody classified. */
	{ 0x42, SocketError::ProxyRefused }
};

/* ------------------------------------------------------------------------- */
/* Codec: the greeting                                                        */
/* ------------------------------------------------------------------------- */

EXO_TEST(socks5_greeting_offers_only_no_authentication,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);

	/* 05 01 00, then the request behind it - which must NOT have gone out
	   yet: the proxy may still demand credentials. */
	return codec.sent == bytes({ 0x05, 0x01, 0x00 });
});

EXO_TEST(socks5_greeting_offers_username_password_when_there_is_one,
{
	ProxySettings proxy = testProxy();
	proxy.setCredentials("user", "secret");

	Codec codec(proxy, Command::Connect, "example.com", 80);
	return codec.sent == bytes({ 0x05, 0x02, 0x00, 0x02 });
});

EXO_TEST(socks5_greeting_never_offers_gssapi,
{
	ProxySettings proxy = testProxy();
	proxy.setCredentials("user", "secret");

	Codec codec(proxy, Command::Connect, "example.com", 80);

	/* 0x01 is GSSAPI, which is not implemented: offering it would invite the
	   proxy to choose an exchange we cannot perform. */
	return codec.sent.find((char) 0x01) == std::string::npos;
});

EXO_TEST(socks5_the_request_does_not_go_out_before_the_method_is_agreed,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);

	/* Only the greeting so far. A request pipelined behind it would be read as
	   part of an authentication exchange the proxy may yet ask for. */
	if (codec.sent.size() != 3) return false;

	codec.feed(methodReply(0x00));
	return codec.sent.size() > 3;
});

/* ------------------------------------------------------------------------- */
/* Codec: how the peer is addressed                                           */
/* ------------------------------------------------------------------------- */

EXO_TEST(socks5_a_name_is_sent_as_a_name_and_never_resolved,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x00));

	const std::string request = codec.sent.substr(3);
	const std::string expected =
		bytes({ 0x05, 0x01, 0x00, 0x03, 11 }) + "example.com" + bytes({ 0x00, 80 });

	return request == expected;
});

EXO_TEST(socks5_an_onion_name_is_sent_as_a_name,
{
	const std::string onion = "expyuzz4wqqyqhjn.onion";

	Codec codec(testProxy(), Command::Connect, onion, 80);
	codec.feed(methodReply(0x00));

	const std::string request = codec.sent.substr(3);
	if (request.size() < 5) return false;

	/* ATYP 3, the length, then the name itself - nothing about .onion needs
	   special handling, it simply is not a literal. */
	return (uint8_t) request[3] == 0x03
		&& (uint8_t) request[4] == onion.size()
		&& request.compare(5, onion.size(), onion) == 0;
});

EXO_TEST(socks5_an_ipv4_literal_is_sent_as_an_address,
{
	Codec codec(testProxy(), Command::Connect, "198.51.100.7", 443);
	codec.feed(methodReply(0x00));

	const std::string request = codec.sent.substr(3);
	const std::string expected =
		bytes({ 0x05, 0x01, 0x00, 0x01, 198, 51, 100, 7, 0x01, 0xBB });

	return request == expected;
});

EXO_TEST(socks5_an_ipv6_literal_is_sent_as_an_address,
{
	Codec codec(testProxy(), Command::Connect, "2001:db8::1", 443);
	codec.feed(methodReply(0x00));

	const std::string request = codec.sent.substr(3);
	if (request.size() != 4 + 16 + 2) return false;

	const std::string expected = bytes({ 0x05, 0x01, 0x00, 0x04,
		0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01,
		0x01, 0xBB });

	return request == expected;
});

EXO_TEST(socks5_the_port_is_sent_in_network_order,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 8080);
	codec.feed(methodReply(0x00));

	const std::string request = codec.sent.substr(3);
	const size_t at = request.size() - 2;

	return (uint8_t) request[at] == 0x1F && (uint8_t) request[at + 1] == 0x90;
});

/* ------------------------------------------------------------------------- */
/* Codec: what is refused before a byte is sent                               */
/* ------------------------------------------------------------------------- */

EXO_TEST(socks5_a_name_too_long_to_encode_produces_no_bytes,
{
	/* The length octet in front of a name tops out at 255. */
	const std::string name(256, 'a');

	Codec codec(testProxy(), Command::Connect, name, 80);
	return codec.status() == Status::Failed
		&& codec.sent.empty()
		&& codec.error() == SocketError::ProxyProtocol;
});

EXO_TEST(socks5_a_name_of_exactly_255_bytes_is_accepted,
{
	const std::string name(255, 'a');

	Codec codec(testProxy(), Command::Connect, name, 80);
	return codec.status() == Status::NeedRead;
});

EXO_TEST(socks5_an_empty_peer_produces_no_bytes,
{
	Codec codec(testProxy(), Command::Connect, "", 80);
	return codec.status() == Status::Failed && codec.sent.empty();
});

EXO_TEST(socks5_a_credential_too_long_to_encode_produces_no_bytes,
{
	/* setCredentials() refuses it, so nothing is set and the handshake would
	   offer no-auth only. Reaching past it proves the codec refuses too. */
	ProxySettings proxy = testProxy();
	if (proxy.setCredentials(std::string(256, 'u'), "p")) return false;
	if (proxy.hasCredentials()) return false;

	return true;
});

EXO_TEST(socks5_a_private_peer_is_refused_before_anything_is_sent,
{
	/* Tor routes none of these, and on a proxy that is not Tor an address the
	   program did not choose becomes a way to reach the proxy's network. */
	for (const char* peer : socks5_local_peers)
	{
		Codec codec(testProxy(), Command::Connect, peer, 80);
		if (codec.status() != Status::Failed) return false;
		if (!codec.sent.empty()) return false;
		if (codec.error() != SocketError::ProxyRefused) return false;
	}

	return true;
});

EXO_TEST(socks5_a_private_peer_is_allowed_when_it_was_asked_for,
{
	ProxySettings proxy = testProxy();
	proxy.setAllowPrivateTargets(true);

	Codec codec(proxy, Command::Connect, "10.1.2.3", 80);
	return codec.status() == Status::NeedRead;
});

EXO_TEST(socks5_a_public_peer_is_not_mistaken_for_a_private_one,
{
	for (const char* peer : socks5_public_peers)
	{
		Codec codec(testProxy(), Command::Connect, peer, 80);
		if (codec.status() != Status::NeedRead) return false;
	}

	return true;
});

EXO_TEST(socks5_a_reverse_lookup_of_a_name_is_refused,
{
	ProxySettings proxy = testProxy();
	proxy.setAllowPrivateTargets(true);

	/* There is nothing to ask about: RESOLVE_PTR takes an address. */
	Codec codec(proxy, Command::ResolvePtr, "example.com", 0);
	return codec.status() == Status::Failed && codec.sent.empty();
});

/* ------------------------------------------------------------------------- */
/* Codec: authentication                                                      */
/* ------------------------------------------------------------------------- */

EXO_TEST(socks5_credentials_are_sent_as_rfc_1929,
{
	ProxySettings proxy = testProxy();
	proxy.setCredentials("user", "secret");

	Codec codec(proxy, Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x02));

	/* Its own version octet, 1 rather than 5: the sub-negotiation is a
	   protocol of its own. */
	const std::string expected =
		bytes({ 0x01, 4 }) + "user" + bytes({ 6 }) + "secret";

	return codec.sent.substr(4) == expected;
});

EXO_TEST(socks5_the_request_follows_an_accepted_credential,
{
	ProxySettings proxy = testProxy();
	proxy.setCredentials("user", "secret");

	Codec codec(proxy, Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x02));

	const size_t before = codec.sent.size();
	codec.feed(bytes({ 0x01, 0x00 }));

	return codec.sent.size() > before && codec.status() == Status::NeedRead;
});

EXO_TEST(socks5_a_rejected_credential_is_reported_as_such,
{
	ProxySettings proxy = testProxy();
	proxy.setCredentials("user", "wrong");

	Codec codec(proxy, Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x02));
	codec.feed(bytes({ 0x01, 0x01 }));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyAuthFailed;
});

EXO_TEST(socks5_no_acceptable_method_is_reported_as_an_authentication_failure,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0xFF));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyAuthFailed;
});

EXO_TEST(socks5_a_method_that_was_never_offered_is_a_protocol_error,
{
	/* No credentials, so only no-auth was offered. */
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x02));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyProtocol;
});

EXO_TEST(socks5_gssapi_is_refused_even_if_the_proxy_asks_for_it,
{
	ProxySettings proxy = testProxy();
	proxy.setCredentials("user", "secret");

	Codec codec(proxy, Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x01));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyProtocol;
});

EXO_TEST(socks5_a_wrong_version_in_the_method_reply_is_a_protocol_error,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(bytes({ 0x04, 0x00 }));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyProtocol;
});

EXO_TEST(socks5_a_wrong_version_in_the_auth_reply_is_a_protocol_error,
{
	ProxySettings proxy = testProxy();
	proxy.setCredentials("user", "secret");

	Codec codec(proxy, Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x02));
	codec.feed(bytes({ 0x05, 0x00 }));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyProtocol;
});

/* ------------------------------------------------------------------------- */
/* Codec: the reply                                                           */
/* ------------------------------------------------------------------------- */

EXO_TEST(socks5_a_successful_reply_completes_the_exchange,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x00));
	codec.feed(connectReply(0x00, 12345));

	return codec.status() == Status::Done
		&& codec.handshake->getResult().port == 12345;
});

EXO_TEST(socks5_the_handshake_reads_no_further_than_its_last_byte,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x00));

	/* A CONNECT reply is routinely followed in the same segment by the peer's
	   first bytes. Those belong to the caller: swallowing them would lose them,
	   there being no buffer above the descriptor to hand them back through. */
	const std::string reply = connectReply(0x00);
	const std::string trailing = "HTTP/1.1 200 OK\r\n";

	size_t consumed = 0;
	const Status status = codec.feed(reply + trailing, &consumed);

	return status == Status::Done
		&& consumed == reply.size()
		&& codec.handshake->wanted() == 0;
});

EXO_TEST(socks5_a_reply_fed_one_byte_at_a_time_parses_identically,
{
	Codec whole(testProxy(), Command::Connect, "example.com", 80);
	whole.feed(methodReply(0x00));
	whole.feed(connectReply(0x00, 4242));

	Codec split(testProxy(), Command::Connect, "example.com", 80);
	split.feedByteAtATime(methodReply(0x00));
	split.feedByteAtATime(connectReply(0x00, 4242));

	return whole.status() == Status::Done
		&& split.status() == Status::Done
		&& whole.handshake->getResult().port == split.handshake->getResult().port;
});

EXO_TEST(socks5_a_request_written_one_byte_at_a_time_is_still_complete,
{
	/* A socket is allowed to take part of what it was offered, so consumeOutgoing()
	   has to retire exactly that much and no more: losing or repeating a byte here
	   corrupts the request the proxy reads. */
	Codec whole(testProxy(), Command::Connect, "example.com", 80);
	Codec split(testProxy(), Command::Connect, "example.com", 80, true);

	whole.feed(methodReply(0x00));
	split.feed(methodReply(0x00));

	return !whole.sent.empty()
		&& whole.sent == split.sent
		&& whole.status() == split.status();
});

EXO_TEST(socks5_an_ipv6_reply_parses,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x00));

	std::string reply = bytes({ 0x05, 0x00, 0x00, 0x04,
		0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01,
		0x00, 0x50 });

	codec.feed(reply);

	return codec.status() == Status::Done
		&& codec.handshake->getResult().address.getType() == InetAddress::Version::IPv6
		&& codec.handshake->getResult().port == 80;
});

EXO_TEST(socks5_a_named_reply_parses_its_length_prefix,
{
	Codec codec(testProxy(), Command::Resolve, "198.51.100.7", 0);
	codec.feed(methodReply(0x00));

	std::string reply = bytes({ 0x05, 0x00, 0x00, 0x03, 11 })
		+ "example.com" + bytes({ 0x00, 0x00 });

	codec.feed(reply);

	return codec.status() == Status::Done
		&& codec.handshake->getResult().name == "example.com";
});

EXO_TEST(socks5_an_empty_named_reply_is_a_protocol_error,
{
	Codec codec(testProxy(), Command::Resolve, "198.51.100.7", 0);
	codec.feed(methodReply(0x00));
	codec.feed(bytes({ 0x05, 0x00, 0x00, 0x03, 0x00 }));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyProtocol;
});

EXO_TEST(socks5_an_undefined_reply_address_type_is_a_protocol_error,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x00));

	/* 0x02 is not one of the three RFC 1928 defines. */
	codec.feed(bytes({ 0x05, 0x00, 0x00, 0x02 }));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyProtocol;
});

EXO_TEST(socks5_a_wrong_version_in_the_reply_is_a_protocol_error,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x00));
	codec.feed(bytes({ 0x04, 0x00, 0x00, 0x01 }));

	return codec.status() == Status::Failed
		&& codec.error() == SocketError::ProxyProtocol;
});

EXO_TEST(socks5_each_reply_code_maps_to_its_own_error,
{
	for (const ReplyExpectation& entry : socks5_reply_table)
	{
		Codec codec(testProxy(), Command::Connect, "example.com", 80);
		codec.feed(methodReply(0x00));
		codec.feed(connectReply(entry.reply));

		if (codec.status() != Status::Failed) return false;
		if (codec.error() != entry.code) return false;
		if (codec.handshake->getReplyCode() != entry.reply) return false;
	}

	return true;
});

EXO_TEST(socks5_a_failed_reply_is_still_read_to_its_end,
{
	Codec codec(testProxy(), Command::Connect, "example.com", 80);
	codec.feed(methodReply(0x00));

	const std::string reply = connectReply(0x05);
	size_t consumed = 0;
	codec.feed(reply, &consumed);

	/* Stopping at the code would leave the rest of the reply to be read as
	   whatever came next. */
	return consumed == reply.size() && codec.status() == Status::Failed;
});

/* ------------------------------------------------------------------------- */
/* ProxySettings                                                              */
/* ------------------------------------------------------------------------- */

EXO_TEST(socks5_settings_start_disabled,
{
	ProxySettings none;
	return !none.isEnabled()
		&& none.getKind() == ProxySettings::Kind::None
		&& none.toString() == "none";
});

EXO_TEST(socks5_tor_settings_name_the_default_socks_port,
{
	const ProxySettings tor = ProxySettings::tor();
	return tor.isEnabled()
		&& tor.getHost() == "127.0.0.1"
		&& tor.getPort() == 9050
		/* Off even for Tor: it redirects every lookup in the process, so it
		   is something a program asks for rather than inherits. */
		&& !tor.getTorExtensions()
		&& !tor.getAllowPrivateTargets();
});

EXO_TEST(socks5_half_a_proxy_is_no_proxy,
{
	/* A host with no port, or a port with no host, cannot be connected to.
	   Reporting Kind::None means a caller that ignored it connects directly
	   rather than to nothing. */
	return !ProxySettings("127.0.0.1", 0).isEnabled()
		&& !ProxySettings("", 1080).isEnabled();
});

EXO_TEST(socks5_settings_parse_from_a_url,
{
	const ProxySettings p = ProxySettings::fromString("socks5://127.0.0.1:9050");
	return p.isEnabled() && p.getHost() == "127.0.0.1" && p.getPort() == 9050;
});

EXO_TEST(socks5_settings_parse_the_socks5h_spelling_identically,
{
	/* curl distinguishes the two by where the name is resolved. Here a name is
	   never resolved locally, so there is nothing to distinguish. */
	return ProxySettings::fromString("socks5://127.0.0.1:9050")
		== ProxySettings::fromString("socks5h://127.0.0.1:9050");
});

EXO_TEST(socks5_settings_parse_credentials_from_a_url,
{
	const ProxySettings p =
		ProxySettings::fromString("socks5://user:secret@127.0.0.1:9050");

	return p.isEnabled()
		&& p.getUsername() == "user"
		&& p.getPassword() == "secret"
		&& p.hasCredentials();
});

EXO_TEST(socks5_settings_parse_a_bare_host_and_port,
{
	const ProxySettings p = ProxySettings::fromString("127.0.0.1:9050");
	return p.isEnabled() && p.getHost() == "127.0.0.1" && p.getPort() == 9050;
});

EXO_TEST(socks5_settings_parse_a_bracketed_ipv6_proxy,
{
	const ProxySettings p = ProxySettings::fromString("[::1]:9050");
	return p.isEnabled() && p.getHost() == "::1" && p.getPort() == 9050;
});

EXO_TEST(socks5_settings_assume_the_registered_port_when_none_is_given,
{
	const ProxySettings p = ProxySettings::fromString("socks5://127.0.0.1");
	return p.isEnabled() && p.getPort() == 1080;
});

EXO_TEST(socks5_settings_refuse_a_scheme_that_is_not_socks5,
{
	return !ProxySettings::fromString("http://127.0.0.1:3128").isEnabled()
		&& !ProxySettings::fromString("socks4://127.0.0.1:1080").isEnabled();
});

EXO_TEST(socks5_settings_do_not_put_the_password_in_their_text,
{
	ProxySettings p = testProxy();
	p.setCredentials("user", "secret");

	/* toString() ends up in log lines. */
	const std::string text = p.toString();
	return text.find("secret") == std::string::npos
		&& text.find("user") == std::string::npos;
});

EXO_TEST(socks5_the_process_default_can_be_moved_and_cleared,
{
	ProxyDefaultGuard guard;

	ProxySettings::setDefault(ProxySettings::tor());
	if (!ProxySettings::getDefault().isEnabled()) return false;

	ProxySettings::clearDefault();
	return !ProxySettings::getDefault().isEnabled();
});

/* ------------------------------------------------------------------------- */
/* A stub SOCKS5 proxy on the loopback                                        */
/* ------------------------------------------------------------------------- */

/*
 * Speaks the server half of RFC 1928 by hand, records what the client asked
 * for, and then stands in for the peer.
 *
 * The parsing here is deliberately independent of Socks5Handshake: a stub built
 * from the same code would agree with it about a mistake as readily as about
 * anything else.
 */
class StubProxy
	: public Samurai::IO::Net::ServerSocketEventHandler
	, public Samurai::IO::Net::SocketEventHandler
{
	public:
		/* What to answer with. */
		uint8_t offer_method = 0x00;
		uint8_t auth_status = 0x00;
		uint8_t reply_code = 0x00;
		/* Read the request and then say nothing, so the deadline has to act. */
		bool stall_before_reply = false;
		/* Hang up once the method is agreed. */
		bool hangup_after_method = false;
		/* Answer the request with fewer bytes than the grammar needs. */
		bool truncate_reply = false;
		/* Become a TLS server once the tunnel is up. */
		bool serve_tls = false;

		/* What arrived. */
		std::string greeting;
		std::string auth_username;
		std::string auth_password;
		uint8_t request_command = 0;
		uint8_t request_atyp = 0;
		std::string request_target;
		uint16_t request_port = 0;
		bool request_seen = false;
		bool tunnelled = false;
		std::string payload;

		StubProxy()
		{
			InetSocketAddress any((uint16_t) 0);
			server = Samurai::IO::Net::ServerSocket::create(this, any);
			if (!server || server->getFD() == -1) return;
			if (!server->listen()) return;
			port = server->getLocalPort();
		}

		~StubProxy() override
		{
			accepted.reset();
			server.reset();
		}

		StubProxy(const StubProxy&) = delete;
		StubProxy& operator=(const StubProxy&) = delete;

		bool ready() const { return server && port != 0; }
		uint16_t getPort() const { return port; }

		ProxySettings settings() const
		{
			return ProxySettings("127.0.0.1", port);
		}

		/** Send something as the peer, once the tunnel is up. */
		bool say(const std::string& text)
		{
			if (!accepted || !tunnelled) return false;
			return accepted->write(text.data(), text.size()) == (ssize_t) text.size();
		}

	protected:
		void EventAcceptError(const Samurai::IO::Net::ServerSocket*, const char*) override
		{ }

		void EventAcceptSocket(const Samurai::IO::Net::ServerSocket*,
			std::shared_ptr<Socket> socket) override
		{
			accepted = socket;
			accepted->setEventHandler(this);

			/* Each connection is negotiated from the start. Carrying 'tunnelled'
			   over would send the next one's greeting straight to payload. */
			step = Step::Greeting;
			tunnelled = false;
			incoming.clear();
		}

		void EventDataAvailable(const Socket* which) override
		{
			Socket* socket = const_cast<Socket*>(which);

			char scratch[1024];
			size_t got = 0;
			std::error_code ec;

			if (socket->read(scratch, sizeof(scratch), got, ec)
				!= Samurai::IO::ReadResult::Ok) return;

			if (tunnelled)
			{
				payload.append(scratch, got);
				return;
			}

			incoming.append(scratch, got);
			advance(socket);
		}

		void EventDisconnected(const Socket*) override { }
		void EventCanWrite(const Socket*) override { }

		void EventTLSConnected(const Socket*) override { }

	private:
		enum class Step { Greeting, Auth, Request, Tunnel };

		void advance(Socket* socket)
		{
			/* Each step consumes exactly what it needs and leaves the rest, so
			   a client that wrote everything in one segment is handled the same
			   as one that dribbled it. */
			while (true)
			{
				if (step == Step::Greeting)
				{
					if (incoming.size() < 2) return;
					const uint8_t count = (uint8_t) incoming[1];
					if (incoming.size() < (size_t) 2 + count) return;

					greeting = incoming.substr(0, 2 + count);
					incoming.erase(0, 2 + count);

					send(socket, std::string(1, (char) 0x05)
					           + std::string(1, (char) offer_method));

					if (hangup_after_method) { accepted.reset(); return; }

					step = (offer_method == 0x02) ? Step::Auth : Step::Request;
					continue;
				}

				if (step == Step::Auth)
				{
					if (incoming.size() < 2) return;
					const uint8_t ulen = (uint8_t) incoming[1];
					if (incoming.size() < (size_t) 2 + ulen + 1) return;
					const uint8_t plen = (uint8_t) incoming[2 + ulen];
					if (incoming.size() < (size_t) 3 + ulen + plen) return;

					auth_username = incoming.substr(2, ulen);
					auth_password = incoming.substr(3 + ulen, plen);
					incoming.erase(0, 3 + ulen + plen);

					send(socket, std::string(1, (char) 0x01)
					           + std::string(1, (char) auth_status));

					if (auth_status != 0x00) return;

					step = Step::Request;
					continue;
				}

				if (step == Step::Request)
				{
					if (incoming.size() < 5) return;

					request_command = (uint8_t) incoming[1];
					request_atyp = (uint8_t) incoming[3];

					size_t addr_len = 0;
					size_t addr_at = 4;

					if (request_atyp == 0x01) addr_len = 4;
					else if (request_atyp == 0x04) addr_len = 16;
					else if (request_atyp == 0x03)
					{
						addr_len = (uint8_t) incoming[4];
						addr_at = 5;
					}
					else return;

					if (incoming.size() < addr_at + addr_len + 2) return;

					request_target = incoming.substr(addr_at, addr_len);
					request_port = (uint16_t) (((uint8_t) incoming[addr_at + addr_len] << 8)
					                          | (uint8_t) incoming[addr_at + addr_len + 1]);
					incoming.erase(0, addr_at + addr_len + 2);
					request_seen = true;

					if (stall_before_reply) return;

					std::string reply;
					reply += (char) 0x05;
					reply += (char) reply_code;
					reply += (char) 0x00;

					if (request_command == 0xF0)
					{
						/* Tor answers a RESOLVE with the address itself. */
						reply += (char) 0x01;
						reply += bytes({ 198, 51, 100, 7 });
						reply += bytes({ 0x00, 0x00 });
					}
					else if (request_command == 0xF1)
					{
						/* And a RESOLVE_PTR with a name. */
						const std::string name = "peer.example.com";
						reply += (char) 0x03;
						reply += (char) name.size();
						reply += name;
						reply += bytes({ 0x00, 0x00 });
					}
					else
					{
						/* A CONNECT reply carries the proxy's own bound
						   address, which Tor gives as 0.0.0.0. */
						reply += (char) 0x01;
						reply += std::string(4, (char) 0x00);
						reply += bytes({ 0x00, 0x00 });
					}

					if (truncate_reply) reply.resize(3);

					send(socket, reply);

					if (reply_code != 0x00 || truncate_reply) return;
					/* A lookup ends with the reply; there is no tunnel. */
					if (request_command != 0x01) return;

					step = Step::Tunnel;
					tunnelled = true;

					if (serve_tls)
					{
						/* The stub is the peer from here on, and the peer is
						   what the client is about to hand a TLS handshake. */
						const bool untrusted =
							Samurai::IO::Net::TlsFactory::defaultAllowUntrusted();
						Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(true);
						const bool ok = socket->TLSInitialize(true);
						Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(untrusted);
						if (ok) socket->TLSsendHandshake();
					}
					return;
				}

				return;
			}
		}

		static void send(Socket* socket, const std::string& text)
		{
			/* These are all a handful of bytes into an empty socket buffer. */
			socket->write(text.data(), text.size());
		}

		std::shared_ptr<Samurai::IO::Net::ServerSocket> server;
		std::shared_ptr<Socket> accepted;
		std::string incoming;
		uint16_t port = 0;
		Step step = Step::Greeting;
};

/* Records what a proxied client socket was told, and in what order. */
class ClientRecorder : public Samurai::IO::Net::SocketEventHandler
{
	public:
		bool host_lookup = false;
		bool host_found = false;
		bool connecting = false;
		bool connected = false;
		bool tls_connected = false;
		bool disconnected = false;
		bool error = false;
		SocketError last_error = SocketError::SocketUnknown;
		std::string last_message;
		std::string received;
		/* Whether TLS was already up when EventConnected arrived, which it must
		   never be: the tunnel comes first. */
		bool tls_before_connected = false;
		bool start_tls = false;

	protected:
		void EventHostLookup(const Socket*) override { host_lookup = true; }
		void EventHostFound(const Socket*) override { host_found = true; }
		void EventConnecting(const Socket*) override { connecting = true; }

		void EventConnected(const Socket* which) override
		{
			if (tls_connected) tls_before_connected = true;
			connected = true;

			if (!start_tls) return;

			Socket* socket = const_cast<Socket*>(which);
			const bool untrusted =
				Samurai::IO::Net::TlsFactory::defaultAllowUntrusted();
			Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(true);
			const bool ok = socket->TLSInitialize(false);
			Samurai::IO::Net::TlsFactory::setDefaultAllowUntrusted(untrusted);
			if (ok) socket->TLSsendHandshake();
		}

		void EventTLSConnected(const Socket*) override { tls_connected = true; }
		void EventDisconnected(const Socket*) override { disconnected = true; }

		void EventDataAvailable(const Socket* which) override
		{
			Socket* socket = const_cast<Socket*>(which);
			char scratch[1024];
			size_t got = 0;
			std::error_code ec;

			if (socket->read(scratch, sizeof(scratch), got, ec)
				== Samurai::IO::ReadResult::Ok)
				received.append(scratch, got);
		}

		void EventCanWrite(const Socket*) override { }

		void EventError(const Socket*, SocketError which, const char* msg) override
		{
			error = true;
			last_error = which;
			last_message = msg ? msg : "";
		}
};

/*
 * A proxied client against a stub, taken as far as the tunnel.
 *
 * The peer is a name that cannot resolve, deliberately: if the connection comes
 * up anyway then nothing asked the system resolver about it, which is the whole
 * claim being made. .invalid is reserved by RFC 2606 for exactly this.
 */
struct ProxyFixture
{
	ProxyDefaultGuard guard;
	StubProxy stub;
	ClientRecorder events;
	std::shared_ptr<Socket> client;
	size_t baseline = 0;
	bool ready = false;

	explicit ProxyFixture(const char* peer = "no-such-host.invalid",
	                      uint16_t peer_port = 80)
	{
		baseline = SocketMonitor::getInstance()->size();
		if (!stub.ready()) return;

		client = Socket::create(&events, std::string(peer), peer_port);
		if (!client) return;

		client->setProxy(stub.settings());
		client->setConnectTimeout(std::chrono::milliseconds(SOCKS_TEST_TIMEOUT_MS));
		ready = true;
	}

	~ProxyFixture()
	{
		client.reset();
		pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 1000);
	}

	ProxyFixture(const ProxyFixture&) = delete;
	ProxyFixture& operator=(const ProxyFixture&) = delete;

	/** Connect and wait for the tunnel, or for a failure. */
	bool settle()
	{
		if (!ready) return false;
		client->connect();
		return pump([&] { return events.connected || events.error; });
	}
};

/* ------------------------------------------------------------------------- */
/* Socket: through a proxy, end to end                                        */
/* ------------------------------------------------------------------------- */

EXO_TEST(socks5_socket_reaches_a_peer_through_the_proxy,
{
	ProxyFixture fix;
	if (!fix.settle()) return false;

	return fix.events.connected && !fix.events.error;
});

EXO_TEST(socks5_socket_never_resolves_the_peer_locally,
{
	/* The peer is an unresolvable name. Connecting at all is the proof: a
	   local lookup would have failed with HostNotFound before the proxy was
	   ever asked. */
	ProxyFixture fix;
	if (!fix.settle()) return false;

	return fix.events.connected
		&& !fix.events.host_lookup
		&& !fix.events.host_found;
});

EXO_TEST(socks5_the_proxy_is_asked_for_the_name_the_caller_gave,
{
	ProxyFixture fix("example.com", 8080);
	if (!fix.settle()) return false;

	return fix.stub.request_seen
		&& fix.stub.request_atyp == 0x03
		&& fix.stub.request_target == "example.com"
		&& fix.stub.request_port == 8080
		/* CONNECT, and nothing else. */
		&& fix.stub.request_command == 0x01;
});

EXO_TEST(socks5_an_onion_peer_reaches_the_proxy_intact,
{
	const char* onion = "expyuzz4wqqyqhjn.onion";

	ProxyFixture fix(onion, 80);
	if (!fix.settle()) return false;

	return fix.events.connected && fix.stub.request_target == onion;
});

EXO_TEST(socks5_the_peer_name_is_what_identifies_a_proxied_socket,
{
	ProxyFixture fix("example.com", 80);
	if (!fix.settle()) return false;

	/* getAddress() reports the proxy, because that is what the descriptor is
	   connected to. The peer is only ever the name that was asked for. */
	const InetAddress* where = fix.client->getAddress();

	return fix.client->getPeerName() == "example.com"
		&& where && where->getAddress() == "127.0.0.1"
		&& fix.client->getPort() == fix.stub.getPort();
});

EXO_TEST(socks5_data_flows_both_ways_once_the_tunnel_is_up,
{
	ProxyFixture fix;
	if (!fix.settle()) return false;

	const std::string question = "ping";
	if (fix.client->write(question.data(), question.size()) != (ssize_t) question.size())
		return false;

	if (!pump([&] { return fix.stub.payload.size() >= question.size(); })) return false;

	if (!fix.stub.say("pong")) return false;
	if (!pump([&] { return fix.events.received.size() >= 4; })) return false;

	return fix.stub.payload == "ping" && fix.events.received == "pong";
});

EXO_TEST(socks5_credentials_round_trip_through_a_real_socket,
{
	ProxyFixture fix("example.com", 80);
	if (!fix.ready) return false;

	fix.stub.offer_method = 0x02;

	ProxySettings proxy = fix.stub.settings();
	proxy.setCredentials("user", "secret");
	fix.client->setProxy(proxy);

	if (!fix.settle()) return false;

	return fix.events.connected
		&& fix.stub.auth_username == "user"
		&& fix.stub.auth_password == "secret";
});

EXO_TEST(socks5_a_rejected_credential_is_reported_to_the_socket,
{
	ProxyFixture fix("example.com", 80);
	if (!fix.ready) return false;

	fix.stub.offer_method = 0x02;
	fix.stub.auth_status = 0x01;

	ProxySettings proxy = fix.stub.settings();
	proxy.setCredentials("user", "wrong");
	fix.client->setProxy(proxy);

	fix.settle();

	return fix.events.error
		&& !fix.events.connected
		&& fix.events.last_error == SocketError::ProxyAuthFailed;
});

EXO_TEST(socks5_a_refused_peer_is_reported_as_refused,
{
	ProxyFixture fix("example.com", 80);
	if (!fix.ready) return false;

	fix.stub.reply_code = 0x05;
	fix.settle();

	return fix.events.error
		&& !fix.events.connected
		&& fix.events.last_error == SocketError::ConnectionRefused;
});

EXO_TEST(socks5_a_proxy_that_hangs_up_mid_handshake_is_not_a_connection,
{
	ProxyFixture fix("example.com", 80);
	if (!fix.ready) return false;

	fix.stub.hangup_after_method = true;
	fix.settle();

	/* Reporting this as a closed connection would say the peer had been
	   reached. It had not: the proxy never confirmed it. */
	return fix.events.error
		&& !fix.events.connected
		&& fix.events.last_error == SocketError::ProxyProtocol;
});

EXO_TEST(socks5_a_stalled_handshake_still_times_out,
{
	ProxyFixture fix("example.com", 80);
	if (!fix.ready) return false;

	fix.stub.stall_before_reply = true;
	/* The socket's own connect deadline has to cover the negotiation: the
	   caller has not been told it is connected, so nothing else would. */
	fix.client->setConnectTimeout(std::chrono::milliseconds(300));

	fix.client->connect();
	pump([&] { return fix.events.connected || fix.events.error; });

	return fix.events.error
		&& !fix.events.connected
		&& fix.events.last_error == SocketError::ConnectionTimeout;
});

EXO_TEST(socks5_a_private_peer_never_reaches_the_proxy,
{
	ProxyFixture fix("10.1.2.3", 80);
	if (!fix.ready) return false;

	fix.settle();

	/* Refused before anything was connected, so the proxy saw no greeting at
	   all - it was not even told the address. */
	return fix.events.error
		&& !fix.events.connected
		&& fix.events.last_error == SocketError::ProxyRefused
		&& fix.stub.greeting.empty()
		&& !fix.stub.request_seen;
});

EXO_TEST(socks5_a_socket_can_be_reconnected_through_the_proxy,
{
	/* A handshake left over from the first attempt would be half negotiated, and
	   resuming it against a proxy that has never heard of it would send the
	   second half of an exchange as though it were the first. */
	ProxyFixture fix("example.com", 80);
	if (!fix.settle()) return false;

	fix.client->disconnect();
	pump([&] { return !fix.events.connected; }, 200);

	fix.events.connected = false;
	fix.events.error = false;
	fix.stub.request_seen = false;
	fix.stub.request_target.clear();

	fix.client->connect();
	if (!pump([&] { return fix.events.connected || fix.events.error; })) return false;

	return fix.events.connected
		&& !fix.events.error
		&& fix.stub.request_target == "example.com";
});

EXO_TEST(socks5_a_peer_given_as_an_address_object_still_names_a_peer,
{
	ProxyDefaultGuard guard;

	StubProxy stub;
	if (!stub.ready()) return false;

	const size_t baseline = SocketMonitor::getInstance()->size();
	bool connected = false;

	{
		/* Built from raw bytes, so it carries no name at all - the address
		   written out is what the proxy has to be asked for. */
		InetAddress peer;
		const uint8_t raw[] = { 198, 51, 100, 7 };
		if (!peer.setRawAddress((void*) raw, sizeof(raw), InetAddress::Version::IPv4))
			return false;
		if (!peer.getHostname().empty()) return false;

		ClientRecorder events;
		auto client = Socket::create(&events, peer, (uint16_t) 80);
		if (!client) return false;

		client->setProxy(stub.settings());
		client->connect();
		pump([&] { return events.connected || events.error; });
		connected = events.connected;
	}

	pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 1000);

	return connected
		/* And as an address, not as a name. */
		&& stub.request_atyp == 0x01
		&& stub.request_target == std::string("\xc6\x33\x64\x07", 4);
});

EXO_TEST(socks5_lookup_is_refused_on_a_proxied_socket,
{
	ProxyFixture fix("example.com", 80);
	if (!fix.ready) return false;

	/* The one call that would hand the peer's name to the system resolver. */
	fix.client->lookup();

	return fix.events.error
		&& fix.events.last_error == SocketError::HostNotFound
		&& !fix.events.host_lookup;
});

EXO_TEST(socks5_the_process_default_reaches_a_socket_made_after_it,
{
	ProxyDefaultGuard guard;

	StubProxy stub;
	if (!stub.ready()) return false;

	const size_t baseline = SocketMonitor::getInstance()->size();
	bool settled = false;

	{
		ClientRecorder events;
		ProxySettings::setDefault(stub.settings());

		auto client = Socket::create(&events, std::string("example.com"), (uint16_t) 80);
		if (!client) return false;

		client->connect();
		settled = pump([&] { return events.connected || events.error; });

		if (!settled || !events.connected) return false;
	}

	pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 1000);
	return stub.request_target == "example.com";
});

EXO_TEST(socks5_a_socket_made_before_the_default_moved_is_not_proxied,
{
	ProxyDefaultGuard guard;

	StubProxy stub;
	if (!stub.ready()) return false;

	const size_t baseline = SocketMonitor::getInstance()->size();

	ClientRecorder events;
	ProxySettings::clearDefault();

	/* Settings are copied when the socket is constructed, so moving the default
	   afterwards must not reach back into it. */
	auto client = Socket::create(&events, std::string("example.com"), (uint16_t) 80);
	if (!client) return false;

	ProxySettings::setDefault(stub.settings());
	const bool proxied = client->isProxied();

	client.reset();
	pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 1000);

	return !proxied;
});

EXO_TEST(socks5_setting_no_proxy_overrides_a_proxied_default,
{
	ProxyDefaultGuard guard;
	ProxySettings::setDefault(ProxySettings::tor());

	ClientRecorder events;
	auto client = Socket::create(&events, std::string("example.com"), (uint16_t) 80);
	if (!client) return false;

	if (!client->isProxied()) return false;

	client->setProxy(ProxySettings());
	return !client->isProxied();
});

EXO_TEST(socks5_an_accepted_socket_is_never_proxied,
{
	ProxyDefaultGuard guard;
	ProxySettings::setDefault(ProxySettings::tor());

	/* A socket ServerSocket accepted is already connected to whoever called;
	   there is nothing for a proxy to do and a handshake would corrupt it. */
	StubProxy stub;
	if (!stub.ready()) return false;

	const size_t baseline = SocketMonitor::getInstance()->size();
	bool accepted_unproxied = false;

	{
		ProxySettings::clearDefault();

		ClientRecorder events;
		auto client = Socket::create(&events, std::string("127.0.0.1"), stub.getPort());
		if (!client) return false;

		client->connect();
		pump([&] { return events.connected || events.error; });

		ProxySettings::setDefault(ProxySettings::tor());

		/* The stub's accepted socket is the inbound one; it came up before the
		   default moved, and an accepted socket ignores it regardless. */
		accepted_unproxied = events.connected;
	}

	pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 1000);
	return accepted_unproxied;
});

/* ------------------------------------------------------------------------- */
/* Socket: TLS inside the tunnel                                              */
/* ------------------------------------------------------------------------- */

EXO_TEST(socks5_tls_is_refused_before_the_tunnel_exists,
{
	ProxyFixture fix("example.com", 80);
	if (!fix.ready) return false;

	fix.stub.stall_before_reply = true;
	fix.client->setConnectTimeout(std::chrono::milliseconds(2000));
	fix.client->connect();

	/* Wait until the negotiation is under way, then try to start TLS on top of
	   it. A handshake begun here would go to the proxy rather than through it. */
	pump([&] { return fix.stub.request_seen || fix.events.error; }, 2000);
	if (fix.events.error) return false;

	return !fix.client->TLSInitialize(false) && !fix.client->isSecure();
});

/* ------------------------------------------------------------------------- */
/* DNS::Resolver over the proxy, using Tor's RESOLVE                          */
/* ------------------------------------------------------------------------- */

class ResolveRecorder : public Samurai::IO::Net::ResolveEventHandler
{
	public:
		bool found = false;
		bool failed = false;
		Samurai::IO::Net::DNS::Resolver::Error error =
			Samurai::IO::Net::DNS::Resolver::Error::Unknown;
		std::string address;
		std::string hostname;

		void EventHostFound(const InetAddress* addr) override
		{
			found = true;
			if (!addr) return;
			address = addr->getAddress();
			hostname = addr->getHostname();
		}

		void EventHostError(Samurai::IO::Net::DNS::Resolver::Error which) override
		{
			failed = true;
			error = which;
		}
};

EXO_TEST(socks5_a_lookup_goes_to_the_proxy_when_tor_extensions_are_on,
{
	ProxyDefaultGuard guard;

	StubProxy stub;
	if (!stub.ready()) return false;

	ProxySettings proxy = stub.settings();
	proxy.setTorExtensions(true);
	ProxySettings::setDefault(proxy);

	const size_t baseline = SocketMonitor::getInstance()->size();
	ResolveRecorder events;

	{
		/* An unresolvable name again: an answer at all proves the question went
		   to the proxy rather than to the system resolver. */
		std::unique_ptr<Samurai::IO::Net::DNS::Resolver> resolver =
			Samurai::IO::Net::DNS::Resolver::getHostByName(&events, "no-such-host.invalid");

		if (!resolver) return false;
		pump([&] { return events.found || events.failed; });
	}

	pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 1000);

	return events.found
		&& !events.failed
		&& events.address == "198.51.100.7"
		/* RESOLVE, not CONNECT. */
		&& stub.request_command == 0xF0
		&& stub.request_target == "no-such-host.invalid";
});

EXO_TEST(socks5_a_reverse_lookup_goes_to_the_proxy_too,
{
	ProxyDefaultGuard guard;

	StubProxy stub;
	if (!stub.ready()) return false;

	ProxySettings proxy = stub.settings();
	proxy.setTorExtensions(true);
	ProxySettings::setDefault(proxy);

	const size_t baseline = SocketMonitor::getInstance()->size();
	ResolveRecorder events;
	InetAddress queried("198.51.100.7");

	{
		std::unique_ptr<Samurai::IO::Net::DNS::Resolver> resolver =
			Samurai::IO::Net::DNS::Resolver::getNameByAddress(&events, &queried);

		if (!resolver) return false;
		pump([&] { return events.found || events.failed; });
	}

	pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 1000);

	/* The name travels attached to the address that was asked about rather than
	   replacing it, which is how the pooled resolver reports one too. */
	return events.found
		&& events.hostname == "peer.example.com"
		&& events.address == "198.51.100.7"
		&& stub.request_command == 0xF1
		&& stub.request_atyp == 0x01;
});

EXO_TEST(socks5_a_lookup_does_not_go_to_the_proxy_unless_asked,
{
	ProxyDefaultGuard guard;

	StubProxy stub;
	if (!stub.ready()) return false;

	/* Tor extensions off, which is the default: a proxy that is not Tor answers
	   an unknown command with command-not-supported, so the system resolver is
	   left to answer as it always has. */
	ProxySettings::setDefault(stub.settings());

	const size_t baseline = SocketMonitor::getInstance()->size();
	ResolveRecorder events;

	{
		std::unique_ptr<Samurai::IO::Net::DNS::Resolver> resolver =
			Samurai::IO::Net::DNS::Resolver::getHostByName(&events, "no-such-host.invalid");

		if (!resolver) return false;
		pump([&] { return events.found || events.failed; });
	}

	pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 2000);

	return events.failed && !stub.request_seen && stub.greeting.empty();
});

EXO_TEST(socks5_a_lookup_falls_back_when_the_proxy_is_named_rather_than_numbered,
{
	ProxyDefaultGuard guard;

	StubProxy stub;
	if (!stub.ready()) return false;

	/* Resolving the proxy's own name is a question only the system resolver can
	   answer - asking this backend would ask the proxy where the proxy is. */
	ProxySettings proxy("proxy.example.invalid", stub.getPort());
	proxy.setTorExtensions(true);
	ProxySettings::setDefault(proxy);

	const size_t baseline = SocketMonitor::getInstance()->size();
	ResolveRecorder events;

	{
		std::unique_ptr<Samurai::IO::Net::DNS::Resolver> resolver =
			Samurai::IO::Net::DNS::Resolver::getHostByName(&events, "no-such-host.invalid");

		if (!resolver) return false;
		pump([&] { return events.found || events.failed; });
	}

	pump([&] { return SocketMonitor::getInstance()->size() <= baseline; }, 2000);

	/* Answered by the pool, which cannot resolve it, and the stub was never
	   contacted. */
	return events.failed && !stub.request_seen;
});

EXO_TEST(socks5_tls_negotiates_inside_the_tunnel,
{
	/* Claimed rather than merely read: setKeys() is process-wide, and several
	   cases in this binary point it at a pair of their own, so what the stub
	   presents has to be decided here rather than by whatever ran before. */
	if (!tls_test_keys_claimed().ready) return false;

	ProxyFixture fix("localhost.example.com", 443);
	if (!fix.ready) return false;

	fix.stub.serve_tls = true;
	fix.events.start_tls = true;

	fix.client->connect();
	const bool settled = pump([&] {
		return fix.events.tls_connected || fix.events.error;
	});

	return settled
		&& fix.events.tls_connected
		&& fix.client->isSecure()
		/* The tunnel came first. TLS reported after EventConnected, never
		   before it. */
		&& fix.events.connected
		&& !fix.events.tls_before_connected;
});

}

// eof
