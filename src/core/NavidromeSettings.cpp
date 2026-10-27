#include "stdafx.h"
#include "NavidromeSettings.h"
#include "NavidromeGuids.h"
#include "NavidromeDebugLog.h"
#include "NavidromeLibraryPlatform.h"

namespace navidrome {

cfg_string cfg_server_url(guids::cfgServerUrl, "http://localhost:4533/");
cfg_string cfg_username(guids::cfgUsername, "");
cfg_string cfg_password(guids::cfgPassword, "");
cfg_string cfg_salt(guids::cfgSalt, "fb2k_navidrome");
cfg_string cfg_custom_headers(guids::cfgCustomHeaders, "");
cfg_var_modern::cfg_bool cfg_scrobble(guids::cfgScrobble, true);
cfg_string cfg_stream_format(guids::cfgStreamFormat, "");
cfg_var_modern::cfg_int cfg_max_bitrate(guids::cfgMaxBitrate, 0);
cfg_var_modern::cfg_bool cfg_library_filter(guids::cfgLibraryFilter, false);
cfg_string cfg_library_ids(guids::cfgLibraryIds, "");
cfg_string cfg_ui_language(guids::cfgUiLanguage, "");
cfg_string cfg_lastfm_api_key(guids::cfgLastFmApiKey, "");
cfg_string cfg_browser_hidden_categories(guids::cfgBrowserHiddenCategories, "");
cfg_string cfg_server_profiles(guids::cfgServerProfiles, "");
cfg_string cfg_active_server_profile(guids::cfgActiveServerProfile, "");

namespace {

ServerProfile activeServerFields() {
    ServerProfile p;
    p.url           = cfg_server_url.get().c_str();
    p.username      = cfg_username.get().c_str();
    p.password      = cfg_password.get().c_str();
    p.customHeaders = cfg_custom_headers.get().c_str();
    p.libraryFilter = cfg_library_filter.get();
    p.libraryIds    = cfg_library_ids.get().c_str();
    return p;
}

ServerProfileState storedServerProfiles() {
    ServerProfileState state;
    state.profiles = parseServerProfiles(cfg_server_profiles.get().c_str());
    state.active   = cfg_active_server_profile.get().c_str();
    return state;
}

void storeServerProfiles(const ServerProfileState& state) {
    cfg_server_profiles.set(formatServerProfiles(state.profiles).c_str());
    cfg_active_server_profile.set(state.active.c_str());
}

void loadServerFields(const ServerProfile& p) {
    cfg_server_url.set(p.url.c_str());
    cfg_username.set(p.username.c_str());
    cfg_password.set(p.password.c_str());
    cfg_custom_headers.set(p.customHeaders.c_str());
    cfg_library_filter.set(p.libraryFilter);
    cfg_library_ids.set(p.libraryIds.c_str());
}

void serverSwitched(const char* action, const ServerProfileState& state) {
    NAVIDROME_NOTE("Env", std::string("server profile ") + action + ": now \"" + state.active + "\" (" +
                   std::to_string(state.profiles.size()) + " profile(s))");
    lyricsCache().clear();
    serverSettingsChanged();
}

}

ServerProfileState currentServerProfiles() {
    return syncActiveServerProfile(storedServerProfiles(), activeServerFields());
}

bool selectServerProfile(const std::string& name) {
    ServerProfileState state = storedServerProfiles();
    ServerProfile load;
    if (!switchServerProfile(state, activeServerFields(), name, load)) return false;
    storeServerProfiles(state);
    loadServerFields(load);
    serverSwitched("switched", state);
    return true;
}

std::string addNewServerProfile(const std::string& wantedName) {
    ServerProfileState state = storedServerProfiles();
    const ServerProfile p = addServerProfile(state, activeServerFields(), wantedName);
    storeServerProfiles(state);
    loadServerFields(p);
    serverSwitched("added", state);
    return p.name;
}

bool removeCurrentServerProfile() {
    ServerProfileState state = storedServerProfiles();
    ServerProfile load;
    if (!removeActiveServerProfile(state, activeServerFields(), load)) return false;
    storeServerProfiles(state);
    loadServerFields(load);
    serverSwitched("removed", state);
    return true;
}

}
