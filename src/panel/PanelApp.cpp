/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Lumina Panel application: one PanelWindow per entry in `panels`.
 *
 * Works with or without lumina_server: with the server it follows config
 * changes and the event-driven window registry; without it (crashed, or the
 * panel was started by hand) it falls back to a default bottom panel and
 * polls the WindowInfo API like Deskbar. A panel must never be the reason a
 * user cannot launch applications.
 */
#include <Application.h>
#include <Entry.h>
#include <MessageRunner.h>
#include <Roster.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <lumina/ConfigStore.h>
#include <lumina/Log.h>
#include <lumina/fx/Strategy.h>
#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/HaikuLog.h>
#include <lumina/haiku/LuminaClient.h>
#include <lumina/haiku/MessageCodec.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>

#include "Applet.h"
#include "PanelProtocol.h"
#include "PanelWindow.h"

using namespace lumina;
using namespace lumina::haiku;
using namespace lumina::panel;

namespace {

class PanelApp : public BApplication {
public:
	PanelApp()
		:
		BApplication(LUMINA_PANEL_SIGNATURE),
		fPollRunner(NULL),
		fServerSubscribed(false)
	{
	}

	~PanelApp()
	{
		delete fPollRunner;
	}

	void ReadyToRun()
	{
		InitLogging("panel");
		RegisterBuiltinApplets();
		_LoadConfig();
		_CreatePanels();
		_Subscribe();
		fPollRunner = new BMessageRunner(BMessenger(this),
			new BMessage(kMsgPollWindows), 1000000);
		_PollWindows(true);
	}

	void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgLaunchRef:
			{
				entry_ref ref;
				if (message->FindRef("refs", &ref) == B_OK) {
					status_t status = be_roster->Launch(&ref);
					if (status != B_OK && status != B_ALREADY_RUNNING)
						LUMINA_LOG_WARN("panel", "launch failed: %s",
							strerror(status));
				}
				break;
			}
			case kMsgOpenModePreflet:
				be_roster->Launch(LUMINA_MODE_SIGNATURE);
				break;
			case kMsgPollWindows:
				_PollWindows(false);
				break;
			case LUMINA_WINDOWS_CHANGED:
				_PollWindows(true);	// cheap: re-read the ordered list
				break;
			case LUMINA_CONFIG_CHANGED:
			{
				bool relevant = false;
				const char* path;
				for (int32 i = 0; message->FindString(LUMINA_FIELD_PATH, i,
						&path) == B_OK; i++) {
					BString p(path);
					relevant |= p.StartsWith("panels") || p.StartsWith("fx.");
				}
				if (relevant) {
					_LoadConfig();
					_CreatePanels();
				}
				break;
			}
			case LUMINA_MODE_CHANGED:
				if (BString(message->GetString("mode", "")) == "stock")
					PostMessage(B_QUIT_REQUESTED);
				break;
			default:
				BApplication::MessageReceived(message);
		}
	}

private:
	void _LoadConfig()
	{
		LuminaClient client(500000);
		Value fx;
		fConfig.reset(new ConfigStore());
		fConfig->AddLayer("server", false);
		if (client.Get("", fx) == B_OK && fx.IsObject()) {
			fConfig->ReplaceLayer("server", fx);
		} else {
			LUMINA_LOG_WARN("panel", "lumina_server unreachable; "
				"using a default panel");
		}
		fPanels = fConfig->Get("panels");
		if (fPanels.Size() == 0) {
			Value panel = Value::MakeObject();
			panel["edge"] = "bottom";
			panel["thickness"] = 28;
			Value applets = Value::MakeArray();
			for (const char* spec : {"launcher", "windows", "workspaces",
					"clock"})
				applets.Push(spec);
			panel["applets"] = applets;
			fPanels = Value::MakeArray();
			fPanels.Push(panel);
		}

		fx::EffectProfile profile = fx::EffectProfile::FromConfig(*fConfig);
		fx::Capabilities caps;
		caps.screenReadback = true;	// BScreen::ReadBitmap is always there
		caps.readbackMsPerMegapixel = -1;	// unknown until measured
		caps.engineLoaded = EngineLink::IsEngineSelected();
		fPlan = fx::SelectPlan(profile, caps, fx::Quality::High);
		LUMINA_LOG_INFO("panel", "effects plan: quality=%s glass=%s (%s)",
			fx::QualityName(fPlan.quality),
			fx::GlassStrategyName(fPlan.glass), fPlan.reason.c_str());
	}

	void _CreatePanels()
	{
		size_t wanted = fPanels.Size();
		while (fWindows.size() > wanted) {
			PanelWindow* window = fWindows.back();
			fWindows.pop_back();
			if (window->Lock())
				window->Quit();
		}
		for (size_t i = 0; i < wanted; i++) {
			const Value& config = fPanels.Items()[i];
			if (i < fWindows.size()) {
				if (fWindows[i]->Lock()) {
					fWindows[i]->ApplyConfig(config, fPlan);
					fWindows[i]->Unlock();
				}
				continue;
			}
			PanelWindow* window = new PanelWindow(int32(i), config, fPlan);
			window->Show();
			fWindows.push_back(window);
		}
	}

	void _Subscribe()
	{
		LuminaClient client(500000);
		fServerSubscribed = client.Subscribe(BMessenger(this),
			{LUMINA_TOPIC_CONFIG, LUMINA_TOPIC_WINDOWS, LUMINA_TOPIC_MODE})
			== B_OK;
	}

	void _PollWindows(bool force)
	{
		// The window list is cheap to read and Deskbar does the same; the
		// server subscription only tells us *when* to re-read promptly.
		std::vector<WindowSnapshot> all;
		if (PrivateApi::ListWindows(all) != B_OK)
			return;
		std::vector<WindowSnapshot> windows;
		for (const WindowSnapshot& w : all) {
			if (PrivateApi::IsUserWindow(w))
				windows.push_back(w);
		}
		std::sort(windows.begin(), windows.end(),
			[](const WindowSnapshot& a, const WindowSnapshot& b) {
				return a.team != b.team ? a.team < b.team : a.token < b.token;
			});
		bool changed = force || windows.size() != fLast.size();
		for (size_t i = 0; !changed && i < windows.size(); i++) {
			changed = windows[i].token != fLast[i].token
				|| windows[i].title != fLast[i].title
				|| windows[i].minimized != fLast[i].minimized
				|| windows[i].workspaces != fLast[i].workspaces;
		}
		if (!changed)
			return;
		fLast = windows;
		for (PanelWindow* window : fWindows) {
			if (window->Lock()) {
				window->SetWindows(windows);
				window->Unlock();
			}
		}
		if (!fServerSubscribed)
			_Subscribe();	// server may have come up meanwhile
	}

	std::unique_ptr<ConfigStore> fConfig;
	Value						fPanels;
	fx::EffectPlan				fPlan;
	std::vector<PanelWindow*>	fWindows;
	std::vector<WindowSnapshot>	fLast;
	BMessageRunner*				fPollRunner;
	bool						fServerSubscribed;
};

}	// namespace


int
main()
{
	PanelApp app;
	app.Run();
	return 0;
}
