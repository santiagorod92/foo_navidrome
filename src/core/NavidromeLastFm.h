#pragma once
#include "SubsonicTypes.h"

#include <string>

namespace navidrome {

ArtistInfo artistInfoForDisplay(const std::string& artistId, const std::string& artistName,
                                std::string& outError);
}
