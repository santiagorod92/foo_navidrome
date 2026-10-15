#include "stdafx.h"
#include "NavidromeDebugLog.h"
#include "NavidromeGuids.h"
#include "NavidromeTrackInput.h"
#include "SubsonicTypes.h"
#include <SDK/input_impl.h>
#include <SDK/skipTrack.h>
#include <string>
#include <cstring>

namespace {

constexpr const char* kPrefix    = "navidrome://track/";
constexpr size_t      kPrefixLen = 18;

class navidrome_input : public input_stubs {
public:
    void open(service_ptr_t<file> , const char* p_path,
              t_input_open_reason reason, abort_callback&) {
        if (reason == input_open_info_write) throw exception_tagging_unsupported();
        m_track = navidrome::parseTrackURI(p_path ? p_path : "");
        if (m_track.id.empty()) throw exception_io_data();
    }

    void get_info(file_info& info, abort_callback&) { navidrome::applyTrackInfo(m_track, info); }

    t_filestats2 get_stats2(uint32_t, abort_callback&) { return filestats2_invalid; }

    void decode_initialize(unsigned p_flags, abort_callback& p_abort) {
        m_decoder.open(m_track, p_flags, p_abort);
    }

    bool decode_run(audio_chunk& chunk, abort_callback& abort) { return m_decoder.run(chunk, abort); }
    void decode_seek(double s, abort_callback& abort) { m_decoder.seek(s, abort); }
    bool decode_can_seek() { return m_decoder.canSeek(); }
    bool decode_get_dynamic_info(file_info& out, double& delta) {
        return m_decoder.getDynamicInfo(out, delta);
    }
    bool decode_get_dynamic_info_track(file_info& out, double& delta) {
        return m_decoder.getDynamicInfoTrack(out, delta);
    }
    void decode_on_idle(abort_callback& abort) { m_decoder.onIdle(abort); }

    void retag(const file_info&, abort_callback&) { throw exception_tagging_unsupported(); }
    void remove_tags(abort_callback&)             { throw exception_tagging_unsupported(); }

    static bool g_is_our_content_type(const char*) { return false; }
    static bool g_is_our_path(const char* p_path, const char*) {
        return p_path != nullptr && strncmp(p_path, kPrefix, kPrefixLen) == 0;
    }
    static GUID g_get_guid() { return navidrome::guids::inputHandler; }
    static const char* g_get_name() { return "Navidrome"; }

private:
    navidrome::TrackURI     m_track;
    navidrome::TrackDecoder m_decoder;
};

static input_singletrack_factory_t<navidrome_input, input_entry::flag_redirect>
    g_navidrome_input_factory;

class navidrome_skip_track : public fb2k::skipTrack {
public:
    bool testTrack(const fb2k::skipTrackParam& p) override {
        if (p.track.is_empty()) return true;
        std::string id = navidrome::trackIdFromURI(p.track->get_path());
        if (id.empty()) return true;
        if (navidrome::brokenTrackRegistry().isBroken(id)) {
            NAVIDROME_WARN("Input", "skipTrack: skipping known-broken id=" + id);
            return false;
        }
        return true;
    }
};

FB2K_SERVICE_FACTORY(navidrome_skip_track);
}
