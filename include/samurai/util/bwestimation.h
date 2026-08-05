/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_BANDWIDTH_ESTIMATION_H
#define HAVE_SAMURAI_BANDWIDTH_ESTIMATION_H

#include <time.h>
#include <array>
#include <cstddef>

namespace Samurai {
namespace Util {

/* Bandwidth is averaged over this many seconds. Must be greater than 1. */
inline constexpr size_t BANDWIDTH_ESTIMATION_TIMEOUT = 3;

/**
 * This class will handle flow estimation of bytes transfered per second,
 * over a recent average of seconds.
 */
class RateEstimator {
	public:
		RateEstimator() = default;

		void add(size_t bytesTransfered);
		size_t getBps();

	private:
		time_t current = 0;
		/* Carries the previous second's total across a tick, so that getBps()
		 * does not report a dip while the current second is still filling. */
		size_t last = 0;
		/* One bucket per second of the averaging window. Held inline: the size
		 * is a compile time constant, so there is nothing to allocate, and the
		 * class stays copyable. */
		std::array<size_t, BANDWIDTH_ESTIMATION_TIMEOUT> log = {};
};

}
}

#endif // HAVE_SAMURAI_BANDWIDTH_ESTIMATION_H
