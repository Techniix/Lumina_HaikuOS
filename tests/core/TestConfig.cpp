#include "../LuminaTest.h"

#include <algorithm>

#include <lumina/ConfigSchema.h>
#include <lumina/ConfigStore.h>
#include <lumina/Json.h>

using namespace lumina;

TEST_CASE("schema: builtin parses and defaults are stock-safe")
{
	const ConfigSchema& schema = ConfigSchema::Builtin();
	CHECK(schema.Entries().size() > 30);
	Value defaults = schema.Defaults();
	// Installing Lumina must not change anything until enabled.
	CHECK_EQ(defaults.FindPath("session.mode")->AsString(), std::string("stock"));
	CHECK_EQ(defaults.FindPath("deskbar.mode")->AsString(), std::string("keep"));
	CHECK_EQ(defaults.FindPath("fx.quality")->AsString(), std::string("off"));
	CHECK(!defaults.FindPath("windows.animation.enabled")->AsBool(true));
	CHECK(!defaults.FindPath("windows.snap.enabled")->AsBool(true));
}

TEST_CASE("schema: wildcard paths and validation")
{
	const ConfigSchema& schema = ConfigSchema::Builtin();
	CHECK(ConfigSchema::PathMatches("panels.*.edge", "panels.3.edge"));
	CHECK(!ConfigSchema::PathMatches("panels.*.edge", "panels.x.edge"));
	CHECK(!ConfigSchema::PathMatches("panels.*.edge", "panels.3"));
	std::string message;
	CHECK(schema.ValidateValue("panels.0.edge", Value("left"), &message));
	CHECK(!schema.ValidateValue("panels.0.edge", Value("middle"), &message));
	CHECK(!schema.ValidateValue("panels.0.thickness", Value(4), &message));
	CHECK(!schema.ValidateValue("windows.animation.durationMs", Value(1.5),
		&message));
	CHECK(schema.ValidateValue("fx.panelGlass.tint", Value("#11223344"),
		&message));
	CHECK(!schema.ValidateValue("fx.panelGlass.tint", Value("red"), &message));
	CHECK(!schema.ValidateValue("no.such.key", Value(1), &message));
}

TEST_CASE("schema: document validation descends into panel elements")
{
	Value doc;
	REQUIRE(ParseJson(R"({
		"panels": [ { "edge": "bottom", "thickness": 2000, "mystery": 1 } ],
		"session": { "mode": "lumina" },
		"unknownTop": true
	})", doc));
	auto issues = ConfigSchema::Builtin().Validate(doc);
	bool thickness = false, mystery = false, unknown = false;
	for (const auto& issue : issues) {
		if (issue.path == "panels.0.thickness" && issue.fatal)
			thickness = true;
		if (issue.path == "panels.0.mystery" && !issue.fatal)
			mystery = true;
		if (issue.path == "unknownTop" && !issue.fatal)
			unknown = true;
	}
	CHECK(thickness);
	CHECK(mystery);
	CHECK(unknown);
	Value copy = doc;
	ConfigSchema::Builtin().Sanitize(copy);
	CHECK(copy.FindPath("panels.0.thickness") == nullptr);
	CHECK(copy.FindPath("panels.0.edge") != nullptr);
	CHECK(copy.FindPath("unknownTop") != nullptr);	// unknown keys kept
}

TEST_CASE("color parsing")
{
	uint8_t r, g, b, a;
	CHECK(ParseColor("#fff", r, g, b, a));
	CHECK_EQ(int(r), 255);
	CHECK(ParseColor("#10203040", r, g, b, a));
	CHECK_EQ(int(g), 0x20);
	CHECK_EQ(int(a), 0x40);
	CHECK(!ParseColor("#12345", r, g, b, a));
	CHECK(!ParseColor("123456", r, g, b, a));
}

TEST_CASE("store: layering, sources, generation, diffs")
{
	ConfigStore store;
	store.AddLayer("personality", true);
	store.AddLayer("user", false);
	CHECK_EQ(store.GetString("session.mode"), std::string("stock"));
	CHECK_EQ(store.SourceOf("session.mode"), std::string("defaults"));

	Value personality;
	ParseJson(R"({"windows":{"snap":{"enabled":true}},"fx":{"quality":"medium"}})",
		personality);
	auto changed = store.ReplaceLayer("personality", personality);
	CHECK(std::find(changed.begin(), changed.end(), "windows.snap.enabled")
		!= changed.end());
	CHECK(store.GetBool("windows.snap.enabled"));

	uint64_t generation = store.Generation();
	std::string error;
	CHECK(!store.Set("personality", "fx.quality", Value("high"), &error));
	CHECK(!store.Set("user", "fx.quality", Value("ultra"), &error));
	CHECK(store.Set("user", "fx.quality", Value("high"), &error, &changed));
	CHECK_EQ(changed.size(), size_t(1));
	CHECK_EQ(store.GetString("fx.quality"), std::string("high"));
	CHECK_EQ(store.SourceOf("fx.quality"), std::string("user"));
	CHECK(store.Generation() > generation);

	// Setting the same value again changes nothing.
	generation = store.Generation();
	CHECK(store.Set("user", "fx.quality", Value("high"), &error, &changed));
	CHECK(changed.empty());
	CHECK_EQ(store.Generation(), generation);

	CHECK(store.Unset("user", "fx.quality", &changed));
	CHECK_EQ(store.GetString("fx.quality"), std::string("medium"));
}

TEST_CASE("store: element defaults through wildcard entries")
{
	ConfigStore store;
	store.AddLayer("user", false);
	std::string error;
	Value panels = Value::MakeArray();
	Value panel = Value::MakeObject();
	panel["edge"] = "top";
	panels.Push(panel);
	REQUIRE(store.Set("user", "panels", panels, &error));
	CHECK_EQ(store.GetString("panels.0.edge"), std::string("top"));
	CHECK(store.Set("user", "panels.0.thickness", Value(30), &error));
	CHECK_EQ(int(store.GetNumber("panels.0.thickness")), 30);
	CHECK(!store.Set("user", "panels.0.thickness", Value(3), &error));
}

TEST_CASE("store: replacing a layer drops invalid values")
{
	ConfigStore store;
	store.AddLayer("user", false);
	Value doc;
	ParseJson(R"({"session":{"mode":"bogus","personality":"modern"}})", doc);
	std::vector<ValidationIssue> issues;
	store.ReplaceLayer("user", doc, &issues);
	CHECK_EQ(store.GetString("session.mode"), std::string("stock"));
	CHECK_EQ(store.GetString("session.personality"), std::string("modern"));
	CHECK(!issues.empty());
}
