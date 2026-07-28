/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_BANDWIDTH_ESTIMATION_H
#define HAVE_SAMURAI_BANDWIDTH_ESTIMATION_H

#include <time.h>

namespace Samurai {
namespace Util {

/**
 * This class will handle flow estimation of bytes transfered per second,
 * over a recent average of seconds.
 */
class RateEstimator {
	public:
		RateEstimator();
		~RateEstimator();
		
		void add(size_t bytesTransfered);
		size_t getBps();
		
	private:
		time_t current = 0;
		/* Carries the previous second's total across a tick, so that getBps()
		 * does not report a dip while the current second is still filling. */
		size_t last = 0;
		size_t* log;
};

}
}

#endif // HAVE_SAMURAI_BANDWIDTH_ESTIMATION_H
