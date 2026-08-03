/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_UPNP_ERROR_H
#define HAVE_SAMURAI_UPNP_ERROR_H

#include <stdint.h>

namespace Samurai {
namespace IO {
namespace Net {
namespace UPnP {

/** Why an operation did not produce an answer, or None when it did. */
enum class Error
{
	None,
	NoGateway,          /**< no internet gateway answered the search */
	DescriptionFailed,  /**< the device description could not be fetched or parsed */
	NoService,          /**< the device has no connection service to talk to */
	Network,            /**< the exchange failed below the protocol */
	BadResponse,        /**< the device answered with something unreadable */
	Unsupported,        /**< the device does not implement what was asked */
	Cancelled,
	Reentrant,          /**< a blocking call was made from inside the event loop */
	Device              /**< the device refused; see the DeviceError alongside */
};

const char* toString(Error error);

/**
 * The error codes an internet gateway returns inside a SOAP fault.
 *
 * The numeric code is kept alongside this wherever it is reported, because a
 * vendor is free to invent one and folding an unknown code into Unknown would
 * throw away the only thing that could identify it.
 */
enum class DeviceError : uint16_t
{
	Unknown                          = 0,
	InvalidAction                    = 401,
	InvalidArgs                      = 402,
	ActionFailed                     = 501,
	ArgumentValueInvalid             = 600,
	ArgumentValueOutOfRange          = 601,
	OptionalActionNotImplemented     = 602,
	OutOfMemory                      = 603,
	HumanInterventionRequired        = 604,
	StringArgumentTooLong            = 605,
	ActionNotAuthorized              = 606,
	SignatureFailure                 = 607,
	/* Also spelled NoSuchEntry by the firewall service, which uses the same
	   number for the same meaning. */
	SpecifiedArrayIndexInvalid       = 713,
	NoSuchEntryInArray               = 714,
	WildCardNotPermittedInSrcIP      = 715,
	WildCardNotPermittedInExtPort    = 716,
	ConflictInMappingEntry           = 718,
	SamePortValuesRequired           = 724,
	OnlyPermanentLeasesSupported     = 725,
	RemoteHostOnlySupportsWildcard   = 726,
	ExternalPortOnlySupportsWildcard = 727,
	NoPortMapsAvailable              = 728,
	ConflictWithOtherMechanisms      = 729,
	WildCardNotPermittedInIntPort    = 732
};

/** The enumerator for a code, or Unknown when it is not one of these. */
DeviceError toDeviceError(uint16_t code);

const char* toString(DeviceError error);

}
}
}
}

#endif // HAVE_SAMURAI_UPNP_ERROR_H
