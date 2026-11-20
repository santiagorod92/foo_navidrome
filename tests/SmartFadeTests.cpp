#include "TestHarness.h"
#include "../src/core/SmartFade.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

namespace sf = navidrome::smartfade;

std::vector<float> constant(std::size_t frames, unsigned channels, float v) {
    return std::vector<float>(frames * channels, v);
}

std::vector<float> sine(std::size_t frames, unsigned rate, double hz) {
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) out[i] = static_cast<float>(std::sin(2.0 * sf::kPi * hz * i / rate));
    return out;
}

std::size_t upCrossings(const std::vector<float>& x, std::size_t from, std::size_t to) {
    std::size_t n = 0;
    for (std::size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++n;
    return n;
}

sf::Transition pair(const std::string& from, const std::string& to) {
    sf::Transition t;
    t.fromId = from; t.toId = to;
    t.mixAt = 213.78; t.mixSeconds = 2.62; t.inAt = 21.15; t.resumeAt = 23.74;
    return t;
}

TEST_CASE(testSmartFadeTransitionCache) {
    sf::Transition t = pair("a", "b");
    check(t.valid(), "AudioMuse's example transition is valid");
    sf::Transition bad = t;
    bad.resumeAt = bad.inAt;
    check(!bad.valid(), "an empty piece is refused");
    bad = t; bad.toId = "a";
    check(!bad.valid(), "a song can't mix into itself");

    sf::TransitionCache cache;
    check(cache.begin("a", "b"), "first request for a pair starts");
    check(!cache.begin("a", "b"), "a pair in flight isn't requested twice");
    check(cache.pendingFrom("a") && !cache.pendingFrom("b") && !cache.pendingFrom(""), "pending by the song that ends");
    check(!cache.pendingFrom("ab"), "pending matches the whole id");
    cache.put(t);
    cache.end("a", "b");
    check(!cache.begin("a", "b"), "a cached pair isn't requested again");
    check(cache.outgoing("a") && cache.outgoing("a")->toId == "b", "outgoing by the song that ends");
    check(cache.incoming("b") && cache.incoming("b")->fromId == "a", "incoming by the song that starts");
    check(!cache.outgoing("b") && !cache.incoming("a"), "no reverse lookups");
    check(cache.begin("a", "c"), "a new next song asks again");
    cache.put(pair("a", "c"));
    check(cache.outgoing("a")->toId == "c", "the latest pair for a song wins");
}

TEST_CASE(testSmartFadeStretch) {
    const unsigned rate = 44100;
    const std::vector<float> same = sine(1000, rate, 440.0);
    const auto copy = sf::stretchTo(same.data(), same.size(), 1, rate, same.size());
    check(copy == same, "equal lengths copy the audio");

    const std::size_t outFrames = rate;
    const std::vector<float> in = sine(static_cast<std::size_t>(outFrames * 1.05), rate, 440.0);
    const auto out = sf::stretchTo(in.data(), in.size(), 1, rate, outFrames);
    check(out.size() == outFrames, "stretch gives exactly the frames asked for");
    const std::size_t crossings = upCrossings(out, rate / 10, outFrames - rate / 10);
    const double hz = crossings / 0.8;
    check(std::fabs(hz - 440.0) < 8.0, "stretch keeps the pitch");
    double peak = 0.0;
    for (std::size_t i = rate / 10; i < outFrames - rate / 10; ++i) peak = (std::max)(peak, static_cast<double>(std::fabs(out[i])));
    check(peak > 0.9 && peak < 1.1, "stretch keeps the level");

    sf::Stretcher<float> chunked;
    chunked.start(1, rate, in.size(), outFrames);
    std::vector<float> pieces;
    for (std::size_t at = 0; at < in.size(); at += 441) {
        chunked.push(in.data() + at, (std::min)(std::size_t(441), in.size() - at));
        chunked.pull(pieces);
    }
    check(chunked.done() && pieces == out, "fed in chunks, the stretch is the same audio");

    std::vector<float> stereo(in.size() * 2);
    for (std::size_t i = 0; i < in.size(); ++i) { stereo[2 * i] = in[i]; stereo[2 * i + 1] = -in[i]; }
    const auto st = sf::stretchTo(stereo.data(), in.size(), 2, rate, outFrames);
    check(st.size() == outFrames * 2 && std::fabs(st[2 * 5000] + st[2 * 5000 + 1]) < 1e-5f, "channels move together");
}

TEST_CASE(testSmartFadeMixerHold) {
    sf::Mixer m;
    m.configure(1000, 2);
    std::vector<float> out;
    const auto in = constant(100, 2, 0.25f);
    m.process(in.data(), 100, out);
    check(out == in, "no hold passes audio through");

    out.clear();
    m.setHold(30, out);
    m.process(in.data(), 100, out);
    check(out.size() == 70 * 2 && m.latencyFrames() == 30, "hold keeps the last frames back");
    out.clear();
    m.setHold(0, out);
    check(out.size() == 30 * 2 && m.latencyFrames() == 0, "dropping the hold releases them");

    out.clear();
    m.setHold(30, out);
    m.process(in.data(), 100, out);
    m.flush();
    check(m.latencyFrames() == 0, "flush drops held audio");

    out.clear();
    m.process(in.data(), 100, out);
    m.drain(out);
    check(out.size() == 100 * 2 && out.back() < 0.25f * 0.1f && out[0] == 0.25f, "drain fades the held end out");
}

TEST_CASE(testSmartFadeMixerMix) {
    const unsigned rate = 1000;
    sf::Mixer m;
    m.configure(rate, 1);
    std::vector<float> out;
    m.setHold(200, out);
    const auto a = constant(1000, 1, 1.0f);
    m.process(a.data(), 1000, out);
    check(out.size() == 800, "song A plays up to its mix");
    m.endTrack(out);
    check(m.state() == sf::Mixer::State::Tail, "end of A keeps its tail");
    m.beginNext(200, 0, out);
    check(m.state() == sf::Mixer::State::Collect, "B's piece is collected");
    const auto b = constant(1000, 1, 0.5f);
    m.process(b.data(), 100, out);
    check(out.size() == 900, "the mix comes out as B arrives");
    m.process(b.data() + 100, 900, out);
    check(out.size() == 1800, "A and B overlap for the mix: no frame is lost or doubled");
    check(std::fabs(out[800] - 1.0f) < 0.02f, "mix starts on A");
    check(std::fabs(out[999] - 0.5f) < 0.02f, "mix ends on B");
    const float mid = out[900];
    check(std::fabs(mid - (std::cos(sf::kPi / 4) + 0.5 * std::sin(sf::kPi / 4))) < 0.02, "equal-power curves");
    check(out[1000] == 0.5f && out.back() == 0.5f, "B goes on at normal speed");
    check(m.state() == sf::Mixer::State::Live && m.latencyFrames() == 0, "back to live after the mix");
}

TEST_CASE(testSmartFadeMixerStretch) {
    const unsigned rate = 8000;
    sf::Mixer m;
    m.configure(rate, 1);
    std::vector<float> out;
    const std::size_t tail = 2000, piece = 2100;
    m.setHold(tail, out);
    const auto a = sine(8000, rate, 200.0);
    m.process(a.data(), a.size(), out);
    m.endTrack(out);
    m.beginNext(piece, 0, out);
    const auto b = sine(8000, rate, 300.0);
    const std::size_t before = out.size();
    m.process(b.data(), 1000, out);
    check(out.size() > before + 500, "a stretched mix also comes out while B arrives");
    m.process(b.data() + 1000, b.size() - 1000, out);
    check(out.size() == a.size() + b.size() - piece, "stretched mix: B resumes right after its piece");
}

TEST_CASE(testSmartFadeMixerFallback) {
    sf::Mixer m;
    m.configure(1000, 1);
    std::vector<float> out;
    m.setHold(100, out);
    const auto a = constant(500, 1, 1.0f);
    m.process(a.data(), 500, out);
    m.endTrack(out);
    m.beginNext(0, 50, out);
    check(out.size() == 500 && out.back() < 0.05f, "a track that doesn't follow gets A's tail faded out");
    const auto b = constant(200, 1, 1.0f);
    m.process(b.data(), 200, out);
    check(out.size() == 700 && out[500] < 0.05f && out[560] == 1.0f, "and fades in itself");

    m.flush();
    out.clear();
    m.setHold(100, out);
    m.process(a.data(), 500, out);
    m.endTrack(out);
    m.beginNext(150, 0, out);
    m.process(b.data(), 50, out);
    m.endTrack(out);
    check(out.size() == 550 && m.state() == sf::Mixer::State::Live, "a next song too short for its piece falls back to fades");
}

}
