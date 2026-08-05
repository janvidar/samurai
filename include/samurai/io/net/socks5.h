/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_SOCKS5_H
#define HAVE_SAMURAI_SOCKS5_H

#include <samurai/samurai.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/proxy.h>
#include <samurai/io/net/socketbase.h>

#include <string>
#include <string_view>
#include <system_error>

namespace Samurai {
namespace IO {
namespace Net {

/**
 * The SOCKS5 negotiation as a codec: bytes in, bytes out, no socket.
 *
 * Kept apart from Socket so that the wire format can be tested without a
 * connection to test it over - a reply arriving one byte at a time is the case
 * that breaks a parser, and it is tedious to arrange through a real proxy.
 *
 * The sequence is a greeting, optionally a username/password exchange, then one
 * request and one reply. Drive it by alternating on getStatus():
 *
 *     while (true) {
 *         if (status == Status::NeedWrite) {
 *             n = send(outgoing());        // partial writes are normal
 *             handshake.consumeOutgoing(n);
 *             if (handshake.getStatus() == Status::NeedWrite) return;  // more later
 *         }
 *         if (status == Status::NeedRead) {
 *             n = recv(buf, handshake.wanted());   // at most wanted(), see below
 *             status = handshake.feed(buf, n, consumed);
 *             continue;
 *         }
 *         break;                           // Done or Failed
 *     }
 */
class Socks5Handshake {
	public:
		/**
		 * The commands this speaks.
		 *
		 * CONNECT is RFC 1928. Resolve and ResolvePtr are Tor's extensions,
		 * which return an answer instead of opening a stream.
		 *
		 * BIND (0x02) and UDP ASSOCIATE (0x03) are absent deliberately, not for
		 * want of time: a command that cannot be named cannot be sent, so there
		 * is no path by which a datagram or an inbound connection is arranged
		 * through the proxy. Tor implements neither in any case.
		 */
		enum class Command : uint8_t
		{
			Connect    = 0x01,
			Resolve    = 0xF0,
			ResolvePtr = 0xF1
		};

		enum class Status
		{
			NeedWrite,  /**<< outgoing() has bytes that have not gone out */
			NeedRead,   /**<< waiting for the next wanted() bytes */
			Done,       /**<< the reply arrived; see getResult() */
			Failed      /**<< see getError() */
		};

		/**
		 * @param target for Connect and Resolve, the peer's name or address
		 *        literal, which is sent as it was given and never resolved
		 *        here. For ResolvePtr, an address literal.
		 * @param port the peer's port. Ignored by the two lookup commands,
		 *        which the protocol still requires a port field for.
		 *
		 * A target that cannot be encoded leaves getStatus() at Status::Failed
		 * and produces no bytes at all: over 255 bytes (which is what the single
		 * length octet in front of a name can express), empty, or - for
		 * ResolvePtr - not an address literal.
		 */
		Socks5Handshake(const ProxySettings& proxy, Command command,
		                const std::string& target, uint16_t port);

		Status getStatus() const { return status; }

		/** Bytes still waiting to go out. Empty unless NeedWrite. */
		std::string_view outgoing() const;

		/** Retire bytes that were accepted by the socket. */
		void consumeOutgoing(size_t length);

		/**
		 * How many bytes the grammar needs next, and never more than that.
		 *
		 * Read exactly this many. Reading further would swallow bytes belonging
		 * to the peer rather than to the proxy: a CONNECT reply is routinely
		 * followed in the same segment by the target's first bytes, and this
		 * class has nowhere to hand them back to - a socket that has just
		 * finished a handshake has no buffer above its descriptor. Bounding the
		 * read costs a handful of extra recv() calls per connection and removes
		 * the whole question.
		 *
		 * Zero when the status is not NeedRead.
		 */
		size_t wanted() const;

		/**
		 * Feed what arrived.
		 *
		 * @param consumed set to the bytes taken, which is 'length' whenever the
		 *        caller honoured wanted(). Anything left over belongs to the
		 *        peer and this class has not looked at it.
		 */
		Status feed(const char* data, size_t length, size_t& consumed);

		struct Result
		{
			/** The REP octet. Zero on success. */
			uint8_t reply = 0;

			/**
			 * BND.ADDR and BND.PORT, or - for a lookup command - the answer.
			 *
			 * For CONNECT this is the proxy's own bound address and not the
			 * peer's: Tor answers 0.0.0.0, and a proxy that answers something
			 * else is describing its own end of the connection. Nothing should
			 * treat it as where the peer is.
			 */
			InetAddress address;
			/** An ATYP=3 reply, which is what RESOLVE_PTR answers with. */
			std::string name;
			uint16_t port = 0;
		};

		const Result& getResult() const { return result; }

		/**
		 * What went wrong, translated into the vocabulary a socket handler
		 * already branches on. Only meaningful once the status is Failed.
		 */
		SocketError getError(const char*& message) const;

		/** The reply code an unsuccessful exchange returned, or 0. */
		uint8_t getReplyCode() const { return result.reply; }

		/**
		 * The proxy hung up before the exchange finished.
		 *
		 * Reported as a proxy fault rather than as a closed connection, because
		 * a closed connection would say the peer had been reached. It had not:
		 * the proxy never confirmed it.
		 */
		void closed();

		/** The most bytes a single step can ask for: a 255-byte name and its
		 *  length octet. A driver can size one buffer from this. */
		static constexpr size_t MAX_STEP = 256;

	private:
		enum class Step
		{
			MethodReply,   /**<< VER METHOD */
			AuthReply,     /**<< VER STATUS */
			ReplyHead,     /**<< VER REP RSV ATYP */
			ReplyAddrLen,  /**<< the length octet of an ATYP=3 address */
			ReplyAddr,     /**<< the address itself */
			ReplyPort      /**<< BND.PORT */
		};

		void encodeGreeting();
		void encodeAuth();
		bool encodeRequest();
		void queue(const std::string& bytes);
		void expect(Step next, size_t bytes);
		void fail(SocketError code, const char* message);
		bool consumeStep(const char* data, size_t length);

		ProxySettings settings;
		Command command;
		std::string target;
		uint16_t target_port;

		Status status = Status::NeedWrite;
		Step step = Step::MethodReply;

		std::string out;
		size_t out_sent = 0;

		/*
		 * The request, encoded once in the constructor so that one that cannot
		 * be expressed is refused before the proxy has been contacted - and, in
		 * particular, before it has been told the peer's name. It is not queued
		 * behind the greeting: the proxy may answer the greeting by demanding
		 * credentials, and a request sent before that exchange would be read as
		 * part of it.
		 */
		std::string request;

		/* Partial steps accumulate here. Never larger than the longest single
		   step, because the driver reads no more than wanted(). */
		std::string in;
		size_t need = 0;

		/* Which method the proxy chose, kept so that the reply parser knows
		   whether an authentication exchange still has to happen. */
		uint8_t method = 0;
		/* ATYP of the reply, and the address length it implies. */
		uint8_t reply_atyp = 0;

		Result result;
		SocketError error = SocketError::SocketUnknown;
		const char* error_message = "";
};

/**
 * Move a handshake along over a descriptor as far as it will go, then stop.
 *
 * Shared by Socket, which negotiates a tunnel, and SocksResolver, which asks the
 * same proxy a question instead. Only the shuffling of bytes is here: what to do
 * about the outcome differs between the two, and stays with them.
 *
 * Uses send() and recv() rather than Socket's own read() and write(), which
 * refuse unless the socket is Connected - which it deliberately is not yet - and
 * would account these bytes as payload when they are setup.
 *
 * @param want_write set when bytes are still queued that the socket would not
 *        take, so the caller can arm its write notifier.
 * @param ec set when the descriptor itself failed. A proxy that hung up is not
 *        a descriptor failure: it comes back as Status::Failed, with
 *        Socks5Handshake::getError() explaining it.
 * @return the status the handshake stopped at. NeedRead means it is waiting for
 *         the socket to become readable again.
 */
Socks5Handshake::Status socks5_pump(Socks5Handshake& handshake, socket_t sd,
                                   bool& want_write, std::error_code& ec);

}
}
}

#endif // HAVE_SAMURAI_SOCKS5_H
