#pragma once

#include <string>

namespace navidrome {

struct SubsonicRequestContext;

class EsLyricBridge {
public:
    static std::string installOrUpdate(const SubsonicRequestContext& context,
                                       bool debug = false);
    static bool isEsLyricInstalled();
};
}
