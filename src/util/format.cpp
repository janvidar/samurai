#include <samurai/util/format.h>
#include <format>

std::string Samurai::Util::formatSize(uint64_t size)
{
	static const char* const units[] = { " B", "KB", "MB", "GB", "TB", "PB", "EB" };
	constexpr size_t last_unit = (sizeof(units) / sizeof(units[0])) - 1;

	/* Stop at the last unit: one more multiplication would overflow uint64_t,
	   which wraps the divisor to zero and never terminates. */
	size_t index = 0;
	uint64_t unit = 1;
	while (index < last_unit && size / 1024 >= unit)
	{
		unit *= 1024;
		index++;
	}

	if (size % unit == 0)
		return std::format("{:>4} {}", size / unit, units[index]);

	return std::format("{:.2f} {}", (double) size / (double) unit, units[index]);
}
