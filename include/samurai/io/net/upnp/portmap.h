/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UPNP_PORTMAP_H
#define HAVE_SAMURAI_UPNP_PORTMAP_H

#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/upnp/error.h>
#include <samurai/io/net/upnp/portmapper.h>
#include <samurai/io/net/url.h>

#include <chrono>
#include <string>
#include <string_view>

namespace Samurai {
namespace IO {
namespace Net {
namespace UPnP {

/** What a blocking call came back with. */
struct MapResult
{
	Error error = Error::None;
	/** Meaningful when 'error' is Error::Device. */
	DeviceError deviceError = DeviceError::Unknown;
	/** The code as the gateway sent it, recognised or not. */
	uint16_t code = 0;

	/** The port that was granted, which need not be the one asked for. */
	uint16_t externalPort = 0;
	InetAddress externalAddress;
	InetAddress internalClient;

	bool ok() const { return error == Error::None; }
};

/**
 * Add a port mapping, and wait for the answer.
 *
 * This is the whole of the utility in one call: it finds the gateway, reads its
 * description, asks for the mapping and comes back with the result.
 *
 * MUST NOT be called from inside a socket or timer callback, nor from a thread
 * that is already running the event loop. It drives the process-wide
 * SocketMonitor and TimerManager itself, so from inside a handler it would
 * dispatch that loop's events re-entrantly - and a handler running inside
 * another handler can destroy the socket whose callback is on the stack. Call it
 * from main() during startup, before the application's own loop begins, or use
 * Gateway and PortMapper directly.
 *
 * A nested call is refused with Error::Reentrant rather than allowed to recurse.
 * A loop this does not know about cannot be detected at all, which is why the
 * name carries the hazard to every call site.
 *
 * @param externalPort the port wanted on the outside, or 0 to let the gateway
 *        choose. Look at MapResult::externalPort for what was granted.
 */
MapResult mapPortBlocking(Protocol protocol, uint16_t internalPort,
                          uint16_t externalPort, std::string_view description,
                          std::chrono::seconds lease = std::chrono::seconds(7200),
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(15000));

/** Remove a mapping. The same warning applies. */
MapResult unmapPortBlocking(Protocol protocol, uint16_t externalPort,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(10000));

/** Ask the gateway for the address the world sees. The same warning applies. */
MapResult getExternalAddressBlocking(
	std::chrono::milliseconds timeout = std::chrono::milliseconds(10000));

/**
 * Use this description URL instead of searching, for every later blocking call.
 *
 * For a caller that keeps the gateway's URL in its configuration, and what makes
 * the blocking calls usable before SSDP discovery is wired up. An invalid URL
 * clears it.
 */
void setBlockingGateway(const URL& location);

/** Forget the cached gateway, so the next blocking call finds it again. */
void resetBlockingCache();

}
}
}
}

#endif // HAVE_SAMURAI_UPNP_PORTMAP_H
