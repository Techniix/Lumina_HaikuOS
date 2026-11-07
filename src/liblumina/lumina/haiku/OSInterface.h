/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Haiku implementation of Lumina's OS interface (the replacement for
 * Lumina's LuminaOS-<OS>.cpp shell-outs). Native kernel/kit calls only.
 * Every subsystem has a Supported() check, mirroring Lumina 2's
 * framework-OSInterface capability design.
 */
#ifndef LUMINA_HAIKU_OS_INTERFACE_H
#define LUMINA_HAIKU_OS_INTERFACE_H

#include <OS.h>

#include <string>
#include <vector>

namespace lumina {
namespace haiku {

struct BatteryStatus {
	bool	present = false;
	bool	charging = false;
	bool	critical = false;
	int		percent = -1;
	int		minutesLeft = -1;
};

class OSInterface {
public:
	OSInterface();

	// CPU usage since the previous call, 0..100 (-1 on first call/error).
	int CpuUsagePercent();
	int CpuCount() const;
	int MemoryUsagePercent() const;
	uint64 MemoryUsedBytes() const;
	uint64 MemoryTotalBytes() const;
	// Percent of capacity used on the volume containing `path`.
	int DiskUsagePercent(const char* path = "/boot") const;

	bool BatterySupported() const;
	BatteryStatus Battery() const;

	bool BrightnessSupported() const;
	int Brightness() const;			// 0..100, -1 unsupported
	bool SetBrightness(int percent);

	std::string HaikuRevision() const;

private:
	std::vector<bigtime_t>	fLastActive;
	bigtime_t				fLastSample = 0;
};

}	// namespace haiku
}	// namespace lumina

#endif
