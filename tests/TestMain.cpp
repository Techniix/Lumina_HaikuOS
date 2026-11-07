/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "LuminaTest.h"

#include <cstring>
#include <exception>

namespace lumina_test {

int
RunAll(int argc, char** argv)
{
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int run = 0, failedTests = 0;
	for (const TestCase& test : Registry()) {
		if (filter != nullptr && std::strstr(test.name, filter) == nullptr)
			continue;
		run++;
		GetState().current = test.name;
		int before = GetState().failures;
		try {
			test.body();
		} catch (const FatalFailure&) {
		} catch (const std::exception& e) {
			Fail(__FILE__, __LINE__, std::string("exception: ") + e.what(),
				false);
		}
		bool failed = GetState().failures != before;
		if (failed)
			failedTests++;
		std::printf("%s %s\n", failed ? "[FAIL]" : "[ ok ]", test.name);
	}
	std::printf("\n%d tests, %d failed, %d checks, %d check failures\n", run,
		failedTests, GetState().checks, GetState().failures);
	return failedTests == 0 ? 0 : 1;
}

}	// namespace lumina_test


int
main(int argc, char** argv)
{
	return lumina_test::RunAll(argc, argv);
}
