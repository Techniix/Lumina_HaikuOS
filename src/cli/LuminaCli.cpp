/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * `lumina` — command-line control and diagnostics.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include <Application.h>
#include <Roster.h>
#include <Screen.h>

#include <lumina/ConfigSchema.h>
#include <lumina/ConfigStore.h>
#include <lumina/Json.h>
#include <lumina/Profile.h>
#include <lumina/Rules.h>
#include <lumina/anim/Easing.h>
#include <lumina/fx/Glass.h>
#include <lumina/haiku/EngineLink.h>
#include <lumina/haiku/LuminaClient.h>
#include <lumina/haiku/MessageCodec.h>
#include <lumina/haiku/ModeControl.h>
#include <lumina/haiku/Paths.h>
#include <lumina/haiku/PrivateApi.h>
#include <lumina/haiku/Protocol.h>
#include <lumina/haiku/SystemAppearance.h>

using namespace lumina;
using namespace lumina::haiku;

namespace {

const char* kUsage =
"usage: lumina <command> [arguments]\n"
"\n"
"Mode\n"
"  status                         mode, personality, engine, Deskbar\n"
"  mode stock                     return to stock Haiku (restores snapshot)\n"
"  mode lumina [personality]      enable Lumina (classic|modern|poweruser|minimal|…)\n"
"  restore-stock                  restore stock Haiku WITHOUT the server\n"
"\n"
"Configuration\n"
"  get [path]                     print effective value (JSON) and its layer\n"
"  set <path> <json> [--preview]  validate and set in the user layer\n"
"  unset <path>                   remove a user override\n"
"  confirm                        keep a --preview change\n"
"  schema [--markdown]            list every setting\n"
"  validate <file.lumina>         check a config/profile file against the schema\n"
"\n"
"Profiles\n"
"  profiles                       list personalities and user profiles\n"
"  profile save <name>            save current setup as a profile\n"
"  profile duplicate <src> <dst>  copy a profile\n"
"  profile remove <name>          delete a user profile\n"
"  profile export <name> <file>   write a shareable .luminaprofile\n"
"  profile import <file> [name]   import a .luminaprofile\n"
"\n"
"Windows & engine\n"
"  windows                        list windows (token, team, title, frame)\n"
"  engine                         Window Engine status and statistics\n"
"  engine move <token> <l> <t> <r> <b>\n"
"  engine animate <token> <l> <t> <r> <b> [ms] [curve]\n"
"  engine snap <token> <zone>     e.g. left-half, top-right-quarter, maximize\n"
"\n"
"Diagnostics\n"
"  diag                           recent server log, counters, effective config\n"
"  snapshot                       print the current system appearance capture\n"
"  bench-glass [w] [h]            time the frosted-glass pipeline (CPU only)\n";

int
Fail(const char* format, const char* argument = "")
{
	fprintf(stderr, "lumina: ");
	fprintf(stderr, format, argument);
	fprintf(stderr, "\n");
	return 1;
}


int
PrintJson(const Value& value)
{
	printf("%s", WriteJson(value).c_str());
	return 0;
}


int
CmdStatus(LuminaClient& client)
{
	ServerStatus status;
	EngineInfo engine;
	EngineLink link;
	link.Probe(engine);
	if (client.GetStatus(status) == B_OK) {
		printf("mode:          %s\n", status.mode.String());
		printf("personality:   %s\n", status.personality.String());
		printf("deskbar:       %s (%s)\n", status.deskbarMode.String(),
			ModeControl::IsDeskbarRunning() ? "running" : "not running");
		printf("server:        running, %d recent crash(es)\n",
			int(status.crashCount));
	} else {
		printf("mode:          %s (server not running)\n",
			ModeControl::IsEnabledAtLogin() ? "lumina at next login" : "stock");
		printf("deskbar:       %s\n",
			ModeControl::IsDeskbarRunning() ? "running" : "not running");
	}
	printf("start at login: %s\n", ModeControl::IsEnabledAtLogin() ? "yes" : "no");
	printf("engine:        %s%s%s\n", !engine.present ? "not loaded"
		: engine.active ? "active" : "loaded but INACTIVE",
		engine.reason.IsEmpty() ? "" : " — ", engine.reason.String());
	BString addon;
	if (EngineLink::FindEngineAddOn(addon))
		printf("engine add-on: %s\n", addon.String());
	printf("haiku:         %s\n", PrivateApi::RunningHaikuRevision().String());
	return 0;
}


int
CmdMode(LuminaClient& client, int argc, char** argv)
{
	if (argc < 3)
		return Fail("mode needs 'stock' or 'lumina'");
	BString error;
	if (strcmp(argv[2], "stock") == 0) {
		if (!client.IsServerRunning()) {
			// Nothing running: only clear the login marker.
			ModeControl::SetEnabledAtLogin(false);
			printf("Lumina is off (it was not running).\n");
			return 0;
		}
		if (client.SetMode("stock", NULL, &error) != B_OK)
			return Fail("%s", error.String());
		printf("Returning to stock Haiku.\n");
		return 0;
	}
	if (strcmp(argv[2], "lumina") == 0) {
		if (client.EnsureServer() != B_OK)
			return Fail("cannot start lumina_server");
		if (client.SetMode("lumina", argc > 3 ? argv[3] : NULL, &error) != B_OK)
			return Fail("%s", error.String());
		printf("Lumina enabled%s%s.\n", argc > 3 ? " with " : "",
			argc > 3 ? argv[3] : "");
		return 0;
	}
	return Fail("unknown mode '%s'", argv[2]);
}


int
CmdGet(LuminaClient& client, int argc, char** argv)
{
	Value value;
	BString error;
	if (client.Get(argc > 2 ? argv[2] : "", value, &error) != B_OK)
		return Fail("%s", error.String());
	return PrintJson(value);
}


int
CmdSet(LuminaClient& client, int argc, char** argv)
{
	if (argc < 4)
		return Fail("set needs <path> <json>");
	Value value;
	JsonError jsonError;
	if (!ParseJson(argv[3], value, &jsonError)) {
		// Convenience: bare words are strings.
		value = Value(argv[3]);
	}
	bool preview = argc > 4 && strcmp(argv[4], "--preview") == 0;
	BString error;
	if (client.Set(argv[2], value, &error, preview) != B_OK)
		return Fail("%s", error.String());
	if (preview)
		printf("Previewing; run `lumina confirm` within 15 s to keep it.\n");
	return 0;
}


int
CmdSchema(int argc, char** argv)
{
	bool markdown = argc > 2 && strcmp(argv[2], "--markdown") == 0;
	const ConfigSchema& schema = ConfigSchema::Builtin();
	if (markdown) {
		printf("| Setting | Type | Default | Description |\n|---|---|---|---|\n");
	}
	for (const SchemaEntry& e : schema.Entries()) {
		std::string type = SchemaTypeName(e.type);
		if (e.type == SchemaType::Enum) {
			type += " (";
			for (size_t i = 0; i < e.values.size(); i++)
				type += (i ? "|" : "") + e.values[i];
			type += ")";
		}
		if (e.hasMin || e.hasMax) {
			char range[64];
			snprintf(range, sizeof(range), " [%g..%g]", e.min, e.max);
			type += range;
		}
		std::string def = e.defaultValue.IsNull() ? "—"
			: WriteJson(e.defaultValue, {false, 0});
		if (markdown) {
			printf("| `%s` | %s | `%s` | %s%s |\n", e.path.c_str(), type.c_str(),
				def.c_str(), e.description.c_str(),
				e.risky ? " *(try-mode)*" : "");
		} else
			printf("%-38s %-28s %s\n", e.path.c_str(), type.c_str(), def.c_str());
	}
	return 0;
}


int
CmdValidate(int argc, char** argv)
{
	if (argc < 3)
		return Fail("validate needs a file");
	std::string text;
	if (!ReadTextFile(argv[2], text))
		return Fail("cannot read %s", argv[2]);
	Value doc;
	JsonError error;
	if (!ParseJson(text, doc, &error))
		return Fail("%s", error.ToString().c_str());
	int fatal = 0;
	for (const ValidationIssue& issue : ConfigSchema::Builtin().Validate(doc)) {
		printf("%s %s: %s\n", issue.fatal ? "error  " : "warning", issue.path.c_str(),
			issue.message.c_str());
		fatal += issue.fatal ? 1 : 0;
	}
	if (const Value* rules = doc.FindPath("windows.rules")) {
		RuleSet set;
		std::vector<RuleIssue> issues;
		set.Load(*rules, &issues);
		for (const RuleIssue& issue : issues) {
			printf("error   windows.rules.%d: %s\n", issue.index,
				issue.message.c_str());
			fatal++;
		}
	}
	printf("%s\n", fatal == 0 ? "valid" : "invalid");
	return fatal == 0 ? 0 : 1;
}


int
CmdProfiles(LuminaClient& client, int argc, char** argv)
{
	BMessage request, reply;
	if (strcmp(argv[1], "profiles") == 0) {
		request.AddString("op", "list");
		if (client.ProfileOp(request, reply) != B_OK) {
			// Offline listing straight from disk.
			ProfileStore store(Paths::PersonalityDirs(), Paths::UserProfilesDir());
			for (const ProfileInfo& info : store.List())
				printf("%-16s %-24s %s\n", info.name.c_str(),
					info.displayName.c_str(), info.readOnly ? "system" : "user");
			return 0;
		}
		BMessage item;
		for (int32 i = 0; reply.FindMessage("profile", i, &item) == B_OK; i++) {
			printf("%-16s %-24s %s%s\n", item.GetString("name", ""),
				item.GetString("displayName", ""),
				item.GetBool("readOnly", false) ? "system" : "user",
				item.GetBool("customized", false) ? " (customized)" : "");
		}
		return 0;
	}
	if (argc < 4)
		return Fail("profile needs a sub-command and a name");
	const char* op = argv[2];
	request.AddString("op", op);
	if (strcmp(op, "import") == 0) {
		std::string bundle;
		if (!ReadTextFile(argv[3], bundle))
			return Fail("cannot read %s", argv[3]);
		request.AddString("bundle", bundle.c_str());
		if (argc > 4)
			request.AddString("name", argv[4]);
	} else {
		request.AddString("name", argv[3]);
		if (strcmp(op, "duplicate") == 0 && argc > 4)
			request.AddString("target", argv[4]);
	}
	status_t status = client.ProfileOp(request, reply);
	if (status != B_OK)
		return Fail("%s", reply.GetString(LUMINA_FIELD_ERROR, strerror(status)));
	if (strcmp(op, "export") == 0) {
		if (argc < 5)
			return Fail("export needs an output file");
		if (!WriteFileAtomic(argv[4], reply.GetString("bundle", "")))
			return Fail("cannot write %s", argv[4]);
		printf("Exported to %s\n", argv[4]);
	} else if (strcmp(op, "import") == 0) {
		printf("Imported as '%s'\n", reply.GetString("name", "?"));
		const char* text;
		for (int32 i = 0; reply.FindString("needsApproval", i, &text) == B_OK; i++)
			printf("  disabled pending approval: %s\n", text);
		for (int32 i = 0; reply.FindString("issue", i, &text) == B_OK; i++)
			printf("  dropped: %s\n", text);
	}
	return 0;
}


int
CmdWindows()
{
	std::vector<WindowSnapshot> windows;
	if (PrivateApi::ListWindows(windows) != B_OK)
		return Fail("cannot read the window list");
	printf("%-7s %-6s %-5s %-24s %s\n", "token", "team", "flags", "frame", "title");
	for (const WindowSnapshot& w : windows) {
		if (!PrivateApi::IsUserWindow(w))
			continue;
		char frame[64];
		snprintf(frame, sizeof(frame), "%.0f,%.0f-%.0f,%.0f", w.frame.left,
			w.frame.top, w.frame.right, w.frame.bottom);
		printf("%-7d %-6d %-5s %-24s %s  [%s]\n", int(w.token), int(w.team),
			w.minimized ? "min" : "", frame, w.title.String(),
			w.signature.String());
	}
	return 0;
}


bool
ParseRect(char** argv, BRect& rect)
{
	for (int i = 0; i < 4; i++) {
		char* end;
		strtod(argv[i], &end);
		if (*end != '\0')
			return false;
	}
	rect.Set(atof(argv[0]), atof(argv[1]), atof(argv[2]), atof(argv[3]));
	return rect.IsValid();
}


int
CmdEngine(int argc, char** argv)
{
	EngineLink link;
	EngineInfo info;
	link.Probe(info);
	if (argc == 2) {
		printf("present:  %s\n", info.present ? "yes" : "no");
		if (!info.present) {
			printf("The Lumina decorator is not selected (Appearance → "
				"Decorator, or `lumina mode lumina`).\n");
			return 0;
		}
		printf("active:   %s\n", info.active ? "yes" : "no");
		printf("protocol: %d\n", int(info.version));
		printf("built:    %s\nrunning:  %s\n", info.builtAbi.String(),
			info.runningAbi.String());
		if (!info.reason.IsEmpty())
			printf("note:     %s\n", info.reason.String());
		printf("animating: %d\n", int(info.animating));
		Value stats;
		if (ParseJson(info.statsJson.String(), stats))
			PrintJson(stats);
		return 0;
	}
	if (!info.present || !info.active)
		return Fail("engine not active");
	if (strcmp(argv[2], "move") == 0 && argc >= 8) {
		BRect frame;
		if (!ParseRect(argv + 4, frame))
			return Fail("bad rectangle");
		return link.MoveResize(atoi(argv[3]), frame) == B_OK ? 0 : 1;
	}
	if (strcmp(argv[2], "animate") == 0 && argc >= 8) {
		BRect frame;
		if (!ParseRect(argv + 4, frame))
			return Fail("bad rectangle");
		float ms = argc > 8 ? float(atof(argv[8])) : 250;
		const char* curve = argc > 9 ? argv[9] : "ease-out-cubic";
		anim::Curve parsed;
		std::string error;
		if (!anim::ParseCurve(curve, parsed, &error))
			return Fail("%s", error.c_str());
		// The CLI is its own clock: tick until the engine reports idle.
		if (link.Animate(atoi(argv[3]), frame, ms / 1000.0f, curve) != B_OK)
			return Fail("command failed");
		bigtime_t until = system_time() + bigtime_t(parsed.Duration(ms / 1000.0)
			* 1000000) + 200000;
		BMessage tick(LUMINA_ENGINE_COMMAND);
		tick.AddString(LUMINA_ENGINE_FIELD_OP, "tick");
		int frames = 0;
		while (system_time() < until) {
			BMessage copy(tick);
			link.Send(copy);
			frames++;
			snooze(16667);
		}
		printf("sent %d ticks\n", frames);
		return 0;
	}
	if (strcmp(argv[2], "snap") == 0 && argc >= 5) {
		WindowSnapshot w;
		if (PrivateApi::GetWindow(atoi(argv[3]), w) != B_OK)
			return Fail("no such window");
		BScreen screen;
		BRect s = screen.Frame();
		RectF target;
		if (!PlacementRect(argv[4], RectF{s.left, s.top, s.right, s.bottom},
				target))
			return Fail("unknown zone '%s'", argv[4]);
		BRect content(target.left + w.borderSize,
			target.top + w.tabHeight + w.borderSize,
			target.right - w.borderSize, target.bottom - w.borderSize);
		return link.MoveResize(w.token, content) == B_OK ? 0 : 1;
	}
	fputs(kUsage, stderr);
	return 1;
}


int
CmdDiag(LuminaClient& client)
{
	BMessage reply;
	if (client.GetDiagnostics(reply) != B_OK)
		return Fail("lumina_server is not running (logs: %s)",
			Paths::LogDir().c_str());
	const char* line;
	for (int32 i = 0; reply.FindString("log", i, &line) == B_OK; i++)
		printf("%s\n", line);
	printf("\nwarnings: %" B_PRId64 "  errors: %" B_PRId64 "\n",
		reply.GetInt64("warnings", 0), reply.GetInt64("errors", 0));
	if (const char* stats = reply.GetString("engine_stats", NULL))
		printf("engine: %s\n", stats);
	return 0;
}


int
CmdBenchGlass(int argc, char** argv)
{
	int width = argc > 2 ? atoi(argv[2]) : 1920;
	int height = argc > 3 ? atoi(argv[3]) : 32;
	fx::Image source(width, height), out(width, height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			uint8_t* p = source.View().Pixel(x, y);
			p[0] = uint8_t(x * 255 / width);
			p[1] = uint8_t(((x / 8 + y / 8) & 1) * 255);
			p[2] = uint8_t(y * 255 / height);
			p[3] = 255;
		}
	}
	fx::GlassRenderer renderer;
	for (int downsample = 0; downsample <= 3; downsample++) {
		fx::GlassParams params;
		params.downsample = downsample;
		double total = 0, worst = 0;
		const int runs = 50;
		for (int i = 0; i < runs; i++) {
			fx::GlassTiming t;
			renderer.Render(source.View(), out.View(), params, &t);
			total += t.totalMs;
			worst = std::max(worst, t.totalMs);
		}
		printf("glass %dx%d downsample=%d: avg %.2f ms, worst %.2f ms\n",
			width, height, downsample, total / runs, worst);
	}
	return 0;
}

}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0) {
		fputs(kUsage, argc < 2 ? stderr : stdout);
		return argc < 2 ? 1 : 0;
	}
	// A BApplication is needed for BWindow-based engine links and messaging.
	BApplication app(LUMINA_CLI_SIGNATURE);
	LuminaClient client;
	const char* command = argv[1];

	if (strcmp(command, "status") == 0)
		return CmdStatus(client);
	if (strcmp(command, "mode") == 0)
		return CmdMode(client, argc, argv);
	if (strcmp(command, "restore-stock") == 0) {
		std::vector<std::string> failures;
		BString report;
		status_t status = ModeControl::RestoreStockLocally(&failures, &report);
		printf("%s\n", report.String());
		for (const std::string& f : failures)
			printf("  failed: %s\n", f.c_str());
		return status == B_OK ? 0 : 1;
	}
	if (strcmp(command, "get") == 0)
		return CmdGet(client, argc, argv);
	if (strcmp(command, "set") == 0)
		return CmdSet(client, argc, argv);
	if (strcmp(command, "unset") == 0 && argc > 2) {
		BString error;
		return client.Unset(argv[2], &error) == B_OK ? 0
			: Fail("%s", error.String());
	}
	if (strcmp(command, "confirm") == 0)
		return client.Confirm() == B_OK ? 0 : Fail("nothing to confirm");
	if (strcmp(command, "schema") == 0)
		return CmdSchema(argc, argv);
	if (strcmp(command, "validate") == 0)
		return CmdValidate(argc, argv);
	if (strcmp(command, "profiles") == 0 || strcmp(command, "profile") == 0)
		return CmdProfiles(client, argc, argv);
	if (strcmp(command, "windows") == 0)
		return CmdWindows();
	if (strcmp(command, "engine") == 0)
		return CmdEngine(argc, argv);
	if (strcmp(command, "diag") == 0)
		return CmdDiag(client);
	if (strcmp(command, "snapshot") == 0)
		return PrintJson(SystemAppearance::Capture());
	if (strcmp(command, "bench-glass") == 0)
		return CmdBenchGlass(argc, argv);
	fputs(kUsage, stderr);
	return 1;
}
