/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

namespace Samurai {
namespace Media {
namespace Video {

class DeviceList
{
	private:
		static DeviceList* instance;
		std::vector<Device*> devices;
		DeviceList();
		void scan();
		
	public:
		static CVideoCollector *Instance();
		size_t countDevices() const;
		VideoDevice* getDevice(size_t n);
};


}
}
}