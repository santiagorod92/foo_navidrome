#include "stdafx.h"
#include "NavidromeInputWin.h"
#include "SubsonicClientWin.h"
#include "MediaEnrichmentLogic.h"
#include "../NavidromePlaylistSync.h"
#include <SDK/cfg_var.h>
#include <SDK/input_impl.h>
#include <SDK/file.h>
#include <SDK/http_client.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// Windows input_singletrack handler for the navidrome://track/<id>?... scheme,
// mirroring the macOS NavidromeInput.mm. On decode_initialize() it resolves the
// current HTTP stream URL from SubsonicClientWin and — when custom headers are
// configured (e.g. Cloudflare Access service tokens) — opens the stream itself
// via http_client with those headers, handing the resulting file::ptr to a
// nested decoder. Without custom headers it falls back to letting foobar open
// the URL directly (Content-Type sniffing), as before.
// ---------------------------------------------------------------------------

// Defined in NavidromePluginWin.cpp
namespace navidrome {
    extern cfg_string cfg_stream_format;
}

namespace {

constexpr const char* kPrefix    = "navidrome://track/";
constexpr size_t      kPrefixLen = 18;

class navidrome_input_win : public input_stubs {
public:
    void open(service_ptr_t<file> /*hint*/, const char* p_path,
              t_input_open_reason reason, abort_callback&) {
        if (reason == input_open_info_write) throw exception_tagging_unsupported();
        m_path = p_path;
        parse_uri(p_path);
        if (m_song_id.empty()) throw exception_io_data();
    }

    void get_info(file_info& info, abort_callback&) {
        if (!m_title.empty())  info.meta_set("title",  m_title.c_str());
        if (!m_artist.empty()) info.meta_set("artist", m_artist.c_str());
        if (!m_album.empty())  info.meta_set("album",  m_album.c_str());
        if (m_track > 0)       info.meta_set("tracknumber", pfc::format_int(m_track));
        if (m_year > 0)        info.meta_set("date",   pfc::format_int(m_year));
        if (m_duration > 0)    info.set_length(m_duration);
        if (!m_suffix.empty()) info.info_set("codec", m_suffix.c_str());
        // Server-side per-user state, snapshotted into the URI at enqueue time.
        // Left unset when absent so old URIs and unrated tracks render as empty
        // rather than "0" in a custom column.
        if (m_rating > 0)      info.meta_set(navidrome::kRatingTag, pfc::format_int(m_rating));
        if (m_starred)         info.meta_set(navidrome::kStarredTag, "1");
    }

    t_filestats2 get_stats2(uint32_t, abort_callback&) { return filestats2_invalid; }

    void decode_initialize(unsigned p_flags, abort_callback& p_abort) {
        std::string url = navidrome::SubsonicClientWin::get().streamURL(m_song_id);
        if (url.empty()) throw exception_io_data();
        m_resolved_url = url.c_str();

        // When custom headers are configured, open the stream ourselves so the
        // headers ride along; otherwise hand a null file and let foobar open the
        // URL (preserving the original Content-Type-based decoder selection).
        file::ptr httpFile;
        auto headers = navidrome::SubsonicClientWin::customHeaderLines();
        if (!headers.empty()) {
            http_request::ptr req = http_client::get()->create_request("GET");
            for (const auto& h : headers) req->add_header(h.c_str());
            httpFile = req->run(url.c_str(), p_abort);
        }

        // Our own file has no audio extension in the URL, so give the decoder a
        // suffix-based hint (track.<suffix>) to pick the codec; it still reads
        // bytes from httpFile, not from the hint path. When a transcoding format
        // is configured the server sends that codec, not the track's own —
        // hinting the original suffix would pick the wrong decoder.
        std::string effSuffix = navidrome::effectiveStreamSuffix(
            navidrome::cfg_stream_format.get().c_str(), m_suffix);
        const char* hint = m_resolved_url.c_str();
        pfc::string8 hintBuf;
        if (httpFile.is_valid() && !effSuffix.empty()) {
            hintBuf << "track." << effSuffix.c_str();
            hint = hintBuf.c_str();
        }

        input_entry::g_open_for_decoding(m_decoder, httpFile, hint, p_abort, true);
        if (m_decoder.is_empty()) throw exception_io_data();
        m_decoder->initialize(0, p_flags, p_abort);
    }

    bool decode_run(audio_chunk& chunk, abort_callback& abort) {
        return m_decoder.is_valid() && m_decoder->run(chunk, abort);
    }
    void decode_seek(double s, abort_callback& abort) {
        if (m_decoder.is_valid()) m_decoder->seek(s, abort);
    }
    bool decode_can_seek() { return m_decoder.is_valid() && m_decoder->can_seek(); }
    bool decode_get_dynamic_info(file_info& out, double& delta) {
        return m_decoder.is_valid() && m_decoder->get_dynamic_info(out, delta);
    }
    bool decode_get_dynamic_info_track(file_info& out, double& delta) {
        return m_decoder.is_valid() && m_decoder->get_dynamic_info_track(out, delta);
    }
    void decode_on_idle(abort_callback& abort) {
        if (m_decoder.is_valid()) m_decoder->on_idle(abort);
    }

    void retag(const file_info&, abort_callback&) { throw exception_tagging_unsupported(); }
    void remove_tags(abort_callback&)             { throw exception_tagging_unsupported(); }

    static bool g_is_our_content_type(const char*) { return false; }
    static bool g_is_our_path(const char* p_path, const char*) {
        return p_path != nullptr && strncmp(p_path, kPrefix, kPrefixLen) == 0;
    }
    static GUID g_get_guid() {
        static constexpr GUID guid = { 0xa1b2c3d4, 0x1111, 0x2222,
            { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x02, 0x01 } };
        return guid;
    }
    static const char* g_get_name() { return "Navidrome"; }

private:
    void parse_uri(const char* uri) {
        std::string s = uri;
        if (s.size() <= kPrefixLen) return;
        std::string rest = s.substr(kPrefixLen);   // <id>[?query]
        std::string idPart, query;
        size_t q = rest.find('?');
        if (q == std::string::npos) { idPart = rest; }
        else { idPart = rest.substr(0, q); query = rest.substr(q + 1); }
        m_song_id = navidrome::uriDecode(idPart);

        size_t pos = 0;
        while (pos <= query.size() && !query.empty()) {
            size_t amp = query.find('&', pos);
            std::string pair = (amp == std::string::npos)
                ? query.substr(pos) : query.substr(pos, amp - pos);
            size_t eq = pair.find('=');
            std::string k = (eq == std::string::npos) ? pair : pair.substr(0, eq);
            std::string v = (eq == std::string::npos) ? "" : navidrome::uriDecode(pair.substr(eq + 1));
            if      (k == "title")       m_title  = v;
            else if (k == "artist")      m_artist = v;
            else if (k == "album")       m_album  = v;
            else if (k == "tracknumber") m_track  = atoi(v.c_str());
            else if (k == "date")        m_year   = atoi(v.c_str());
            else if (k == "duration")    m_duration = atof(v.c_str());
            else if (k == "coverArt")    m_cover_art_id = v;
            else if (k == "suffix")      m_suffix = v;
            else if (k == "rating")      m_rating = atoi(v.c_str());
            else if (k == "starred")     m_starred = (atoi(v.c_str()) != 0);
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
    }

    std::string  m_path, m_song_id, m_cover_art_id, m_title, m_artist, m_album, m_suffix;
    pfc::string8 m_resolved_url;
    int          m_track = 0, m_year = 0;
    int          m_rating = 0;      // 0 = unrated (also: param absent)
    bool         m_starred = false;
    double       m_duration = 0.0;
    service_ptr_t<input_decoder> m_decoder;
};

static input_singletrack_factory_t<navidrome_input_win, input_entry::flag_redirect>
    g_navidrome_input_win_factory;

} // namespace

// ---------------------------------------------------------------------------
// URI builder (public)
// ---------------------------------------------------------------------------
std::string navidrome::makeTrackURI(const std::string& id,
                                    const std::string& title,
                                    const std::string& artist,
                                    const std::string& album,
                                    int track,
                                    int year,
                                    double duration,
                                    const std::string& coverArtId,
                                    const std::string& suffix,
                                    int rating,
                                    bool starred,
                                    const std::string& albumId) {
    if (id.empty()) return "";
    std::string uri = std::string(kPrefix) + navidrome::uriEncode(id);

    std::vector<std::string> q;
    if (!title.empty())      q.push_back("title="  + navidrome::uriEncode(title));
    if (!artist.empty())     q.push_back("artist=" + navidrome::uriEncode(artist));
    if (!album.empty())      q.push_back("album="  + navidrome::uriEncode(album));
    if (track > 0)           q.push_back("tracknumber=" + std::to_string(track));
    if (year > 0)            q.push_back("date="   + std::to_string(year));
    if (duration > 0) {
        char b[32];
        snprintf(b, sizeof(b), "%g", duration);
        q.push_back(std::string("duration=") + b);
    }
    if (!coverArtId.empty()) q.push_back("coverArt=" + navidrome::uriEncode(coverArtId));
    if (!suffix.empty())     q.push_back("suffix="   + navidrome::uriEncode(suffix));
    // Omitted when unset, so the URI of an unrated track is byte-identical to
    // what earlier versions produced.
    if (rating > 0)          q.push_back("rating="   + std::to_string(rating));
    if (starred)             q.push_back("starred=1");
    if (!albumId.empty())    q.push_back("albumId=" + navidrome::uriEncode(albumId));

    for (size_t i = 0; i < q.size(); ++i) {
        uri += (i == 0 ? "?" : "&");
        uri += q[i];
    }
    return uri;
}
