#pragma once
//==============================================================================
// Glitch Nasty - effects (JUCE-free, real-time safe: all memory in prepare()).
//
//   Crusher     sample-and-hold rate reduction + bit-depth quantising, like an
//               early sampler with no reconstruction filter (the aliasing IS the sound)
//   TempoDelay  tempo-synced stereo delay, tone filter + soft clip in the feedback
//               loop (like a tape echo's repeats getting darker), optional ping-pong
//   Reverb      8-line feedback delay network, the same idea as a digital plate:
//               delay lines feeding back through a mixing matrix, damped each pass
//==============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gn
{

//==============================================================================
class Crusher
{
public:
    void prepare (double sampleRate) noexcept
    {
        envRelease = (float) std::exp (-1.0 / (0.25 * sampleRate)); // 250 ms release
        reset();
    }

    void reset() noexcept
    {
        held = {};
        counter = 1.0e9; // grab a new sample straight away
        env = 0.0f;
    }

    // One stereo frame in place. factor = sample-rate divider (1 = off),
    // bits = bit depth (>= 23.5 = off). Bits are continuous so a sweep is smooth.
    //
    // The quantiser follows the signal's level (a peak envelope, instant attack,
    // 250 ms release): "4 bits" means 4 bits of the sound that's actually there,
    // so a quiet guitar crunches like a loud drum bus instead of rounding to
    // silence - like riding the input gain of an old sampler to hit it hard.
    void processFrame (float* frame, int numCh, double factor, double bits) noexcept
    {
        if (factor > 1.0001)
        {
            counter += 1.0;
            if (counter >= factor)
            {
                counter -= factor;
                if (counter >= factor) counter = 0.0; // factor dropped a lot: resync
                for (int c = 0; c < numCh; ++c) held[(size_t) c] = frame[c];
            }
            for (int c = 0; c < numCh; ++c) frame[c] = held[(size_t) c];
        }
        else
        {
            counter = 1.0e9;
        }

        float peak = 0.0f;
        for (int c = 0; c < numCh; ++c)
            peak = std::max (peak, std::abs (frame[c]));
        env = std::max (peak, env * envRelease);

        if (bits < 23.5 && env > 1.0e-5f) // true silence stays silent
        {
            const float q = (float) std::exp2 (std::max (1.0, bits) - 1.0) / env; // levels per unit of the current level
            for (int c = 0; c < numCh; ++c)
                frame[c] = std::round (frame[c] * q) / q;
        }
    }

private:
    std::array<float, 2> held {};
    double counter = 1.0e9;
    float env = 0.0f, envRelease = 0.9999f;
};

//==============================================================================
// One-pole filter used for "tone" knobs: -1..0 = low-pass (dark), 0..+1 = high-pass (thin).
struct ToneFilter
{
    float z = 0.0f;

    static float coefFor (double hz, double sr) noexcept
    {
        return (float) (1.0 - std::exp (-6.283185307179586 * std::min (hz, 0.45 * sr) / sr));
    }

    float process (float x, float lpCoef, float hpCoef, float tone) noexcept
    {
        if (tone < -0.001f) { z += lpCoef * (x - z); return z; }
        if (tone > 0.001f)  { z += hpCoef * (x - z); return x - z; }
        z = x;
        return x;
    }
};

//==============================================================================
class TempoDelay
{
public:
    void prepare (double sampleRate, double maxSeconds)
    {
        sr = sampleRate;
        int64_t cap = 1;
        while (cap < (int64_t) (maxSeconds * sampleRate) + 4) cap <<= 1;
        mask = cap - 1;
        for (auto& b : buf) b.assign ((size_t) cap, 0.0f);
        maxDelay = (double) (cap - 4);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& t : tone) t = {};
        w = 0;
        delay = -1.0;
    }

    // in/out: per-channel pointers (out = the delay's own return, 100 % wet).
    void process (const float* const* in, float* const* out, int numCh, int start, int num,
                  double delaySamples, float feedback, float toneAmt, bool pingPong) noexcept
    {
        delaySamples = std::clamp (delaySamples, 1.0, maxDelay);
        if (delay < 0.0) delay = delaySamples;                        // first use: no glide
        const double glide = 1.0 - std::exp (-1.0 / (0.08 * sr));     // tempo changes glide (tape-style)
        feedback = std::clamp (feedback, 0.0f, 0.95f);
        const float lpC = ToneFilter::coefFor (20000.0 * std::pow (0.04, (double) std::max (0.0f, -toneAmt)), sr); // 20k -> 800 Hz
        const float hpC = ToneFilter::coefFor (20.0 * std::pow (75.0, (double) std::max (0.0f, toneAmt)), sr);      // 20 -> 1.5k

        for (int i = start; i < start + num; ++i)
        {
            delay += glide * (delaySamples - delay);
            const double rp = (double) w - delay;
            float y[2];
            for (int c = 0; c < 2; ++c)
                y[c] = read (c, rp);

            float fb[2];
            for (int c = 0; c < 2; ++c)
            {
                const float f = tone[(size_t) c].process (y[c], lpC, hpC, toneAmt) * feedback;
                fb[c] = f / (1.0f + std::abs (f) * 0.25f); // gentle soft clip: high feedback stays tame
            }

            const float inL = in[0][i];
            const float inR = numCh > 1 ? in[1][i] : inL;
            if (pingPong)
            {
                buf[0][(size_t) (w & mask)] = 0.5f * (inL + inR) + fb[1];
                buf[1][(size_t) (w & mask)] = fb[0];
            }
            else
            {
                buf[0][(size_t) (w & mask)] = inL + fb[0];
                buf[1][(size_t) (w & mask)] = inR + fb[1];
            }
            ++w;

            out[0][i] = y[0];
            if (numCh > 1) out[1][i] = y[1];
            else out[0][i] = 0.5f * (y[0] + y[1]);
        }
    }

private:
    float read (int c, double pos) const noexcept
    {
        const auto i0 = (int64_t) std::floor (pos);
        const auto f = (float) (pos - (double) i0);
        const auto& b = buf[(size_t) c];
        const float a = b[(size_t) (i0 & mask)], n = b[(size_t) ((i0 + 1) & mask)];
        return a + f * (n - a);
    }

    std::array<std::vector<float>, 2> buf;
    std::array<ToneFilter, 2> tone {};
    int64_t mask = 0, w = 0;
    double sr = 48000.0, delay = -1.0, maxDelay = 1.0;
};

//==============================================================================
class Reverb
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int l = 0; l < kLines; ++l)
        {
            int64_t cap = 1;
            while (cap < (int64_t) (kBaseMs[l] * 0.001 * kMaxScale * sr) + 8) cap <<= 1;
            lines[(size_t) l].assign ((size_t) cap, 0.0f);
            masks[(size_t) l] = cap - 1;
        }
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        damp = {};
        w = 0;
        scale = -1.0;
        cacheValid = false;
    }

    // size 0..1 (room -> huge hall), dampAmt 0..1 (bright -> dark). out = 100 % wet.
    void process (const float* const* in, float* const* out, int numCh, int start, int num, float size, float dampAmt) noexcept
    {
        size = std::clamp (size, 0.0f, 1.0f);
        const double targetScale = 0.55 + (kMaxScale - 0.55) * (double) size;
        if (scale < 0.0) scale = targetScale;
        const double glide = 1.0 - std::exp (-1.0 / (0.3 * sr)); // slow, so moving Size doesn't warble much
        const double rt60 = 0.35 + 9.0 * std::pow ((double) size, 1.6);
        const float dampCoef = ToneFilter::coefFor (18000.0 * std::pow (0.08, (double) std::clamp (dampAmt, 0.0f, 1.0f)), sr);

        for (int i = start; i < start + num; ++i)
        {
            // Line lengths and decay gains only change while Size is moving.
            if (std::abs (scale - targetScale) > 1.0e-7 || std::abs (rt60 - cachedRt60) > 1.0e-9 || ! cacheValid)
            {
                scale += glide * (targetScale - scale);
                if (std::abs (scale - targetScale) <= 1.0e-7) scale = targetScale;
                for (int l = 0; l < kLines; ++l)
                {
                    lenCache[(size_t) l] = kBaseMs[l] * 0.001 * scale * sr;
                    gainCache[(size_t) l] = (float) std::pow (10.0, -3.0 * lenCache[(size_t) l] / (rt60 * sr)); // this line's share of the RT60
                }
                cachedRt60 = rt60;
                cacheValid = true;
            }

            const float inL = in[0][i];
            const float inR = numCh > 1 ? in[1][i] : inL;

            std::array<float, kLines> x {};
            float sum = 0.0f;
            for (int l = 0; l < kLines; ++l)
            {
                const float v = readLine (l, (double) w - lenCache[(size_t) l]) * gainCache[(size_t) l];
                damp[(size_t) l] += dampCoef * (v - damp[(size_t) l]);            // highs die faster, like air/absorption
                x[(size_t) l] = damp[(size_t) l];
                sum += x[(size_t) l];
            }

            // Householder mixing: every line feeds every other one, energy-preserving.
            const float mixTerm = sum * (2.0f / (float) kLines);
            float outL = 0.0f, outR = 0.0f;
            for (int l = 0; l < kLines; ++l)
            {
                const float y = x[(size_t) l] - mixTerm;
                const float input = (l % 2 == 0 ? inL : inR) * 0.35f;
                lines[(size_t) l][(size_t) (w & masks[(size_t) l])] = y + input;
                (l % 2 == 0 ? outL : outR) += x[(size_t) l];
            }
            ++w;

            out[0][i] = outL * 0.5f;
            if (numCh > 1) out[1][i] = outR * 0.5f;
            else out[0][i] = 0.25f * (outL + outR);
        }
    }

private:
    static constexpr double kMaxScale = 1.7;
    static constexpr double kBaseMs[kLines] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.1, 73.1 };

    float readLine (int l, double pos) const noexcept
    {
        const auto i0 = (int64_t) std::floor (pos);
        const auto f = (float) (pos - (double) i0);
        const auto& b = lines[(size_t) l];
        const auto m = masks[(size_t) l];
        const float a = b[(size_t) (i0 & m)], n = b[(size_t) ((i0 + 1) & m)];
        return a + f * (n - a);
    }

    std::array<std::vector<float>, kLines> lines;
    std::array<int64_t, kLines> masks {};
    std::array<float, kLines> damp {};
    std::array<double, kLines> lenCache {};
    std::array<float, kLines> gainCache {};
    bool cacheValid = false;
    double cachedRt60 = 0.0;
    int64_t w = 0;
    double sr = 48000.0, scale = -1.0;
};

} // namespace gn
