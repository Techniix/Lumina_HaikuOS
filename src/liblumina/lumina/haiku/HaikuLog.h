/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef LUMINA_HAIKU_LOG_H
#define LUMINA_HAIKU_LOG_H

#include <lumina/Log.h>

namespace lumina {
namespace haiku {

// Sends warnings and errors to syslog (visible in /var/log/syslog), so
// failures are discoverable without knowing Lumina's log location.
class SyslogSink : public LogSink {
public:
	explicit SyslogSink(const char* ident);
	~SyslogSink() override;
	void Write(const LogEntry& entry) override;
};

// Configures the default logger for `component` ("server", "panel", …):
// rotating file in Paths::LogDir(), syslog for warnings+, stderr if it is
// a terminal. Level from $LUMINA_LOG_LEVEL or `level`.
void InitLogging(const char* component, LogLevel level = LogLevel::Info);

}	// namespace haiku
}	// namespace lumina

#endif
