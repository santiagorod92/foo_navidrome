#pragma once
// The platform halves of navidrome_library_api (NavidromeLibraryService.h). The service itself
// — album listing, cover hand-off, play album/artist — is implemented once in main.cpp over
// these; each platform supplies them (Windows: src/platform/win/NavidromeLibraryServiceWin.cpp,
// macOS: src/platform/mac/MacSubsonicBrowserClient.mm). Same pattern as setRatingOnServer().
#include "NavidromeBrowserModel.h"
#include <SDK/abort_callback.h>
#include <cstdint>
#include <string>
#include <vector>

namespace navidrome {

// Server URL + user set in Preferences.
bool libraryIsConfigured();

// The platform's browser client (stateless; safe to use from worker threads).
IBrowserClient& libraryClient();

// BLOCKING — worker thread only. Cover art bytes (jpeg/png) for a coverArtId; `size` = max edge
// in pixels, 0 = original. Empty = not found / failed. Throws exception_aborted if `abort` fires.
std::vector<uint8_t> libraryFetchCover(const std::string& coverArtId, int size, abort_callback& abort);

} // namespace navidrome
