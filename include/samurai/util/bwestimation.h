/*
 * Copyright (C) 2001-2006 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_BANDWIDTH_ESTIMATION_H
#define HAVE_QUICKDC_BANDWIDTH_ESTIMATION_H

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
		virtual ~RateEstimator();
		
		void add(size_t bytesTransfered);
		size_t getBps();
		
	private:
		time_t current;
		size_t last;
		size_t* log;
};

}
}

#endif // HAVE_QUICKDC_BANDWIDTH_ESTIMATION_H
