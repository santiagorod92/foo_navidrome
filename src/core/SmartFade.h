#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace navidrome::smartfade {

constexpr double kMaxMixSeconds      = 40.0;
constexpr double kFallbackMixSeconds = 10.0;
constexpr double kHighpassQ          = 1.1220184543019633;
constexpr double kPi                 = 3.14159265358979323846;
constexpr std::size_t kFilterBlock   = 64;

struct SongCurves {
    double gain = 1.0;
    std::vector<double> speed;
    std::vector<double> volume;
    std::vector<double> highDb;
    std::vector<double> midDb;
    std::vector<double> bassDb;
    std::vector<double> highpassHz;
};

inline double curveAt(const std::vector<double>& v, double step, double t, double fallback) {
    if (v.empty()) return fallback;
    if (v.size() == 1 || step <= 0.0 || t <= 0.0) return v.front();
    const double x = t / step;
    const std::size_t i = static_cast<std::size_t>(x);
    if (i + 1 >= v.size()) return v.back();
    const double f = x - static_cast<double>(i);
    return v[i] + (v[i + 1] - v[i]) * f;
}

inline bool curveIsFlat(const std::vector<double>& v) {
    return std::all_of(v.begin(), v.end(), [](double d) { return std::fabs(d) < 1e-6; });
}

struct Transition {
    std::string fromId;
    std::string toId;
    std::string type;
    double start        = 0.0;
    double duration     = 0.0;
    double step         = 0.0;
    double highHz       = 4000.0;
    double midHz        = 1000.0;
    double midQ         = 0.5;
    double bassHz       = 180.0;
    SongCurves from;
    SongCurves to;
    double toStart      = 0.0;
    double continueFrom = 0.0;

    double fromSeconds() const {
        if (from.speed.size() < 2 || step <= 0.0) return duration * curveAt(from.speed, step, 0.0, 1.0);
        double sum = 0.0;
        const std::size_t n = 2000;
        for (std::size_t i = 0; i < n; ++i)
            sum += curveAt(from.speed, step, duration * (static_cast<double>(i) + 0.5) / n, 1.0);
        return sum * duration / n;
    }
    double toSeconds() const { return continueFrom - toStart; }
    double mixEnd() const { return start + fromSeconds(); }
    bool filtered() const {
        return !curveIsFlat(from.highDb) || !curveIsFlat(from.midDb) || !curveIsFlat(from.bassDb)
            || !curveIsFlat(to.highDb) || !curveIsFlat(to.midDb) || !curveIsFlat(to.bassDb) || !to.highpassHz.empty();
    }
    bool valid() const {
        if (fromId.empty() || toId.empty() || fromId == toId) return false;
        if (start < 0.0 || toStart < 0.0) return false;
        if (!(duration > 0.0) || duration > kMaxMixSeconds) return false;
        if (!(from.gain > 0.0) || !(to.gain > 0.0) || from.gain > 16.0 || to.gain > 16.0) return false;
        const double fromLen = fromSeconds(), toLen = toSeconds();
        if (!(fromLen > 0.25 * duration) || fromLen > 4.0 * duration) return false;
        return toLen > 0.25 * duration && toLen <= 4.0 * duration;
    }
};

inline std::vector<double> sampled(double seconds, double step, double (*shape)(double)) {
    const std::size_t count = static_cast<std::size_t>(std::llround(seconds / step)) + 1;
    std::vector<double> out(count);
    for (std::size_t i = 0; i < count; ++i)
        out[i] = shape((std::min)(1.0, static_cast<double>(i) * step / seconds));
    return out;
}

inline Transition legacyTransition(const std::string& fromId, const std::string& toId, double mixAt,
                                   double mixSeconds, double inAt, double resumeAt) {
    Transition t;
    t.fromId = fromId;
    t.toId = toId;
    t.type = "legacy";
    t.start = mixAt;
    t.duration = mixSeconds;
    t.step = mixSeconds > 0.0 ? mixSeconds / 64.0 : 0.0;
    t.toStart = inAt;
    t.continueFrom = resumeAt;
    if (t.step > 0.0) {
        t.from.volume = sampled(mixSeconds, t.step, [](double x) { return std::cos(0.5 * kPi * x); });
        t.to.volume = sampled(mixSeconds, t.step, [](double x) { return std::sin(0.5 * kPi * x); });
    }
    if (mixSeconds > 0.0) t.to.speed = { (resumeAt - inAt) / mixSeconds };
    return t;
}

inline Transition fallbackCrossfade(const std::string& fromId, const std::string& toId, double outSeconds,
                                    double inSeconds) {
    Transition t;
    t.fromId = fromId;
    t.toId = toId;
    t.type = "fallback";
    t.duration = (std::min)(kFallbackMixSeconds, (std::max)(0.0, outSeconds));
    t.start = outSeconds - t.duration;
    t.step = t.duration > 0.0 ? t.duration / 64.0 : 0.0;
    t.toStart = inSeconds;
    t.continueFrom = inSeconds + t.duration;
    if (t.step > 0.0) {
        t.from.volume = sampled(t.duration, t.step, [](double x) { return 0.5 + 0.5 * std::cos(kPi * x); });
        t.to.volume = sampled(t.duration, t.step, [](double x) { return 0.5 - 0.5 * std::cos(kPi * x); });
    }
    return t;
}

class TransitionCache {
public:
    void put(const Transition& t) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_out[t.fromId] = t;
        m_in[t.toId] = t;
    }
    std::optional<Transition> outgoing(const std::string& fromId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_out.find(fromId);
        if (it == m_out.end()) return std::nullopt;
        return it->second;
    }
    std::optional<Transition> incoming(const std::string& toId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_in.find(toId);
        if (it == m_in.end()) return std::nullopt;
        return it->second;
    }
    bool begin(const std::string& fromId, const std::string& toId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_out.find(fromId);
        if (it != m_out.end() && it->second.toId == toId) return false;
        return m_pending.insert(fromId + '\n' + toId).second;
    }
    bool pendingFrom(const std::string& fromId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_pending.lower_bound(fromId + '\n');
        return it != m_pending.end() && it->compare(0, fromId.size() + 1, fromId + '\n') == 0;
    }
    void end(const std::string& fromId, const std::string& toId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(fromId + '\n' + toId);
    }
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_out.clear();
        m_in.clear();
        m_pending.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::map<std::string, Transition> m_out;
    std::map<std::string, Transition> m_in;
    std::set<std::string> m_pending;
};

inline TransitionCache& transitions() {
    static TransitionCache cache;
    return cache;
}

inline std::size_t framesFor(double seconds, unsigned rate) {
    if (seconds <= 0.0 || rate == 0) return 0;
    return static_cast<std::size_t>(std::llround(seconds * rate));
}

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static Biquad normalized(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        Biquad q;
        q.b0 = nb0 / na0; q.b1 = nb1 / na0; q.b2 = nb2 / na0;
        q.a1 = na1 / na0; q.a2 = na2 / na0;
        return q;
    }
    static double angle(double hz, unsigned rate) {
        return 2.0 * kPi * (std::min)(hz, 0.45 * rate) / rate;
    }
    static Biquad shelf(double hz, double db, unsigned rate, double side) {
        const double g = std::pow(10.0, db / 40.0);
        const double w = angle(hz, rate);
        const double root = std::sqrt(2.0 * g) * std::sin(w);
        const double c = side * std::cos(w);
        return normalized(g * ((g + 1) - (g - 1) * c + root), 2 * side * g * ((g - 1) - (g + 1) * c),
                          g * ((g + 1) - (g - 1) * c - root),
                          (g + 1) + (g - 1) * c + root, -2 * side * ((g - 1) + (g + 1) * c),
                          (g + 1) + (g - 1) * c - root);
    }
    static Biquad lowShelf(double hz, double db, unsigned rate) { return shelf(hz, db, rate, 1.0); }
    static Biquad highShelf(double hz, double db, unsigned rate) { return shelf(hz, db, rate, -1.0); }
    static Biquad peaking(double hz, double q, double db, unsigned rate) {
        const double g = std::pow(10.0, db / 40.0);
        const double w = angle(hz, rate);
        const double alpha = std::sin(w) / (2.0 * q);
        const double c = std::cos(w);
        return normalized(1 + alpha * g, -2 * c, 1 - alpha * g, 1 + alpha / g, -2 * c, 1 - alpha / g);
    }
    static Biquad highpass(double hz, unsigned rate) {
        const double w = angle(hz, rate);
        const double alpha = std::sin(w) / (2.0 * kHighpassQ);
        const double c = std::cos(w);
        return normalized((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
};

struct BiquadState {
    double z1 = 0.0, z2 = 0.0;
    double run(const Biquad& q, double x) {
        const double y = q.b0 * x + z1;
        z1 = q.b1 * x - q.a1 * y + z2;
        z2 = q.b2 * x - q.a2 * y;
        return y;
    }
};

template <typename T>
double cubicAt(const std::vector<T>& buf, std::size_t head, std::size_t frames, unsigned channels, unsigned c, double pos) {
    if (frames == 0) return 0.0;
    const double floorPos = std::floor(pos);
    const double f = pos - floorPos;
    const long long i = static_cast<long long>(floorPos);
    auto at = [&](long long k) -> double {
        if (k < 0) k = 0;
        if (k >= static_cast<long long>(frames)) k = static_cast<long long>(frames) - 1;
        return static_cast<double>(buf[head + static_cast<std::size_t>(k) * channels + c]);
    };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
    const double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    const double d = -0.5 * y0 + 0.5 * y2;
    return ((a * f + b) * f + d) * f + y1;
}

template <typename T>
class BasicMixer {
public:
    enum class State { Live, Tail, Collect };

    void configure(unsigned rate, unsigned channels) {
        m_rate = rate;
        m_channels = channels;
        flush();
    }
    unsigned rate() const { return m_rate; }
    unsigned channels() const { return m_channels; }
    State state() const { return m_state; }
    std::size_t hold() const { return m_hold; }
    double gain() const { return m_gainTarget; }
    bool idle() const {
        return m_state == State::Live && m_hold == 0 && bufFrames() == 0 && m_fadeInDone >= m_fadeIn
            && m_gain == 1.0 && m_gainTarget == 1.0;
    }

    void setGain(double target) {
        if (!(target > 0.0)) target = 1.0;
        if (target == m_gainTarget) return;
        m_gainTarget = target;
        const std::size_t ramp = (std::max)(framesFor(0.1, m_rate), std::size_t(1));
        m_gainStep = (m_gainTarget - m_gain) / static_cast<double>(ramp);
    }

    void setHold(std::size_t frames, std::vector<T>& out) {
        m_hold = frames;
        if (m_state == State::Live) release(out);
    }

    void process(const T* in, std::size_t frames, std::vector<T>& out) {
        if (frames == 0 || m_channels == 0) return;
        if (m_state == State::Collect) { collect(in, frames, out); return; }
        if (m_state == State::Tail) {
            emitFaded(out);
            m_state = State::Live;
        }
        if (m_hold == 0 && bufFrames() == 0) { emitLive(in, frames, out); return; }
        m_buf.insert(m_buf.end(), in, in + frames * m_channels);
        release(out);
    }

    void endTrack(std::vector<T>& out) {
        if (m_state == State::Collect) { fallback(out); return; }
        if (m_state == State::Live && m_hold > 0 && bufFrames() > 0) m_state = State::Tail;
    }

    void beginNext(const Transition* plan, std::size_t fadeInFrames, std::vector<T>& out) {
        m_hold = 0;
        const std::size_t tail = bufFrames();
        if (m_state == State::Tail && plan && tail > 0 && plan->duration > 0.0) {
            startPlan(*plan, tail);
            return;
        }
        if (m_state == State::Tail) {
            emitFaded(out);
            m_state = State::Live;
        }
        startFadeIn(fadeInFrames);
    }

    void startFadeIn(std::size_t frames) {
        m_fadeIn = frames;
        m_fadeInDone = 0;
    }

    void drain(std::vector<T>& out) {
        if (m_state == State::Collect) { fallback(out); return; }
        emitFaded(out);
        m_state = State::Live;
    }

    void flush() {
        m_buf.clear();
        m_head = 0;
        m_b.clear();
        m_state = State::Live;
        m_fadeIn = 0;
        m_fadeInDone = 0;
    }

    std::size_t latencyFrames() const {
        if (m_state != State::Collect) return bufFrames();
        const std::size_t used = static_cast<std::size_t>(m_posB);
        const std::size_t have = bFrames();
        return (m_total - (std::min)(m_done, m_total)) + (have > used ? have - used : 0);
    }

private:
    std::size_t bufFrames() const { return m_channels ? (m_buf.size() - m_head) / m_channels : 0; }
    std::size_t bFrames() const { return m_channels ? m_b.size() / m_channels : 0; }

    void compact() {
        if (m_head == 0) return;
        if (m_head >= m_buf.size()) { m_buf.clear(); m_head = 0; return; }
        if (m_head * 2 < m_buf.size()) return;
        m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(m_head));
        m_head = 0;
    }

    void release(std::vector<T>& out) {
        const std::size_t have = bufFrames();
        if (have <= m_hold) return;
        const std::size_t n = have - m_hold;
        emitLive(m_buf.data() + m_head, n, out);
        m_head += n * m_channels;
        compact();
    }

    double nextGain() {
        if (m_gain == m_gainTarget) return m_gain;
        m_gain += m_gainStep;
        if ((m_gainStep > 0.0 && m_gain >= m_gainTarget) || (m_gainStep < 0.0 && m_gain <= m_gainTarget)) m_gain = m_gainTarget;
        return m_gain;
    }

    void emitLive(const T* in, std::size_t frames, std::vector<T>& out) {
        const std::size_t start = out.size();
        out.insert(out.end(), in, in + frames * m_channels);
        if (m_fadeInDone >= m_fadeIn && m_gain == 1.0 && m_gainTarget == 1.0) return;
        for (std::size_t f = 0; f < frames; ++f) {
            double g = nextGain();
            if (m_fadeInDone < m_fadeIn) {
                const double t = (static_cast<double>(m_fadeInDone) + 0.5) / static_cast<double>(m_fadeIn);
                g *= std::sin(0.5 * kPi * t);
                ++m_fadeInDone;
            }
            if (g == 1.0) continue;
            for (unsigned c = 0; c < m_channels; ++c)
                out[start + f * m_channels + c] = static_cast<T>(out[start + f * m_channels + c] * g);
        }
    }

    void emitFaded(std::vector<T>& out) {
        const std::size_t n = bufFrames();
        const T* src = m_buf.data() + m_head;
        for (std::size_t f = 0; f < n; ++f) {
            const double t = (static_cast<double>(f) + 0.5) / static_cast<double>(n);
            const double g = nextGain() * std::cos(0.5 * kPi * t);
            for (unsigned c = 0; c < m_channels; ++c) out.push_back(static_cast<T>(src[f * m_channels + c] * g));
        }
        m_buf.clear();
        m_head = 0;
    }

    double rateSum(const std::vector<double>& speed) const {
        double sum = 0.0;
        for (std::size_t n = 0; n < m_total; ++n)
            sum += curveAt(speed, m_plan.step, static_cast<double>(n) / m_rate, 1.0);
        return sum;
    }

    void startPlan(const Transition& plan, std::size_t tail) {
        m_plan = plan;
        m_total = (std::max)(framesFor(plan.duration, m_rate), std::size_t(1));
        m_tail = tail;
        m_piece = (std::max)(framesFor(plan.toSeconds(), m_rate), std::size_t(1));
        const double sumA = rateSum(plan.from.speed), sumB = rateSum(plan.to.speed);
        m_kA = sumA > 0.0 ? static_cast<double>(m_tail) / sumA : 1.0;
        m_kB = sumB > 0.0 ? static_cast<double>(m_piece) / sumB : 1.0;
        m_posA = 0.0;
        m_posB = 0.0;
        m_done = 0;
        m_eq = plan.filtered();
        m_stateA.assign(static_cast<std::size_t>(m_channels) * 3, BiquadState{});
        m_stateB.assign(static_cast<std::size_t>(m_channels) * 3, BiquadState{});
        m_b.clear();
        m_gain = m_gainTarget = 1.0;
        m_state = State::Collect;
    }

    void coefficients(double t) {
        const Transition& p = m_plan;
        m_qA[0] = Biquad::lowShelf(p.bassHz, curveAt(p.from.bassDb, p.step, t, 0.0), m_rate);
        m_qA[1] = Biquad::peaking(p.midHz, p.midQ, curveAt(p.from.midDb, p.step, t, 0.0), m_rate);
        m_qA[2] = Biquad::highShelf(p.highHz, curveAt(p.from.highDb, p.step, t, 0.0), m_rate);
        m_qB[0] = p.to.highpassHz.empty() ? Biquad::lowShelf(p.bassHz, curveAt(p.to.bassDb, p.step, t, 0.0), m_rate)
                                          : Biquad::highpass(curveAt(p.to.highpassHz, p.step, t, 10.0), m_rate);
        m_qB[1] = Biquad::peaking(p.midHz, p.midQ, curveAt(p.to.midDb, p.step, t, 0.0), m_rate);
        m_qB[2] = Biquad::highShelf(p.highHz, curveAt(p.to.highDb, p.step, t, 0.0), m_rate);
    }

    double filterA(unsigned c, double x) {
        BiquadState* s = &m_stateA[static_cast<std::size_t>(c) * 3];
        return s[2].run(m_qA[2], s[1].run(m_qA[1], s[0].run(m_qA[0], x)));
    }
    double filterB(unsigned c, double x) {
        BiquadState* s = &m_stateB[static_cast<std::size_t>(c) * 3];
        return s[2].run(m_qB[2], s[1].run(m_qB[1], s[0].run(m_qB[0], x)));
    }

    void collect(const T* in, std::size_t frames, std::vector<T>& out) {
        m_b.insert(m_b.end(), in, in + frames * m_channels);
        const std::size_t have = bFrames();
        const Transition& p = m_plan;
        for (; m_done < m_total; ++m_done) {
            if (m_posB + 3.0 >= static_cast<double>(have) && have < m_piece + 3) break;
            const double t = static_cast<double>(m_done) / m_rate;
            if (m_eq && m_done % kFilterBlock == 0) coefficients(t);
            const double gA = p.from.gain * curveAt(p.from.volume, p.step, t, 1.0);
            const double gB = p.to.gain * curveAt(p.to.volume, p.step, t, 1.0);
            for (unsigned c = 0; c < m_channels; ++c) {
                double a = cubicAt(m_buf, m_head, m_tail, m_channels, c, m_posA);
                double b = cubicAt(m_b, 0, have, m_channels, c, m_posB);
                if (m_eq) { a = filterA(c, a); b = filterB(c, b); }
                out.push_back(static_cast<T>(a * gA + b * gB));
            }
            m_posA += m_kA * curveAt(p.from.speed, p.step, t, 1.0);
            m_posB += m_kB * curveAt(p.to.speed, p.step, t, 1.0);
        }
        if (m_done < m_total) return;
        std::vector<T> b;
        b.swap(m_b);
        m_buf.clear();
        m_head = 0;
        m_state = State::Live;
        m_gain = m_gainTarget = p.to.gain;
        const std::size_t total = b.size() / m_channels;
        const std::size_t rest = (std::min)(m_piece, total);
        if (rest < total) process(b.data() + rest * m_channels, total - rest, out);
    }

    void fallback(std::vector<T>& out) {
        const std::size_t fromA = (std::min)(static_cast<std::size_t>(m_posA), m_tail);
        const std::size_t left = m_tail - fromA;
        const T* a = m_buf.data() + m_head;
        for (std::size_t f = 0; f < left; ++f) {
            const double g = m_plan.from.gain * std::cos(0.5 * kPi * (static_cast<double>(f) + 0.5) / static_cast<double>(left));
            for (unsigned c = 0; c < m_channels; ++c)
                out.push_back(static_cast<T>(a[(fromA + f) * m_channels + c] * g));
        }
        std::vector<T> b;
        b.swap(m_b);
        const std::size_t have = b.size() / m_channels;
        const std::size_t from = (std::min)(static_cast<std::size_t>(m_posB), have);
        m_buf.clear();
        m_head = 0;
        m_state = State::Live;
        m_gain = m_gainTarget = m_plan.to.gain;
        startFadeIn(m_done == 0 ? have - from : 0);
        if (from < have) emitLive(b.data() + from * m_channels, have - from, out);
    }

    unsigned m_rate = 0;
    unsigned m_channels = 0;
    State m_state = State::Live;
    std::size_t m_hold = 0;
    std::vector<T> m_buf;
    std::size_t m_head = 0;
    std::vector<T> m_b;
    std::size_t m_fadeIn = 0;
    std::size_t m_fadeInDone = 0;
    double m_gain = 1.0;
    double m_gainTarget = 1.0;
    double m_gainStep = 0.0;

    Transition m_plan;
    std::size_t m_total = 0, m_tail = 0, m_piece = 0, m_done = 0;
    double m_kA = 1.0, m_kB = 1.0, m_posA = 0.0, m_posB = 0.0;
    bool m_eq = false;
    Biquad m_qA[3], m_qB[3];
    std::vector<BiquadState> m_stateA, m_stateB;
};

using Mixer = BasicMixer<float>;

}
