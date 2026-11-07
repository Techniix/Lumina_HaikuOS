#include "../LuminaTest.h"

#include <filesystem>
#include <fstream>

#include <lumina/Json.h>
#include <lumina/Profile.h>

namespace fs = std::filesystem;
using namespace lumina;

namespace {
fs::path
TempRoot(const char* name)
{
	fs::path root = fs::temp_directory_path() / ("lumina-test-" + std::string(name));
	fs::remove_all(root);
	fs::create_directories(root);
	return root;
}

void
WriteFile(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}
}	// namespace

TEST_CASE("base64 and fnv")
{
	for (std::string s : {"", "f", "fo", "foo", "foob", "fooba", "foobar"}) {
		std::string decoded;
		REQUIRE(Base64Decode(Base64Encode(s), decoded));
		CHECK_EQ(decoded, s);
	}
	CHECK_EQ(Base64Encode("foobar"), std::string("Zm9vYmFy"));
	std::string bin;
	for (int i = 0; i < 256; i++)
		bin += char(i);
	std::string back;
	REQUIRE(Base64Decode(Base64Encode(bin), back));
	CHECK(back == bin);
	CHECK(!Base64Decode("Zm9v!", back));
	CHECK(Fnv1a64("a") != Fnv1a64("b"));
}

TEST_CASE("profile names")
{
	CHECK(IsValidProfileName("classic"));
	CHECK(IsValidProfileName("My-Profile_2.0"));
	CHECK(!IsValidProfileName(""));
	CHECK(!IsValidProfileName("../evil"));
	CHECK(!IsValidProfileName(".hidden"));
	CHECK(!IsValidProfileName("with space"));
}

TEST_CASE("profile store: system + user layering, load merges sections")
{
	fs::path root = TempRoot("store");
	fs::path system = root / "system", user = root / "user";
	WriteFile(system / "classic" / "profile.lumina",
		R"({"displayName":"Haiku Classic","schemaVersion":1})");
	WriteFile(system / "classic" / "session.lumina",
		R"({"session":{"personality":"classic"}})");
	WriteFile(system / "classic" / "windows.lumina",
		R"({"windows":{"snap":{"enabled":false}}})");
	WriteFile(system / "modern" / "profile.lumina", R"({"displayName":"Modern"})");
	WriteFile(system / "broken" / "nomanifest.lumina", "{}");

	ProfileStore store({system.string()}, user.string());
	auto list = store.List();
	REQUIRE(list.size() == 2);
	CHECK(list[0].readOnly);

	Value doc;
	std::string error;
	REQUIRE(store.Load("classic", doc, nullptr, &error));
	CHECK_EQ(doc.FindPath("session.personality")->AsString(),
		std::string("classic"));
	CHECK(doc.FindPath("windows.snap.enabled") != nullptr);

	// Customise: saving to the user root shadows the system profile.
	doc.SetPath("windows.snap.enabled", true);
	ProfileInfo info;
	store.Find("classic", info);
	REQUIRE(store.Save("classic", doc, info, &error));
	list = store.List();
	REQUIRE(list.size() == 2);
	CHECK(!list[0].readOnly);
	CHECK(list[0].shadowsSystem);
	Value reloaded;
	REQUIRE(store.Load("classic", reloaded));
	CHECK(reloaded.FindPath("windows.snap.enabled")->AsBool());

	CHECK(!store.Remove("modern", &error));	// system profile
	CHECK(store.Remove("classic", &error));		// removes the user copy only
	REQUIRE(store.Load("classic", reloaded));
	CHECK(!reloaded.FindPath("windows.snap.enabled")->AsBool(true));
	fs::remove_all(root);
}

TEST_CASE("profile store: broken section is quarantined, not fatal")
{
	fs::path root = TempRoot("broken");
	WriteFile(root / "p" / "profile.lumina", R"({"displayName":"P"})");
	WriteFile(root / "p" / "a.lumina", R"({"session":{"personality":"x"}})");
	WriteFile(root / "p" / "b.lumina", R"({ this is not json )");
	ProfileStore store({}, root.string());
	Value doc;
	std::vector<ValidationIssue> issues;
	REQUIRE(store.Load("p", doc, &issues));
	CHECK_EQ(doc.FindPath("session.personality")->AsString(), std::string("x"));
	CHECK_EQ(issues.size(), size_t(1));
	fs::remove_all(root);
}

TEST_CASE("profile bundles: export/import round trip with assets")
{
	fs::path root = TempRoot("bundle");
	ProfileStore store({}, (root / "user").string());
	Value doc;
	ParseJson(R"({
		"session": {"personality": "mine"},
		"panels": [{"edge": "top", "applets": ["clock", "jsonmenu:/bin/evil"]}]
	})", doc);
	ProfileInfo info;
	info.displayName = "Mine";
	info.author = "tester";
	std::string error;
	REQUIRE(store.Save("mine", doc, info, &error));
	WriteFile(root / "user" / "mine" / "assets" / "wall" / "bg.png",
		std::string("\x89PNG\0\1\2", 7));

	std::string bundle;
	REQUIRE(store.Export("mine", bundle, &error));

	ProfileStore other({}, (root / "other").string());
	ImportReport report = other.Import(bundle, "imported");
	REQUIRE(report.ok);
	CHECK_EQ(report.name, std::string("imported"));
	CHECK_EQ(report.needsApproval.size(), size_t(1));
	Value loaded;
	REQUIRE(other.Load("imported", loaded));
	// The script applet was stripped pending approval; clock survived.
	CHECK_EQ(loaded.FindPath("panels.0.applets")->Size(), size_t(1));
	std::string asset;
	REQUIRE(ReadTextFile((root / "other" / "imported" / "assets" / "wall"
		/ "bg.png").string(), asset));
	CHECK_EQ(asset.size(), size_t(7));

	// Tampering is detected.
	std::string tampered = bundle;
	size_t pos = tampered.find("\"top\"");
	REQUIRE(pos != std::string::npos);
	tampered.replace(pos, 5, "\"left\"");
	CHECK(!other.Import(tampered, "t").ok);
	CHECK(!other.Import("{\"format\":\"zip\"}").ok);
	fs::remove_all(root);
}

TEST_CASE("profile bundles: path traversal in assets is rejected")
{
	fs::path root = TempRoot("traversal");
	Value bundle = Value::MakeObject();
	bundle["format"] = "lumina-profile";
	bundle["formatVersion"] = 1;
	bundle["name"] = "evil";
	bundle["config"] = Value::MakeObject();
	Value assets = Value::MakeObject();
	assets["../../escape.txt"] = Base64Encode("x");
	assets["ok.txt"] = Base64Encode("y");
	bundle["assets"] = assets;
	ProfileStore store({}, (root / "user").string());
	ImportReport report = store.Import(WriteJson(bundle));
	REQUIRE(report.ok);
	CHECK(!fs::exists(root / "escape.txt"));
	CHECK(fs::exists(root / "user" / "evil" / "assets" / "ok.txt"));
	fs::remove_all(root);
}

TEST_CASE("migration refuses newer schemas")
{
	Value doc = Value::MakeObject();
	std::string error;
	CHECK(MigrateConfig(doc, 1, &error));
	CHECK(!MigrateConfig(doc, kCurrentSchemaVersion + 1, &error));
	CHECK(!MigrateConfig(doc, 0, &error));
}
