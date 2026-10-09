#pragma once

#include <string>
#include <vector>

namespace navidrome {

constexpr const char* kRatingTag  = "NAVIDROME_RATING";
constexpr const char* kStarredTag = "NAVIDROME_STARRED";

struct RatingUpdate {
    std::string songId;
    int  rating  = 0;
    bool starred = false;
};

void syncRatingsToPlaylists(std::vector<RatingUpdate> updates);

struct PlaylistAlbumScan {
    std::vector<std::string> albumIds;
    std::size_t entries   = 0;
    std::size_t ungrouped = 0;
};

PlaylistAlbumScan scanPlaylistAlbums();

bool refreshRatingsOnStartEnabled();

bool setRatingOnServer(const std::string& songId, int rating);
bool setStarredOnServer(const std::string& songId, bool starred);
}
