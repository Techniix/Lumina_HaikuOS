#include "../LuminaTest.h"

#include <lumina/Json.h>
#include <lumina/Log.h>
#include <lumina/Rules.h>

using namespace lumina;

TEST_CASE("rules: matching, ordering, final")
{
	Value rules;
	REQUIRE(ParseJson(R"([
		{ "name": "terminals", "match": {"signature": "application/x-vnd.Haiku-Terminal"},
		  "set": {"workspace": 2, "placement": "right-half"} },
		{ "name": "ssh", "match": {"signature": "application/x-vnd.Haiku-*",
		  "title~": "^ssh "}, "set": {"placement": "left-half"}, "final": true },
		{ "name": "never", "match": {"title~": "."}, "set": {"layer": "desktop"} }
	])", rules));
	RuleSet set;
	std::vector<RuleIssue> issues;
	REQUIRE(set.Load(rules, &issues));
	CHECK_EQ(set.Count(), size_t(3));

	WindowDescriptor w;
	w.signature = "application/x-vnd.Haiku-Terminal";
	w.title = "ssh box";
	std::vector<std::string> matched;
	Value actions = set.Evaluate(w, &matched);
	CHECK_EQ(matched.size(), size_t(2));
	CHECK_EQ(actions.FindPath("placement")->AsString(), std::string("left-half"));
	CHECK_EQ(actions.FindPath("workspace")->AsInt(), 2);
	CHECK(actions.FindPath("layer") == nullptr);	// stopped by final

	w.title = "local";
	actions = set.Evaluate(w);
	CHECK_EQ(actions.FindPath("placement")->AsString(), std::string("right-half"));
	CHECK_EQ(actions.FindPath("layer")->AsString(), std::string("desktop"));
}

TEST_CASE("rules: bad rules are reported, good ones kept")
{
	Value rules;
	REQUIRE(ParseJson(R"([
		{ "match": {"title~": "(unclosed"}, "set": {} },
		{ "match": {"colour": "red"}, "set": {} },
		"nonsense",
		{ "match": {"title": "x"}, "set": {"workspace": 1} }
	])", rules));
	RuleSet set;
	std::vector<RuleIssue> issues;
	CHECK(!set.Load(rules, &issues));
	CHECK_EQ(issues.size(), size_t(3));
	CHECK_EQ(set.Count(), size_t(1));
}

TEST_CASE("placement presets")
{
	RectF area{0, 0, 1919, 1079};
	RectF out;
	REQUIRE(PlacementRect("left-half", area, out));
	CHECK_EQ(out.right, 959.0f);
	REQUIRE(PlacementRect("right-half", area, out));
	CHECK_EQ(out.left, 960.0f);
	REQUIRE(PlacementRect("bottom-right-quarter", area, out));
	CHECK_EQ(out.top, 540.0f);
	REQUIRE(PlacementRect("center", area, out));
	CHECK(out.left > 0 && out.right < 1919);
	CHECK(!PlacementRect("diagonal", area, out));
}

TEST_CASE("snap zones")
{
	RectF screen{0, 0, 1919, 1079};
	CHECK_EQ(SnapZoneAt(0, 500, screen, 2, "halves"), std::string("left-half"));
	CHECK_EQ(SnapZoneAt(1919, 500, screen, 2, "halves"),
		std::string("right-half"));
	CHECK_EQ(SnapZoneAt(900, 0, screen, 2, "halves"), std::string("maximize"));
	CHECK_EQ(SnapZoneAt(0, 10, screen, 2, "halves+quarters"),
		std::string("top-left-quarter"));
	CHECK_EQ(SnapZoneAt(1919, 1079, screen, 2, "quarters"),
		std::string("bottom-right-quarter"));
	CHECK_EQ(SnapZoneAt(500, 500, screen, 2, "halves+quarters"), std::string());
	CHECK_EQ(SnapZoneAt(0, 500, screen, 2, "quarters"), std::string());
}

namespace {
class CaptureSink : public LogSink {
public:
	void Write(const LogEntry& entry) override { entries.push_back(entry); }
	std::vector<LogEntry> entries;
};
}

TEST_CASE("log: levels, sinks, ring buffer, long messages")
{
	Logger& log = Logger::Default();
	auto sink = std::make_shared<CaptureSink>();
	log.ClearSinks();
	log.AddSink(sink);
	log.SetLevel(LogLevel::Info);
	log.SetRingCapacity(4);
	LUMINA_LOG_DEBUG("test", "hidden %d", 1);
	LUMINA_LOG_INFO("test", "shown %d", 2);
	std::string big(3000, 'x');
	LUMINA_LOG_WARN("test", "%s", big.c_str());
	CHECK_EQ(sink->entries.size(), size_t(2));
	CHECK_EQ(sink->entries[0].message, std::string("shown 2"));
	CHECK_EQ(sink->entries[1].message.size(), size_t(3000));
	for (int i = 0; i < 10; i++)
		LUMINA_LOG_INFO("test", "spam %d", i);
	CHECK_EQ(log.RecentEntries().size(), size_t(4));
	CHECK(FormatLogEntry(sink->entries[0]).find("[info] test: shown 2")
		!= std::string::npos);
	LogLevel level;
	CHECK(ParseLogLevel("warn", level) && level == LogLevel::Warning);
	log.ClearSinks();
}
