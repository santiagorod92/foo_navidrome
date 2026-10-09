#include "TestHarness.h"

#include <iostream>
#include <utility>

namespace navidrome {

std::vector<RatingUpdate> g_lastRatingSync;
void syncRatingsToPlaylists(std::vector<RatingUpdate> u) { g_lastRatingSync = std::move(u); }
}

namespace tests {

namespace {

int g_failures = 0;

std::vector<std::pair<const char*, TestFn>>& registry() {
    static std::vector<std::pair<const char*, TestFn>> r;
    return r;
}
}

void check(bool condition, const char* description) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << description << '\n';
}

std::vector<std::uint8_t> bytes(const std::string& value) {
    return {value.begin(), value.end()};
}

bool registerTest(const char* name, TestFn fn) {
    registry().emplace_back(name, fn);
    return true;
}
}

int main() {
    for (const auto& t : tests::registry()) {
        const int before = tests::g_failures;
        t.second();
        if (tests::g_failures != before)
            std::cerr << "  in " << t.first << '\n';
    }
    if (tests::g_failures != 0) {
        std::cerr << tests::g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All " << tests::registry().size() << " unit tests passed\n";
    return 0;
}
