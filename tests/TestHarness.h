#pragma once
// Minimal shared harness for the SDK-free unit tests (tests/*.cpp).
//
// Every tests/*.cpp is one topic and is built into the same executable on all
// three toolchains (scripts/run-unit-tests.sh, tests/MediaEnrichmentTests.vcxproj
// — both glob tests/*.cpp, so a new file needs no build edit). A test is a
// TEST_CASE(name) { ... } body; it registers itself, so there is no central
// list to keep in sync and a test can't be silently left out of main().
#include <cstdint>
#include <string>
#include <vector>

namespace tests {

using TestFn = void (*)();

// Records a failure (and prints its description) when `condition` is false.
// Never aborts — every assertion in the suite runs on every pass.
void check(bool condition, const char* description);

// Byte vector from a string literal, for the body-classification tests.
std::vector<std::uint8_t> bytes(const std::string& value);

bool registerTest(const char* name, TestFn fn);

} // namespace tests

using tests::bytes;
using tests::check;

#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    [[maybe_unused]] static const bool name##_registered =                     \
        ::tests::registerTest(#name, &name);                                   \
    static void name()

// NavidromeBrowserModel.cpp calls navidrome::syncRatingsToPlaylists, whose real
// implementation lives in main.cpp (SDK-only, not linked here). TestMain.cpp
// stubs it to record what it was handed, so tests can assert the
// node -> RatingUpdate filtering.
#include "../src/core/NavidromePlaylistSync.h"
namespace navidrome {
extern std::vector<RatingUpdate> g_lastRatingSync;
}
