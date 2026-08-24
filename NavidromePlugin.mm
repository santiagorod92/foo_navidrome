#import "stdafx.h"
#import "SubsonicClient.h"
#import "Mac/NavidromeBrowserController.h"
#import "Mac/NavidromePreferencesController.h"
#include <helpers/advconfig_impl.h>
#include <SDK/cfg_var.h>
#include <SDK/library_manager.h>
#include <SDK/play_callback.h>
#include <SDK/ui_element_mac.h>
#include "SubsonicTypes.h"
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// GUIDs — replace with your own when forking this component
// ---------------------------------------------------------------------------
static constexpr GUID guid_cfg_server_url  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x01 } };
static constexpr GUID guid_cfg_username    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02 } };
static constexpr GUID guid_cfg_password    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x03 } };
static constexpr GUID guid_cfg_salt        = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x04 } };
static constexpr GUID guid_prefs_page      = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x05 } };
static constexpr GUID guid_mainmenu_group  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x06 } };
static constexpr GUID guid_mainmenu_cmd    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x07 } };
static constexpr GUID guid_library_viewer  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x08 } };
static constexpr GUID guid_library_prefs   = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x09 } };
static constexpr GUID guid_cfg_custom_headers = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0a } };
static constexpr GUID guid_cfg_scrobble    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0b } };
static constexpr GUID guid_cfg_stream_format = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0c } };
static constexpr GUID guid_cfg_max_bitrate = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0d } };
static constexpr GUID guid_ui_element_mac  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0e } };

// ---------------------------------------------------------------------------
// Config variables (exported so SubsonicClient.mm can access them)
// ---------------------------------------------------------------------------
namespace navidrome {
    cfg_string cfg_server_url(guid_cfg_server_url, "http://navidrome.santirod.local:4533/");
    cfg_string cfg_username  (guid_cfg_username,   "");
    cfg_string cfg_password  (guid_cfg_password,   "");
    cfg_string cfg_salt      (guid_cfg_salt,        "fb2k_navidrome");
    // Extra HTTP headers (one "Name: Value" per line) sent on every request —
    // API, cover art and audio stream. Used e.g. for Cloudflare Access
    // service-token headers when Navidrome sits behind a Zero Trust tunnel.
    cfg_string cfg_custom_headers(guid_cfg_custom_headers, "");
    // Report plays back to Navidrome (play counts, "Recently Played", and any
    // Last.fm / ListenBrainz relay the server has configured).
    // Qualified: an unqualified cfg_bool resolves to the legacy
    // cfg_int_t<bool> (no set()) on the Windows SDK headers, and the two
    // flavours serialize differently — both platforms must use the same one.
    cfg_var_modern::cfg_bool cfg_scrobble(guid_cfg_scrobble, true);

    // Transcoding preferences, applied to every stream.view request.
    // cfg_stream_format: "" = let the server decide, "raw" = never transcode,
    // otherwise a Subsonic format name ("mp3", "opus", "aac", …).
    // cfg_max_bitrate: kbps ceiling; 0 = unlimited.
    // Qualified for the same reason as cfg_scrobble — an unqualified cfg_int
    // resolves to the legacy cfg_int_t<t_int32>, which has no set() and
    // serializes differently.
    cfg_string cfg_stream_format(guid_cfg_stream_format, "");
    cfg_var_modern::cfg_int cfg_max_bitrate(guid_cfg_max_bitrate, 0);
}

// ---------------------------------------------------------------------------
// Scrobbler — reports plays back to Navidrome so play counts, "Recently
// Played" and any Last.fm / ListenBrainz relay configured server-side reflect
// what's played through foobar2000.
//
// Two calls per track, matching the Subsonic contract: submission=false on
// start ("now playing"), submission=true once enough of the track has been
// heard (half its length, capped at 4 minutes — the Last.fm convention).
// ---------------------------------------------------------------------------

namespace {

class navidrome_scrobbler : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_time |
               flag_on_playback_stop;
    }

    void on_playback_new_track(metadb_handle_ptr track) override {
        m_songId.clear();
        m_submitted = false;
        m_length    = 0.0;
        if (track.is_empty() || !navidrome::cfg_scrobble.get()) return;

        m_songId = navidrome::trackIdFromURI(track->get_path());
        if (m_songId.empty()) return;   // not one of ours
        m_length = track->get_length();
        scrobbleAsync(m_songId, NO);
    }

    void on_playback_time(double time) override {
        if (m_songId.empty() || m_submitted) return;
        // Unknown length (live stream): fall back to the 4-minute cap alone.
        double threshold = (m_length > 0) ? std::min(240.0, m_length * 0.5) : 240.0;
        if (time < threshold) return;
        m_submitted = true;
        scrobbleAsync(m_songId, YES);
    }

    void on_playback_stop(play_control::t_stop_reason) override {
        m_songId.clear();
        m_submitted = false;
    }

    // Unused callbacks (not requested in get_flags, but the interface is pure).
    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info &) override {}
    void on_playback_dynamic_info_track(const file_info &) override {}
    void on_volume_change(float) override {}

private:
    // Fire and forget on a background queue — a slow or unreachable server must
    // never stall playback, and a failed scrobble isn't worth interrupting for.
    static void scrobbleAsync(std::string songId, BOOL submission) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            NSError *err = nil;
            [SubsonicClient.sharedClient
                scrobbleSongId:[NSString stringWithUTF8String:songId.c_str()]
                    submission:submission
                         error:&err];
        });
    }

    std::string m_songId;
    double      m_length    = 0.0;
    bool        m_submitted = false;
};

static play_callback_static_factory_t<navidrome_scrobbler> g_navidrome_scrobbler_factory;

} // namespace

// ---------------------------------------------------------------------------
// Preferences page (Mac)
// ---------------------------------------------------------------------------

namespace {

class preferences_page_navidrome : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromePreferencesController new]);
    }
    const char *get_name() override { return "Navidrome"; }
    GUID get_guid() override { return guid_prefs_page; }
    GUID get_parent_guid() override { return guid_tools; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome);

// ---------------------------------------------------------------------------
// Main menu: File > Open Navidrome Browser
// ---------------------------------------------------------------------------

class mainmenu_navidrome : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 1; }

    GUID get_command(t_uint32 p_index) override {
        if (p_index == 0) return guid_mainmenu_cmd;
        throw pfc::exception_invalid_params();
    }

    void get_name(t_uint32 p_index, pfc::string_base &p_out) override {
        if (p_index == 0) { p_out = "Open Navidrome Browser"; return; }
        throw pfc::exception_invalid_params();
    }

    bool get_description(t_uint32 p_index, pfc::string_base &p_out) override {
        if (p_index == 0) {
            p_out = "Browse and stream music from your Navidrome server";
            return true;
        }
        return false;
    }

    GUID get_parent() override { return mainmenu_groups::file; }

    t_uint32 get_sort_priority() override { return 0xFF; }

    bool get_display(t_uint32 p_index, pfc::string_base &p_out, t_uint32 &p_flags) override {
        get_name(p_index, p_out);
        p_flags = 0;
        return true;
    }

    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        if (p_index != 0) throw pfc::exception_invalid_params();
        NavidromeShowStandaloneBrowser();
    }
};

FB2K_SERVICE_FACTORY(mainmenu_navidrome);

// ---------------------------------------------------------------------------
// Library viewer — exposes the browser to the foobar Media Library system.
// On macOS the visible surface is the preferences sub-page below, registered
// under guid_media_library, which mirrors how Album List / ReFacets show up.
// ---------------------------------------------------------------------------

class library_viewer_navidrome : public library_viewer {
public:
    GUID get_preferences_page() override { return guid_library_prefs; }
    bool have_activate()        override { return true; }

    void activate() override { NavidromeShowStandaloneBrowser(); }

    GUID         get_guid() override { return guid_library_viewer; }
    const char * get_name() override { return "Navidrome"; }
};

static library_viewer_factory_t<library_viewer_navidrome> g_library_viewer_navidrome_factory;

} // namespace

// ---------------------------------------------------------------------------
// Media Library preferences sub-page — embeds the browser directly so users
// see Artists/Albums/Songs without opening a separate window. The page IS
// the browser. A fresh NavidromeBrowserController is created per page mount.
// ---------------------------------------------------------------------------

namespace {

class preferences_page_navidrome_library : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeBrowserController new]);
    }
    const char *get_name() override { return "Navidrome"; }
    GUID get_guid() override { return guid_library_prefs; }
    GUID get_parent_guid() override { return guid_media_library; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_library);

// ---------------------------------------------------------------------------
// Native layout panel — lets the browser be docked inside the main window
// layout (Preferences > Display > Layout > Edit Layout > add "Navidrome"),
// as a third mount point alongside the standalone window and the Media
// Library prefs sub-page above. Same VC-per-mount rule applies: the layout
// system may instantiate() more than once (e.g. multiple splits/tabs), so
// each call must return a fresh NavidromeBrowserController, never a shared
// singleton.
// ---------------------------------------------------------------------------

class ui_element_mac_navidrome : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr /*arg*/) override {
        return fb2k::wrapNSObject([NavidromeBrowserController new]);
    }
    bool match_name(const char *name) override {
        return name != nullptr && !strcmp(name, "Navidrome");
    }
    fb2k::stringRef get_name() override { return fb2k::makeString("Navidrome"); }
    GUID get_guid() override { return guid_ui_element_mac; }
};

FB2K_SERVICE_FACTORY(ui_element_mac_navidrome);

} // namespace
