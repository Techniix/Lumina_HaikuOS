/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Client side of the lumina_server protocol. Synchronous calls use a short
 * timeout so a hung server can never freeze a panel or the CLI.
 */
#ifndef LUMINA_HAIKU_CLIENT_H
#define LUMINA_HAIKU_CLIENT_H

#include <Messenger.h>
#include <String.h>

#include <string>
#include <vector>

#include <lumina/Value.h>

namespace lumina {
namespace haiku {

struct ServerStatus {
	BString	mode;
	BString	personality;
	bool	engineLoaded = false;
	BString	engineReason;
	int32	crashCount = 0;
	uint64	generation = 0;
	BString	deskbarMode;
};

class LuminaClient {
public:
	explicit LuminaClient(bigtime_t timeout = 2000000);

	bool IsServerRunning() const;
	// Launches lumina_server through the roster if it isn't running.
	status_t EnsureServer();

	status_t Subscribe(const BMessenger& target,
		const std::vector<std::string>& topics);
	status_t Unsubscribe(const BMessenger& target);

	status_t Get(const std::string& path, Value& value, BString* error = NULL);
	status_t Set(const std::string& path, const Value& value,
		BString* error = NULL, bool preview = false,
		bigtime_t revertAfter = 0);
	status_t Unset(const std::string& path, BString* error = NULL);
	status_t Confirm();

	status_t SetMode(const char* mode, const char* personality = NULL,
		BString* error = NULL);
	status_t RestoreStock(BString* error = NULL);
	status_t GetStatus(ServerStatus& status);
	status_t GetDiagnostics(BMessage& reply);
	status_t ProfileOp(BMessage& request, BMessage& reply);
	status_t WindowList(BMessage& reply);
	status_t WindowCommand(BMessage& command, BString* error = NULL);
	status_t ReserveArea(const char* owner, int32 screen, const char* edge,
		float thickness);

private:
	status_t _Send(BMessage& request, BMessage& reply, BString* error);
	BMessenger _Server() const;

	bigtime_t	fTimeout;
};

}	// namespace haiku
}	// namespace lumina

#endif
