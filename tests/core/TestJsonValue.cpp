#include "../LuminaTest.h"

#include <lumina/Json.h>
#include <lumina/Value.h>

using namespace lumina;

TEST_CASE("json: parses comments and trailing commas")
{
	const char* text = R"({
		// line comment
		"a": 1, /* block */ "b": [true, false, null,],
		"c": { "d": "x\nyé" },
	})";
	Value v;
	JsonError error;
	REQUIRE(ParseJson(text, v, &error));
	CHECK_EQ(v.FindPath("a")->AsInt(), 1);
	CHECK_EQ(v.FindPath("b")->Size(), size_t(3));
	CHECK(v.FindPath("b.2")->IsNull());
	CHECK_EQ(v.FindPath("c.d")->AsString(), std::string("x\ny\xc3\xa9"));
}

TEST_CASE("json: reports error position")
{
	Value v;
	JsonError error;
	CHECK(!ParseJson("{\n  \"a\": tru }", v, &error));
	CHECK_EQ(error.line, 2);
	CHECK(error.column > 1);
	CHECK(!ParseJson("[1, 2", v, &error));
	CHECK(!ParseJson("{\"a\":1} x", v, &error));
	CHECK(!ParseJson("/* open", v, &error));
}

TEST_CASE("json: surrogate pairs and escapes round-trip")
{
	Value v;
	REQUIRE(ParseJson(R"(["😀", "tab\there", "q\"uote"])", v));
	std::string out = WriteJson(v, {false, 0});
	Value back;
	REQUIRE(ParseJson(out, back));
	CHECK(v == back);
	CHECK_EQ(v.Items()[0].AsString(), std::string("\xf0\x9f\x98\x80"));
}

TEST_CASE("json: numbers")
{
	Value v;
	REQUIRE(ParseJson("[0, -3, 1.5, 2e3, -0.25e-2]", v));
	CHECK_EQ(v.Items()[1].AsInt(), -3);
	CHECK_NEAR(v.Items()[2].AsNumber(), 1.5, 1e-12);
	CHECK_NEAR(v.Items()[3].AsNumber(), 2000, 1e-9);
	CHECK_NEAR(v.Items()[4].AsNumber(), -0.0025, 1e-12);
	CHECK(!ParseJson("[01.]", v));
	CHECK_EQ(WriteJson(Value(42), {false, 0}), std::string("42"));
}

TEST_CASE("json: deep nesting is bounded")
{
	std::string deep(100, '[');
	deep += std::string(100, ']');
	Value v;
	CHECK(!ParseJson(deep, v));
}

TEST_CASE("value: paths set/find/erase")
{
	Value v;
	CHECK(v.SetPath("panels.0.edge", "bottom"));
	CHECK(v.FindPath("panels") != nullptr);
	// "panels" was created as an object with key "0", not an array.
	CHECK(v.FindPath("panels")->IsObject());
	Value arr = Value::MakeArray();
	arr.Push(Value::MakeObject());
	v["list"] = arr;
	CHECK(v.SetPath("list.0.x", 5));
	CHECK(v.SetPath("list.1", "appended"));
	CHECK(!v.SetPath("list.5", 1));
	CHECK_EQ(v.FindPath("list.0.x")->AsInt(), 5);
	CHECK_EQ(v.FindPath("list.1")->AsString(), std::string("appended"));
	CHECK(v.ErasePath("list.0"));
	CHECK_EQ(v.FindPath("list")->Size(), size_t(1));
	CHECK(!v.ErasePath("nope.nothing"));
}

TEST_CASE("value: deep merge and equality")
{
	Value a, b;
	ParseJson(R"({"x":{"y":1,"z":2},"list":[1,2]})", a);
	ParseJson(R"({"x":{"z":3},"list":[9]})", b);
	a.MergeFrom(b);
	CHECK_EQ(a.FindPath("x.y")->AsInt(), 1);
	CHECK_EQ(a.FindPath("x.z")->AsInt(), 3);
	CHECK_EQ(a.FindPath("list")->Size(), size_t(1));	// arrays replace
	Value c, d;
	ParseJson(R"({"p":1,"q":2})", c);
	ParseJson(R"({"q":2,"p":1})", d);
	CHECK(c == d);	// order-insensitive
}
