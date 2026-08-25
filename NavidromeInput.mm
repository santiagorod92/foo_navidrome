#import "stdafx.h"
#import "NavidromeInput.h"
#import "SubsonicClient.h"
#import "SubsonicTypes.h"
#import "NavidromePlaylistSync.h"
#import <Foundation/Foundation.h>
#include <SDK/input_impl.h>
#include <SDK/file.h>
#include <SDK/filesystem.h>
#include <SDK/http_client.h>
#include <SDK/cfg_var.h>

namespace navidrome {
    extern cfg_string cfg_custom_headers;
    extern cfg_string cfg_stream_format;
}

NSString *const NavidromeURIScheme = @"navidrome";
NSString *const NavidromeURIPrefix = @"navidrome://track/";

namespace {

constexpr const char *kPrefix = "navidrome://track/";
constexpr size_t       kPrefixLen = 18;

// ---------------------------------------------------------------------------
// URI builder
// ---------------------------------------------------------------------------

static NSString *encodeQuery(NSString *s) {
    return [s stringByAddingPercentEncodingWithAllowedCharacters:
            [NSCharacterSet URLQueryAllowedCharacterSet]] ?: @"";
}

// ---------------------------------------------------------------------------
// Input implementation — proxy/redirect to foobar's HTTP input.
//
// On open(): parse the URI, store metadata.
// On get_info(): return the embedded metadata, no network needed.
// On decode_initialize(): build the authenticated HTTP stream URL from the
// current cfg credentials and open a nested input_decoder on it. Forward
// every decode call to that nested decoder.
// ---------------------------------------------------------------------------

class navidrome_input : public input_stubs {
public:
    void open(service_ptr_t<file> /*p_filehint*/, const char *p_path,
              t_input_open_reason p_reason, abort_callback & /*p_abort*/) {
        if (p_reason == input_open_info_write) throw exception_tagging_unsupported();
        m_path = p_path;
        parse_uri(p_path);
        if (m_song_id.is_empty()) throw exception_io_data();
    }

    void get_info(file_info &p_info, abort_callback & /*p_abort*/) {
        if (!m_title.is_empty())  p_info.meta_set("title",  m_title);
        if (!m_artist.is_empty()) p_info.meta_set("artist", m_artist);
        if (!m_album.is_empty())  p_info.meta_set("album",  m_album);
        if (m_track > 0) {
            pfc::string_formatter tn; tn << m_track;
            p_info.meta_set("tracknumber", tn);
        }
        if (m_year > 0) {
            pfc::string_formatter y; y << m_year;
            p_info.meta_set("date", y);
        }
        if (m_duration > 0) p_info.set_length(m_duration);
        if (!m_suffix.is_empty()) p_info.info_set("codec", m_suffix);
        // Server-side per-user state, snapshotted into the URI at enqueue time.
        // Left unset when absent so old URIs and unrated tracks render as empty
        // rather than "0" in a custom column.
        if (m_rating > 0) {
            pfc::string_formatter r; r << m_rating;
            p_info.meta_set(navidrome::kRatingTag, r);
        }
        if (m_starred) p_info.meta_set(navidrome::kStarredTag, "1");
    }

    t_filestats2 get_stats2(uint32_t /*flags*/, abort_callback & /*p_abort*/) {
        return filestats2_invalid;
    }

    void decode_initialize(unsigned p_flags, abort_callback &p_abort) {
        pfc::string8 httpURL;
        @autoreleasepool {
            SubsonicClient *c = [SubsonicClient sharedClient];
            NSString *songIdNS  = [NSString stringWithUTF8String:m_song_id.c_str()];
            NSString *coverArt  = [NSString stringWithUTF8String:m_cover_art_id.c_str()];
            NSString *http      = [c streamURLForSongId:songIdNS coverArtId:coverArt];
            if (http.length == 0) throw exception_io_data();
            httpURL = [http UTF8String];
        }
        m_resolved_url = httpURL;

        // When custom headers are configured (e.g. Cloudflare Access service
        // tokens), open the stream ourselves via http_client so the headers
        // ride along, and hand the resulting file to the nested decoder.
        // Otherwise pass a null file and let foobar open the URL directly
        // (preserving Content-Type-based decoder selection).
        file::ptr httpFile;
        std::vector<std::string> headers =
            navidrome::parseHeaderLines(navidrome::cfg_custom_headers.get().c_str());
        if (!headers.empty()) {
            http_request::ptr req = http_client::get()->create_request("GET");
            for (const std::string &h : headers) req->add_header(h.c_str());
            httpFile = req->run(m_resolved_url.c_str(), p_abort);
        }

        // Our own file has no audio extension in the URL, so give the decoder a
        // suffix-based hint (track.<suffix>) for codec selection; it still reads
        // bytes from httpFile. When a transcoding format is configured the
        // server sends that codec, not the track's own — hinting the original
        // suffix would pick the wrong decoder.
        std::string effSuffix = navidrome::effectiveStreamSuffix(
            navidrome::cfg_stream_format.get().c_str(), m_suffix.c_str());
        const char *hint = m_resolved_url.c_str();
        pfc::string8 hintBuf;
        if (httpFile.is_valid() && !effSuffix.empty()) {
            hintBuf << "track." << effSuffix.c_str();
            hint = hintBuf.c_str();
        }

        // The `true` flag marks this as a redirect open so foobar will not feed
        // it back to us.
        input_entry::g_open_for_decoding(m_decoder, httpFile, hint, p_abort, true);
        if (m_decoder.is_empty()) throw exception_io_data();
        m_decoder->initialize(0, p_flags, p_abort);
    }

    bool decode_run(audio_chunk &p_chunk, abort_callback &p_abort) {
        if (m_decoder.is_empty()) return false;
        return m_decoder->run(p_chunk, p_abort);
    }

    void decode_seek(double p_seconds, abort_callback &p_abort) {
        if (m_decoder.is_valid()) m_decoder->seek(p_seconds, p_abort);
    }

    bool decode_can_seek() {
        return m_decoder.is_valid() && m_decoder->can_seek();
    }

    bool decode_get_dynamic_info(file_info &p_out, double &p_timestamp_delta) {
        if (m_decoder.is_empty()) return false;
        return m_decoder->get_dynamic_info(p_out, p_timestamp_delta);
    }

    bool decode_get_dynamic_info_track(file_info &p_out, double &p_timestamp_delta) {
        if (m_decoder.is_empty()) return false;
        return m_decoder->get_dynamic_info_track(p_out, p_timestamp_delta);
    }

    void decode_on_idle(abort_callback &p_abort) {
        if (m_decoder.is_valid()) m_decoder->on_idle(p_abort);
    }

    void retag(const file_info &, abort_callback &) { throw exception_tagging_unsupported(); }
    void remove_tags(abort_callback &)              { throw exception_tagging_unsupported(); }

    static bool g_is_our_content_type(const char *) { return false; }
    static bool g_is_our_path(const char *p_path, const char * /*p_extension*/) {
        return p_path != nullptr && strncmp(p_path, kPrefix, kPrefixLen) == 0;
    }
    static GUID g_get_guid() {
        static constexpr GUID guid = { 0xa1b2c3d4, 0x1111, 0x2222,
            { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x02, 0x01 } };
        return guid;
    }
    static const char *g_get_name() { return "Navidrome"; }

private:
    void parse_uri(const char *uri) {
        @autoreleasepool {
            NSString *uriNS = [NSString stringWithUTF8String:uri];
            NSURLComponents *c = [NSURLComponents componentsWithString:uriNS];
            if (!c) return;

            // For navidrome://track/<id>?... NSURLComponents parses "track" as
            // the host and "/<id>" as the path (URI authority semantics).
            // So the song id is percentEncodedPath with the leading "/" stripped.
            NSString *path = c.percentEncodedPath ?: @"";
            NSString *idPart = [path hasPrefix:@"/"] ? [path substringFromIndex:1] : path;
            if (idPart.length > 0) {
                NSString *songId = [idPart stringByRemovingPercentEncoding] ?: idPart;
                m_song_id = [songId UTF8String];
            }

            for (NSURLQueryItem *q in (c.queryItems ?: @[])) {
                NSString *k = q.name; NSString *v = q.value ?: @"";
                if      ([k isEqualToString:@"title"])       m_title  = [v UTF8String];
                else if ([k isEqualToString:@"artist"])      m_artist = [v UTF8String];
                else if ([k isEqualToString:@"album"])       m_album  = [v UTF8String];
                else if ([k isEqualToString:@"tracknumber"]) m_track  = v.intValue;
                else if ([k isEqualToString:@"date"])        m_year   = v.intValue;
                else if ([k isEqualToString:@"duration"])    m_duration = v.doubleValue;
                else if ([k isEqualToString:@"coverArt"])    m_cover_art_id = [v UTF8String];
                else if ([k isEqualToString:@"suffix"])      m_suffix = [v UTF8String];
                else if ([k isEqualToString:@"rating"])      m_rating = v.intValue;
                else if ([k isEqualToString:@"starred"])     m_starred = (v.intValue != 0);
            }
        }
    }

    pfc::string8 m_path;
    pfc::string8 m_song_id;
    pfc::string8 m_cover_art_id;
    pfc::string8 m_title;
    pfc::string8 m_artist;
    pfc::string8 m_album;
    pfc::string8 m_suffix;
    int          m_track    = 0;
    int          m_year     = 0;
    double       m_duration = 0.0;
    int          m_rating   = 0;      // 0 = unrated (also: param absent)
    bool         m_starred  = false;

    pfc::string8 m_resolved_url;
    service_ptr_t<input_decoder> m_decoder;
};

static input_singletrack_factory_t<navidrome_input, input_entry::flag_redirect>
    g_navidrome_input_factory;

} // namespace

// ---------------------------------------------------------------------------
// Public URI builder
// ---------------------------------------------------------------------------

NSString *NavidromeMakeTrackURIWithFields(NSString *songId,
                                          NSString *title,
                                          NSString *artist,
                                          NSString *album,
                                          NSInteger track,
                                          NSInteger year,
                                          NSTimeInterval duration,
                                          NSString *coverArtId,
                                          NSString *suffix,
                                          NSInteger rating,
                                          BOOL starred,
                                          NSString *albumId) {
    if (!songId || songId.length == 0) return nil;

    NSMutableArray<NSString *> *q = [NSMutableArray array];
    if (title.length)
        [q addObject:[NSString stringWithFormat:@"title=%@",  encodeQuery(title)]];
    if (artist.length)
        [q addObject:[NSString stringWithFormat:@"artist=%@", encodeQuery(artist)]];
    if (album.length)
        [q addObject:[NSString stringWithFormat:@"album=%@",  encodeQuery(album)]];
    if (track > 0)
        [q addObject:[NSString stringWithFormat:@"tracknumber=%ld", (long)track]];
    if (year > 0)
        [q addObject:[NSString stringWithFormat:@"date=%ld",  (long)year]];
    if (duration > 0)
        [q addObject:[NSString stringWithFormat:@"duration=%g", duration]];
    if (coverArtId.length)
        [q addObject:[NSString stringWithFormat:@"coverArt=%@", encodeQuery(coverArtId)]];
    if (suffix.length)
        [q addObject:[NSString stringWithFormat:@"suffix=%@", encodeQuery(suffix)]];
    // Omitted when unset, so the URI of an unrated track is byte-identical to
    // what earlier versions produced.
    if (rating > 0)
        [q addObject:[NSString stringWithFormat:@"rating=%ld", (long)rating]];
    if (starred)
        [q addObject:@"starred=1"];
    if (albumId.length)
        [q addObject:[NSString stringWithFormat:@"albumId=%@", encodeQuery(albumId)]];

    NSString *query = [q componentsJoinedByString:@"&"];
    NSString *idPart = encodeQuery(songId);
    if (query.length > 0)
        return [NSString stringWithFormat:@"%@%@?%@", NavidromeURIPrefix, idPart, query];
    return [NSString stringWithFormat:@"%@%@", NavidromeURIPrefix, idPart];
}

NSString *NavidromeMakeTrackURI(SubsonicSong *song) {
    return NavidromeMakeTrackURIWithFields(song.songId,
                                           song.title,
                                           song.artist,
                                           song.album,
                                           song.track,
                                           song.year,
                                           song.duration,
                                           song.coverArtId,
                                           song.suffix,
                                           song.rating,
                                           song.starred,
                                           song.albumId);
}
