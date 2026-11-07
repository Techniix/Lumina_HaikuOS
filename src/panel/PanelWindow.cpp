/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "PanelWindow.h"

#include <Application.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Screen.h>

#include <lumina/Log.h>
#include <lumina/haiku/LuminaClient.h>

#include "Applet.h"
#include "PanelProtocol.h"

namespace lumina {
namespace panel {

namespace {

std::string
GetString(const Value& config, const char* key, const char* fallback)
{
	const Value* v = config.Find(key);
	return v != NULL && v->IsString() ? v->AsString() : fallback;
}

double
GetNumber(const Value& config, const char* key, double fallback)
{
	const Value* v = config.Find(key);
	return v != NULL && v->IsNumber() ? v->AsNumber() : fallback;
}

}	// namespace


BRect
PanelWindow::FrameFor(const Value& config, BRect screen)
{
	std::string edge = GetString(config, "edge", "bottom");
	float thickness = float(GetNumber(config, "thickness", 28));
	float percent = float(GetNumber(config, "lengthPercent", 100)) / 100.0f;
	std::string align = GetString(config, "align", "center");
	bool horizontal = edge == "top" || edge == "bottom";
	float available = horizontal ? screen.Width() + 1 : screen.Height() + 1;
	float length = std::floor(available * percent);
	float start = align == "start" ? 0 : align == "end" ? available - length
		: std::floor((available - length) / 2);

	BRect frame;
	if (horizontal) {
		frame.left = screen.left + start;
		frame.right = frame.left + length - 1;
		frame.top = edge == "top" ? screen.top : screen.bottom - thickness + 1;
		frame.bottom = frame.top + thickness - 1;
	} else {
		frame.top = screen.top + start;
		frame.bottom = frame.top + length - 1;
		frame.left = edge == "left" ? screen.left : screen.right - thickness + 1;
		frame.right = frame.left + thickness - 1;
	}
	return frame;
}


PanelWindow::PanelWindow(int32 index, const Value& config,
	const fx::EffectPlan& plan)
	:
	BWindow(FrameFor(config, BScreen().Frame()), "Lumina panel",
		B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
		B_AVOID_FOCUS | B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_WILL_ACCEPT_FIRST_CLICK
			| B_ASYNCHRONOUS_CONTROLS, B_ALL_WORKSPACES),
	fIndex(index),
	fSkinRunner(NULL)
{
	ApplyConfig(config, plan);
}


PanelWindow::~PanelWindow()
{
	delete fSkinRunner;
}


bool
PanelWindow::QuitRequested()
{
	_Reserve(false);
	return true;
}


void
PanelWindow::_Reserve(bool reserve)
{
	bool wanted = reserve;
	if (const Value* v = fConfig.Find("reserveSpace"))
		wanted = reserve && v->AsBool(true);
	BString owner;
	owner << "panel-" << fIndex;
	std::string edge = GetString(fConfig, "edge", "bottom");
	bool horizontal = edge == "top" || edge == "bottom";
	float thickness = horizontal ? Frame().Height() + 1 : Frame().Width() + 1;
	haiku::LuminaClient client(300000);
	client.ReserveArea(owner.String(), int32(GetNumber(fConfig, "screen", 0)),
		edge.c_str(), wanted ? thickness : 0);
}


void
PanelWindow::ApplyConfig(const Value& config, const fx::EffectPlan& plan)
{
	fConfig = config;
	fPlan = plan;
	BRect frame = FrameFor(config, BScreen(this).Frame());
	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());

	fx::GlassStrategy strategy = fx::GlassStrategy::Solid;
	if (GetString(config, "style", "system") == "glass")
		strategy = plan.glass;
	fSkin.Configure(strategy, plan.glassParams,
		GetString(config, "edge", "bottom").c_str());

	_Layout();
	_RefreshSkin(true);

	delete fSkinRunner;
	fSkinRunner = NULL;
	if (fSkin.Strategy() == fx::GlassStrategy::Edge && plan.glassRefreshHz > 0) {
		fSkinRunner = new BMessageRunner(BMessenger(this),
			new BMessage(kMsgRefreshSkin),
			bigtime_t(1000000 / plan.glassRefreshHz));
	}
	_Reserve(true);
}


void
PanelWindow::_Layout()
{
	// Rebuild the applet views from the configured list.
	while (BView* old = ChildAt(0)) {
		RemoveChild(old);
		delete old;
	}
	fApplets.clear();

	std::string edge = GetString(fConfig, "edge", "bottom");
	orientation direction = edge == "left" || edge == "right"
		? B_VERTICAL : B_HORIZONTAL;
	float thickness = direction == B_HORIZONTAL ? Frame().Height() + 1
		: Frame().Width() + 1;

	BGroupLayout* layout = new BGroupLayout(direction,
		float(GetNumber(fConfig, "spacing", 2)));
	SetLayout(layout);
	layout->SetInsets(2, 2, 2, 2);

	AppletContext context;
	context.panelConfig = fConfig;
	context.direction = direction;
	context.thickness = thickness - 4;
	context.skin = &fSkin;

	Value applets = fConfig.Find("applets") != NULL ? *fConfig.Find("applets")
		: Value::MakeArray();
	if (applets.Size() == 0) {
		for (const char* spec : {"launcher", "windows", "clock"})
			applets.Push(spec);
	}
	for (const Value& spec : applets.Items()) {
		Applet* applet = AppletRegistry::Default().Create(spec.AsString(),
			context);
		if (applet == NULL)
			continue;
		layout->AddView(applet);
		fApplets.push_back(applet);
	}
	SetWindows(fWindows);
}


void
PanelWindow::_RefreshSkin(bool force)
{
	if (fSkin.Update(Frame(), force)) {
		for (Applet* applet : fApplets)
			applet->Invalidate();
	}
}


void
PanelWindow::SetWindows(const std::vector<haiku::WindowSnapshot>& windows)
{
	fWindows = windows;
	for (Applet* applet : fApplets)
		applet->WindowsChanged(windows);
}


void
PanelWindow::ScreenChanged(BRect frame, color_space mode)
{
	ApplyConfig(fConfig, fPlan);
}


void
PanelWindow::WorkspaceActivated(int32 workspace, bool active)
{
	if (!active)
		return;
	SetWindows(fWindows);	// window list is per workspace
	_RefreshSkin(true);		// wallpaper may differ per workspace
}


void
PanelWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefreshSkin:
			_RefreshSkin(false);
			break;
		default:
			BWindow::MessageReceived(message);
	}
}

}	// namespace panel
}	// namespace lumina
