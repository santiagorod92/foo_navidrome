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

double response(const sf::Biquad& q, double w) {
    const double cr = 1.0 + q.a1 * std::cos(w) + q.a2 * std::cos(2 * w);
    const double ci = -(q.a1 * std::sin(w) + q.a2 * std::sin(2 * w));
    const double nr = q.b0 + q.b1 * std::cos(w) + q.b2 * std::cos(2 * w);
    const double ni = -(q.b1 * std::sin(w) + q.b2 * std::sin(2 * w));
    return std::sqrt((nr * nr + ni * ni) / (cr * cr + ci * ci));
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(to - from));
}

sf::Transition flatPlan(double seconds, double pieceSeconds) {
    sf::Transition t = sf::legacyTransition("a", "b", 10.0, seconds, 20.0, 20.0 + pieceSeconds);
    return t;
}

TEST_CASE(testSmartFadeTransitionCache) {
    sf::Transition t = sf::legacyTransition("a", "b", 213.78, 2.62, 21.15, 23.74);
    check(t.valid() && t.type == "legacy", "AudioMuse 3.7's transition converts to a valid plan");
    check(std::fabs(t.fromSeconds() - 2.62) < 1e-9 && std::fabs(t.mixEnd() - 216.40) < 1e-9, "legacy: song A plays its mix at normal speed");
    check(std::fabs(t.toSeconds() - 2.59) < 1e-9 && std::fabs(t.to.speed.front() - 2.59 / 2.62) < 1e-9,
          "legacy: song B's piece plays at its stretch rate");
    sf::Transition bad = t;
    bad.continueFrom = bad.toStart;
    check(!bad.valid(), "an empty piece is refused");
    bad = t; bad.toId = "a";
    check(!bad.valid(), "a song can't mix into itself");
    bad = t; bad.duration = 90.0;
    check(!bad.valid(), "an overlong mix is refused");

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
    cache.put(sf::legacyTransition("a", "c", 1, 2, 3, 5));
    check(cache.outgoing("a")->toId == "c", "the latest pair for a song wins");
}

TEST_CASE(testSmartFadeCurves) {
    const std::vector<double> v = { 0.0, 1.0, 3.0 };
    check(sf::curveAt(v, 0.5, 0.25, 9.0) == 0.5 && sf::curveAt(v, 0.5, 0.75, 9.0) == 2.0, "curves join points with straight lines");
    check(sf::curveAt(v, 0.5, -1.0, 9.0) == 0.0 && sf::curveAt(v, 0.5, 5.0, 9.0) == 3.0, "curves hold their ends");
    check(sf::curveAt({}, 0.5, 1.0, 9.0) == 9.0, "a missing curve gives its default");

    sf::Transition t;
    t.duration = 10.0; t.step = 5.0;
    t.from.speed = { 1.0, 1.0, 0.9 };
    check(std::fabs(t.fromSeconds() - 9.75) < 1e-3, "song A's length follows its speed");

    const sf::Transition fade = sf::fallbackCrossfade("a", "b", 200.0, 12.0);
    check(fade.valid() && fade.start == 190.0 && fade.duration == 10.0 && fade.toStart == 12.0 && fade.continueFrom == 22.0,
          "no answer: a 10 s crossfade at the smart fade points");
    bool sumsToOne = true;
    for (double x = 0.0; x <= 10.0; x += 0.37)
        sumsToOne &= std::fabs(sf::curveAt(fade.from.volume, fade.step, x, 0) + sf::curveAt(fade.to.volume, fade.step, x, 0) - 1.0) < 1e-6;
    check(sumsToOne, "the fallback's two S-curves add up to 1");
    check(sf::fallbackCrossfade("a", "b", 4.0, 0.0).duration == 4.0, "a short song gets a shorter fallback");
}

TEST_CASE(testSmartFadeBiquads) {
    const unsigned rate = 44100;
    const double low = 2 * sf::kPi * 20.0 / rate, high = 2 * sf::kPi * 20000.0 / rate;
    check(std::fabs(response(sf::Biquad::lowShelf(180, 0, rate), low) - 1.0) < 1e-9 &&
          std::fabs(response(sf::Biquad::peaking(1000, 0.5, 0, rate), 0.3) - 1.0) < 1e-9 &&
          std::fabs(response(sf::Biquad::highShelf(4000, 0, rate), high) - 1.0) < 1e-9, "0 dB filters leave the audio alone");
    check(std::fabs(20 * std::log10(response(sf::Biquad::lowShelf(180, -40, rate), 1e-4)) + 40.0) < 0.1 &&
          response(sf::Biquad::lowShelf(180, -40, rate), high) > 0.99, "the bass shelf cuts the bass only");
    check(std::fabs(20 * std::log10(response(sf::Biquad::highShelf(4000, -40, rate), sf::kPi * 0.999)) + 40.0) < 0.5 &&
          response(sf::Biquad::highShelf(4000, -40, rate), low) > 0.99, "the high shelf cuts the highs only");
    check(std::fabs(20 * std::log10(response(sf::Biquad::peaking(1000, 0.5, -27, rate), 2 * sf::kPi * 1000 / rate)) + 27.0) < 0.1,
          "the mids filter cuts at its centre");
    check(response(sf::Biquad::highpass(800, rate), 2 * sf::kPi * 50 / rate) < 0.01 &&
          std::fabs(response(sf::Biquad::highpass(800, rate), high) - 1.0) < 0.01, "the high-pass removes what is below it");
}

TEST_CASE(testSmartFadeMixerHold) {
    sf::Mixer m;
    m.configure(1000, 2);
    std::vector<float> out;
    const auto in = constant(100, 2, 0.25f);
    check(m.idle(), "a fresh mixer is idle");
    m.process(in.data(), 100, out);
    check(out == in && m.idle(), "no hold passes audio through");

    out.clear();
    m.setHold(30, out);
    check(!m.idle(), "a hold means work to do");
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

    sf::Mixer g;
    g.configure(1000, 1);
    out.clear();
    g.setGain(0.5);
    check(!g.idle(), "a loudness gain means work to do");
    const auto one = constant(300, 1, 1.0f);
    g.process(one.data(), 300, out);
    check(out[0] > 0.99f && std::fabs(out[150] - 0.5f) < 1e-6 && std::fabs(out[299] - 0.5f) < 1e-6,
          "the gain glides in over 0.1 s and then holds");
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
    const sf::Transition plan = flatPlan(0.2, 0.2);
    m.beginNext(&plan, 0, out);
    check(m.state() == sf::Mixer::State::Collect, "B is mixed in as it arrives");
    const auto b = constant(1000, 1, 0.5f);
    m.process(b.data(), 100, out);
    check(out.size() > 850 && out.size() < 900, "the mix comes out as B arrives");
    m.process(b.data() + 100, 900, out);
    check(out.size() == 1800, "A and B overlap for the mix: no frame is lost or doubled");
    check(std::fabs(out[800] - 1.0f) < 0.02f, "mix starts on A");
    check(std::fabs(out[999] - 0.5f) < 0.02f, "mix ends on B");
    check(std::fabs(out[900] - (std::cos(sf::kPi / 4) + 0.5 * std::sin(sf::kPi / 4))) < 0.02, "equal-power curves");
    check(out[1000] == 0.5f && out.back() == 0.5f, "B goes on untouched after the mix");
    check(m.state() == sf::Mixer::State::Live && m.latencyFrames() == 0 && m.idle(), "back to idle after the mix");
}

TEST_CASE(testSmartFadeMixerSpeed) {
    const unsigned rate = 8000;
    sf::Mixer m;
    m.configure(rate, 1);
    std::vector<float> out;
    const std::size_t tail = 2000, piece = 2100;
    m.setHold(tail, out);
    const auto a = sine(8000, rate, 200.0);
    m.process(a.data(), a.size(), out);
    m.endTrack(out);
    const sf::Transition plan = flatPlan(0.25, 0.2625);
    m.beginNext(&plan, 0, out);
    const auto b = sine(8000, rate, 300.0);
    const std::size_t before = out.size();
    m.process(b.data(), 1000, out);
    check(out.size() > before + 800, "a faster piece also comes out while B arrives");
    m.process(b.data() + 1000, b.size() - 1000, out);
    check(out.size() == a.size() + b.size() - piece, "B resumes right after its piece");
    double jump = 0.0;
    for (std::size_t i = out.size() - (b.size() - piece) - 3; i < out.size() - (b.size() - piece) + 3; ++i)
        jump = (std::max)(jump, static_cast<double>(std::fabs(out[i + 1] - out[i])));
    check(jump < 0.3, "no click where B goes back to normal speed");
}

TEST_CASE(testSmartFadeMixerShapes) {
    const unsigned rate = 8000;
    sf::Transition plan = flatPlan(1.0, 1.0);
    plan.step = 0.1;
    plan.from.volume.assign(11, 1.0);
    plan.to.volume.assign(11, 1.0);
    plan.from.gain = 0.5;
    plan.to.gain = 2.0;
    plan.from.bassDb.assign(11, 0.0);
    plan.to.highpassHz.assign(11, 2000.0);
    plan.from.highDb.assign(11, -40.0);
    check(plan.filtered(), "a plan with filter curves runs the filters");

    sf::Mixer m;
    m.configure(rate, 1);
    std::vector<float> out;
    m.setHold(8000, out);
    m.setGain(plan.from.gain);
    const auto a = sine(16000, rate, 100.0);
    m.process(a.data(), a.size(), out);
    m.endTrack(out);
    check(std::fabs(rms(out, 2000, 8000) - 0.5 / std::sqrt(2.0)) < 0.01, "song A plays at its loudness gain");
    m.beginNext(&plan, 0, out);
    const auto b = sine(24000, rate, 100.0);
    const std::size_t mixStart = out.size();
    m.process(b.data(), b.size(), out);
    const double mixed = rms(out, mixStart + 2000, mixStart + 7000);
    check(std::fabs(mixed - 0.5 / std::sqrt(2.0)) < 0.05, "during the mix B's bass is held back by its high-pass");
    bool finite = true;
    for (float v : out) finite &= std::isfinite(v);
    check(finite, "the filters stay stable");
    check(std::fabs(rms(out, out.size() - 4000, out.size()) - 2.0 / std::sqrt(2.0)) < 0.02, "song B goes on at its own gain");
    check(std::fabs(m.gain() - 2.0) < 1e-9 && !m.idle(), "the mixer keeps song B's gain");
}

TEST_CASE(testSmartFadeMixerFallback) {
    sf::Mixer m;
    m.configure(1000, 1);
    std::vector<float> out;
    m.setHold(100, out);
    const auto a = constant(500, 1, 1.0f);
    m.process(a.data(), 500, out);
    m.endTrack(out);
    m.beginNext(nullptr, 50, out);
    check(out.size() == 500 && out.back() < 0.05f, "a track that doesn't follow gets A's tail faded out");
    const auto b = constant(200, 1, 1.0f);
    m.process(b.data(), 200, out);
    check(out.size() == 700 && out[500] < 0.05f && out[560] == 1.0f, "and fades in itself");
    check(m.idle(), "idle again once the fade in is done");

    m.flush();
    out.clear();
    m.setHold(100, out);
    m.process(a.data(), 500, out);
    m.endTrack(out);
    const sf::Transition plan = flatPlan(0.1, 0.15);
    m.beginNext(&plan, 0, out);
    m.process(b.data(), 50, out);
    m.endTrack(out);
    check(out.size() >= 500 && m.state() == sf::Mixer::State::Live, "a next song too short for its piece falls back to fades");

    sf::Mixer x;
    x.configure(1000, 1);
    out.clear();
    x.setHold(200, out);
    const auto ones = constant(1000, 1, 1.0f);
    x.process(ones.data(), 1000, out);
    x.endTrack(out);
    const sf::Transition fade = sf::fallbackCrossfade("a", "b", 0.2, 0.0);
    x.beginNext(&fade, 0, out);
    x.process(ones.data(), 1000, out);
    float lo = 2.0f, hi = 0.0f;
    for (std::size_t i = 800; i < 1000; ++i) { lo = (std::min)(lo, out[i]); hi = (std::max)(hi, out[i]); }
    check(lo > 0.98f && hi < 1.02f, "the fallback crossfade keeps the level of two equal songs");
}

}
