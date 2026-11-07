/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "LuminaClient.h"

#include <Roster.h>

#include "MessageCodec.h"
#include "Protocol.h"

namespace lumina {
namespace haiku {

LuminaClient::LuminaClient(bigtime_t timeout)
	:
	fTimeout(timeout)
{
}


BMessenger
LuminaClient::_Server() const
{
	return BMessenger(LUMINA_SERVER_SIGNATURE);
}


bool
LuminaClient::IsServerRunning() const
{
	return _Server().IsValid();
}


status_t
LuminaClient::EnsureServer()
{
	if (IsServerRunning())
		return B_OK;
	status_t status = be_roster->Launch(LUMINA_SERVER_SIGNATURE);
	if (status != B_OK && status != B_ALREADY_RUNNING)
		return status;
	for (int i = 0; i < 50 && !IsServerRunning(); i++)
		snooze(20000);
	return IsServerRunning() ? B_OK : B_TIMED_OUT;
}


status_t
LuminaClient::_Send(BMessage& request, BMessage& reply, BString* error)
{
	BMessenger server = _Server();
	if (!server.IsValid()) {
		if (error)
			*error = "lumina_server is not running";
		return B_NAME_NOT_FOUND;
	}
	status_t status = server.SendMessage(&request, &reply, fTimeout, fTimeout);
	if (status != B_OK) {
		if (error)
			*error = strerror(status);
		return status;
	}
	int32 result = B_ERROR;
	if (reply.FindInt32(LUMINA_FIELD_STATUS, &result) != B_OK)
		result = B_BAD_REPLY;
	if (result != B_OK && error != NULL) {
		const char* text = NULL;
		*error = reply.FindString(LUMINA_FIELD_ERROR, &text) == B_OK
			? text : strerror(result);
	}
	return result;
}


status_t
LuminaClient::Subscribe(const BMessenger& target,
	const std::vector<std::string>& topics)
{
	BMessage request(LUMINA_SUBSCRIBE), reply;
	request.AddMessenger(LUMINA_FIELD_TARGET, target);
	for (const std::string& topic : topics)
		request.AddString(LUMINA_FIELD_TOPIC, topic.c_str());
	return _Send(request, reply, NULL);
}


status_t
LuminaClient::Unsubscribe(const BMessenger& target)
{
	BMessage request(LUMINA_UNSUBSCRIBE), reply;
	request.AddMessenger(LUMINA_FIELD_TARGET, target);
	return _Send(request, reply, NULL);
}


status_t
LuminaClient::Get(const std::string& path, Value& value, BString* error)
{
	BMessage request(LUMINA_CONFIG_GET), reply;
	request.AddString(LUMINA_FIELD_PATH, path.c_str());
	status_t status = _Send(request, reply, error);
	if (status != B_OK)
		return status;
	return GetJson(reply, LUMINA_FIELD_JSON, value);
}


status_t
LuminaClient::Set(const std::string& path, const Value& value, BString* error,
	bool preview, bigtime_t revertAfter)
{
	BMessage request(LUMINA_CONFIG_SET), reply;
	request.AddString(LUMINA_FIELD_PATH, path.c_str());
	PutJson(request, LUMINA_FIELD_JSON, value);
	if (preview) {
		request.AddBool("preview", true);
		request.AddInt64("revert_after", revertAfter > 0 ? revertAfter
			: 15000000);
	}
	return _Send(request, reply, error);
}


status_t
LuminaClient::Unset(const std::string& path, BString* error)
{
	BMessage request(LUMINA_CONFIG_UNSET), reply;
	request.AddString(LUMINA_FIELD_PATH, path.c_str());
	return _Send(request, reply, error);
}


status_t
LuminaClient::Confirm()
{
	BMessage request(LUMINA_CONFIG_CONFIRM), reply;
	return _Send(request, reply, NULL);
}


status_t
LuminaClient::SetMode(const char* mode, const char* personality,
	BString* error)
{
	BMessage request(LUMINA_MODE_SET), reply;
	request.AddString("mode", mode);
	if (personality != NULL)
		request.AddString("personality", personality);
	return _Send(request, reply, error);
}


status_t
LuminaClient::RestoreStock(BString* error)
{
	BMessage request(LUMINA_RESTORE_STOCK), reply;
	return _Send(request, reply, error);
}


status_t
LuminaClient::GetStatus(ServerStatus& out)
{
	BMessage request(LUMINA_STATUS_GET), reply;
	status_t status = _Send(request, reply, NULL);
	if (status != B_OK)
		return status;
	out.mode = reply.GetString("mode", "");
	out.personality = reply.GetString("personality", "");
	out.engineLoaded = reply.GetBool("engine_loaded", false);
	out.engineReason = reply.GetString("engine_reason", "");
	out.crashCount = reply.GetInt32("crash_count", 0);
	out.generation = reply.GetUInt64("generation", 0);
	out.deskbarMode = reply.GetString("deskbar_mode", "");
	return B_OK;
}


status_t
LuminaClient::GetDiagnostics(BMessage& reply)
{
	BMessage request(LUMINA_DIAG_GET);
	return _Send(request, reply, NULL);
}


status_t
LuminaClient::ProfileOp(BMessage& request, BMessage& reply)
{
	request.what = LUMINA_PROFILE_OP;
	return _Send(request, reply, NULL);
}


status_t
LuminaClient::WindowList(BMessage& reply)
{
	BMessage request(LUMINA_WINDOW_LIST_GET);
	return _Send(request, reply, NULL);
}


status_t
LuminaClient::WindowCommand(BMessage& command, BString* error)
{
	command.what = LUMINA_WINDOW_COMMAND;
	BMessage reply;
	return _Send(command, reply, error);
}


status_t
LuminaClient::ReserveArea(const char* owner, int32 screen, const char* edge,
	float thickness)
{
	BMessage request(LUMINA_RESERVE_AREA), reply;
	request.AddString("owner", owner);
	request.AddInt32("screen", screen);
	request.AddString("edge", edge);
	request.AddFloat("thickness", thickness);
	return _Send(request, reply, NULL);
}

}	// namespace haiku
}	// namespace lumina
