/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

namespace Samurai {
namespace Media {
namespace Video {

struct Size
{
	size_t width;
	size_t height;
};

class DeviceHandle { }

class VideoFormatInfo
{

};

enum Capability
{
	Capa_None  = 0,
	Capa_Audio = 1, /**<<< "Device has audio capabilities" */
	Capa_Tuner = 2, /**<<< "Device has frequency tuner capabilities (TV)" */
	Capa_Radio = 3, /**<<< "Device has radio capabilities" */
};


class Device
{
	public:
		Device(const DeviceHandle& handle);
		~Device();
		
		const DeviceHandle& getHandle();
		std::string getName() const = 0;
};

}
}
}