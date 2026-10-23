#pragma once
#include "NavidromeBrowserModel.h"
#include "SubsonicCore.h"
#include <SDK/abort_callback.h>
#include <cstdint>
#include <string>
#include <vector>

namespace navidrome {

SubsonicCore& libraryCore();

IBrowserClient& libraryClient();

void showBrowserWindow();

void serverSettingsChanged();

std::string systemLanguageCode();

std::vector<uint8_t> libraryFetchCover(const std::string& coverArtId, int size, abort_callback& abort);
}
