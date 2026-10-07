#pragma once
// Instant Mix + AudioMuse-AI actions (issue #16), the SDK half — written once
// in main.cpp for both platforms, over the SDK-free client in AudioMuse.h.
//
//  - Instant Mix: getSimilarSongs2.view for one song / album / artist (which a
//    Navidrome with the AudioMuse-AI plugin answers from AudioMuse), into the
//    dedicated "Instant Mix" playlist (replaced each time), seed song first,
//    then played. Needs no AudioMuse settings.
//  - Text Search / Instant Playlist / Song Alchemy: straight to the AudioMuse-AI
//    server (Preferences > Tools > Navidrome > AudioMuse-AI); the result lands
//    in a new foobar2000 playlist named after the query, and starts playing.
//
// Every entry point is main-thread; the network work runs under a modeless
// threaded_process (progress window with an Abort button).
#include "AudioMuse.h"

#include <SDK/cfg_var.h>
#include <string>
#include <vector>

namespace navidrome {

// AudioMuse-AI preferences (defined in main.cpp, edited by each platform's
// prefs sub-page). cfg_audiomuse_count also sizes Instant Mix.
extern cfg_string cfg_audiomuse_url;
extern cfg_string cfg_audiomuse_token;
extern cfg_string cfg_audiomuse_server;
extern cfg_var_modern::cfg_int cfg_audiomuse_count;

audiomuse::Settings audioMuseSettings();

// Instant Mix from a browser node (song, album or artist). Main thread.
void startInstantMix(const BrowserNodePtr& seed);

// Asks for the text (promptForText), then runs the search into a new playlist.
void audioMuseTextSearchPrompt();
void audioMuseInstantPlaylistPrompt();
// Runs a Song Alchemy blend of `seeds` into a new playlist; `label` names it.
void audioMuseAlchemy(std::vector<audiomuse::AlchemySeed> seeds, std::string label);

// --- Platform seams ---------------------------------------------------------
// Windows: src/platform/win/NavidromeLibraryServiceWin.cpp (+ BrowserWindow.cpp
// for the prompt); macOS: src/platform/mac/MacSubsonicBrowserClient.mm.

// The HTTP POST AudioMuse-AI requests go through. Blocking; worker thread only.
audiomuse::IJsonPoster& audioMusePoster();

// Modal single-line text prompt over the main window. `inOut` is the initial
// text and, when the user confirms (returns true), the entered text.
bool promptForText(const char* title, const char* label, std::string& inOut);

} // namespace navidrome
