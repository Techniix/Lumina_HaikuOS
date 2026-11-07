#include "../LuminaTest.h"

#include <cstdlib>
#include <filesystem>

#include <lumina/ConfigStore.h>
#include <lumina/Profile.h>
#include <lumina/Rules.h>
#include <lumina/fx/Strategy.h>

using namespace lumina;

// Validates the personalities shipped in data/personalities: every one must
// load with zero schema issues, and Classic must keep stock behaviour.
TEST_CASE("personalities: shipped profiles are valid")
{
	const char* dataDir = std::getenv("LUMINA_DATA_DIR");
	if (dataDir == nullptr) {
		std::printf("    (LUMINA_DATA_DIR not set; skipped)\n");
		return;
	}
	std::string root = std::string(dataDir) + "/personalities";
	ProfileStore store({root}, "/nonexistent-user-root");
	auto list = store.List();
	std::vector<std::string> names;
	for (const ProfileInfo& info : list)
		names.push_back(info.name);
	for (const char* required : {"classic", "modern", "poweruser", "minimal"}) {
		bool found = false;
		for (const std::string& n : names)
			found |= n == required;
		CHECK(found);
	}
	for (const ProfileInfo& info : list) {
		Value doc;
		std::vector<ValidationIssue> issues;
		std::string error;
		REQUIRE(store.Load(info.name, doc, &issues, &error));
		for (const ValidationIssue& issue : issues) {
			std::printf("    %s: %s %s\n", info.name.c_str(), issue.path.c_str(),
				issue.message.c_str());
		}
		CHECK(issues.empty());
		auto remaining = ConfigSchema::Builtin().Validate(doc);
		CHECK(remaining.empty());
		RuleSet rules;
		if (const Value* r = doc.FindPath("windows.rules"))
			CHECK(rules.Load(*r));
	}

	Value classic;
	REQUIRE(store.Load("classic", classic));
	ConfigStore config;
	config.AddLayer("personality", true);
	config.ReplaceLayer("personality", classic);
	CHECK_EQ(config.GetString("deskbar.mode"), std::string("keep"));
	CHECK_EQ(config.GetString("windows.decorator.style"), std::string("haiku"));
	CHECK_EQ(config.GetString("fx.quality"), std::string("off"));
	CHECK(!config.GetBool("windows.animation.enabled", true));
}
