/*
 * Copyright (C) 2001-2008 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/socketmonitor.h>
#include <samurai/io/net/interface.h>
#include <samurai/io/net/inetaddress.h>
#include <samurai/io/net/hardwareaddress.h>
#include <vector>
#include <stdio.h>

bool arg_show_all = false;

int main(int argc, char** argv)
{
	(void) argc;
	(void) argv;
	
	for (int n = 0; n < argc; n++)
	{
		if (strcmp(argv[n], "-a") == 0 || strcmp(argv[n], "--all") == 0)
			arg_show_all = true;
	}
	
#ifdef SAMURAI_WINDOWS
	if (!Samurai::IO::Net::SocketMonitor::getInstance())
	{
		fprintf(stderr, "Unable to initialize winsock\n");
		exit(1);
	}
#endif

	std::vector<Samurai::IO::Net::NetworkInterface*> interfaces;
	bool ok = Samurai::IO::Net::NetworkInterface::getInterfaces(interfaces);
	if (!ok)
	{
		fprintf(stderr, "Unable to obtain interface list\n");
		return 1;
	}
	
	
	std::vector<Samurai::IO::Net::NetworkInterface*>::iterator it = interfaces.begin();
	for (; it != interfaces.end(); it++)
	{
		Samurai::IO::Net::NetworkInterface* iface = (*it);
		Samurai::IO::Net::HardwareAddress* hwaddr = iface->getHWAddress();
		
		if (iface->isEnabled() || arg_show_all)
		{
			printf("Interface:            %s\n", iface->getName());
			
			printf("        Enabled:      %s\n", iface->isEnabled() ? "yes" : "no");
			printf("        Multicast:    %s\n", iface->isMulticast() ? "yes" : "no");
			printf("        Loopback:     %s\n", iface->isLoopback() ? "yes" : "no");
			printf("        MTU:          %d\n", iface->getMtu());
			
			if (iface->getAddress())
				printf("        Address:      %s\n", iface->getAddress()->toString());
			
			if (iface->getNetmask())
				printf("        Netmask:      %s\n", iface->getNetmask()->toString());
			
			if (iface->isBroadcast() && iface->getBroadcastAddress())
				printf("        Broadcast:    %s\n", iface->getBroadcastAddress()->toString());
			
			
			if (iface->isPointToPoint() && iface->getDestinationAddress())
				printf("        Destination:  %s\n", iface->getDestinationAddress()->toString());
			
			if (hwaddr)
				printf("        HW-address:   %s\n", hwaddr->getAddress());
				
			printf("\n");
		}
	}
	
	return 0;
}

