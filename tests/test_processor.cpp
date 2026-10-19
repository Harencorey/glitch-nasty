//==============================================================================
// Glitch Nasty - plug-in level tests: the real GlitchNastyProcessor, driven the
// way the UI and a host drive it (editStep / setMod from the "message thread",
// processBlock with a play head, get/setStateInformation).
//
// Covers what the engine tests can't: the UI -> audio hand-off, the plug-in
// wrapper around the core, and session save / load (including old sessions).
//==============================================================================

#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
int gFailures = 0;

#define CHECK(cond, ...)                                                                 \
    do {                                                                                 \
        if (! (cond)) { ++gFailures; std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); } \
    } while (0)

struct PlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (true);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Result { double outDb = 0.0, changedDb = -300.0; int nonFinite = 0; };

// Plays a decaying 196 Hz "pluck" every half second through the processor.
// 'edit' runs before playback, 'midEdit' (optional) at 1.5 s - a knob move while it plays.
Result play (int chans, double sr, int bs, float level,
             const std::function<void (GlitchNastyProcessor&)>& edit,
             const std::function<void (GlitchNastyProcessor&)>& midEdit = {})
{
    GlitchNastyProcessor proc;
    PlayHead ph;
    proc.setPlayHead (&ph);
    proc.setPlayConfigDetails (chans, chans, sr, bs);
    proc.prepareToPlay (sr, bs);
    edit (proc);

    juce::AudioBuffer<float> buf (chans, bs);
    juce::MidiBuffer midi;
    double sumIn = 0, sumOut = 0, sumDiff = 0;
    Result r;
    std::vector<float> in ((size_t) bs);
    long n = 0;
    const int blocks = (int) (4.3 * sr / bs);
    for (int b = 0; b < blocks; ++b)
    {
        if (midEdit && b == (int) (1.5 * sr / bs))
            midEdit (proc);
        ph.ppq = (double) n / (sr * 60.0 / ph.bpm);
        for (int i = 0; i < bs; ++i)
        {
            const double t = (double) (n + i);
            const float x = level * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 196.0 * t / sr)
                            * (float) std::exp (-std::fmod (t, sr * 0.5) / (sr * 0.19));
            for (int ch = 0; ch < chans; ++ch)
                buf.setSample (ch, i, x);
            in[(size_t) i] = x;
        }
        proc.processBlock (buf, midi);
        for (int i = 0; i < bs; ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
                if (! std::isfinite (buf.getSample (ch, i))) ++r.nonFinite;
            const double x = in[(size_t) i], y = buf.getSample (0, i);
            if (b >= (int) (2.0 * sr / bs) && std::isfinite (y))
            {
                sumIn += x * x;
                sumOut += y * y;
                sumDiff += (y - x) * (y - x);
            }
        }
        n += bs;
    }
    r.outDb = 10.0 * std::log10 (sumOut / sumIn + 1e-30);
    r.changedDb = 10.0 * std::log10 (sumDiff / sumIn + 1e-30);
    return r;
}

// Every step on (Repeat 1/32), with the given per-step crush.
void crushAll (GlitchNastyProcessor& p, int lane, float bits, float rate)
{
    for (int i = 0; i < gn::kMaxSteps; ++i)
        p.editStep (p.getEditPattern(), lane, i, [bits, rate] (gn::StepData& s)
        {
            s = gn::makeStep (gn::StutterType::repeat, gn::kDiv32);
            s.bits = bits;
            s.rate = rate;
        });
}

void clearAll (GlitchNastyProcessor& p)
{
    for (int l = 0; l < gn::kNumLanes; ++l)
        p.clearPattern (p.getEditPattern(), l);
}

//------------------------------------------------------------------------------
void testStepCrushLevel()
{
    std::printf ("Per-step Bits / Rate through the plug-in (UI edit path)\n");
    struct Cfg { int ch; double sr; int bs; float level; };
    struct Amt { float bits, rate; };
    int runs = 0;
    double worst = 0.0;
    for (auto c : { Cfg { 2, 48000.0, 512, 0.5f }, Cfg { 1, 44100.0, 256, 0.5f }, Cfg { 2, 96000.0, 1024, 0.0316f }, Cfg { 2, 44100.0, 64, 0.0316f } })
        for (auto a : { Amt { 0.1f, 0.0f }, Amt { 0.5f, 0.0f }, Amt { 0.95f, 0.0f }, Amt { 0.0f, 0.1f }, Amt { 0.0f, 0.5f },
                        Amt { 0.0f, 0.95f }, Amt { 0.6f, 0.6f } })
            for (int lane : { 0, 1 })
            {
                const auto base = play (c.ch, c.sr, c.bs, c.level, [] (GlitchNastyProcessor& p) { clearAll (p); crushAll (p, 0, 0.0f, 0.0f); });
                const auto r = play (c.ch, c.sr, c.bs, c.level, [&] (GlitchNastyProcessor& p) { clearAll (p); crushAll (p, lane, a.bits, a.rate); });
                const auto moved = play (c.ch, c.sr, c.bs, c.level, [&] (GlitchNastyProcessor& p) { clearAll (p); crushAll (p, lane, 0.0f, 0.0f); },
                                         [&] (GlitchNastyProcessor& p) { crushAll (p, lane, a.bits, a.rate); });
                const double drop = std::max (std::abs (r.outDb - base.outDb), std::abs (moved.outDb - base.outDb));
                worst = std::max (worst, drop);
                ++runs;
                CHECK (r.nonFinite == 0 && moved.nonFinite == 0, "non-finite output");
                CHECK (drop < 3.0, "%d ch %.0f Hz block %d level %.3f lane %d bits %.2f rate %.2f: level %+.1f dB vs uncrushed %+.1f dB",
                       c.ch, c.sr, c.bs, (double) c.level, lane + 1, (double) a.bits, (double) a.rate, r.outDb, base.outDb);
                CHECK (r.changedDb > -60.0, "crush had no effect (bits %.2f rate %.2f)", (double) a.bits, (double) a.rate);
            }
    std::printf ("  %d runs (mono/stereo, 44.1-96 kHz, 64-1024 blocks, loud and -30 dBFS, both lanes, set before and while playing):\n"
                 "  biggest level change from crushing %.1f dB\n", runs, worst);
}

//------------------------------------------------------------------------------
gn::PatternBank bankOf (GlitchNastyProcessor& p)
{
    gn::PatternBank b;
    for (int i = 0; i < gn::kNumPatterns; ++i)
        b.patterns[(size_t) i] = p.getPattern (i);
    return b;
}

bool sameStep (const gn::StepData& a, const gn::StepData& b)
{
    auto near = [] (float x, float y) { return std::abs (x - y) < 1.0e-4f; };
    return a.on == b.on && a.type == b.type && a.division == b.division && a.length == b.length && near (a.chance, b.chance)
           && near (a.levelDb, b.levelDb) && near (a.pan, b.pan) && near (a.filter, b.filter) && near (a.p1, b.p1) && near (a.p2, b.p2)
           && near (a.bits, b.bits) && near (a.rate, b.rate) && near (a.send, b.send);
}

void testState()
{
    std::printf ("Session save / load\n");

    GlitchNastyProcessor a;
    a.editStep (2, 1, 5, [] (gn::StepData& s) { s = gn::makeStep (gn::StutterType::roll, gn::kDiv64, 0.5f, 3); s.bits = 0.4f; });
    a.setMod (2, 0, (int) gn::ModTarget::filter, 7, 0.25f);
    a.setMod (2, 1, (int) gn::ModTarget::ctl1, 6, 1.0f);
    a.setMod (2, 1, (int) gn::ModTarget::ctl1, 6 + 1, 0.0f);
    a.setActiveLane (1);
    a.setBarsTarget ((int) gn::ModTarget::level);

    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    GlitchNastyProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());

    const auto ba = bankOf (a), bb = bankOf (b);
    bool same = true;
    for (int p = 0; p < gn::kNumPatterns; ++p)
        for (int l = 0; l < gn::kNumLanes; ++l)
            for (int i = 0; i < gn::kMaxSteps; ++i)
            {
                same &= sameStep (ba.patterns[(size_t) p].lanes[(size_t) l].steps[(size_t) i], bb.patterns[(size_t) p].lanes[(size_t) l].steps[(size_t) i]);
                for (int t = 0; t < gn::kNumModTargets; ++t)
                {
                    const float x = ba.patterns[(size_t) p].lanes[(size_t) l].mod[(size_t) t][(size_t) i];
                    const float y = bb.patterns[(size_t) p].lanes[(size_t) l].mod[(size_t) t][(size_t) i];
                    same &= gn::isSet (x) == gn::isSet (y) && (! gn::isSet (x) || std::abs (x - y) < 1.0e-4f);
                }
            }
    CHECK (same, "patterns / lanes / locks didn't survive save + load");
    CHECK (b.getActiveLane() == 1 && b.getBarsTarget() == (int) gn::ModTarget::level, "editor state didn't survive");
    CHECK (gn::isSet (bb.patterns[7].lanes[0].mod[(size_t) gn::ModTarget::ctl1][2]), "factory pattern H lost its pitch locks");
    std::printf ("  Both lanes, every lock (incl. the factory pitch melody on H) and the editor state round-trip\n");

    // A 0.4 session: steps straight under P, a "layer" (an on step inside a tie) on step 3.
    {
        juce::ValueTree root ("GlitchNasty");
        juce::ValueTree pats ("PATTERNS");
        juce::ValueTree pt ("P");
        pt.setProperty ("i", 0, nullptr);
        auto step = [] (int i, int type, int len)
        {
            juce::ValueTree s ("S");
            s.setProperty ("i", i, nullptr);
            s.setProperty ("on", true, nullptr);
            s.setProperty ("type", type, nullptr);
            s.setProperty ("len", len, nullptr);
            s.setProperty ("div", gn::kDiv16, nullptr);
            return s;
        };
        pt.appendChild (step (0, (int) gn::StutterType::pitch, 4), nullptr);
        pt.appendChild (step (2, (int) gn::StutterType::roll, 1), nullptr);
        pt.appendChild (step (8, (int) gn::StutterType::reverse, 1), nullptr);
        pats.appendChild (pt, nullptr);
        root.appendChild (pats, nullptr);

        juce::MemoryBlock old;
        if (auto xml = root.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, old);

        GlitchNastyProcessor c;
        c.setStateInformation (old.getData(), (int) old.getSize());
        const auto pat = c.getPattern (0);
        const bool ok = pat.lanes[0].steps[0].on && pat.lanes[0].steps[0].length == 4
                        && ! pat.lanes[0].steps[2].on                                         // the layer left lane 1 ...
                        && pat.lanes[1].steps[2].on && pat.lanes[1].steps[2].type == (int) gn::StutterType::roll // ... for lane 2
                        && pat.lanes[1].steps[2].length == 1
                        && pat.lanes[0].steps[8].on && ! pat.lanes[1].steps[8].on;          // ordinary steps stay put
        CHECK (ok, "0.4 layer didn't migrate to lane 2");
        std::printf ("  0.4 sessions: layers inside ties move to lane 2, everything else stays on lane 1\n");
    }
}

//------------------------------------------------------------------------------
void testLocksThroughPlugin()
{
    std::printf ("Locks through the plug-in\n");
    // One bar-long Repeat with a Level lock all the way down on every step = silence while it plays.
    const auto muted = play (2, 48000.0, 512, 0.5f, [] (GlitchNastyProcessor& p)
    {
        clearAll (p);
        p.editStep (p.getEditPattern(), 0, 0, [] (gn::StepData& s) { s = gn::makeStep (gn::StutterType::repeat, gn::kDiv16, 1.0f, 16); });
        for (int i = 0; i < gn::kMaxSteps; ++i)
            p.setMod (p.getEditPattern(), 0, (int) gn::ModTarget::level, i, 0.0f);
    });
    CHECK (muted.outDb < -60.0, "level locks didn't reach the audio (%.1f dB)", muted.outDb);
    std::printf ("  Level locks drawn in the UI mute a bar-long glitch (%.0f dB)\n", muted.outDb);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testStepCrushLevel();
    testState();
    testLocksThroughPlugin();
    std::printf (gFailures == 0 ? "\nALL PROCESSOR TESTS PASSED\n" : "\n%d FAILURE(S)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
