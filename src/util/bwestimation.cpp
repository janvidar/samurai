/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/util/bwestimation.h>

 /* measure bandwidth over 3 seconds. Note MUST be > 1. */
#define BANDWIDTH_ESTIMATION_TIMEOUT 3

Samurai::Util::RateEstimator::RateEstimator() {
	current = 0;
	log = new size_t[BANDWIDTH_ESTIMATION_TIMEOUT];
	for (size_t i = 0; i < BANDWIDTH_ESTIMATION_TIMEOUT; i++) 
		log[i] = 0;
}

Samurai::Util::RateEstimator::~RateEstimator() {
	delete[] log;
}

void Samurai::Util::RateEstimator::add(size_t bytesTransfered) {
	time_t now = time(0);
	if (now > current + BANDWIDTH_ESTIMATION_TIMEOUT) {
		for (size_t i = 0; i < BANDWIDTH_ESTIMATION_TIMEOUT; i++) 
			log[i] = 0;
	}
	
	if (now != current) {
		last = log[now % BANDWIDTH_ESTIMATION_TIMEOUT];
		log[now % BANDWIDTH_ESTIMATION_TIMEOUT] = 0;
		current = now;
	}
	log[current % BANDWIDTH_ESTIMATION_TIMEOUT] += bytesTransfered;
}

size_t Samurai::Util::RateEstimator::getBps() {
	time_t now = time(0);
	size_t bps = 0;
	
	if (now > current + BANDWIDTH_ESTIMATION_TIMEOUT)
		return 0;
	
	if (now != current) {
		for (size_t i = 0; i < BANDWIDTH_ESTIMATION_TIMEOUT; i++)
			bps += log[i];

	} else {

		for (time_t i = 0; i < BANDWIDTH_ESTIMATION_TIMEOUT; i++) {
			if ((now % BANDWIDTH_ESTIMATION_TIMEOUT) != i)
				bps += log[i];
		}
		bps += last;
	}
	
	return bps / BANDWIDTH_ESTIMATION_TIMEOUT;
}

