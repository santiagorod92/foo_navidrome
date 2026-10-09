#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tests {

using TestFn = void (*)();

void check(bool condition, const char* description);

std::vector<std::uint8_t> bytes(const std::string& value);

bool registerTest(const char* name, TestFn fn);
}

using tests::bytes;
using tests::check;

#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    [[maybe_unused]] static const bool name##_registered =                     \
        ::tests::registerTest(#name, &name);                                   \
    static void name()

#include "../src/core/NavidromePlaylistSync.h"
namespace navidrome {

extern std::vector<RatingUpdate> g_lastRatingSync;
}
