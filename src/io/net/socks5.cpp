/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketglue.h>
#include <samurai/io/net/socks5.h>
#include <samurai/io/net/socketaddress.h>
#include <samurai/stdc.h>

#include <string.h>

namespace {

const uint8_t SOCKS5_VERSION = 0x05;
const uint8_t SOCKS5_AUTH_VERSION = 0x01;

const uint8_t METHOD_NONE     = 0x00;
const uint8_t METHOD_USERPASS = 0x02;
const uint8_t METHOD_NONE_ACCEPTABLE = 0xFF;

const uint8_t ATYP_IPV4   = 0x01;
const uint8_t ATYP_DOMAIN = 0x03;
const uint8_t ATYP_IPV6   = 0x04;

/* The length octet in front of a name, and in front of each credential. */
const size_t MAX_OCTET_LENGTH = 255;

/*
 * The reply codes RFC 1928 defines, mapped onto what a socket handler already
 * knows how to talk about.
 *
 * Three of them describe the peer rather than the proxy - unreachable, refused,
 * expired - and are reported as if we had reached for the peer ourselves,
 * because that is what happened and a caller's retry logic wants to hear it in
 * the same terms. The rest are the proxy declining, which is a different thing
 * and gets its own error.
 */
Samurai::IO::Net::SocketError classify_reply(uint8_t reply, const char*& message)
{
	using Samurai::IO::Net::SocketError;

	switch (reply)
	{
		case 0x01:
			message = "Proxy reported a general failure";
			return SocketError::SocketUnknown;
		case 0x02:
			message = "Proxy policy does not allow this connection";
			return SocketError::ProxyRefused;
		case 0x03:
			message = "Network is unreachable from the proxy";
			return SocketError::NetUnreachable;
		case 0x04:
			message = "Host is unreachable from the proxy";
			return SocketError::HostUnreachable;
		case 0x05:
			message = "Connection refused";
			return SocketError::ConnectionRefused;
		case 0x06:
			message = "TTL expired";
			return SocketError::ConnectionTimeout;
		case 0x07:
			message = "Proxy does not support the requested command";
			return SocketError::ProxyRefused;
		case 0x08:
			message = "Proxy does not support the requested address type";
			return SocketError::ProxyRefused;
	}

	message = "Proxy refused the request";
	return SocketError::ProxyRefused;
}


/**
 * Whether an address is one the proxy has no business being asked for.
 *
 * Tor routes none of these - no exit relay carries RFC 1918, loopback or
 * link-local - so asking can only fail. On a proxy that is not Tor it can
 * succeed, which is worse: a peer the program did not pick for itself would
 * turn the proxy into a way to reach machines on its network.
 *
 * isValid() is part of the test rather than a separate one: it already rejects
 * 0.0.0.0, the unspecified address and 240.0.0.0/4, none of which name a peer.
 */
bool is_local_address(const Samurai::IO::Net::InetAddress& address)
{
	return !address.isValid()
		|| address.isLoopback()
		|| address.isPrivate()
		|| address.isLinkLocal()
		|| address.isMulticast();
}


/**
 * The names that mean "this machine" or "this network" without being literals.
 *
 * A proxy resolves a name on the far side, so what one resolves to is not
 * knowable here and is deliberately not checked. These three are the exception:
 * they cannot mean anything else, and each is a plausible slip rather than a
 * contrived one.
 */
bool is_local_name(const std::string& name)
{
	if (Samurai::Util::iequals(name, "localhost")) return true;

	const std::string::size_type dot = name.rfind('.');
	if (dot == std::string::npos) return false;

	const std::string suffix = name.substr(dot);
	return Samurai::Util::iequals(suffix, ".local")
		|| Samurai::Util::iequals(suffix, ".localhost");
}

}


Samurai::IO::Net::Socks5Handshake::Socks5Handshake(
	const Samurai::IO::Net::ProxySettings& proxy,
	Samurai::IO::Net::Socks5Handshake::Command command_,
	const std::string& target_, uint16_t port_)
	: settings(proxy)
	, command(command_)
	, target(target_)
	, target_port(port_)
{
	if (target.empty())
	{
		fail(SocketError::ProxyProtocol, "No peer to ask the proxy for");
		return;
	}

	if (target.size() > MAX_OCTET_LENGTH)
	{
		fail(SocketError::ProxyProtocol, "Peer name is too long for SOCKS5");
		return;
	}

	if (settings.getUsername().size() > MAX_OCTET_LENGTH
	 || settings.getPassword().size() > MAX_OCTET_LENGTH)
	{
		fail(SocketError::ProxyProtocol, "Proxy credential is too long for SOCKS5");
		return;
	}

	/* Encoded here, but not sent here: a request that cannot be expressed is
	   refused before the proxy has been contacted at all, and in particular
	   before it has been told the peer's name. */
	if (!encodeRequest()) return;

	encodeGreeting();
}


void Samurai::IO::Net::Socks5Handshake::fail(Samurai::IO::Net::SocketError code, const char* message)
{
	status = Status::Failed;
	error = code;
	error_message = message;
	out.clear();
	out_sent = 0;
	need = 0;
}


void Samurai::IO::Net::Socks5Handshake::expect(Step next, size_t bytes)
{
	step = next;
	need = bytes;
	in.clear();
	status = Status::NeedRead;
}


void Samurai::IO::Net::Socks5Handshake::queue(const std::string& bytes)
{
	out = bytes;
	out_sent = 0;
	status = Status::NeedWrite;
}


void Samurai::IO::Net::Socks5Handshake::encodeGreeting()
{
	/* VER NMETHODS METHODS...
	 *
	 * "No authentication" is always offered, and username/password only when
	 * there is one to send. GSSAPI (0x01) is never offered: it is not
	 * implemented, and offering a method we cannot perform invites the proxy to
	 * choose it and leaves the exchange stuck. */
	std::string greeting;
	greeting += (char) SOCKS5_VERSION;

	if (settings.hasCredentials())
	{
		greeting += (char) 2;
		greeting += (char) METHOD_NONE;
		greeting += (char) METHOD_USERPASS;
	}
	else
	{
		greeting += (char) 1;
		greeting += (char) METHOD_NONE;
	}

	queue(greeting);
}


void Samurai::IO::Net::Socks5Handshake::encodeAuth()
{
	/* RFC 1929: VER ULEN UNAME PLEN PASSWD, with its own version octet that is
	   1 rather than 5 - the sub-negotiation is a protocol of its own. */
	std::string auth;
	auth += (char) SOCKS5_AUTH_VERSION;
	auth += (char) settings.getUsername().size();
	auth += settings.getUsername();
	auth += (char) settings.getPassword().size();
	auth += settings.getPassword();

	queue(auth);
}


bool Samurai::IO::Net::Socks5Handshake::encodeRequest()
{
	/* VER CMD RSV ATYP DST.ADDR DST.PORT
	 *
	 * A literal is sent as one, and everything else as a name for the proxy to
	 * resolve. That second case is the point of the exercise: the name has not
	 * been looked up here and will not be, so no question about the peer has
	 * left this host except to the proxy itself. An .onion name needs nothing
	 * special - it is not a literal, so it lands here untouched.
	 *
	 * The bytes come out of an InetSocketAddress rather than from inet_pton
	 * here, so that there is one place in the tree that knows how a literal is
	 * parsed and how wide each family is.
	 */
	std::string encoded;
	encoded += (char) SOCKS5_VERSION;
	encoded += (char) command;
	encoded += (char) 0x00;

	InetAddress address(target);
	const bool is_literal = (address.getType() != InetAddress::Version::Unspecified);

	if (!settings.getAllowPrivateTargets())
	{
		if (is_literal && is_local_address(address))
		{
			fail(SocketError::ProxyRefused,
			     "Refusing to ask a proxy for a peer on the local network");
			return false;
		}

		if (!is_literal && is_local_name(target))
		{
			fail(SocketError::ProxyRefused,
			     "Refusing to ask a proxy for a peer on the local network");
			return false;
		}
	}

	InetSocketAddress sa(address, target_port);
	struct sockaddr* raw = is_literal ? sa.getSockAddr() : nullptr;

	if (raw && raw->sa_family == AF_INET)
	{
		const struct sockaddr_in* sin = (const struct sockaddr_in*) raw;
		encoded += (char) ATYP_IPV4;
		encoded.append((const char*) &sin->sin_addr, sizeof(sin->sin_addr));
	}
	else if (raw && raw->sa_family == AF_INET6)
	{
		const struct sockaddr_in6* sin6 = (const struct sockaddr_in6*) raw;
		encoded += (char) ATYP_IPV6;
		encoded.append((const char*) &sin6->sin6_addr, sizeof(sin6->sin6_addr));
	}
	else
	{
		/* A reverse lookup has nothing to ask about without an address, and the
		   proxy would answer address-type-not-supported a round trip later. */
		if (command == Command::ResolvePtr)
		{
			fail(SocketError::ProxyProtocol,
			     "A reverse lookup needs an address, not a name");
			return false;
		}

		encoded += (char) ATYP_DOMAIN;
		encoded += (char) target.size();
		encoded += target;
	}

	const uint16_t net_port = htons(target_port);
	encoded.append((const char*) &net_port, sizeof(net_port));

	request = encoded;
	return true;
}


std::string_view Samurai::IO::Net::Socks5Handshake::outgoing() const
{
	if (out_sent >= out.size()) return std::string_view();
	return std::string_view(out).substr(out_sent);
}


void Samurai::IO::Net::Socks5Handshake::consumeOutgoing(size_t length)
{
	if (status != Status::NeedWrite) return;

	out_sent += length;
	if (out_sent > out.size()) out_sent = out.size();
	if (out_sent < out.size()) return;

	/* Everything queued is away, so the next thing is the reply to it. 'step'
	   already names which reply that is: whatever queued these bytes set it
	   when it did. */
	switch (step)
	{
		case Step::MethodReply:
			expect(Step::MethodReply, 2);
			break;

		case Step::AuthReply:
			expect(Step::AuthReply, 2);
			break;

		default:
			expect(Step::ReplyHead, 4);
			break;
	}
}


size_t Samurai::IO::Net::Socks5Handshake::wanted() const
{
	if (status != Status::NeedRead) return 0;
	if (in.size() >= need) return 0;
	return need - in.size();
}


Samurai::IO::Net::Socks5Handshake::Status
Samurai::IO::Net::Socks5Handshake::feed(const char* data, size_t length, size_t& consumed)
{
	consumed = 0;

	while (status == Status::NeedRead && consumed < length)
	{
		const size_t missing = need - in.size();
		const size_t take = (length - consumed < missing) ? (length - consumed) : missing;

		in.append(data + consumed, take);
		consumed += take;

		if (in.size() < need) break;

		if (!consumeStep(in.data(), in.size())) break;
	}

	return status;
}


bool Samurai::IO::Net::Socks5Handshake::consumeStep(const char* data, size_t length)
{
	const uint8_t* bytes = (const uint8_t*) data;

	switch (step)
	{
		case Step::MethodReply:
		{
			if (length < 2) return false;

			if (bytes[0] != SOCKS5_VERSION)
			{
				fail(SocketError::ProxyProtocol, "Proxy is not a SOCKS5 proxy");
				return false;
			}

			method = bytes[1];

			if (method == METHOD_NONE_ACCEPTABLE)
			{
				fail(SocketError::ProxyAuthFailed,
				     "Proxy accepts no authentication method we offered");
				return false;
			}

			if (method == METHOD_NONE)
			{
				queue(request);
				step = Step::ReplyHead;
				return true;
			}

			if (method == METHOD_USERPASS && settings.hasCredentials())
			{
				encodeAuth();
				step = Step::AuthReply;
				return true;
			}

			/* A method we never offered cannot be performed, and a proxy naming
			   one is either broken or steering us towards an exchange we would
			   have to improvise. */
			fail(SocketError::ProxyProtocol,
			     "Proxy chose an authentication method that was not offered");
			return false;
		}

		case Step::AuthReply:
		{
			if (length < 2) return false;

			if (bytes[0] != SOCKS5_AUTH_VERSION)
			{
				fail(SocketError::ProxyProtocol,
				     "Proxy answered the credentials with the wrong version");
				return false;
			}

			if (bytes[1] != 0x00)
			{
				fail(SocketError::ProxyAuthFailed, "Proxy rejected the credentials");
				return false;
			}

			queue(request);
			step = Step::ReplyHead;
			return true;
		}

		case Step::ReplyHead:
		{
			if (length < 4) return false;

			if (bytes[0] != SOCKS5_VERSION)
			{
				fail(SocketError::ProxyProtocol, "Proxy reply is not SOCKS5");
				return false;
			}

			result.reply = bytes[1];
			reply_atyp = bytes[3];

			/*
			 * The reply is read to its end even when REP says the request
			 * failed, rather than reported the moment the code is known: the
			 * bytes are on the way regardless, and leaving them unread would
			 * mean a caller retrying on the same connection - which is what a
			 * lookup does - reads them as the next reply.
			 */
			switch (reply_atyp)
			{
				case ATYP_IPV4:
					expect(Step::ReplyAddr, 4);
					return true;
				case ATYP_IPV6:
					expect(Step::ReplyAddr, 16);
					return true;
				case ATYP_DOMAIN:
					expect(Step::ReplyAddrLen, 1);
					return true;
			}

			fail(SocketError::ProxyProtocol, "Proxy reply has an unknown address type");
			return false;
		}

		case Step::ReplyAddrLen:
		{
			if (length < 1) return false;

			/* A zero length name is not something the grammar can express: the
			   port would be read as part of the address. */
			if (bytes[0] == 0)
			{
				fail(SocketError::ProxyProtocol, "Proxy reply has an empty name");
				return false;
			}

			expect(Step::ReplyAddr, bytes[0]);
			return true;
		}

		case Step::ReplyAddr:
		{
			if (reply_atyp == ATYP_IPV4)
			{
				if (length < 4) return false;
				result.address.setRawAddress((void*) data, 4, InetAddress::Version::IPv4);
			}
			else if (reply_atyp == ATYP_IPV6)
			{
				if (length < 16) return false;
				result.address.setRawAddress((void*) data, 16, InetAddress::Version::IPv6);
			}
			else
			{
				result.name.assign(data, length);
			}

			expect(Step::ReplyPort, 2);
			return true;
		}

		case Step::ReplyPort:
		{
			if (length < 2) return false;

			uint16_t net_port = 0;
			memcpy(&net_port, data, sizeof(net_port));
			result.port = ntohs(net_port);

			in.clear();
			need = 0;

			if (result.reply != 0x00)
			{
				const char* message = nullptr;
				const SocketError code = classify_reply(result.reply, message);
				fail(code, message);
				return false;
			}

			status = Status::Done;
			return false;
		}
	}

	fail(SocketError::ProxyProtocol, "Proxy handshake reached an unknown state");
	return false;
}


Samurai::IO::Net::SocketError Samurai::IO::Net::Socks5Handshake::getError(const char*& message) const
{
	message = error_message;
	return error;
}


void Samurai::IO::Net::Socks5Handshake::closed()
{
	if (status == Status::Done || status == Status::Failed) return;
	fail(SocketError::ProxyProtocol, "Proxy closed the connection during the handshake");
}


Samurai::IO::Net::Socks5Handshake::Status
Samurai::IO::Net::socks5_pump(Samurai::IO::Net::Socks5Handshake& handshake, socket_t sd,
                              bool& want_write, std::error_code& ec)
{
	using Status = Samurai::IO::Net::Socks5Handshake::Status;

	want_write = false;
	ec.clear();

	while (true)
	{
		if (handshake.getStatus() == Status::NeedWrite)
		{
			const std::string_view pending = handshake.outgoing();
			const ssize_t sent = ::send(sd, pending.data(), pending.size(),
			                            Samurai::IO::Net::send_flags);

			if (sent < 0)
			{
				const int err = Samurai::IO::Net::net_error();
				if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
				{
					want_write = true;
					return handshake.getStatus();
				}
				ec = Samurai::system_error(err);
				return handshake.getStatus();
			}

			handshake.consumeOutgoing((size_t) sent);

			/* A short write leaves the rest for the write notifier. */
			if (handshake.getStatus() == Status::NeedWrite)
			{
				want_write = true;
				return handshake.getStatus();
			}

			continue;
		}

		if (handshake.getStatus() == Status::NeedRead)
		{
			char buf[Samurai::IO::Net::Socks5Handshake::MAX_STEP];
			const size_t want = handshake.wanted();

			/* wanted() is bounded by the grammar, so this cannot be exceeded
			   unless the handshake is in a state it should not be in. */
			if (!want || want > sizeof(buf))
			{
				handshake.closed();
				return handshake.getStatus();
			}

			const ssize_t got = ::recv(sd, buf, want, 0);

			if (got < 0)
			{
				const int err = Samurai::IO::Net::net_error();
				if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
					return handshake.getStatus();
				ec = Samurai::system_error(err);
				return handshake.getStatus();
			}

			if (got == 0)
			{
				handshake.closed();
				return handshake.getStatus();
			}

			size_t consumed = 0;
			handshake.feed(buf, (size_t) got, consumed);
			continue;
		}

		return handshake.getStatus();
	}
}

// eof
