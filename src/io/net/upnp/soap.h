/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UPNP_SOAP_H
#define HAVE_SAMURAI_UPNP_SOAP_H

#include <samurai/io/net/upnp/error.h>
#include <samurai/io/net/upnp/gateway.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Samurai {
namespace IO {
namespace Net {
namespace UPnP {

/** A fault, as it arrived. */
struct ActionFault
{
	DeviceError error = DeviceError::Unknown;
	/** The code as sent, so a vendor's own survives not being recognised. */
	uint16_t code = 0;
	std::string description;
};

namespace Soap {

/**
 * The SOAP envelope for an action, ready to be a request body.
 *
 * 'serviceType' has to be the string the description advertised, byte for byte:
 * it goes into the SOAPAction header as well, and gateways compare it exactly.
 */
std::string buildRequest(std::string_view serviceType, std::string_view action,
                         std::span<const Argument> arguments);

/** The value for the SOAPAction header, quotes included. */
std::string buildActionHeader(std::string_view serviceType, std::string_view action);

/**
 * Read a response body.
 *
 * A fault is looked for whatever the HTTP status was: some gateways return
 * <UPnPError> with a 200, so branching on the status first loses them.
 *
 * The single element under <Body> is taken as the response without requiring it
 * to be named after the action, because devices misname it. Matching throughout
 * is on the local name, since the namespace prefixes are the vendor's choice.
 *
 * @param fault set when the device reported one, in which case the result is
 *              left empty.
 * @return BadResponse when the body is not a SOAP envelope at all, Device when
 *         a fault was read, None otherwise.
 */
Error parseResponse(std::string_view body, ActionResult& result,
                    std::optional<ActionFault>& fault);

}

}
}
}
}

#endif // HAVE_SAMURAI_UPNP_SOAP_H
