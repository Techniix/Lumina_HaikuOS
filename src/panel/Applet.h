/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Panel applet API.
 *
 * An applet is a BView placed in a panel's layout. Built-in applets are
 * registered statically; third-party applets are add-ons in
 * add-ons/Lumina/applets/ exporting:
 *
 *   extern "C" lumina::panel::Applet*
 *   instantiate_lumina_applet(const char* id, const lumina::panel::AppletContext&);
 *
 * Applets get configuration through AppletContext and window-list updates
 * through WindowsChanged(). They must not block their looper.
 */
#ifndef LUMINA_PANEL_APPLET_H
#define LUMINA_PANEL_APPLET_H

#include <View.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <lumina/Value.h>
#include <lumina/haiku/PrivateApi.h>

namespace lumina {
namespace panel {

class PanelSkin;

struct AppletContext {
	std::string	id;				// "clock", "windows", or "addon:name"
	std::string	argument;		// text after ':' in the applet spec
	Value		panelConfig;	// the panel's config object
	orientation	direction = B_HORIZONTAL;
	float		thickness = 28;
	const PanelSkin* skin = NULL;	// owned by the panel window
};

class Applet : public BView {
public:
	Applet(const char* name, const AppletContext& context,
		uint32 flags = B_WILL_DRAW);
	virtual ~Applet();

	// The panel model changed (window list snapshot, ordered).
	virtual void WindowsChanged(
		const std::vector<haiku::WindowSnapshot>& windows) {}
	// Config of the panel changed.
	virtual void ConfigChanged(const Value& panelConfig) {}

	const AppletContext& Context() const { return fContext; }

protected:
	// Paints the shared panel background (solid Deskbar look or glass).
	void DrawPanelBackground(BRect update);

	AppletContext	fContext;
};

typedef std::function<Applet*(const AppletContext&)> AppletFactory;

class AppletRegistry {
public:
	static AppletRegistry& Default();

	void Register(const std::string& id, AppletFactory factory);
	// spec: "clock", "launcher", "jsonmenu:/path/script" …
	Applet* Create(const std::string& spec, AppletContext context);
	std::vector<std::string> Known() const;
	void LoadAddOns();

private:
	std::map<std::string, AppletFactory> fFactories;
	bool fAddOnsLoaded = false;
};

void RegisterBuiltinApplets();

}	// namespace panel
}	// namespace lumina

#endif
