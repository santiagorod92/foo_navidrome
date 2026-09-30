#pragma once

#include <string>

namespace navidrome {

struct SubsonicRequestContext;

class EsLyricBridge {
public:
    // Refreshes the ESLyric searcher and generated config. Returns an empty
    // string on success or when ESLyric is not installed.
    static std::string installOrUpdate(const SubsonicRequestContext& context,
                                       bool debug = false);
    static bool isEsLyricInstalled();
};

} // namespace navidrome
