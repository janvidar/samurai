/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/util/bwestimation.h>

void Samurai::Util::RateEstimator::add(size_t bytesTransfered) {
	const time_t now = time(nullptr);
	if (now > current + (time_t) BANDWIDTH_ESTIMATION_TIMEOUT)
		log.fill(0);

	if (now != current) {
		last = log[now % BANDWIDTH_ESTIMATION_TIMEOUT];
		log[now % BANDWIDTH_ESTIMATION_TIMEOUT] = 0;
		current = now;
	}
	log[current % BANDWIDTH_ESTIMATION_TIMEOUT] += bytesTransfered;
}

size_t Samurai::Util::RateEstimator::getBps() {
	const time_t now = time(nullptr);
	size_t bps = 0;

	if (now > current + (time_t) BANDWIDTH_ESTIMATION_TIMEOUT)
		return 0;

	if (now != current) {
		for (size_t total : log)
			bps += total;

	} else {
		/* The bucket for the current second is still filling, so the previous
		   second's total stands in for it. */
		for (size_t i = 0; i < BANDWIDTH_ESTIMATION_TIMEOUT; i++) {
			if ((size_t) (now % BANDWIDTH_ESTIMATION_TIMEOUT) != i)
				bps += log[i];
		}
		bps += last;
	}

	return bps / BANDWIDTH_ESTIMATION_TIMEOUT;
}
