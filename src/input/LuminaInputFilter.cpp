/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * input_server filter: the emergency chord.
 *
 *   Ctrl + Alt + Shift + Backspace  →  restore stock Haiku
 *
 * Works whatever state Lumina is in (server hung, panel crashed, Deskbar
 * hidden): it runs `lumina restore-stock`, which needs no server. Always
 * active while the Lumina package is installed; never consumes any other
 * key. ("Alt" is B_COMMAND_KEY in Haiku's default keymap.)
 */
#include <image.h>
#include <new>
#include <stdio.h>
#include <unistd.h>

#include <InputServerFilter.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Path.h>
#include <PathFinder.h>

namespace {

const uint32 kChordModifiers = B_CONTROL_KEY | B_COMMAND_KEY | B_SHIFT_KEY;

class LuminaInputFilter : public BInputServerFilter {
public:
	LuminaInputFilter() : fLastFire(0) {}

	status_t InitCheck() { return B_OK; }

	filter_result Filter(BMessage* message, BList* outList)
	{
		if (message->what != B_KEY_DOWN && message->what != B_UNMAPPED_KEY_DOWN)
			return B_DISPATCH_MESSAGE;
		int32 modifiers = 0;
		const char* bytes = NULL;
		if (message->FindInt32("modifiers", &modifiers) != B_OK)
			return B_DISPATCH_MESSAGE;
		if ((uint32(modifiers) & kChordModifiers) != kChordModifiers)
			return B_DISPATCH_MESSAGE;
		if (message->FindString("bytes", &bytes) != B_OK || bytes == NULL
			|| bytes[0] != B_BACKSPACE)
			return B_DISPATCH_MESSAGE;
		// Debounce key repeat.
		bigtime_t now = system_time();
		if (now - fLastFire > 2000000) {
			fLastFire = now;
			_RestoreStock();
		}
		return B_SKIP_MESSAGE;
	}

private:
	void _RestoreStock()
	{
		BPath path;
		BPathFinder finder;
		if (finder.FindPath(B_FIND_PATH_BIN_DIRECTORY, "lumina",
				B_FIND_PATH_EXISTING_ONLY, path) != B_OK) {
			// Last resort: ask a running server directly.
			BMessenger server("application/x-vnd.Lumina-Server");
			server.SendMessage(uint32('Lrst'));
			return;
		}
		const char* argv[] = {path.Path(), "restore-stock", NULL};
		thread_id thread = load_image(2, argv, (const char**)environ);
		if (thread >= 0)
			resume_thread(thread);
	}

	bigtime_t	fLastFire;
};

}	// namespace


extern "C" BInputServerFilter*
instantiate_input_filter()
{
	return new (std::nothrow) LuminaInputFilter();
}
