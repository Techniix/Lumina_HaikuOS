/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "EngineLink.h"

#include <Path.h>
#include <PathFinder.h>
#include <StringList.h>
#include <Window.h>

#include "PrivateApi.h"
#include "Protocol.h"

namespace lumina {
namespace haiku {

namespace {
// A titled (so it has a decorator tab — GetDecoratorSettings() needs one)
// but permanently hidden window, used only as an app_server link.
class LinkWindow : public BWindow {
public:
	LinkWindow()
		:
		BWindow(BRect(-200, -200, -100, -150), "Lumina engine link",
			B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
			B_AVOID_FOCUS | B_NOT_MOVABLE | B_NOT_RESIZABLE
				| B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NO_WORKSPACE_ACTIVATION,
			B_CURRENT_WORKSPACE)
	{
	}
};
}	// namespace


EngineLink::EngineLink()
	:
	fWindow(new LinkWindow())
{
	// Start the looper without ever showing the window.
	fWindow->Hide();
	fWindow->Show();
}


EngineLink::~EngineLink()
{
	if (fWindow != NULL && fWindow->Lock())
		fWindow->Quit();
}


status_t
EngineLink::Probe(EngineInfo& info)
{
	info = EngineInfo();
	BMessage settings;
	status_t status = fWindow->GetDecoratorSettings(&settings);
	if (status != B_OK)
		return status;
	info.present = settings.GetBool(LUMINA_ENGINE_STATUS_PRESENT, false);
	if (!info.present)
		return B_OK;
	info.active = settings.GetBool(LUMINA_ENGINE_STATUS_ACTIVE, false);
	info.version = settings.GetInt32(LUMINA_ENGINE_STATUS_VERSION, 0);
	info.builtAbi = settings.GetString(LUMINA_ENGINE_STATUS_ABI, "");
	info.runningAbi = settings.GetString(LUMINA_ENGINE_STATUS_RUNNING_ABI, "");
	info.reason = settings.GetString(LUMINA_ENGINE_STATUS_REASON, "");
	info.animating = settings.GetInt32(LUMINA_ENGINE_STATUS_ANIMATING, 0);
	info.statsJson = settings.GetString(LUMINA_ENGINE_STATUS_STATS, "");
	return B_OK;
}


status_t
EngineLink::Send(BMessage& command)
{
	if (fWindow == NULL)
		return B_NO_INIT;
	command.what = LUMINA_ENGINE_COMMAND;
	return fWindow->SetDecoratorSettings(command);
}


static BMessage
Command(const char* op)
{
	BMessage message(LUMINA_ENGINE_COMMAND);
	message.AddString(LUMINA_ENGINE_FIELD_OP, op);
	return message;
}


status_t
EngineLink::Subscribe(const BMessenger& target)
{
	BMessage m = Command(LUMINA_OP_SUBSCRIBE);
	m.AddMessenger(LUMINA_ENGINE_FIELD_TARGET, target);
	return Send(m);
}


status_t
EngineLink::Unsubscribe(const BMessenger& target)
{
	BMessage m = Command(LUMINA_OP_UNSUBSCRIBE);
	m.AddMessenger(LUMINA_ENGINE_FIELD_TARGET, target);
	return Send(m);
}


status_t
EngineLink::MoveResize(int32 token, BRect frame)
{
	BMessage m = Command(LUMINA_OP_MOVE_RESIZE);
	m.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
	m.AddRect(LUMINA_ENGINE_FIELD_FRAME, frame);
	return Send(m);
}


status_t
EngineLink::Animate(int32 token, BRect frame, float duration, const char* curve,
	const char* resizeMode, int32 maxFps)
{
	BMessage m = Command(LUMINA_OP_ANIMATE);
	m.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
	m.AddRect(LUMINA_ENGINE_FIELD_FRAME, frame);
	m.AddFloat(LUMINA_ENGINE_FIELD_DURATION, duration);
	m.AddString(LUMINA_ENGINE_FIELD_CURVE, curve != NULL ? curve : "ease-out-cubic");
	m.AddString(LUMINA_ENGINE_FIELD_RESIZE, resizeMode != NULL
		? resizeMode : "throttled");
	m.AddInt32(LUMINA_ENGINE_FIELD_FPS, maxFps);
	return Send(m);
}


status_t
EngineLink::Cancel(int32 token)
{
	BMessage m = Command(LUMINA_OP_CANCEL);
	m.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
	return Send(m);
}


status_t
EngineLink::Minimize(int32 token, bool minimize)
{
	BMessage m = Command(LUMINA_OP_MINIMIZE);
	m.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
	m.AddBool("lumina:value", minimize);
	return Send(m);
}


status_t
EngineLink::Activate(int32 token)
{
	BMessage m = Command(LUMINA_OP_ACTIVATE);
	m.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
	return Send(m);
}


status_t
EngineLink::SetWorkspace(int32 token, int32 workspace)
{
	BMessage m = Command(LUMINA_OP_WORKSPACE);
	m.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
	m.AddInt32(LUMINA_ENGINE_FIELD_WORKSPACE, workspace);
	return Send(m);
}


status_t
EngineLink::Configure(const BString& json)
{
	BMessage m = Command(LUMINA_OP_CONFIGURE);
	m.AddString(LUMINA_ENGINE_FIELD_JSON, json);
	return Send(m);
}


status_t
EngineLink::Reserve(const BString& json)
{
	BMessage m = Command(LUMINA_OP_RESERVE);
	m.AddString(LUMINA_ENGINE_FIELD_JSON, json);
	return Send(m);
}


status_t
EngineLink::RequestList(const BMessenger& target)
{
	BMessage m = Command(LUMINA_OP_LIST);
	m.AddMessenger(LUMINA_ENGINE_FIELD_TARGET, target);
	return Send(m);
}


bool
EngineLink::FindEngineAddOn(BString& path)
{
	BStringList paths;
	if (BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY,
			"decorators/LuminaEngine", B_FIND_PATH_EXISTING_ONLY, paths) != B_OK
		|| paths.IsEmpty()) {
		return false;
	}
	path = paths.StringAt(0);
	return true;
}


bool
EngineLink::IsEngineSelected()
{
	BString current;
	if (!PrivateApi::GetDecorator(current))
		return false;
	return current.EndsWith("/LuminaEngine");
}

}	// namespace haiku
}	// namespace lumina
