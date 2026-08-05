/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/io/net/bandwidth.h>

/*
 * A function-local static is initialised once, and without the check-then-assign
 * race the raw pointer version had.
 */
Samurai::IO::Net::BandwidthManager* Samurai::IO::Net::BandwidthManager::getInstance()
{
	static Samurai::IO::Net::BandwidthManager manager;
	return &manager;
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
