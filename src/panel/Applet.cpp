/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Applet.h"

#include <image.h>

#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <PathFinder.h>
#include <StringList.h>

#include <lumina/Log.h>

#include "PanelSkin.h"

namespace lumina {
namespace panel {

Applet::Applet(const char* name, const AppletContext& context, uint32 flags)
	:
	BView(name, flags),
	fContext(context)
{
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);
}


Applet::~Applet()
{
}


void
Applet::DrawPanelBackground(BRect update)
{
	if (fContext.skin != NULL)
		fContext.skin->DrawBackground(this, update);
	else {
		SetLowUIColor(B_MENU_BACKGROUND_COLOR);
		FillRect(update, B_SOLID_LOW);
	}
}


AppletRegistry&
AppletRegistry::Default()
{
	static AppletRegistry sRegistry;
	return sRegistry;
}


void
AppletRegistry::Register(const std::string& id, AppletFactory factory)
{
	fFactories[id] = std::move(factory);
}


std::vector<std::string>
AppletRegistry::Known() const
{
	std::vector<std::string> ids;
	for (const auto& entry : fFactories)
		ids.push_back(entry.first);
	return ids;
}


Applet*
AppletRegistry::Create(const std::string& spec, AppletContext context)
{
	std::string id = spec;
	size_t colon = spec.find(':');
	if (colon != std::string::npos) {
		id = spec.substr(0, colon);
		context.argument = spec.substr(colon + 1);
	}
	context.id = id;
	auto it = fFactories.find(id);
	if (it == fFactories.end()) {
		LoadAddOns();
		it = fFactories.find(id);
	}
	if (it == fFactories.end()) {
		LUMINA_LOG_WARN("panel", "unknown applet '%s'", spec.c_str());
		return NULL;
	}
	return it->second(context);
}


typedef Applet* (*instantiate_applet_func)(const char* id,
	const AppletContext& context);


void
AppletRegistry::LoadAddOns()
{
	if (fAddOnsLoaded)
		return;
	fAddOnsLoaded = true;
	BStringList paths;
	if (BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "Lumina/applets",
			B_FIND_PATH_EXISTING_ONLY, paths) != B_OK)
		return;
	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BDirectory directory(paths.StringAt(i).String());
		BEntry entry;
		while (directory.GetNextEntry(&entry, true) == B_OK) {
			BPath path;
			if (entry.GetPath(&path) != B_OK)
				continue;
			image_id image = load_add_on(path.Path());
			if (image < 0) {
				LUMINA_LOG_WARN("panel", "cannot load applet %s", path.Path());
				continue;
			}
			instantiate_applet_func instantiate;
			if (get_image_symbol(image, "instantiate_lumina_applet",
					B_SYMBOL_TYPE_TEXT, (void**)&instantiate) != B_OK) {
				unload_add_on(image);
				continue;
			}
			std::string id = std::string("addon:") + path.Leaf();
			Register(id, [instantiate, id](const AppletContext& context) {
				return instantiate(id.c_str(), context);
			});
			LUMINA_LOG_INFO("panel", "applet add-on %s", id.c_str());
		}
	}
}

}	// namespace panel
}	// namespace lumina
