/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Logging & diagnostics. Thread-safe; pluggable sinks (stderr, rotating
 * file, syslog on Haiku); an in-memory ring buffer keeps the most recent
 * entries so `lumina diag` can dump them from a running process.
 *
 * Usage:  LUMINA_LOG_INFO("panel", "loaded %d applets", count);
 */
#ifndef LUMINA_CORE_LOG_H
#define LUMINA_CORE_LOG_H

#include <cstdarg>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lumina {

enum class LogLevel { Trace = 0, Debug, Info, Warning, Error, Fatal, Off };

const char* LogLevelName(LogLevel level);
bool ParseLogLevel(const std::string& name, LogLevel& level);

struct LogEntry {
	int64_t		timestampUsecs = 0;	// wall clock, microseconds since epoch
	LogLevel	level = LogLevel::Info;
	std::string	category;
	std::string	message;
	uint64_t	threadId = 0;
};

std::string FormatLogEntry(const LogEntry& entry);

class LogSink {
public:
	virtual ~LogSink() = default;
	virtual void Write(const LogEntry& entry) = 0;
	virtual void Flush() {}
};

class StderrSink : public LogSink {
public:
	void Write(const LogEntry& entry) override;
};

// Appends to a file; rotates to <path>.1 when it exceeds maxBytes.
class FileSink : public LogSink {
public:
	FileSink(const std::string& path, size_t maxBytes = 512 * 1024);
	~FileSink() override;
	void Write(const LogEntry& entry) override;
	void Flush() override;
	bool IsOpen() const { return fFile != nullptr; }

private:
	void _Rotate();

	std::string	fPath;
	size_t		fMaxBytes;
	size_t		fWritten = 0;
	void*		fFile = nullptr;	// FILE*
};

class Logger {
public:
	static Logger& Default();

	void SetLevel(LogLevel level) { fLevel = level; }
	LogLevel Level() const { return fLevel; }
	bool IsEnabled(LogLevel level) const
		{ return level >= fLevel && fLevel != LogLevel::Off; }

	void AddSink(std::shared_ptr<LogSink> sink);
	void ClearSinks();

	void SetRingCapacity(size_t capacity);
	std::vector<LogEntry> RecentEntries() const;

	void Log(LogLevel level, const char* category, const char* format, ...)
#if defined(__GNUC__)
		__attribute__((format(printf, 4, 5)))
#endif
		;
	void LogV(LogLevel level, const char* category, const char* format,
		va_list args);
	void Flush();

	// Counters for diagnostics
	uint64_t CountFor(LogLevel level) const;

private:
	mutable std::mutex	fLock;
	LogLevel			fLevel = LogLevel::Info;
	std::vector<std::shared_ptr<LogSink>> fSinks;
	std::deque<LogEntry> fRing;
	size_t				fRingCapacity = 256;
	uint64_t			fCounts[int(LogLevel::Off)] = {};
};

}	// namespace lumina

#define LUMINA_LOG(level, category, ...) \
	do { \
		if (::lumina::Logger::Default().IsEnabled(level)) \
			::lumina::Logger::Default().Log(level, category, __VA_ARGS__); \
	} while (0)

#define LUMINA_LOG_TRACE(c, ...) LUMINA_LOG(::lumina::LogLevel::Trace, c, __VA_ARGS__)
#define LUMINA_LOG_DEBUG(c, ...) LUMINA_LOG(::lumina::LogLevel::Debug, c, __VA_ARGS__)
#define LUMINA_LOG_INFO(c, ...) LUMINA_LOG(::lumina::LogLevel::Info, c, __VA_ARGS__)
#define LUMINA_LOG_WARN(c, ...) LUMINA_LOG(::lumina::LogLevel::Warning, c, __VA_ARGS__)
#define LUMINA_LOG_ERROR(c, ...) LUMINA_LOG(::lumina::LogLevel::Error, c, __VA_ARGS__)

#endif
