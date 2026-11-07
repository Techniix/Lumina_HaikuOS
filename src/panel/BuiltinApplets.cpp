/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Built-in applets, drawn with BControlLook and the system UI colors so
 * they look and update exactly like Deskbar.
 */
#include "Applet.h"

#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <map>
#include <set>

#include <Application.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <MenuItem.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Messenger.h>
#include <Window.h>

#include <lumina/haiku/LuminaClient.h>
#include <lumina/haiku/Protocol.h>

#include "PanelProtocol.h"

using lumina::haiku::WindowSnapshot;
namespace PrivateApi = lumina::haiku::PrivateApi;

namespace lumina {
namespace panel {

namespace {

BBitmap*
IconForRef(const entry_ref& ref, icon_size size = B_MINI_ICON)
{
	BBitmap* icon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (BNodeInfo::GetTrackerIcon(&ref, icon, size) != B_OK) {
		delete icon;
		return NULL;
	}
	return icon;
}


BBitmap*
IconForSignature(const char* signature, icon_size size = B_MINI_ICON)
{
	entry_ref ref;
	if (signature == NULL || be_roster->FindApp(signature, &ref) != B_OK)
		return NULL;
	return IconForRef(ref, size);
}


// A menu item with a 16x16 icon, like Deskbar's.
class IconMenuItem : public BMenuItem {
public:
	IconMenuItem(const char* label, BMessage* message, BBitmap* icon)
		: BMenuItem(label, message), fIcon(icon) {}
	IconMenuItem(BMenu* submenu, BBitmap* icon)
		: BMenuItem(submenu), fIcon(icon) {}
	~IconMenuItem() { delete fIcon; }

	void GetContentSize(float* width, float* height)
	{
		BMenuItem::GetContentSize(width, height);
		float iconSize = be_control_look->ComposeIconSize(B_MINI_ICON).Width();
		*width += iconSize + 6;
		*height = std::max(*height, iconSize + 2);
	}

	void DrawContent()
	{
		BPoint location = ContentLocation();
		if (fIcon != NULL) {
			Menu()->SetDrawingMode(B_OP_ALPHA);
			Menu()->DrawBitmapAsync(fIcon, location + BPoint(0, 1));
			Menu()->SetDrawingMode(B_OP_COPY);
		}
		float iconSize = be_control_look->ComposeIconSize(B_MINI_ICON).Width();
		Menu()->MovePenTo(location + BPoint(iconSize + 6, 0));
		BMenuItem::DrawContent();
	}

private:
	BBitmap*	fIcon;
};


// #pragma mark - launcher


class LauncherApplet : public Applet {
public:
	LauncherApplet(const AppletContext& context)
		: Applet("launcher", context, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetExplicitPreferredSize(BSize(context.direction == B_HORIZONTAL
			? context.thickness * 1.6f : context.thickness, context.thickness));
		SetExplicitMaxSize(PreferredSize());
	}

	void Draw(BRect update)
	{
		BRect bounds = Bounds();
		rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
		uint32 flags = fPressed ? BControlLook::B_ACTIVATED : 0;
		be_control_look->DrawButtonBackground(this, bounds, update, base, flags,
			BControlLook::B_ALL_BORDERS);
		// The Haiku "leaf": the Deskbar app icon when available.
		BBitmap* icon = IconForSignature("application/x-vnd.Be-TSKB",
			B_LARGE_ICON);
		if (icon != NULL) {
			float size = std::min(bounds.Height() - 4, 32.0f);
			BRect target(0, 0, size - 1, size - 1);
			target.OffsetTo(bounds.left + (bounds.Width() - size) / 2,
				bounds.top + (bounds.Height() - size) / 2);
			SetDrawingMode(B_OP_ALPHA);
			DrawBitmap(icon, icon->Bounds(), target, B_FILTER_BITMAP_BILINEAR);
			SetDrawingMode(B_OP_COPY);
			delete icon;
		} else {
			SetHighUIColor(B_MENU_ITEM_TEXT_COLOR);
			be_control_look->DrawLabel(this, "Haiku", bounds, update, base, 0,
				BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE));
		}
	}

	void MouseDown(BPoint where)
	{
		fPressed = true;
		Invalidate();
		BPopUpMenu* menu = new BPopUpMenu("launcher", false, false);
		menu->SetFont(be_plain_font);
		BPath path;
		if (find_directory(B_USER_DESKBAR_DIRECTORY, &path) == B_OK)
			_AddDirectory(menu, path.Path(), 0);
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Lumina Mode" B_UTF8_ELLIPSIS,
			new BMessage(kMsgOpenModePreflet)));
		menu->SetTargetForItems(be_app);
		_TargetAll(menu);
		BPoint screen = ConvertToScreen(Bounds().LeftTop());
		menu->SetAsyncAutoDestruct(true);
		menu->Go(screen, true, false, ConvertToScreen(Bounds()), true);
		fPressed = false;
		Invalidate();
	}

private:
	void _TargetAll(BMenu* menu)
	{
		for (int32 i = 0; i < menu->CountItems(); i++) {
			BMenuItem* item = menu->ItemAt(i);
			if (item->Submenu() != NULL)
				_TargetAll(item->Submenu());
			else
				item->SetTarget(be_app);
		}
	}

	void _AddDirectory(BMenu* menu, const char* directoryPath, int depth)
	{
		if (depth > 4)
			return;
		BDirectory directory(directoryPath);
		std::vector<BEntry> entries;
		BEntry entry;
		while (directory.GetNextEntry(&entry, false) == B_OK)
			entries.push_back(entry);
		std::sort(entries.begin(), entries.end(), [](BEntry& a, BEntry& b) {
			char na[B_FILE_NAME_LENGTH], nb[B_FILE_NAME_LENGTH];
			a.GetName(na);
			b.GetName(nb);
			return strcasecmp(na, nb) < 0;
		});
		for (BEntry& item : entries) {
			char name[B_FILE_NAME_LENGTH];
			item.GetName(name);
			BEntry resolved(item);
			entry_ref ref;
			BEntry target;
			if (resolved.GetRef(&ref) != B_OK)
				continue;
			BEntry traversed(&ref, true);
			entry_ref targetRef;
			traversed.GetRef(&targetRef);
			if (traversed.IsDirectory()) {
				BPath sub;
				traversed.GetPath(&sub);
				BMenu* submenu = new BMenu(name);
				submenu->SetFont(be_plain_font);
				_AddDirectory(submenu, sub.Path(), depth + 1);
				menu->AddItem(new IconMenuItem(submenu, IconForRef(targetRef)));
			} else {
				BMessage* launch = new BMessage(kMsgLaunchRef);
				launch->AddRef("refs", &targetRef);
				menu->AddItem(new IconMenuItem(name, launch,
					IconForRef(targetRef)));
			}
		}
	}

	bool fPressed = false;
};


// #pragma mark - window list


class WindowListApplet : public Applet {
public:
	WindowListApplet(const AppletContext& context)
		: Applet("windows", context, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetExplicitMinSize(BSize(60, context.thickness));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, context.thickness));
		fGrouped = context.argument != "nogroups";
	}

	~WindowListApplet()
	{
		for (auto& entry : fIcons)
			delete entry.second;
	}

	void WindowsChanged(const std::vector<WindowSnapshot>& windows)
	{
		fItems.clear();
		std::map<team_id, size_t> byTeam;
		int32 workspace = current_workspace();
		for (const WindowSnapshot& w : windows) {
			if ((w.workspaces & (1UL << workspace)) == 0)
				continue;	// Deskbar semantics: current workspace only
			if (fGrouped) {
				auto it = byTeam.find(w.team);
				if (it != byTeam.end()) {
					fItems[it->second].count++;
					fItems[it->second].allMinimized &= w.minimized;
					continue;
				}
				byTeam[w.team] = fItems.size();
			}
			Item item;
			item.window = w;
			item.allMinimized = w.minimized;
			fItems.push_back(item);
		}
		Invalidate();
	}

	void Draw(BRect update)
	{
		BRect bounds = Bounds();
		rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
		DrawPanelBackground(update);
		if (fItems.empty())
			return;
		float width = std::min(180.0f, bounds.Width() / fItems.size());
		font_height fh;
		GetFontHeight(&fh);
		for (size_t i = 0; i < fItems.size(); i++) {
			Item& item = fItems[i];
			BRect r(bounds.left + i * width, bounds.top,
				bounds.left + (i + 1) * width - 2, bounds.bottom);
			item.rect = r;
			uint32 flags = item.allMinimized ? 0 : BControlLook::B_ACTIVATED;
			if (r.Intersects(update)) {
				be_control_look->DrawButtonBackground(this, r, update, base,
					flags, BControlLook::B_ALL_BORDERS);
				BBitmap* icon = _Icon(item.window.signature);
				float x = r.left + 4;
				if (icon != NULL) {
					SetDrawingMode(B_OP_ALPHA);
					DrawBitmapAsync(icon, BPoint(x,
						r.top + (r.Height() - icon->Bounds().Height()) / 2));
					SetDrawingMode(B_OP_COPY);
					x += icon->Bounds().Width() + 5;
				}
				BString label = item.window.title;
				if (fGrouped && item.count > 1)
					label << " (" << item.count << ")";
				float available = r.right - x - 4;
				TruncateString(&label, B_TRUNCATE_END, available);
				SetHighUIColor(item.allMinimized ? B_MENU_ITEM_TEXT_COLOR
					: B_MENU_ITEM_TEXT_COLOR, item.allMinimized ? 1.3f : 1.0f);
				DrawString(label.String(), BPoint(x,
					r.top + (r.Height() + fh.ascent - fh.descent) / 2));
			}
		}
	}

	void MouseDown(BPoint where)
	{
		for (const Item& item : fItems) {
			if (!item.rect.Contains(where))
				continue;
			bool front = _IsFrontTeam(item.window.team);
			BRect anchor = ConvertToScreen(item.rect);
			if (fGrouped && item.count > 1) {
				// Whole-team actions have no single window to animate.
				if (front && !item.allMinimized)
					PrivateApi::MinimizeTeam(item.window.team);
				else
					PrivateApi::BringTeamToFront(item.window.team);
			} else {
				bool minimize = front && !item.window.minimized;
				_WindowAction(item.window.token, minimize, anchor);
			}
			return;
		}
	}

private:
	struct Item {
		WindowSnapshot	window;
		int32			count = 1;
		bool			allMinimized = false;
		BRect			rect;
	};

	// Ask lumina_server so the effects engine can play a transition (proxy
	// zoom to/from this button); fall back to the direct call.
	void _WindowAction(int32 token, bool minimize, BRect anchor)
	{
		haiku::LuminaClient client(300000);
		BMessage command;
		command.AddString(LUMINA_ENGINE_FIELD_OP, minimize ? LUMINA_OP_MINIMIZE
			: LUMINA_OP_ACTIVATE);
		command.AddInt32(LUMINA_ENGINE_FIELD_TOKEN, token);
		command.AddBool("lumina:value", minimize);
		command.AddRect(LUMINA_FIELD_ANCHOR, anchor);
		if (client.IsServerRunning() && client.WindowCommand(command) == B_OK)
			return;
		if (minimize)
			PrivateApi::MinimizeWindow(token, true);
		else
			PrivateApi::BringToFront(token);
	}

	bool _IsFrontTeam(team_id team)
	{
		std::vector<int32> order;
		if (PrivateApi::WindowOrder(-1, order) != B_OK)
			return false;
		for (int32 token : order) {
			WindowSnapshot w;
			if (PrivateApi::GetWindow(token, w) != B_OK
				|| !PrivateApi::IsUserWindow(w) || w.minimized)
				continue;
			return w.team == team;
		}
		return false;
	}

	BBitmap* _Icon(const BString& signature)
	{
		auto it = fIcons.find(signature.String());
		if (it != fIcons.end())
			return it->second;
		BBitmap* icon = IconForSignature(signature.String());
		fIcons[signature.String()] = icon;
		return icon;
	}

	std::vector<Item>	fItems;
	std::map<std::string, BBitmap*> fIcons;
	bool				fGrouped = true;
};


// #pragma mark - clock


class ClockApplet : public Applet {
public:
	ClockApplet(const AppletContext& context)
		: Applet("clock", context, B_WILL_DRAW | B_PULSE_NEEDED)
	{
		const Value* format = context.panelConfig.Find("clockFormat");
		fFormat = format != NULL ? format->AsString("%H:%M") : "%H:%M";
		if (!context.argument.empty())
			fFormat = context.argument;
		float width = StringWidth("00:00:00 PM") + 16;
		SetExplicitPreferredSize(BSize(width, context.thickness));
		SetExplicitMaxSize(PreferredSize());
	}

	void AttachedToWindow()
	{
		Applet::AttachedToWindow();
		Window()->SetPulseRate(1000000);
	}

	void Pulse()
	{
		BString text = _Text();
		if (text != fLast) {
			fLast = text;
			Invalidate();
		}
	}

	void Draw(BRect update)
	{
		rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
		DrawPanelBackground(update);
		fLast = _Text();
		SetHighUIColor(B_MENU_ITEM_TEXT_COLOR);
		be_control_look->DrawLabel(this, fLast.String(), Bounds(), update,
			base, 0, BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE));
	}

private:
	BString _Text() const
	{
		time_t now = time(NULL);
		struct tm local;
		localtime_r(&now, &local);
		char buffer[64];
		strftime(buffer, sizeof(buffer), fFormat.c_str(), &local);
		return buffer;
	}

	std::string	fFormat;
	BString		fLast;
};


// #pragma mark - workspaces


class WorkspacesApplet : public Applet {
public:
	WorkspacesApplet(const AppletContext& context)
		: Applet("workspaces", context, B_WILL_DRAW | B_PULSE_NEEDED)
	{
		SetExplicitPreferredSize(BSize(count_workspaces() * (context.thickness
			- 6) + 4, context.thickness));
		SetExplicitMaxSize(PreferredSize());
	}

	void Pulse()
	{
		if (current_workspace() != fShown || count_workspaces() != fCount)
			Invalidate();
	}

	void Draw(BRect update)
	{
		rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
		DrawPanelBackground(update);
		fCount = count_workspaces();
		fShown = current_workspace();
		float size = Bounds().Height() - 6;
		for (int32 i = 0; i < fCount; i++) {
			BRect r(2 + i * (size + 2), 3, 2 + i * (size + 2) + size, 3 + size);
			uint32 flags = i == fShown ? BControlLook::B_ACTIVATED : 0;
			be_control_look->DrawButtonBackground(this, r, update, base, flags);
			BString label;
			label << (i + 1);
			SetHighUIColor(B_MENU_ITEM_TEXT_COLOR);
			be_control_look->DrawLabel(this, label.String(), r, update, base, 0,
				BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE));
		}
	}

	void MouseDown(BPoint where)
	{
		float size = Bounds().Height() - 6;
		int32 index = int32((where.x - 2) / (size + 2));
		if (index < 0 || index >= count_workspaces())
			return;
		// Through the server so a workspace transition can play.
		BMessenger server(LUMINA_SERVER_SIGNATURE);
		BMessage request(haiku::LUMINA_WORKSPACE_SWITCH);
		request.AddInt32("index", index);
		if (!server.IsValid() || server.SendMessage(&request) != B_OK)
			activate_workspace(index);
	}

private:
	int32	fCount = 0;
	int32	fShown = -1;
};


// #pragma mark - show desktop


class ShowDesktopApplet : public Applet {
public:
	ShowDesktopApplet(const AppletContext& context)
		: Applet("showdesktop", context, B_WILL_DRAW)
	{
		SetExplicitPreferredSize(BSize(context.thickness * 0.7f,
			context.thickness));
		SetExplicitMaxSize(PreferredSize());
	}

	void Draw(BRect update)
	{
		rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
		BRect bounds = Bounds();
		be_control_look->DrawButtonBackground(this, bounds, update, base,
			fMinimized.empty() ? 0 : BControlLook::B_ACTIVATED);
	}

	void MouseDown(BPoint where)
	{
		if (!fMinimized.empty()) {
			for (int32 token : fMinimized)
				PrivateApi::BringToFront(token);
			fMinimized.clear();
		} else {
			std::vector<WindowSnapshot> windows;
			PrivateApi::ListWindows(windows);
			uint32 workspace = 1UL << current_workspace();
			for (const WindowSnapshot& w : windows) {
				if (PrivateApi::IsUserWindow(w) && !w.minimized
					&& (w.workspaces & workspace) != 0) {
					PrivateApi::MinimizeWindow(w.token, true);
					fMinimized.push_back(w.token);
				}
			}
		}
		Invalidate();
	}

private:
	std::vector<int32>	fMinimized;
};


// #pragma mark - spacer / separator


class SpacerApplet : public Applet {
public:
	SpacerApplet(const AppletContext& context)
		: Applet("spacer", context, B_WILL_DRAW)
	{
		float width = context.argument.empty() ? 8
			: float(atof(context.argument.c_str()));
		SetExplicitMinSize(BSize(width, 1));
		if (context.argument == "expand")
			SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
		else
			SetExplicitMaxSize(BSize(width, B_SIZE_UNLIMITED));
	}

	void Draw(BRect update)
	{
		DrawPanelBackground(update);
	}
};

}	// namespace


void
RegisterBuiltinApplets()
{
	AppletRegistry& registry = AppletRegistry::Default();
	registry.Register("launcher", [](const AppletContext& c) {
		return new LauncherApplet(c); });
	registry.Register("windows", [](const AppletContext& c) {
		return new WindowListApplet(c); });
	registry.Register("clock", [](const AppletContext& c) {
		return new ClockApplet(c); });
	registry.Register("workspaces", [](const AppletContext& c) {
		return new WorkspacesApplet(c); });
	registry.Register("showdesktop", [](const AppletContext& c) {
		return new ShowDesktopApplet(c); });
	registry.Register("spacer", [](const AppletContext& c) {
		return new SpacerApplet(c); });
}

}	// namespace panel
}	// namespace lumina
