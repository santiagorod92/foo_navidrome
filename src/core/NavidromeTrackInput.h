#pragma once
#include "TrackUri.h"
#include <SDK/input.h>
#include <SDK/file_info.h>
#include <SDK/audio_chunk.h>
#include <SDK/abort_callback.h>

namespace navidrome {

void applyTrackInfo(const TrackURI& track, file_info& info);

class TrackDecoder {
public:
    void open(const TrackURI& track, unsigned flags, abort_callback& abort);
    bool run(audio_chunk& chunk, abort_callback& abort);
    void seek(double seconds, abort_callback& abort);
    bool canSeek();
    bool getDynamicInfo(file_info& out, double& delta);
    bool getDynamicInfoTrack(file_info& out, double& delta);
    void onIdle(abort_callback& abort);

private:
    void openAt(double offsetSeconds, abort_callback& abort);
    bool openCached(abort_callback& abort);

    TrackURI                     m_track;
    unsigned                     m_flags = 0;
    bool                         m_seekByReopen = false;
    double                       m_skipSeconds = 0.0;
    service_ptr_t<input_decoder> m_decoder;
};
}
