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
#include <SDK/replaygain.h>
#include <SDK/playlist.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Mix = navidrome::smartfade::BasicMixer<audio_sample>;

std::atomic<bool> g_dspInChain{ false };
std::atomic<bool> g_mixActive{ false };
std::atomic<bool> g_applyGain{ true };

void refreshMixActive() {
    const bool on = g_dspInChain && navidrome::audioMuseSettings().configured();
    if (g_mixActive.exchange(on) != on)
        NAVIDROME_LOG("AudioMuse", std::string("Smart Fade mixing ") + (on ? "on" : "off")
                      + " (DSP in the chain and an AudioMuse-AI URL set)");
    if (!on) return;
    t_replaygain_config rg;
    replaygain_manager::get()->get_core_settings(rg);
    const bool levels = navidrome::audioMuseSettings().levelLoudness;
    const bool gain = levels && rg.m_source_mode == t_replaygain_config::source_mode_none;
    if (g_applyGain.exchange(gain) != gain)
        NAVIDROME_LOG("AudioMuse", std::string("Smart Fade loudness gains ")
                      + (gain ? "on" : levels ? "off (ReplayGain is on)" : "off (turned off in Preferences)"));
}

navidrome::smartfade::Transition withGains(navidrome::smartfade::Transition t) {
    if (!g_applyGain) t.from.gain = t.to.gain = 1.0;
    return t;
}

void updateChainState(const dsp_chain_config& chain) {
    bool on = false;
    for (t_size i = 0; i < chain.get_count(); ++i)
        if (chain.get_item(i).get_owner() == navidrome::guids::dspSmartFade) on = true;
    if (g_dspInChain.exchange(on) != on)
        NAVIDROME_LOG("AudioMuse", std::string("Smart Fade DSP ") + (on ? "added to" : "removed from") + " the playback chain");
    refreshMixActive();
}

navidrome::TrackURI navidromeTrack(const metadb_handle_ptr& h) {
    if (h.is_empty()) return {};
    const char* path = h->get_path();
    if (path == nullptr || std::strncmp(path, "navidrome://", 12) != 0) return {};
    return navidrome::parseTrackURI(path);
}

navidrome::TrackURI smartFadeTrack(const metadb_handle_ptr& h) {
    navidrome::TrackURI t = navidromeTrack(h);
    if (!t.smartFade) t.id.clear();
    return t;
}

void warnOnce(std::atomic<bool>& flag, const std::string& message) {
    if (!flag.exchange(true)) NAVIDROME_WARN("AudioMuse", message);
}

std::atomic<bool> g_warnedOldPlaylist{ false };
std::atomic<bool> g_warnedNotConfigured{ false };

class navidrome_smart_fade_dsp : public dsp_impl_base {
public:
    static GUID g_get_guid() { return navidrome::guids::dspSmartFade; }
    static void g_get_name(pfc::string_base& out) { out = navidrome::tr("Navidrome Smart Fade (AudioMuse-AI)"); }

    bool on_chunk(audio_chunk* chunk, abort_callback&) override {
        const unsigned rate = chunk->get_srate();
        const unsigned channels = chunk->get_channels();
        const metadb_handle_ptr track = get_cur_file();
        if (track != m_track) beginTrack(track, rate, channels);
        if (m_mix.idle()) {
            const auto out = m_id.empty() ? std::nullopt : navidrome::smartfade::transitions().outgoing(m_id);
            if (!out) {
                if (rate != m_mix.rate() || channels != m_mix.channels()) m_mix.configure(rate, channels);
                return true;
            }
        }
        if (rate != m_mix.rate() || channels != m_mix.channels()) {
            m_mix.drain(m_out);
            emit();
            m_mix.configure(rate, channels);
        }
        m_config = chunk->get_channel_config();
        if (m_mix.state() == Mix::State::Live) {
            const auto out = m_id.empty() ? std::nullopt : navidrome::smartfade::transitions().outgoing(m_id);
            const std::size_t hold = out ? navidrome::smartfade::framesFor(out->fromSeconds(), rate) : 0;
            if (hold != m_mix.hold()) {
                if (hold > 0) NAVIDROME_LOG("AudioMuse", "Smart Fade: holding the last " + std::to_string(out->fromSeconds())
                                            + "s of " + m_id + " to mix into " + out->toId + " (" + out->type + ")");
                m_mix.setHold(hold, m_out);
            }
            if (out) m_mix.setGain(withGains(*out).from.gain);
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

    bool need_track_change_mark() override { return m_mix.hold() > 0 || m_mix.state() != Mix::State::Live; }

private:
    void beginTrack(const metadb_handle_ptr& track, unsigned rate, unsigned channels) {
        if (!m_id.empty() && m_mix.state() == Mix::State::Live && navidrome::smartfade::transitions().pendingFrom(m_id))
            NAVIDROME_WARN("AudioMuse", "Smart Fade: " + m_id + " ended while AudioMuse-AI was still working out its "
                           "transition, so it doesn't mix (a crossfading DSP before Navidrome Smart Fade handles it)");
        m_track = track;
        m_id = g_mixActive ? smartFadeTrack(track).id : std::string();
        const auto in = m_id.empty() ? std::nullopt : navidrome::smartfade::transitions().incoming(m_id);
        const std::size_t fade = in ? navidrome::smartfade::framesFor((std::min)(in->duration, 4.0), rate) : 0;
        const double gain = in ? withGains(*in).to.gain : 1.0;
        if (m_mix.state() != Mix::State::Tail) {
            m_mix.setGain(gain);
            m_mix.startFadeIn(fade);
            return;
        }
        const bool match = in && in->fromId == m_tailId && rate == m_mix.rate() && channels == m_mix.channels();
        if (match) {
            NAVIDROME_LOG("AudioMuse", "Smart Fade: mixing " + m_tailId + " into " + m_id + ": " + in->type + " over "
                          + std::to_string(in->duration) + "s");
            const navidrome::smartfade::Transition plan = withGains(*in);
            m_mix.beginNext(&plan, 0, m_out);
            emit();
            m_tailId.clear();
            return;
        }
        const std::string reason = !in ? std::string("no transition into it is known")
            : in->fromId != m_tailId ? "its transition comes from " + in->fromId
            : "the sample rate or channel count changes (" + std::to_string(m_mix.rate()) + " Hz -> "
              + std::to_string(rate) + " Hz; put a Resampler before Navidrome Smart Fade in the DSP chain so every "
              "song arrives at one rate)";
        NAVIDROME_WARN("AudioMuse", "Smart Fade: " + (m_id.empty() ? std::string("the next track") : m_id)
                       + " doesn't mix with " + m_tailId + " (" + reason + "), fading instead");
        m_mix.beginNext(nullptr, fade, m_out);
        m_mix.setGain(gain);
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

void fetchTransitions(const navidrome::audiomuse::Settings& settings,
                      const std::vector<std::pair<navidrome::TrackURI, navidrome::TrackURI>>& pairs) {
    std::vector<std::pair<navidrome::TrackURI, navidrome::TrackURI>> todo;
    for (const auto& p : pairs)
        if (navidrome::smartfade::transitions().begin(p.first.id, p.second.id)) todo.push_back(p);
    if (todo.empty()) return;
    std::thread([settings, todo] {
        for (const auto& [a, b] : todo) {
            navidrome::dbg::runGuarded("AudioMuse", "Smart Fade transition", [&] {
                const std::string& from = a.id;
                const std::string& to = b.id;
                const double outSeconds = a.clipEnd > 0.0 ? a.clipEnd : a.duration;
                const double inSeconds = b.clipStart;
                navidrome::smartfade::Transition t;
                std::string err;
                const auto started = std::chrono::steady_clock::now();
                const bool ok = navidrome::audiomuse::transition(navidrome::audioMusePoster(), settings, from, to,
                                                                 outSeconds, inSeconds, t, err);
                const std::string took = " (" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - started).count()) + " s)";
                if (ok) {
                    navidrome::smartfade::transitions().put(t);
                    NAVIDROME_LOG("AudioMuse", "Smart Fade transition " + from + " -> " + to + ": " + t.type + " at "
                                  + std::to_string(t.start) + "s for " + std::to_string(t.duration) + "s, next from "
                                  + std::to_string(t.toStart) + "s to " + std::to_string(t.continueFrom) + "s, gains "
                                  + std::to_string(t.from.gain) + "/" + std::to_string(t.to.gain) + took);
                } else {
                    const navidrome::smartfade::Transition fade = navidrome::smartfade::fallbackCrossfade(
                        from, to, outSeconds, inSeconds, settings.fallbackSeconds);
                    const bool usable = fade.valid();
                    if (usable) navidrome::smartfade::transitions().put(fade);
                    NAVIDROME_WARN("AudioMuse", "Smart Fade transition " + from + " -> " + to + " failed: " + err + took
                                   + (usable ? "; crossfading over " + std::to_string(fade.duration) + "s instead"
                                             : std::string("; leaving it to the DSPs before Navidrome Smart Fade (fallback crossfade set to 0 s)")));
                }
                navidrome::smartfade::transitions().end(from, to);
            });
        }
    }).detach();
}

void keepMixGoing(const metadb_handle_ptr& playing, const navidrome::audiomuse::Settings& settings) {
    if (!settings.keepMixGoing) return;
    auto pm = playlist_manager::get();
    const char* order = pm->playback_order_get_name(pm->playback_order_get_active());
    if (order == nullptr || std::strcmp(order, "Default") != 0) return;
    const t_size pl = pm->get_playing_playlist();
    if (pl == pfc_infinite) return;
    t_size idx = pfc_infinite;
    if (!pm->playlist_find_item(pl, playing, idx)) return;
    const t_size count = pm->playlist_get_item_count(pl);
    if (count - 1 - idx > static_cast<t_size>(navidrome::audiomuse::kContinueWhenLeft)) return;
    const metadb_handle_ptr last = pm->playlist_get_item_handle(pl, count - 1);
    if (smartFadeTrack(last).id.empty()) return;
    navidrome::audioMuseContinueSmartFade(last, pl);
}

class navidrome_smart_fade_prefetch : public play_callback_static {
public:
    unsigned get_flags() override { return flag_on_playback_starting | flag_on_playback_new_track; }

    void on_playback_starting(play_control::t_track_command, bool) override { refreshMixActive(); }

    void on_playback_new_track(metadb_handle_ptr playing) override {
        refreshMixActive();
        if (!g_dspInChain) return;
        const navidrome::TrackURI a = smartFadeTrack(playing);
        if (a.id.empty()) {
            const navidrome::TrackURI plain = navidromeTrack(playing);
            if (!plain.id.empty() && navidrome::isClipped(plain))
                warnOnce(g_warnedOldPlaylist, "Smart Fade: the playing track comes from a Smart Fade playlist made "
                         "before 1.45.0, which the DSP can't mix; create the playlist again");
            return;
        }
        const navidrome::audiomuse::Settings settings = navidrome::audioMuseSettings();
        if (!settings.configured()) {
            warnOnce(g_warnedNotConfigured, "Smart Fade: no AudioMuse-AI server URL is set, so the DSP can't mix");
            return;
        }
        metadb_handle_ptr next;
        std::string why;
        if (!navidrome::nextTrack(playing, next, why)) {
            const std::string line = "Smart Fade: no transition after " + a.id + ", " + why;
            if (why.rfind("last track", 0) == 0) NAVIDROME_LOG("AudioMuse", line);
            else NAVIDROME_WARN("AudioMuse", line);
            return;
        }
        const navidrome::TrackURI b = smartFadeTrack(next);
        if (b.id == a.id) {
            NAVIDROME_WARN("AudioMuse", "Smart Fade: the track after " + a.id + " is the same song (" + why
                           + "), so there is nothing to mix into");
            return;
        }
        if (b.id.empty()) {
            NAVIDROME_WARN("AudioMuse", "Smart Fade: the track after " + a.id + " isn't part of a 1.45.0 Smart Fade "
                           "playlist, so there is nothing to mix into");
            return;
        }
        std::vector<std::pair<navidrome::TrackURI, navidrome::TrackURI>> pairs{ { a, b } };
        metadb_handle_ptr after;
        std::string afterWhy;
        if (why != "playback queue" && navidrome::nextTrack(next, after, afterWhy)) {
            const navidrome::TrackURI c = smartFadeTrack(after);
            if (!c.id.empty() && c.id != b.id) pairs.emplace_back(b, c);
        }
        fetchTransitions(settings, pairs);
        keepMixGoing(playing, settings);
    }

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

bool navidrome::smartFadeMixActive() { return g_mixActive; }
