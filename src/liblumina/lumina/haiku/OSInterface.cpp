/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "OSInterface.h"

#include <fcntl.h>
#include <fs_info.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <Screen.h>

#include "../../compat/haiku_private/power_managment.h"
#include "PrivateApi.h"

namespace lumina {
namespace haiku {

namespace {
const char* kBatteryDir = "/dev/power/acpi_battery";

int
OpenFirstBattery()
{
	BDirectory dir(kBatteryDir);
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;
		int fd = open(path.Path(), O_RDONLY);
		if (fd >= 0)
			return fd;
	}
	return -1;
}
}	// namespace


OSInterface::OSInterface()
{
}


int
OSInterface::CpuCount() const
{
	system_info info;
	if (get_system_info(&info) != B_OK)
		return 1;
	return int(info.cpu_count);
}


int
OSInterface::CpuUsagePercent()
{
	int count = CpuCount();
	std::vector<cpu_info> cpus(static_cast<size_t>(count));
	if (get_cpu_info(0, uint32(count), cpus.data()) != B_OK)
		return -1;
	bigtime_t now = system_time();
	int result = -1;
	if (fLastSample > 0 && int(fLastActive.size()) == count) {
		bigtime_t elapsed = now - fLastSample;
		bigtime_t busy = 0;
		for (int i = 0; i < count; i++)
			busy += cpus[size_t(i)].active_time - fLastActive[size_t(i)];
		if (elapsed > 0) {
			result = int(busy * 100 / (elapsed * count));
			result = result < 0 ? 0 : result > 100 ? 100 : result;
		}
	}
	fLastActive.resize(size_t(count));
	for (int i = 0; i < count; i++)
		fLastActive[size_t(i)] = cpus[size_t(i)].active_time;
	fLastSample = now;
	return result;
}


uint64
OSInterface::MemoryUsedBytes() const
{
	system_info info;
	if (get_system_info(&info) != B_OK)
		return 0;
	return uint64(info.used_pages) * B_PAGE_SIZE;
}


uint64
OSInterface::MemoryTotalBytes() const
{
	system_info info;
	if (get_system_info(&info) != B_OK)
		return 0;
	return uint64(info.max_pages) * B_PAGE_SIZE;
}


int
OSInterface::MemoryUsagePercent() const
{
	uint64 total = MemoryTotalBytes();
	if (total == 0)
		return -1;
	return int(MemoryUsedBytes() * 100 / total);
}


int
OSInterface::DiskUsagePercent(const char* path) const
{
	dev_t device = dev_for_path(path);
	fs_info info;
	if (device < 0 || fs_stat_dev(device, &info) != 0 || info.total_blocks <= 0)
		return -1;
	return int((info.total_blocks - info.free_blocks) * 100 / info.total_blocks);
}


bool
OSInterface::BatterySupported() const
{
	int fd = OpenFirstBattery();
	if (fd < 0)
		return false;
	close(fd);
	return true;
}


BatteryStatus
OSInterface::Battery() const
{
	BatteryStatus status;
	int fd = OpenFirstBattery();
	if (fd < 0)
		return status;
	acpi_battery_info info;
	acpi_extended_battery_info extended;
	if (ioctl(fd, GET_BATTERY_INFO, &info, sizeof(info)) == 0
		&& ioctl(fd, GET_EXTENDED_BATTERY_INFO, &extended, sizeof(extended))
			== 0) {
		status.present = true;
		status.charging = (info.state & BATTERY_CHARGING) != 0;
		status.critical = (info.state & BATTERY_CRITICAL_STATE) != 0;
		uint32 full = extended.last_full_charge > 0
			? extended.last_full_charge : extended.design_capacity;
		if (full > 0 && info.capacity >= 0)
			status.percent = int(int64(info.capacity) * 100 / full);
		if (!status.charging && info.current_rate > 0)
			status.minutesLeft = int(int64(info.capacity) * 60
				/ info.current_rate);
	}
	close(fd);
	return status;
}


bool
OSInterface::BrightnessSupported() const
{
	return Brightness() >= 0;
}


int
OSInterface::Brightness() const
{
	BScreen screen;
	float brightness = -1;
	if (screen.GetBrightness(&brightness) != B_OK)
		return -1;
	return int(brightness * 100 + 0.5f);
}


bool
OSInterface::SetBrightness(int percent)
{
	BScreen screen;
	percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
	return screen.SetBrightness(percent / 100.0f) == B_OK;
}


std::string
OSInterface::HaikuRevision() const
{
	return PrivateApi::RunningHaikuRevision().String();
}

}	// namespace haiku
}	// namespace lumina
