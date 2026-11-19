#include "stdafx.h"
#include "NavidromeSmartFade.h"
#include "NavidromeAudioMuse.h"
#include "NavidromeSdkGlue.h"
#include "NavidromeDebugLog.h"
#include "NavidromeGuids.h"
#include "SmartFade.h"
#include "SubsonicTypes.h"
#include <SDK/dsp.h>
#include <SDK/dsp_manager.h>
#include <SDK/play_callback.h>
#include <SDK/initquit.h>
#include <atomic>
#include <cstring>
#include <thread>

namespace {

using Mix = navidrome::smartfade::BasicMixer<audio_sample>;

std::atomic<bool> g_dspInChain{ false };

void updateChainState(const dsp_chain_config& chain) {
    bool on = false;
    for (t_size i = 0; i < chain.get_count(); ++i)
        if (chain.get_item(i).get_owner() == navidrome::guids::dspSmartFade) on = true;
    if (g_dspInChain.exchange(on) != on)
        NAVIDROME_LOG("AudioMuse", std::string("Smart Fade DSP ") + (on ? "added to" : "removed from") + " the playback chain");
}

navidrome::TrackURI smartFadeTrack(const metadb_handle_ptr& h) {
    if (h.is_empty()) return {};
    const char* path = h->get_path();
    if (path == nullptr || std::strncmp(path, "navidrome://", 12) != 0) return {};
    navidrome::TrackURI t = navidrome::parseTrackURI(path);
    if (!t.smartFade) t.id.clear();
    return t;
}

class navidrome_smart_fade_dsp : public dsp_impl_base {
public:
    static GUID g_get_guid() { return navidrome::guids::dspSmartFade; }
    static void g_get_name(pfc::string_base& out) { out = navidrome::tr("Navidrome Smart Fade (AudioMuse-AI)"); }

    bool on_chunk(audio_chunk* chunk, abort_callback&) override {
        const unsigned rate = chunk->get_srate();
        const unsigned channels = chunk->get_channels();
        const metadb_handle_ptr track = get_cur_file();
        if (track != m_track) beginTrack(track, rate, channels);
        if (rate != m_mix.rate() || channels != m_mix.channels()) {
            m_mix.drain(m_out);
            emit();
            m_mix.configure(rate, channels);
        }
        m_config = chunk->get_channel_config();
        if (m_mix.state() == Mix::State::Live) {
            const auto out = m_id.empty() ? std::nullopt : navidrome::smartfade::transitions().outgoing(m_id);
            const std::size_t hold = out ? navidrome::smartfade::framesFor(out->mixSeconds, rate) : 0;
            if (hold != m_mix.hold()) {
                if (hold > 0) NAVIDROME_LOG("AudioMuse", "Smart Fade: holding the last " + std::to_string(out->mixSeconds)
                                            + "s of " + m_id + " to mix into " + out->toId);
                m_mix.setHold(hold, m_out);
            }
        }
        m_mix.process(chunk->get_data(), chunk->get_sample_count(), m_out);
        emit();
        return false;
    }

    void on_endoftrack(abort_callback&) override {
        m_mix.endTrack(m_out);
        emit();
        m_tailId = m_mix.state() == Mix::State::Tail ? m_id : std::string();
    }

    void on_endofplayback(abort_callback&) override {
        m_mix.drain(m_out);
        emit();
        m_tailId.clear();
    }

    void flush() override {
        m_mix.flush();
        m_out.clear();
        m_tailId.clear();
    }

    double get_latency() override {
        return m_mix.rate() ? static_cast<double>(m_mix.latencyFrames()) / m_mix.rate() : 0.0;
    }

    bool need_track_change_mark() override { return true; }

private:
    void beginTrack(const metadb_handle_ptr& track, unsigned rate, unsigned channels) {
        m_track = track;
        m_id = smartFadeTrack(track).id;
        const auto in = m_id.empty() ? std::nullopt : navidrome::smartfade::transitions().incoming(m_id);
        const std::size_t fade = in ? navidrome::smartfade::framesFor(in->mixSeconds, rate) : 0;
        if (m_mix.state() != Mix::State::Tail) {
            m_mix.startFadeIn(fade);
            return;
        }
        const bool match = in && in->fromId == m_tailId && rate == m_mix.rate() && channels == m_mix.channels();
        const std::size_t piece = match ? navidrome::smartfade::framesFor(in->pieceSeconds(), rate) : 0;
        if (match) NAVIDROME_LOG("AudioMuse", "Smart Fade: mixing " + m_tailId + " into " + m_id + " over "
                                 + std::to_string(in->mixSeconds) + "s, stretch " + std::to_string(in->stretch));
        else NAVIDROME_LOG("AudioMuse", "Smart Fade: " + (m_id.empty() ? std::string("next track") : m_id)
                           + " doesn't follow " + m_tailId + ", fading instead of mixing");
        m_mix.beginNext(piece, match ? 0 : fade, m_out);
        emit();
        m_tailId.clear();
    }

    void emit() {
        if (m_out.empty() || m_mix.channels() == 0) { m_out.clear(); return; }
        const std::size_t frames = m_out.size() / m_mix.channels();
        audio_chunk* c = insert_chunk(m_out.size());
        c->set_data(m_out.data(), frames, m_mix.channels(), m_mix.rate(), m_config);
        m_out.clear();
    }

    Mix m_mix;
    std::vector<audio_sample> m_out;
    metadb_handle_ptr m_track;
    std::string m_id;
    std::string m_tailId;
    unsigned m_config = 0;
};

static dsp_factory_nopreset_t<navidrome_smart_fade_dsp> g_smart_fade_dsp;

class navidrome_smart_fade_dsp_watch : public dsp_config_callback {
public:
    void on_core_settings_change(const dsp_chain_config& chain) override { updateChainState(chain); }
};

static service_factory_single_t<navidrome_smart_fade_dsp_watch> g_smart_fade_dsp_watch;

class navidrome_smart_fade_init : public initquit {
public:
    void on_init() override {
        dsp_chain_config_impl chain;
        dsp_config_manager::get()->get_core_settings(chain);
        updateChainState(chain);
    }
    void on_quit() override {}
};

static initquit_factory_t<navidrome_smart_fade_init> g_smart_fade_init;

class navidrome_smart_fade_prefetch : public play_callback_static {
public:
    unsigned get_flags() override { return flag_on_playback_new_track; }

    void on_playback_new_track(metadb_handle_ptr playing) override {
        if (!g_dspInChain) return;
        const navidrome::TrackURI a = smartFadeTrack(playing);
        if (a.id.empty()) return;
        const navidrome::audiomuse::Settings settings = navidrome::audioMuseSettings();
        if (!settings.configured()) return;
        metadb_handle_ptr next;
        std::string why;
        if (!navidrome::nextTrack(playing, next, why)) {
            NAVIDROME_LOG("AudioMuse", "Smart Fade: no transition after " + a.id + ", " + why);
            return;
        }
        const navidrome::TrackURI b = smartFadeTrack(next);
        if (b.id.empty() || b.id == a.id) return;
        const double outSeconds = a.clipEnd > 0.0 ? a.clipEnd : a.duration;
        const double inSeconds = b.clipStart;
        if (!navidrome::smartfade::transitions().begin(a.id, b.id)) return;
        std::thread([settings, from = a.id, to = b.id, outSeconds, inSeconds] {
            navidrome::dbg::runGuarded("AudioMuse", "Smart Fade transition", [&] {
                navidrome::smartfade::Transition t;
                std::string err;
                if (navidrome::audiomuse::transition(navidrome::audioMusePoster(), settings, from, to,
                                                     outSeconds, inSeconds, t, err)) {
                    navidrome::smartfade::transitions().put(t);
                    NAVIDROME_LOG("AudioMuse", "Smart Fade transition " + from + " -> " + to + ": mix at "
                                  + std::to_string(t.mixAt) + "s for " + std::to_string(t.mixSeconds) + "s, in at "
                                  + std::to_string(t.inAt) + "s, resume at " + std::to_string(t.resumeAt) + "s");
                } else {
                    NAVIDROME_WARN("AudioMuse", "Smart Fade transition " + from + " -> " + to + " failed: " + err);
                }
                navidrome::smartfade::transitions().end(from, to);
            });
        }).detach();
    }

    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_stop(play_control::t_stop_reason) override {}
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_playback_time(double) override {}
    void on_volume_change(float) override {}
};

static play_callback_static_factory_t<navidrome_smart_fade_prefetch> g_smart_fade_prefetch;

}

bool navidrome::smartFadeMixActive() { return g_dspInChain; }
