#pragma once
#include "AudioMuse.h"

#include <SDK/cfg_var.h>
#include <string>
#include <vector>

namespace navidrome {

extern cfg_string cfg_audiomuse_url;
extern cfg_string cfg_audiomuse_token;
extern cfg_string cfg_audiomuse_server;
extern cfg_var_modern::cfg_int cfg_audiomuse_count;

audiomuse::Settings audioMuseSettings();

void startInstantMix(const BrowserNodePtr& seed);

void audioMuseTextSearchPrompt();
void audioMuseInstantPlaylistPrompt();
void audioMuseAlchemy(std::vector<audiomuse::AlchemySeed> seeds, std::string label);

audiomuse::IJsonPoster& audioMusePoster();

bool promptForText(const char* title, const char* label, std::string& inOut);
}
