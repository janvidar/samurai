/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/bandwidth.h>

Samurai::IO::Net::BandwidthManager* Samurai::IO::Net::BandwidthManager::instance = 0;

Samurai::IO::Net::BandwidthManager* Samurai::IO::Net::BandwidthManager::getInstance()
{
	if (!instance)
		instance =  new BandwidthManager();
	
	return instance;
}

Samurai::IO::Net::BandwidthManager::BandwidthManager() {

}

Samurai::IO::Net::BandwidthManager::~BandwidthManager() {

}

void Samurai::IO::Net::BandwidthManager::accept()    { count_accepted++; }
void Samurai::IO::Net::BandwidthManager::connected() { count_connected++; }
void Samurai::IO::Net::BandwidthManager::error()     { count_errors++; }

void Samurai::IO::Net::BandwidthManager::dataSendTCP(size_t bytes) {
	data_tcp_tx += bytes;
	estimator_send.add(bytes);
}

void Samurai::IO::Net::BandwidthManager::dataSendUDP(size_t bytes) {
	data_udp_tx += bytes;
	estimator_send.add(bytes);
}

void Samurai::IO::Net::BandwidthManager::dataRecvTCP(size_t bytes) {
	data_tcp_rx += bytes;
	estimator_recv.add(bytes);
}

void Samurai::IO::Net::BandwidthManager::dataRecvUDP(size_t bytes) {
	data_udp_rx += bytes;
	estimator_recv.add(bytes);
}

size_t Samurai::IO::Net::BandwidthManager::getSendBps() {
	return estimator_send.getBps();
}

size_t Samurai::IO::Net::BandwidthManager::getRecvBps() {
	return estimator_recv.getBps();
}
