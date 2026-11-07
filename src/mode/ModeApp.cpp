/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Lumina Mode — the always-available switch between stock Haiku and Lumina.
 *
 * Native Interface Kit only (no Qt), so it works even if optional parts of
 * Lumina are missing. "Restore stock Haiku" works without lumina_server.
 *
 * The same binary provides a Deskbar tray replicant (instantiate_deskbar_item)
 * offering the same choices from the tray.
 */
#include <stdio.h>
#include <string.h>

#include <vector>

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Deskbar.h>
#include <Path.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <MimeType.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <Roster.h>
#include <StringView.h>
#include <Window.h>

#include <lumina/Profile.h>
#include <lumina/haiku/LuminaClient.h>
#include <lumina/haiku/ModeControl.h>
#include <lumina/haiku/Paths.h>
#include <lumina/haiku/Protocol.h>

using namespace lumina;
using namespace lumina::haiku;

namespace {

enum {
	kMsgApply = 'MApl',
	kMsgRestore = 'MRst',
	kMsgSelect = 'MSel',
	kMsgTrayChoice = 'MTry',
	kMsgToggleTray = 'MTgl',
};

const char* kReplicantName = "LuminaModeReplicant";


status_t
ApplyChoice(const BString& choice, BString& message)
{
	LuminaClient client(5000000);
	BString error;
	if (choice == "stock") {
		if (client.IsServerRunning()) {
			if (client.SetMode("stock", NULL, &error) != B_OK) {
				// The server is wedged: restore without it.
				std::vector<std::string> failures;
				ModeControl::RestoreStockLocally(&failures, &message);
				return failures.empty() ? B_OK : B_ERROR;
			}
		} else
			ModeControl::RestoreStockLocally(NULL, &message);
		message = "Stock Haiku restored.";
		return B_OK;
	}
	if (client.EnsureServer() != B_OK) {
		message = "lumina_server could not be started.";
		return B_ERROR;
	}
	if (client.SetMode("lumina", choice.String(), &error) != B_OK) {
		message = error;
		return B_ERROR;
	}
	message.SetToFormat("Lumina (%s) is active.", choice.String());
	return B_OK;
}


std::vector<ProfileInfo>
Personalities()
{
	ProfileStore store(Paths::PersonalityDirs(), Paths::UserProfilesDir());
	return store.List();
}


BString
CurrentChoice()
{
	LuminaClient client(300000);
	ServerStatus status;
	if (client.GetStatus(status) == B_OK && status.mode == "lumina")
		return status.personality;
	return "stock";
}


// #pragma mark - window


class ModeWindow : public BWindow {
public:
	ModeWindow()
		:
		BWindow(BRect(100, 100, 480, 420), "Lumina Mode", B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
	{
		BString current = CurrentChoice();
		BBox* box = new BBox("choices");
		box->SetLabel("Desktop");
		BLayoutBuilder::Group<> group(B_VERTICAL, B_USE_SMALL_SPACING);
		group.SetInsets(B_USE_DEFAULT_SPACING);

		_AddChoice(group, "stock", "Stock Haiku",
			"Lumina off. Exactly your Haiku, nothing running.", current);
		for (const ProfileInfo& info : Personalities()) {
			BString label(info.displayName.c_str());
			if (info.shadowsSystem)
				label << " (customized)";
			_AddChoice(group, info.name.c_str(), label.String(),
				info.description.c_str(), current);
		}
		box->AddChild(group.View());

		fStatus = new BStringView("status", "");
		fApply = new BButton("Apply", new BMessage(kMsgApply));
		fApply->MakeDefault(true);
		BButton* restore = new BButton("Restore stock Haiku now",
			new BMessage(kMsgRestore));
		fTray = new BButton(_TrayInstalled() ? "Remove from Deskbar"
			: "Show in Deskbar", new BMessage(kMsgToggleTray));

		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(box)
			.Add(fStatus)
			.AddGroup(B_HORIZONTAL)
				.Add(fTray)
				.AddGlue()
				.Add(restore)
				.Add(fApply)
			.End();
		CenterOnScreen();
	}

	void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgSelect:
				fSelected = message->GetString("choice", "stock");
				break;
			case kMsgApply:
			{
				BString text;
				fStatus->SetText("Switching" B_UTF8_ELLIPSIS);
				ApplyChoice(fSelected, text);
				fStatus->SetText(text.String());
				break;
			}
			case kMsgRestore:
			{
				std::vector<std::string> failures;
				BString report;
				ModeControl::RestoreStockLocally(&failures, &report);
				fStatus->SetText(failures.empty() ? "Stock Haiku restored."
					: report.String());
				break;
			}
			case kMsgToggleTray:
			{
				BDeskbar deskbar;
				if (_TrayInstalled())
					deskbar.RemoveItem(kReplicantName);
				else {
					app_info info;
					be_app->GetAppInfo(&info);
					deskbar.AddItem(&info.ref);
				}
				fTray->SetLabel(_TrayInstalled() ? "Remove from Deskbar"
					: "Show in Deskbar");
				break;
			}
			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	void _AddChoice(BLayoutBuilder::Group<>& group, const char* name,
		const char* label, const char* description, const BString& current)
	{
		BMessage* message = new BMessage(kMsgSelect);
		message->AddString("choice", name);
		BRadioButton* radio = new BRadioButton(name, label, message);
		if (current == name) {
			radio->SetValue(B_CONTROL_ON);
			fSelected = name;
		}
		group.Add(radio);
		if (description != NULL && description[0] != '\0') {
			BStringView* text = new BStringView(NULL, description);
			text->SetFont(be_plain_font);
			text->SetHighUIColor(B_PANEL_TEXT_COLOR, 0.7f);
			group.AddGroup(B_HORIZONTAL).AddStrut(24).Add(text).End();
		}
	}

	bool _TrayInstalled()
	{
		return BDeskbar().HasItem(kReplicantName);
	}

	BString			fSelected = "stock";
	BStringView*	fStatus;
	BButton*		fApply;
	BButton*		fTray;
};


// #pragma mark - Deskbar replicant


class ModeReplicant : public BView {
public:
	ModeReplicant(BRect frame)
		:
		BView(frame, kReplicantName, B_FOLLOW_NONE, B_WILL_DRAW)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	ModeReplicant(BMessage* archive)
		:
		BView(archive)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	static ModeReplicant* Instantiate(BMessage* archive)
	{
		if (!validate_instantiation(archive, kReplicantName))
			return NULL;
		return new ModeReplicant(archive);
	}

	status_t Archive(BMessage* data, bool deep = true) const
	{
		status_t status = BView::Archive(data, deep);
		data->AddString("add_on", LUMINA_MODE_SIGNATURE);
		data->AddString("class", kReplicantName);
		return status;
	}

	void AttachedToWindow()
	{
		BView::AttachedToWindow();
		AdoptParentColors();
	}

	void Draw(BRect update)
	{
		// A small "L" badge; filled when Lumina is active.
		BRect r = Bounds().InsetByCopy(2, 2);
		bool active = CurrentChoice() != "stock";
		SetHighUIColor(B_CONTROL_HIGHLIGHT_COLOR);
		if (active)
			FillRoundRect(r, 3, 3);
		else
			StrokeRoundRect(r, 3, 3);
		SetHighUIColor(active ? B_CONTROL_BACKGROUND_COLOR
			: B_MENU_ITEM_TEXT_COLOR);
		SetDrawingMode(B_OP_OVER);
		BFont font(be_bold_font);
		font.SetSize(r.Height() - 2);
		SetFont(&font);
		font_height fh;
		font.GetHeight(&fh);
		float x = r.left + (r.Width() - StringWidth("L")) / 2;
		float y = r.top + (r.Height() + fh.ascent - fh.descent) / 2;
		DrawString("L", BPoint(x, y));
	}

	void MouseDown(BPoint where)
	{
		BPopUpMenu* menu = new BPopUpMenu("lumina", false, false);
		BString current = CurrentChoice();
		auto add = [&](const char* name, const char* label) {
			BMessage* message = new BMessage(kMsgTrayChoice);
			message->AddString("choice", name);
			BMenuItem* item = new BMenuItem(label, message);
			item->SetMarked(current == name);
			item->SetTarget(this);
			menu->AddItem(item);
		};
		add("stock", "Stock Haiku");
		for (const ProfileInfo& info : Personalities())
			add(info.name.c_str(), info.displayName.c_str());
		menu->AddSeparatorItem();
		BMenuItem* settings = new BMenuItem("Lumina Mode" B_UTF8_ELLIPSIS,
			new BMessage(B_ABOUT_REQUESTED));
		settings->SetTarget(this);
		menu->AddItem(settings);
		menu->SetAsyncAutoDestruct(true);
		menu->Go(ConvertToScreen(where), true, false, ConvertToScreen(Bounds()),
			true);
	}

	void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgTrayChoice:
			{
				BString text;
				ApplyChoice(message->GetString("choice", "stock"), text);
				Invalidate();
				break;
			}
			case B_ABOUT_REQUESTED:
				be_roster->Launch(LUMINA_MODE_SIGNATURE);
				break;
			default:
				BView::MessageReceived(message);
		}
	}
};


class ModeApp : public BApplication {
public:
	ModeApp() : BApplication(LUMINA_MODE_SIGNATURE) {}

	void ReadyToRun()
	{
		_InstallProfileMimeType();
		if (CountWindows() == 0)
			(new ModeWindow())->Show();
	}

	// Registers the .luminaprofile type so Tracker shows and opens it.
	void _InstallProfileMimeType()
	{
		BMimeType type(LUMINA_PROFILE_MIME_TYPE);
		if (type.IsInstalled())
			return;
		type.Install();
		type.SetShortDescription("Lumina desktop profile");
		type.SetLongDescription("A shareable Lumina for Haiku desktop "
			"configuration");
		type.SetPreferredApp(LUMINA_MODE_SIGNATURE);
		BMessage extensions;
		extensions.AddString("extensions", "luminaprofile");
		type.SetFileExtensions(&extensions);
	}

	// Double-clicking a .luminaprofile in Tracker opens it here.
	void RefsReceived(BMessage* message)
	{
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
			BPath path(&ref);
			std::string bundle;
			if (!ReadTextFile(path.Path(), bundle))
				continue;
			LuminaClient client(5000000);
			client.EnsureServer();
			BMessage request, reply;
			request.AddString("op", "import");
			request.AddString("bundle", bundle.c_str());
			BString text;
			if (client.ProfileOp(request, reply) == B_OK) {
				text.SetToFormat("Imported the desktop profile '%s'. Choose it "
					"in Lumina Mode to use it.", reply.GetString("name", "?"));
				if (reply.HasString("needsApproval"))
					text << "\n\nScript menus in this profile were disabled "
						"for safety.";
			} else {
				text.SetToFormat("Could not import %s:\n%s", ref.name,
					reply.GetString(LUMINA_FIELD_ERROR, "unknown error"));
			}
			(new BAlert("Lumina", text.String(), "OK"))->Go(NULL);
		}
		if (CountWindows() == 0)
			(new ModeWindow())->Show();
	}
};

}	// namespace


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	float size = std::min(maxWidth, maxHeight);
	return new ModeReplicant(BRect(0, 0, size - 1, size - 1));
}


extern "C" _EXPORT BArchivable*
instantiate_LuminaModeReplicant(BMessage* archive)
{
	return ModeReplicant::Instantiate(archive);
}


int
main()
{
	ModeApp app;
	app.Run();
	return 0;
}
