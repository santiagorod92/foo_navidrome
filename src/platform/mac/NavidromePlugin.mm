#import "../../core/stdafx.h"
#import "SubsonicClient.h"
#import "NavidromeBrowserController.h"
#import "NavidromeLyricsController.h"
#import "NavidromePreferencesController.h"
#include <helpers/advconfig_impl.h>
#include <SDK/cfg_var.h>
#include <SDK/library_manager.h>
#include <SDK/play_callback.h>
#include <SDK/initquit.h>
#include <initializer_list>
#include <strings.h>
#include <SDK/ui_element_mac.h>
#include "../../core/SubsonicTypes.h"
#include "../../core/NavidromePlaylistSync.h"
#include "../../core/NavidromeDebugLog.h"
#include "../../core/NavidromeGuids.h"
#include "../../core/NavidromeSettings.h"
#include <algorithm>
#include <cstring>

namespace {

class preferences_page_navidrome : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromePreferencesController new]);
    }
    const char *get_name() override { return "Navidrome"; }
    GUID get_guid() override { return navidrome::guids::prefsPage; }
    GUID get_parent_guid() override { return guid_tools; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome);
}

namespace {

class preferences_page_navidrome_audiomuse : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeAudioMusePrefsController new]);
    }
    const char *get_name() override { return "AudioMuse-AI"; }
    GUID get_guid() override { return navidrome::guids::audioMusePrefsPage; }
    GUID get_parent_guid() override { return navidrome::guids::prefsPage; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_audiomuse);

class library_viewer_navidrome : public library_viewer {
public:
    GUID get_preferences_page() override { return navidrome::guids::libraryPrefsPage; }
    bool have_activate()        override { return true; }

    void activate() override { NavidromeShowStandaloneBrowser(); }

    GUID         get_guid() override { return navidrome::guids::macLibraryViewer; }
    const char * get_name() override { return "Navidrome"; }
};

static library_viewer_factory_t<library_viewer_navidrome> g_library_viewer_navidrome_factory;
}

namespace {

class preferences_page_navidrome_library : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeBrowserController new]);
    }
    const char *get_name() override { return "Navidrome"; }
    GUID get_guid() override { return navidrome::guids::libraryPrefsPage; }
    GUID get_parent_guid() override { return guid_media_library; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_library);

static bool matchesLayoutName(const char *name, std::initializer_list<const char *> names) {
    if (name == nullptr) return false;
    for (const char *n : names)
        if (strcasecmp(name, n) == 0) return true;
    return false;
}

class ui_element_mac_navidrome : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr ) override {
        return fb2k::wrapNSObject([NavidromeBrowserController new]);
    }
    bool match_name(const char *name) override {
        return matchesLayoutName(name, {"Navidrome", "navidrome-browser"});
    }
    fb2k::stringRef get_name() override { return fb2k::makeString("Navidrome"); }
    GUID get_guid() override { return navidrome::guids::macUiElementBrowser; }
};

FB2K_SERVICE_FACTORY(ui_element_mac_navidrome);

class ui_element_mac_navidrome_lyrics : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr ) override {
        return fb2k::wrapNSObject([NavidromeLyricsController new]);
    }
    bool match_name(const char *name) override {
        return matchesLayoutName(name, {"Navidrome Lyrics", "navidrome-lyrics"});
    }
    fb2k::stringRef get_name() override { return fb2k::makeString("Navidrome Lyrics"); }
    GUID get_guid() override { return navidrome::guids::macUiElementLyrics; }
};

FB2K_SERVICE_FACTORY(ui_element_mac_navidrome_lyrics);
}
