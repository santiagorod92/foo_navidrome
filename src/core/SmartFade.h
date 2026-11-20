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

constexpr double kMaxMixSeconds = 16.0;
constexpr double kBlendSeconds  = 0.011;
constexpr double kPi            = 3.14159265358979323846;

struct Transition {
    std::string fromId;
    std::string toId;
    double mixAt      = 0.0;
    double mixSeconds = 0.0;
    double inAt       = 0.0;
    double resumeAt   = 0.0;
    double stretch    = 1.0;

    double mixEnd() const { return mixAt + mixSeconds; }
    double pieceSeconds() const { return resumeAt - inAt; }
    bool valid() const {
        if (fromId.empty() || toId.empty() || fromId == toId) return false;
        if (mixAt < 0.0 || inAt < 0.0) return false;
        if (mixSeconds <= 0.0 || mixSeconds > kMaxMixSeconds) return false;
        const double piece = pieceSeconds();
        return piece >= 0.5 * mixSeconds && piece <= 2.0 * mixSeconds;
    }
};

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

template <typename T>
class Stretcher {
public:
    void start(unsigned channels, unsigned rate, std::size_t inFrames, std::size_t outFrames) {
        m_channels = channels;
        m_inFrames = inFrames;
        m_outFrames = outFrames;
        m_in.clear();
        m_mono.clear();
        m_coarse.clear();
        m_have = 0;
        m_ready = 0;
        m_pulled = 0;
        m_grain = 0;
        m_prev = 0;
        m_window = (std::max)(static_cast<std::size_t>(rate * 0.025), static_cast<std::size_t>(64)) & ~static_cast<std::size_t>(1);
        const std::size_t diff = inFrames > outFrames ? inFrames - outFrames : outFrames - inFrames;
        if (channels == 0 || inFrames == 0 || outFrames == 0 || diff <= 1) m_mode = Mode::Copy;
        else if (inFrames < 2 * m_window || outFrames < 2 * m_window) m_mode = Mode::Linear;
        else m_mode = Mode::Wsola;
        if (m_mode != Mode::Wsola) return;
        m_hop = m_window / 2;
        m_delta = static_cast<std::size_t>(rate * 0.008);
        m_maxPos = inFrames - m_window;
        m_alpha = static_cast<double>(inFrames) / static_cast<double>(outFrames);
        m_decim = (std::max)(rate / 11025u, 1u);
        m_hann.resize(m_window);
        for (std::size_t i = 0; i < m_window; ++i)
            m_hann[i] = static_cast<T>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(m_window)));
        m_acc.assign((outFrames + m_window) * channels, T(0));
        m_norm.assign(outFrames + m_window, T(0));
    }

    void push(const T* in, std::size_t frames) {
        frames = (std::min)(frames, m_inFrames - m_have);
        if (frames == 0) return;
        m_in.insert(m_in.end(), in, in + frames * m_channels);
        if (m_mode == Mode::Wsola) {
            for (std::size_t f = 0; f < frames; ++f) {
                T sum = T(0);
                for (unsigned c = 0; c < m_channels; ++c) sum += in[f * m_channels + c];
                m_mono.push_back(sum / static_cast<T>(m_channels));
                if (m_mono.size() % m_decim == 0) {
                    T block = T(0);
                    for (std::size_t i = m_mono.size() - m_decim; i < m_mono.size(); ++i) block += m_mono[i];
                    m_coarse.push_back(block / static_cast<T>(m_decim));
                }
            }
        }
        m_have += frames;
        advance();
    }

    void pull(std::vector<T>& out) {
        for (; m_pulled < m_ready; ++m_pulled) {
            const std::size_t f = m_pulled;
            for (unsigned c = 0; c < m_channels; ++c) out.push_back(sample(f, c));
        }
    }

    std::size_t inputNeeded() const { return m_inFrames - m_have; }
    bool done() const { return m_pulled == m_outFrames; }

private:
    enum class Mode { Copy, Linear, Wsola };

    void advance() {
        const bool complete = m_have == m_inFrames;
        if (m_mode == Mode::Copy) { m_ready = complete ? m_outFrames : (std::min)(m_have, m_outFrames); return; }
        if (m_mode == Mode::Linear) { m_ready = complete ? m_outFrames : 0; return; }
        while (m_grain * m_hop < m_outFrames) {
            std::size_t natural = 0, nominal = 0, lo = 0, hi = 0;
            bounds(natural, nominal, lo, hi);
            const std::size_t need = (m_grain == 0 ? 0 : (std::max)(natural, hi)) + m_window;
            if (!complete && m_have < need) break;
            addGrain(m_grain == 0 ? 0 : search(natural, nominal, lo, hi));
            ++m_grain;
            m_ready = (std::min)(m_grain * m_hop, m_outFrames);
        }
        if (m_grain * m_hop >= m_outFrames) m_ready = m_outFrames;
    }

    void bounds(std::size_t& natural, std::size_t& nominal, std::size_t& lo, std::size_t& hi) const {
        natural = (std::min)(m_prev + m_hop, m_maxPos);
        nominal = (std::min)(static_cast<std::size_t>(std::llround(m_grain * m_hop * m_alpha)), m_maxPos);
        lo = nominal > m_delta ? nominal - m_delta : 0;
        hi = (std::min)(nominal + m_delta, m_maxPos);
    }

    std::size_t search(std::size_t natural, std::size_t nominal, std::size_t lo, std::size_t hi) const {
        const std::size_t d = m_decim, len = m_window / d, nb = natural / d;
        std::size_t pos = nominal;
        double best = -1e300;
        for (std::size_t pb = (lo + d - 1) / d; pb * d <= hi && pb + len <= m_coarse.size() && nb + len <= m_coarse.size(); ++pb) {
            double dot = 0.0, energy = 1e-12;
            for (std::size_t j = 0; j < len; ++j) {
                dot += static_cast<double>(m_coarse[nb + j]) * m_coarse[pb + j];
                energy += static_cast<double>(m_coarse[pb + j]) * m_coarse[pb + j];
            }
            const double score = dot / std::sqrt(energy);
            if (score > best) { best = score; pos = pb * d; }
        }
        const std::size_t rlo = (std::max)(lo, pos > d ? pos - d : 0);
        const std::size_t rhi = (std::min)(hi, pos + d);
        best = -1e300;
        for (std::size_t p = rlo; p <= rhi; ++p) {
            double dot = 0.0, energy = 1e-12;
            for (std::size_t i = 0; i < m_window; i += 4) {
                dot += static_cast<double>(m_mono[natural + i]) * m_mono[p + i];
                energy += static_cast<double>(m_mono[p + i]) * m_mono[p + i];
            }
            const double score = dot / std::sqrt(energy);
            if (score > best) { best = score; pos = p; }
        }
        return pos;
    }

    void addGrain(std::size_t pos) {
        const std::size_t base = m_grain * m_hop;
        for (std::size_t i = 0; i < m_window; ++i) {
            const T w = m_hann[i];
            m_norm[base + i] += w;
            for (unsigned c = 0; c < m_channels; ++c)
                m_acc[(base + i) * m_channels + c] += m_in[(pos + i) * m_channels + c] * w;
        }
        m_prev = pos;
    }

    T sample(std::size_t f, unsigned c) const {
        if (m_mode == Mode::Copy) return f < m_have ? m_in[f * m_channels + c] : T(0);
        if (m_mode == Mode::Linear) {
            const double step = static_cast<double>(m_inFrames - 1) / static_cast<double>((std::max)(m_outFrames - 1, std::size_t(1)));
            const double x = f * step;
            const std::size_t i = (std::min)(static_cast<std::size_t>(x), m_inFrames - 1);
            const std::size_t j = (std::min)(i + 1, m_inFrames - 1);
            const T frac = static_cast<T>(x - static_cast<double>(i));
            return m_in[i * m_channels + c] * (T(1) - frac) + m_in[j * m_channels + c] * frac;
        }
        const T n = m_norm[f];
        return n < T(1e-6) ? T(0) : m_acc[f * m_channels + c] / n;
    }

    Mode m_mode = Mode::Copy;
    unsigned m_channels = 0;
    std::size_t m_inFrames = 0, m_outFrames = 0, m_have = 0, m_ready = 0, m_pulled = 0;
    std::size_t m_window = 0, m_hop = 0, m_delta = 0, m_maxPos = 0, m_grain = 0, m_prev = 0;
    unsigned m_decim = 1;
    double m_alpha = 1.0;
    std::vector<T> m_in, m_mono, m_coarse, m_hann, m_acc, m_norm;
};

template <typename T>
std::vector<T> stretchTo(const T* in, std::size_t inFrames, unsigned channels, unsigned rate, std::size_t outFrames) {
    Stretcher<T> s;
    s.start(channels, rate, inFrames, outFrames);
    s.push(in, inFrames);
    std::vector<T> out;
    out.reserve(outFrames * channels);
    s.pull(out);
    out.resize(outFrames * channels, T(0));
    return out;
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
    bool idle() const {
        return m_state == State::Live && m_hold == 0 && bufFrames() == 0 && m_fadeInDone >= m_fadeIn;
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

    void beginNext(std::size_t pieceFrames, std::size_t fadeInFrames, std::vector<T>& out) {
        m_hold = 0;
        const std::size_t tail = bufFrames();
        if (m_state == State::Tail && pieceFrames > 0 && tail > 0) {
            const double ratio = static_cast<double>(pieceFrames) / static_cast<double>(tail);
            const bool plain = std::fabs(ratio - 1.0) < 0.002;
            m_tail = tail;
            m_blend = plain ? 0 : blendFrames();
            m_resume = plain ? tail : pieceFrames;
            m_ratio = plain ? 1.0 : ratio;
            const std::size_t pieceIn = plain ? tail : pieceFrames + static_cast<std::size_t>(std::llround(m_blend * ratio));
            m_stretch.start(m_channels, m_rate, pieceIn, tail + m_blend);
            m_s.clear();
            m_b.clear();
            m_mixed = 0;
            m_state = State::Collect;
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
        m_s.clear();
        m_state = State::Live;
        m_fadeIn = 0;
        m_fadeInDone = 0;
    }

    std::size_t latencyFrames() const {
        if (m_state != State::Collect) return bufFrames();
        const std::size_t consumed = (std::min)(static_cast<std::size_t>(std::llround(m_mixed * m_ratio)), bFrames());
        return (m_tail - (std::min)(m_mixed, m_tail)) + bFrames() - consumed;
    }

private:
    std::size_t bufFrames() const { return m_channels ? (m_buf.size() - m_head) / m_channels : 0; }
    std::size_t bFrames() const { return m_channels ? m_b.size() / m_channels : 0; }
    std::size_t blendFrames() const { return framesFor(kBlendSeconds, m_rate); }

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

    void emitLive(const T* in, std::size_t frames, std::vector<T>& out) {
        const std::size_t start = out.size();
        out.insert(out.end(), in, in + frames * m_channels);
        if (m_fadeInDone >= m_fadeIn) return;
        for (std::size_t f = 0; f < frames && m_fadeInDone < m_fadeIn; ++f, ++m_fadeInDone) {
            const double t = (static_cast<double>(m_fadeInDone) + 0.5) / static_cast<double>(m_fadeIn);
            const T g = static_cast<T>(std::sin(0.5 * kPi * t));
            for (unsigned c = 0; c < m_channels; ++c) out[start + f * m_channels + c] *= g;
        }
    }

    void emitFaded(std::vector<T>& out) {
        const std::size_t n = bufFrames();
        const T* src = m_buf.data() + m_head;
        for (std::size_t f = 0; f < n; ++f) {
            const double t = (static_cast<double>(f) + 0.5) / static_cast<double>(n);
            const T g = static_cast<T>(std::cos(0.5 * kPi * t));
            for (unsigned c = 0; c < m_channels; ++c) out.push_back(src[f * m_channels + c] * g);
        }
        m_buf.clear();
        m_head = 0;
    }

    double curve(std::size_t f) const {
        return 0.5 * kPi * (static_cast<double>(f) + 0.5) / static_cast<double>(m_tail);
    }

    void collect(const T* in, std::size_t frames, std::vector<T>& out) {
        const std::size_t room = m_stretch.inputNeeded();
        m_stretch.push(in, (std::min)(frames, room));
        m_b.insert(m_b.end(), in, in + frames * m_channels);
        m_stretch.pull(m_s);
        const T* a = m_buf.data() + m_head;
        const std::size_t ready = m_s.size() / m_channels;
        for (; m_mixed < m_tail && m_mixed < ready; ++m_mixed) {
            const T ga = static_cast<T>(std::cos(curve(m_mixed)));
            const T gb = static_cast<T>(std::sin(curve(m_mixed)));
            for (unsigned c = 0; c < m_channels; ++c)
                out.push_back(a[m_mixed * m_channels + c] * ga + m_s[m_mixed * m_channels + c] * gb);
        }
        for (; m_mixed >= m_tail && m_mixed < m_tail + m_blend && m_mixed < ready; ++m_mixed) {
            const std::size_t j = m_mixed - m_tail;
            if (m_resume + j >= bFrames()) break;
            const T w = static_cast<T>((static_cast<double>(j) + 0.5) / static_cast<double>(m_blend));
            for (unsigned c = 0; c < m_channels; ++c)
                out.push_back(m_s[m_mixed * m_channels + c] * (T(1) - w) + m_b[(m_resume + j) * m_channels + c] * w);
        }
        if (m_mixed < m_tail + m_blend) return;
        const std::size_t rest = m_resume + m_blend;
        std::vector<T> b;
        b.swap(m_b);
        m_buf.clear();
        m_head = 0;
        m_s.clear();
        m_state = State::Live;
        if (rest < b.size() / m_channels) process(b.data() + rest * m_channels, b.size() / m_channels - rest, out);
    }

    void fallback(std::vector<T>& out) {
        const T* a = m_buf.data() + m_head;
        const std::size_t ready = m_s.size() / m_channels;
        const std::size_t mixed = m_mixed;
        for (std::size_t f = (std::min)(m_mixed, m_tail); f < m_tail; ++f) {
            const T ga = static_cast<T>(std::cos(curve(f)));
            for (unsigned c = 0; c < m_channels; ++c)
                out.push_back(a[f * m_channels + c] * ga + (f < ready ? m_s[f * m_channels + c] * static_cast<T>(std::sin(curve(f))) : T(0)));
        }
        std::vector<T> b;
        b.swap(m_b);
        const std::size_t have = b.size() / m_channels;
        std::size_t from = (std::min)(static_cast<std::size_t>(std::llround((std::min)(ready, (std::max)(mixed, m_tail)) * m_ratio)), have);
        if (ready == 0) from = 0;
        m_buf.clear();
        m_head = 0;
        m_s.clear();
        m_state = State::Live;
        startFadeIn(ready == 0 ? have : 0);
        if (from < have) emitLive(b.data() + from * m_channels, have - from, out);
    }

    unsigned m_rate = 0;
    unsigned m_channels = 0;
    State m_state = State::Live;
    std::size_t m_hold = 0;
    std::vector<T> m_buf;
    std::size_t m_head = 0;
    std::vector<T> m_b;
    std::vector<T> m_s;
    Stretcher<T> m_stretch;
    std::size_t m_tail = 0, m_blend = 0, m_resume = 0, m_mixed = 0;
    double m_ratio = 1.0;
    std::size_t m_fadeIn = 0;
    std::size_t m_fadeInDone = 0;
};

using Mixer = BasicMixer<float>;

}
