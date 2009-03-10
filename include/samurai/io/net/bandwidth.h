/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SYSTEM_BANDWIDTH_MANAGER_H
#define HAVE_SYSTEM_BANDWIDTH_MANAGER_H

#include <samurai/samurai.h>
#include <samurai/util/bwestimation.h>

namespace Samurai {
namespace Util {
	class RateEstimator;
}
namespace IO {
namespace Net {

class BandwidthManager {
	public:
		BandwidthManager();
		virtual ~BandwidthManager();
		
		void accept();
		void connected();
		void error();
		
		void dataSendTCP(size_t bytes);
		void dataSendUDP(size_t bytes);
		void dataRecvTCP(size_t bytes);
		void dataRecvUDP(size_t bytes);

		size_t getSendBps();
		size_t getRecvBps();
		
		static BandwidthManager* getInstance();

	protected:
		size_t count_accepted;
		size_t count_connected;
		size_t count_errors;
		
		size_t data_tcp_tx;
		size_t data_tcp_rx;
		size_t data_udp_tx;
		size_t data_udp_rx;
		
		size_t rate_peak;
		Samurai::Util::RateEstimator estimator_send;
		Samurai::Util::RateEstimator estimator_recv;
		
		static BandwidthManager* instance;
};

} // NET
} // IO
} // Samurai

#endif // HAVE_SYSTEM_BANDWIDTH_MANAGER_H
