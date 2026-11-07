/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <thread>

namespace lumina {

const char*
LogLevelName(LogLevel level)
{
	switch (level) {
		case LogLevel::Trace: return "trace";
		case LogLevel::Debug: return "debug";
		case LogLevel::Info: return "info";
		case LogLevel::Warning: return "warning";
		case LogLevel::Error: return "error";
		case LogLevel::Fatal: return "fatal";
		case LogLevel::Off: return "off";
	}
	return "?";
}


bool
ParseLogLevel(const std::string& name, LogLevel& level)
{
	for (int i = 0; i <= int(LogLevel::Off); i++) {
		if (name == LogLevelName(LogLevel(i))) {
			level = LogLevel(i);
			return true;
		}
	}
	if (name == "warn") {
		level = LogLevel::Warning;
		return true;
	}
	return false;
}


std::string
FormatLogEntry(const LogEntry& entry)
{
	time_t seconds = time_t(entry.timestampUsecs / 1000000);
	int millis = int((entry.timestampUsecs / 1000) % 1000);
	struct tm tm;
	localtime_r(&seconds, &tm);
	char stamp[64];
	std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
		tm.tm_sec, millis);
	std::string out(stamp);
	out += " [";
	out += LogLevelName(entry.level);
	out += "] ";
	out += entry.category;
	out += ": ";
	out += entry.message;
	return out;
}


void
StderrSink::Write(const LogEntry& entry)
{
	std::string line = FormatLogEntry(entry);
	std::fprintf(stderr, "%s\n", line.c_str());
}


FileSink::FileSink(const std::string& path, size_t maxBytes)
	:
	fPath(path),
	fMaxBytes(maxBytes)
{
	FILE* file = std::fopen(path.c_str(), "a");
	if (file != nullptr) {
		std::fseek(file, 0, SEEK_END);
		long size = std::ftell(file);
		fWritten = size > 0 ? size_t(size) : 0;
	}
	fFile = file;
}


FileSink::~FileSink()
{
	if (fFile != nullptr)
		std::fclose(static_cast<FILE*>(fFile));
}


void
FileSink::_Rotate()
{
	if (fFile != nullptr)
		std::fclose(static_cast<FILE*>(fFile));
	std::string old = fPath + ".1";
	std::remove(old.c_str());
	std::rename(fPath.c_str(), old.c_str());
	fFile = std::fopen(fPath.c_str(), "w");
	fWritten = 0;
}


void
FileSink::Write(const LogEntry& entry)
{
	if (fFile == nullptr)
		return;
	std::string line = FormatLogEntry(entry);
	line += '\n';
	if (fWritten + line.size() > fMaxBytes)
		_Rotate();
	if (fFile == nullptr)
		return;
	std::fwrite(line.data(), 1, line.size(), static_cast<FILE*>(fFile));
	fWritten += line.size();
	if (entry.level >= LogLevel::Warning)
		std::fflush(static_cast<FILE*>(fFile));
}


void
FileSink::Flush()
{
	if (fFile != nullptr)
		std::fflush(static_cast<FILE*>(fFile));
}


Logger&
Logger::Default()
{
	static Logger sLogger;
	return sLogger;
}


void
Logger::AddSink(std::shared_ptr<LogSink> sink)
{
	std::lock_guard<std::mutex> lock(fLock);
	fSinks.push_back(std::move(sink));
}


void
Logger::ClearSinks()
{
	std::lock_guard<std::mutex> lock(fLock);
	fSinks.clear();
}


void
Logger::SetRingCapacity(size_t capacity)
{
	std::lock_guard<std::mutex> lock(fLock);
	fRingCapacity = capacity;
	while (fRing.size() > fRingCapacity)
		fRing.pop_front();
}


std::vector<LogEntry>
Logger::RecentEntries() const
{
	std::lock_guard<std::mutex> lock(fLock);
	return std::vector<LogEntry>(fRing.begin(), fRing.end());
}


void
Logger::Log(LogLevel level, const char* category, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	LogV(level, category, format, args);
	va_end(args);
}


void
Logger::LogV(LogLevel level, const char* category, const char* format,
	va_list args)
{
	if (!IsEnabled(level))
		return;

	char buffer[1024];
	va_list copy;
	va_copy(copy, args);
	int length = std::vsnprintf(buffer, sizeof(buffer), format, copy);
	va_end(copy);

	LogEntry entry;
	entry.level = level;
	entry.category = category != nullptr ? category : "";
	if (length >= int(sizeof(buffer))) {
		std::string big(size_t(length) + 1, '\0');
		std::vsnprintf(&big[0], big.size(), format, args);
		big.resize(size_t(length));
		entry.message = big;
	} else if (length > 0)
		entry.message.assign(buffer, size_t(length));
	entry.timestampUsecs = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	entry.threadId = std::hash<std::thread::id>()(std::this_thread::get_id());

	std::lock_guard<std::mutex> lock(fLock);
	if (int(level) < int(LogLevel::Off))
		fCounts[int(level)]++;
	if (fRingCapacity > 0) {
		fRing.push_back(entry);
		while (fRing.size() > fRingCapacity)
			fRing.pop_front();
	}
	for (auto& sink : fSinks)
		sink->Write(entry);
}


void
Logger::Flush()
{
	std::lock_guard<std::mutex> lock(fLock);
	for (auto& sink : fSinks)
		sink->Flush();
}


uint64_t
Logger::CountFor(LogLevel level) const
{
	std::lock_guard<std::mutex> lock(fLock);
	if (int(level) >= int(LogLevel::Off))
		return 0;
	return fCounts[int(level)];
}

}	// namespace lumina
