/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "HaikuLog.h"

#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include <filesystem>
#include <memory>

#include "Paths.h"

namespace lumina {
namespace haiku {

SyslogSink::SyslogSink(const char* ident)
{
	openlog(ident, LOG_PID, LOG_USER);
}


SyslogSink::~SyslogSink()
{
	closelog();
}


void
SyslogSink::Write(const LogEntry& entry)
{
	if (entry.level < LogLevel::Warning)
		return;
	int priority = entry.level >= LogLevel::Error ? LOG_ERR : LOG_WARNING;
	syslog(priority, "%s: %s", entry.category.c_str(), entry.message.c_str());
}


void
InitLogging(const char* component, LogLevel level)
{
	Logger& logger = Logger::Default();
	logger.ClearSinks();

	if (const char* env = getenv("LUMINA_LOG_LEVEL")) {
		LogLevel parsed;
		if (ParseLogLevel(env, parsed))
			level = parsed;
	}
	logger.SetLevel(level);
	logger.SetRingCapacity(512);

	std::string dir = Paths::LogDir();
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	auto file = std::make_shared<FileSink>(dir + "/" + component + ".log");
	if (file->IsOpen())
		logger.AddSink(file);

	std::string ident = std::string("lumina-") + component;
	static std::string sIdent;
	sIdent = ident;	// openlog keeps the pointer
	logger.AddSink(std::make_shared<SyslogSink>(sIdent.c_str()));

	if (isatty(STDERR_FILENO))
		logger.AddSink(std::make_shared<StderrSink>());
}

}	// namespace haiku
}	// namespace lumina
