/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/net/upnp/error.h>

const char* Samurai::IO::Net::UPnP::toString(Samurai::IO::Net::UPnP::Error error)
{
	switch (error)
	{
		case Error::None:              return "ok";
		case Error::NoGateway:         return "no internet gateway answered";
		case Error::DescriptionFailed: return "the device description could not be read";
		case Error::NoService:         return "the gateway has no connection service";
		case Error::Network:           return "the exchange with the gateway failed";
		case Error::BadResponse:       return "the gateway answered with something unreadable";
		case Error::Unsupported:       return "the gateway does not implement this";
		case Error::Cancelled:         return "cancelled";
		case Error::Reentrant:         return "called from inside the event loop";
		case Error::Device:            return "the gateway refused the request";
	}
	return "unknown error";
}


Samurai::IO::Net::UPnP::DeviceError
Samurai::IO::Net::UPnP::toDeviceError(uint16_t code)
{
	switch (code)
	{
		case 401: return DeviceError::InvalidAction;
		case 402: return DeviceError::InvalidArgs;
		case 501: return DeviceError::ActionFailed;
		case 600: return DeviceError::ArgumentValueInvalid;
		case 601: return DeviceError::ArgumentValueOutOfRange;
		case 602: return DeviceError::OptionalActionNotImplemented;
		case 603: return DeviceError::OutOfMemory;
		case 604: return DeviceError::HumanInterventionRequired;
		case 605: return DeviceError::StringArgumentTooLong;
		case 606: return DeviceError::ActionNotAuthorized;
		case 607: return DeviceError::SignatureFailure;
		case 713: return DeviceError::SpecifiedArrayIndexInvalid;
		case 714: return DeviceError::NoSuchEntryInArray;
		case 715: return DeviceError::WildCardNotPermittedInSrcIP;
		case 716: return DeviceError::WildCardNotPermittedInExtPort;
		case 718: return DeviceError::ConflictInMappingEntry;
		case 724: return DeviceError::SamePortValuesRequired;
		case 725: return DeviceError::OnlyPermanentLeasesSupported;
		case 726: return DeviceError::RemoteHostOnlySupportsWildcard;
		case 727: return DeviceError::ExternalPortOnlySupportsWildcard;
		case 728: return DeviceError::NoPortMapsAvailable;
		case 729: return DeviceError::ConflictWithOtherMechanisms;
		case 732: return DeviceError::WildCardNotPermittedInIntPort;
	}
	return DeviceError::Unknown;
}


const char* Samurai::IO::Net::UPnP::toString(Samurai::IO::Net::UPnP::DeviceError error)
{
	switch (error)
	{
		case DeviceError::Unknown:
			return "unrecognised error code";
		case DeviceError::InvalidAction:
			return "the service does not have this action";
		case DeviceError::InvalidArgs:
			return "wrong number or type of arguments";
		case DeviceError::ActionFailed:
			return "the action failed";
		case DeviceError::ArgumentValueInvalid:
			return "an argument value is invalid";
		case DeviceError::ArgumentValueOutOfRange:
			return "an argument value is out of range";
		case DeviceError::OptionalActionNotImplemented:
			return "this optional action is not implemented";
		case DeviceError::OutOfMemory:
			return "the device is out of memory";
		case DeviceError::HumanInterventionRequired:
			return "the device needs attention before it can do this";
		case DeviceError::StringArgumentTooLong:
			return "a string argument is too long";
		case DeviceError::ActionNotAuthorized:
			return "the action is not authorized";
		case DeviceError::SignatureFailure:
			return "the signature was rejected";
		case DeviceError::SpecifiedArrayIndexInvalid:
			return "the array index is past the end";
		case DeviceError::NoSuchEntryInArray:
			return "there is no such mapping";
		case DeviceError::WildCardNotPermittedInSrcIP:
			return "a wildcard remote host is not permitted";
		case DeviceError::WildCardNotPermittedInExtPort:
			return "a wildcard external port is not permitted";
		case DeviceError::ConflictInMappingEntry:
			return "the port is already mapped by someone else";
		case DeviceError::SamePortValuesRequired:
			return "the internal and external ports have to match";
		case DeviceError::OnlyPermanentLeasesSupported:
			return "only permanent leases are supported";
		case DeviceError::RemoteHostOnlySupportsWildcard:
			return "the remote host has to be a wildcard";
		case DeviceError::ExternalPortOnlySupportsWildcard:
			return "the external port has to be a wildcard";
		case DeviceError::NoPortMapsAvailable:
			return "the gateway has no room for another mapping";
		case DeviceError::ConflictWithOtherMechanisms:
			return "the mapping conflicts with one made another way";
		case DeviceError::WildCardNotPermittedInIntPort:
			return "a wildcard internal port is not permitted";
	}
	return "unrecognised error code";
}
