#pragma once
#include "NavidromeBrowserModel.h"
#include <SDK/abort_callback.h>
#include <cstdint>
#include <string>
#include <vector>

namespace navidrome {

bool libraryIsConfigured();

bool libraryServerInfo(ServerInfo& out, std::string& outError);

IBrowserClient& libraryClient();

std::vector<uint8_t> libraryFetchCover(const std::string& coverArtId, int size, abort_callback& abort);
}
