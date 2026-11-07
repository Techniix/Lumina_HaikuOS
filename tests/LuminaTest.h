/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Minimal self-registering test framework (no external dependency, so the
 * tests build identically on Haiku and on CI hosts).
 */
#ifndef LUMINA_TEST_H
#define LUMINA_TEST_H

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace lumina_test {

struct TestCase {
	const char*				name;
	std::function<void()>	body;
};

inline std::vector<TestCase>& Registry()
{
	static std::vector<TestCase> sTests;
	return sTests;
}

struct Registrar {
	Registrar(const char* name, std::function<void()> body)
		{ Registry().push_back({name, std::move(body)}); }
};

struct State {
	int		failures = 0;
	int		checks = 0;
	const char* current = "";
};

inline State& GetState()
{
	static State sState;
	return sState;
}

struct FatalFailure {};

inline void Fail(const char* file, int line, const std::string& message,
	bool fatal)
{
	GetState().failures++;
	std::fprintf(stderr, "  FAIL %s:%d [%s] %s\n", file, line,
		GetState().current, message.c_str());
	if (fatal)
		throw FatalFailure();
}

template<typename A, typename B>
std::string Describe(const A& a, const B& b)
{
	std::ostringstream s;
	s << "'" << a << "' vs '" << b << "'";
	return s.str();
}

int RunAll(int argc, char** argv);

}	// namespace lumina_test

#define LT_CONCAT2(a, b) a##b
#define LT_CONCAT(a, b) LT_CONCAT2(a, b)

#define TEST_CASE(name) \
	static void LT_CONCAT(lt_test_, __LINE__)(); \
	static ::lumina_test::Registrar LT_CONCAT(lt_reg_, __LINE__)(name, \
		LT_CONCAT(lt_test_, __LINE__)); \
	static void LT_CONCAT(lt_test_, __LINE__)()

#define CHECK(cond) do { ::lumina_test::GetState().checks++; \
	if (!(cond)) ::lumina_test::Fail(__FILE__, __LINE__, #cond, false); } while (0)
#define REQUIRE(cond) do { ::lumina_test::GetState().checks++; \
	if (!(cond)) ::lumina_test::Fail(__FILE__, __LINE__, #cond, true); } while (0)
#define CHECK_EQ(a, b) do { ::lumina_test::GetState().checks++; \
	auto lt_a = (a); auto lt_b = (b); if (!(lt_a == lt_b)) \
	::lumina_test::Fail(__FILE__, __LINE__, #a " == " #b ": " \
		+ ::lumina_test::Describe(lt_a, lt_b), false); } while (0)
#define CHECK_NEAR(a, b, eps) do { ::lumina_test::GetState().checks++; \
	double lt_a = (a), lt_b = (b); if (std::fabs(lt_a - lt_b) > (eps)) \
	::lumina_test::Fail(__FILE__, __LINE__, #a " ~= " #b ": " \
		+ ::lumina_test::Describe(lt_a, lt_b), false); } while (0)

#endif
