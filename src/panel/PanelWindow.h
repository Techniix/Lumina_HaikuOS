/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef LUMINA_PANEL_WINDOW_H
#define LUMINA_PANEL_WINDOW_H

#include <Window.h>

#include <vector>

#include <lumina/Value.h>
#include <lumina/fx/Strategy.h>
#include <lumina/haiku/PrivateApi.h>

#include "PanelSkin.h"

class BMessageRunner;

namespace lumina {
namespace panel {

class Applet;

class PanelWindow : public BWindow {
public:
	PanelWindow(int32 index, const Value& config, const fx::EffectPlan& plan);
	virtual ~PanelWindow();

	virtual void MessageReceived(BMessage* message);
	virtual void ScreenChanged(BRect frame, color_space mode);
	virtual void WorkspaceActivated(int32 workspace, bool active);
	virtual bool QuitRequested();
	virtual void Show();

	void SetWindows(const std::vector<haiku::WindowSnapshot>& windows);
	void ApplyConfig(const Value& config, const fx::EffectPlan& plan);

	static BRect FrameFor(const Value& config, BRect screen);

private:
	void _Layout();
	void _RecaptureHidden();
	void _RefreshSkin(bool force);
	void _Reserve(bool reserve);

	int32				fIndex;
	Value				fConfig;
	fx::EffectPlan		fPlan;
	PanelSkin			fSkin;
	BView*				fRoot;
	std::vector<Applet*> fApplets;
	BMessageRunner*		fSkinRunner;
	std::vector<haiku::WindowSnapshot> fWindows;
};

}	// namespace panel
}	// namespace lumina

#endif
