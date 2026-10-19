#pragma once
//==============================================================================
// Glitch Nasty - GlitchCore
//
// The whole audio path in one JUCE-free class:
//   clock -> grid (with swing) -> pattern (chance, density, seed, ties,
//            per-step parameter locks) -> lane 1 engine -> lane 2 engine
// Lane 2 is a second, independent sequence whose glitches process lane 1's
// output (so a Roll on lane 2 rolls whatever lane 1 is doing); up to four
// lanes run in series like that.
// plus live performance: MIDI notes / UI pads trigger glitches (optionally
// quantised), pads can be latched on and several latched pads stack (each
// gets one of the lane engines, in series), MIDI notes switch patterns, and a
// footswitch (sustain pedal) engages/disengages the pattern on the next bar.
//
// Everything that happens inside a block (grid lines, MIDI, bar lines, live
// quantise points) is processed in time order at its exact sample. The
// plug-in wrapper and the offline tests both run exactly this code.
//==============================================================================

#include "Effects.h"
#include "GridScheduler.h"
#include "Pattern.h"
#include "StutterEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gn
{

// Live quantise: when a held note / pad actually starts.
constexpr int kNumQuants = 5;
constexpr const char* kQuantNames[kNumQuants] = { "Off", "1/16", "1/8", "1/4", "1 Bar" };
constexpr double kQuantPpq[kNumQuants] = { 0.0, 0.25, 0.5, 1.0, -1.0 /* one bar */ };

// Delay times (tempo-synced).
constexpr int kNumDelayTimes = 11;
constexpr const char* kDelayTimeNames[kNumDelayTimes] = { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2" };
constexpr double kDelayTimePpq[kNumDelayTimes] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };
constexpr int kDefaultDelayTime = 6; // 1/8D

// MIDI map
constexpr int kLiveNoteBase = 60;    // C3 (middle C) .. A#3: hold to glitch with type 0..10
constexpr int kPatternNoteBase = 48; // C2 .. G2: switch to pattern A..H
constexpr int kFootswitchCC = 64;    // sustain pedal: toggle Engage on the next bar

struct CoreParams
{
    int syncMode = 0;          // 0 = Host (falls back to Internal when there's no host clock), 1 = Internal
    double internalBpm = 120.0;
    int gridIndex = kDefaultGrid;
    int numSteps = kMaxSteps;
    float crossfadeMs = 3.0f;
    float mix = 1.0f;          // 0..1
    bool snapToBeat = false;   // one-shot from TAP: snap the internal clock to the nearest beat
    float swing = 0.5f;        // 0.5 = straight .. 0.75 = hard swing
    float density = 0.0f;      // -1 (nothing fires) .. 0 (as drawn) .. +1 (every on-step fires)
    int seed = 1;              // same seed = same "random" decisions at the same song position
    int pattern = 0;           // 0..7 = A..H
    bool engage = true;        // pattern on/off (live notes/pads always work)
    int liveDivision = kDiv16; // repeat length for live notes/pads
    int liveQuant = 1;         // index into kQuantPpq

    // Global crusher (on everything): amounts 0..1 like the per-step ones.
    float crushBits = 0.0f, crushRate = 0.0f, crushMix = 0.0f;

    // Delay / reverb. Source 0 = glitches only (each step's Send), 1 = everything.
    int fxSource = 0;
    int delayTime = kDefaultDelayTime;
    float delayFeedback = 0.45f, delayTone = -0.2f, delayMix = 0.0f;
    bool delayPingPong = true;
    float reverbSize = 0.55f, reverbDamp = 0.4f, reverbMix = 0.0f;
};

struct HostTime
{
    bool hasPpq = false, hasBpm = false, playing = false;
    double ppq = 0.0, bpm = 120.0;
    int timeSigNum = 4, timeSigDen = 4;
};

struct MidiEvent
{
    int offset;              // sample inside the block
    uint8_t status, data1, data2;
};

class GlitchCore
{
public:
    static constexpr double kBufferSeconds = 10.0; // how far back the engine can reach
    static constexpr int kMaxHeld = 16;

    static constexpr int kMaxChunk = 1024; // longer host blocks are processed in pieces

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = std::clamp (numChannels, 1, 2);
        for (auto& l : lanes)
            l.engine.prepare (numChannels, sampleRate, kBufferSeconds);
        delay.prepare (sampleRate, 6.5);
        reverb.prepare (sampleRate);
        globalCrusher.prepare (sampleRate);
        for (auto* v : { &sendBuf, &delayOut, &reverbOut })
            for (auto& ch : *v)
                ch.assign ((size_t) kMaxChunk, 0.0f);
        for (auto& l : lanes)
        {
            for (auto& ch : l.sendBuf)
                ch.assign ((size_t) kMaxChunk, 0.0f);
            l.actBuf.assign ((size_t) kMaxChunk, 0.0f);
        }
        reset();
    }

    void reset()
    {
        for (auto& l : lanes)
        {
            l.engine.reset();
            l.clear();
        }
        stepSched.reset();
        barSched.reset();
        quantSched.reset();
        internalPpq = 0.0;
        playingStep = -1;
        heldCount = 0;
        liveSlots.fill (-1);
        liveActive = livePending = false;
        lastPad = -1;
        lastLatch = 0;
        liveChanged = false;
        patternOverride = -1;
        lastParamPattern = -1;
        footFlip = footDown = false;
        engaged = engageTarget = true;
        engageInit = false;
        liveCounter = 0;
        delay.reset();
        reverb.reset();
        globalCrusher.reset();
        delayActive = reverbActive = false;
        delayMixNow = reverbMixNow = crushMixNow = 0.0f;
    }

    void process (float* const* io, int numCh, int numSamples, const CoreParams& p, const HostTime& host,
                  const PatternBank& bank, const MidiEvent* midi = nullptr, int numMidi = 0, int livePad = -1,
                  uint32_t latchMask = 0) noexcept
    {
        // Process long host blocks in pieces (the output doesn't depend on how a block is split).
        const bool hostTempo = (p.syncMode == 0 && host.hasPpq && host.hasBpm);
        const double bpm = std::clamp (hostTempo ? host.bpm : p.internalBpm, 20.0, 999.0);
        const double samplesPerQ = sr * 60.0 / bpm;

        int done = 0, im = 0;
        while (done < numSamples)
        {
            const int n = std::min (kMaxChunk, numSamples - done);
            int nm = 0;
            while (im < numMidi && midi[im].offset < done + n && nm < (int) chunkMidi.size())
            {
                chunkMidi[(size_t) nm] = midi[im++];
                chunkMidi[(size_t) nm].offset = std::max (0, chunkMidi[(size_t) nm].offset - done);
                ++nm;
            }

            HostTime h = host;
            h.ppq = host.ppq + (double) done / samplesPerQ;
            float* chunk[2] = { io[0] + done, numCh > 1 ? io[1] + done : nullptr };
            processChunk (chunk, numCh, n, p, h, bank, chunkMidi.data(), nm, livePad, latchMask);
            done += n;
        }
    }

private:
    void processChunk (float* const* io, int numCh, int numSamples, const CoreParams& p, const HostTime& host,
                       const PatternBank& bank, const MidiEvent* midi, int numMidi, int livePad, uint32_t latchMask) noexcept
    {
        params = &p;
        bankPtr = &bank;
        for (auto& l : lanes)
            l.engine.setMix (p.mix);

        // ---- Clock ---------------------------------------------------------
        hostClock = (p.syncMode == 0 && host.hasPpq && host.hasBpm);
        const double bpm = std::clamp (hostClock ? host.bpm : p.internalBpm, 20.0, 999.0);
        const bool running = hostClock ? host.playing : true;
        spq = sr * 60.0 / bpm;
        effectiveBpm = bpm;

        double ppq = 0.0;
        if (hostClock)
        {
            ppq = host.ppq;
        }
        else
        {
            if (p.snapToBeat)
                internalPpq = std::round (internalPpq);
            ppq = internalPpq;
            internalPpq += (double) numSamples / spq;
        }

        const int tsNum = (hostClock && host.timeSigNum > 0) ? host.timeSigNum : 4;
        const int tsDen = (hostClock && host.timeSigDen > 0) ? host.timeSigDen : 4;
        barPpq = (double) tsNum * 4.0 / (double) tsDen;

        stepPpq = kGridPpq[std::clamp (p.gridIndex, 0, kNumGrids - 1)];
        samplesPerStep = spq * stepPpq;
        swing = std::clamp (((double) p.swing - 0.5) * 2.0, 0.0, 0.5);
        fade = fadeSamples (p.crossfadeMs, samplesPerStep);
        seedBase = hash32 ((uint32_t) std::max (0, p.seed) * 2654435761u + 0x1234567u);

        // ---- Pattern select & engage ---------------------------------------
        if (p.pattern != lastParamPattern)
        {
            lastParamPattern = p.pattern;
            patternOverride = -1; // moving the Pattern control takes back over from MIDI
        }
        engageTarget = p.engage != footFlip;
        if (! engageInit)
        {
            engaged = engageTarget;
            engageInit = true;
        }

        // ---- Live pad from the UI (behaves like one more held note) --------
        if (livePad != lastPad)
        {
            if (lastPad >= 0) releaseHeld (kPadId);
            if (livePad >= 0 && livePad < kNumStutterTypes) pressHeld (kPadId, livePad);
            lastPad = livePad;
            liveChanged = true;
        }
        latchMask &= (1u << kNumStutterTypes) - 1u;
        if (latchMask != lastLatch) // latched pads (Shift-click / Shift+Cmd-click)
        {
            lastLatch = latchMask;
            liveChanged = true;
        }

        // ---- Transport stopped: straight through ---------------------------
        if (! running)
        {
            for (int m = 0; m < numMidi; ++m)
                handleMidi (midi[m], false);
            engaged = engageTarget;
            stopAllLanes();
            clearLive();
            stepSched.reset();
            barSched.reset();
            quantSched.reset();
            playingStep = -1;
            liveChanged = true; // re-check held notes when playback starts
            runEngines (io, numCh, 0, numSamples);
            applyFx (io, numCh, numSamples); // tails keep ringing while stopped
            return;
        }

        // ---- Find every grid line, bar line and quantise point -------------
        bool resynced = false, barResynced = false, quantResynced = false;
        const int ns = stepSched.process (ppq, spq, stepPpq, swing, numSamples, stepEvents.data(), resynced);
        const int nb = barSched.process (ppq, spq, barPpq, 0.0, numSamples, barEvents.data(), barResynced);

        int nq = 0;
        const int quant = std::clamp (p.liveQuant, 0, kNumQuants - 1);
        if (quant > 0)
            nq = quantSched.process (ppq, spq, kQuantPpq[quant] < 0.0 ? barPpq : kQuantPpq[quant], 0.0,
                                     numSamples, quantEvents.data(), quantResynced);
        else
            quantSched.reset();

        if (resynced)
        {
            stopAllLanes();
            clearLive();
            engaged = engageTarget; // starting / locating applies Engage right away
            playingStep = stepIndex (stepSched.currentStep(), p.numSteps);
            liveChanged = true;
        }

        if (liveChanged)
        {
            liveChanged = false;
            updateLive (0.0);
        }

        // Knob moves on whatever is glitching right now take effect immediately.
        if (liveActive)
        {
            for (int i = 0; i < kMaxLanes; ++i)
                if (liveSlots[(size_t) i] >= 0)
                    lanes[(size_t) i].engine.updateControls (controlsOf (bank.live[(size_t) liveSlots[(size_t) i]]));
        }
        else
        {
            for (int L = 0; L < kMaxLanes; ++L)
                refreshLaneControls (L);
        }

        // ---- Walk the block in time order ----------------------------------
        // At the same sample: MIDI first, then bar lines, then steps, then
        // quantise points (so a live note always wins over the pattern).
        int im = 0, ib = 0, is = 0, iq = 0, cursor = 0;
        for (;;)
        {
            int next = numSamples, which = -1;
            auto consider = [&] (int offset, int kind)
            {
                offset = std::max (offset, cursor);
                if (offset < next) { next = offset; which = kind; }
            };
            if (im < numMidi) consider (std::min (midi[im].offset, numSamples - 1), 0);
            if (ib < nb)      consider (barEvents[(size_t) ib].offset, 1);
            if (is < ns)      consider (stepEvents[(size_t) is].offset, 2);
            if (iq < nq)      consider (quantEvents[(size_t) iq].offset, 3);
            if (which < 0)
                break;

            runEngines (io, numCh, cursor, next - cursor);
            cursor = next;

            switch (which)
            {
                case 0: handleMidi (midi[im++], true); break;
                case 1: onBar(); ++ib; break;
                case 2: onStep (stepEvents[(size_t) is++]); break;
                default: onQuant (quantEvents[(size_t) iq++]); break;
            }
        }
        runEngines (io, numCh, cursor, numSamples - cursor);
        applyFx (io, numCh, numSamples);
    }

public:
    // For the UI.
    int getPlayingStep() const noexcept             { return playingStep; }
    int getFiringStep (int lane = 0) const noexcept   { return lanes[(size_t) std::clamp (lane, 0, kMaxLanes - 1)].firingStep; }
    int getFiringLength (int lane = 0) const noexcept { return lanes[(size_t) std::clamp (lane, 0, kMaxLanes - 1)].firingLen; }
    // Bit t set = live type t is playing right now (one per stacked slot).
    uint32_t getLiveMask() const noexcept
    {
        uint32_t m = 0;
        for (int t : liveSlots)
            if (t >= 0) m |= 1u << t;
        return m;
    }
    bool isLiveActive() const noexcept              { return liveActive; }
    int getEffectivePattern() const noexcept  { return currentPatternIndex(); }
    bool isEngaged() const noexcept           { return engaged; }
    bool isEngagePending() const noexcept     { return engaged != engageTarget; }
    double getEffectiveBpm() const noexcept   { return effectiveBpm; }
    bool isUsingHostClock() const noexcept    { return hostClock; }

private:
    static constexpr int kPadId = 1000;

    struct Held { int id, type; };

    static int stepIndex (int64_t absStep, int numSteps) noexcept
    {
        const int64_t n = std::clamp (numSteps, 1, kMaxSteps);
        return (int) (((absStep % n) + n) % n);
    }

    int fadeSamples (float ms, double stepSamples) const noexcept
    {
        const double f = std::min ((double) ms * 0.001 * sr, stepSamples * 0.5);
        return std::max (0, (int) std::lround (f));
    }

    int currentPatternIndex() const noexcept
    {
        if (patternOverride >= 0)
            return patternOverride;
        return params != nullptr ? std::clamp (params->pattern, 0, kNumPatterns - 1) : 0;
    }

    static float applyDensity (float chance, float density) noexcept
    {
        density = std::clamp (density, -1.0f, 1.0f);
        return density < 0.0f ? chance * (1.0f + density) : chance + (1.0f - chance) * density;
    }

    //--------------------------------------------------------------------------
    void onStep (const GridEvent& ev) noexcept
    {
        const int numSteps = std::clamp (params->numSteps, 1, kMaxSteps);
        const int idx = stepIndex (ev.step, numSteps);
        playingStep = idx;
        for (int L = 0; L < kMaxLanes; ++L)
            onLaneStep (L, ev, idx, numSteps);
    }

    // One lane's step: either a step inside a running tie (its parameter locks
    // land now, at the exact sample), or a new step that may fire.
    void onLaneStep (int L, const GridEvent& ev, int idx, int numSteps) noexcept
    {
        auto& ln = lanes[(size_t) L];

        if (liveActive) // live pads/notes own the engines; the pattern sits out
        {
            ln.tieRemaining = 0;
            return;
        }

        if (L >= std::clamp (bankPtr->patterns[(size_t) currentPatternIndex()].numLanes, 1, kMaxLanes))
        {
            stopLane (L); // this pattern doesn't use the lane (any more)
            ln.tieRemaining = 0;
            return;
        }

        if (ln.tieRemaining > 0) // still inside a tied glitch
        {
            --ln.tieRemaining;
            ln.curStep = idx;
            refreshLaneControls (L);
            return;
        }

        if (! engaged)
        {
            stopLane (L);
            return;
        }

        const int pat = currentPatternIndex();
        const auto& lane = bankPtr->patterns[(size_t) pat].lanes[(size_t) L];
        const StepData& s = lane.steps[(size_t) idx];
        if (! s.on)
        {
            stopLane (L);
            return;
        }

        const int len = std::clamp (s.length, 1, numSteps - idx);
        ln.tieRemaining = len - 1; // the following steps belong to this one, fire or not

        const float chance = applyDensity (s.chance, params->density);
        const uint32_t laneSalt = L == 0 ? 0u : 0x51ED27u;
        const float dice = rand01 (seedBase ^ laneSalt, (uint32_t) ev.step, (uint32_t) ((uint64_t) ev.step >> 32));
        if (! (chance >= 0.999f || dice < chance))
        {
            stopLane (L);
            return;
        }

        const StepData e = effectiveStep (lane, idx, idx);
        StutterSettings st;
        st.type = (StutterType) std::clamp (e.type, 0, kNumStutterTypes - 1);
        st.sliceLen = kDivisionPpq[std::clamp (e.division, 0, kNumDivisions - 1)] * spq;
        st.stepLen = (GridScheduler::stepStart (ev.step + len, swing) - GridScheduler::stepStart (ev.step, swing)) * samplesPerStep;
        st.lateBy = ev.lateBy;
        st.minPeriod = spq / 32.0; // Roll bottoms out at 1/128 notes
        st.fadeSamples = fade;
        st.p1 = e.p1;
        st.p2 = e.p2;
        st.level = levelGain (e.levelDb);
        st.pan = e.pan;
        st.filter = e.filter;
        st.bits = e.bits;
        st.rate = e.rate;
        st.send = e.send;
        st.seed = hash32 (seedBase ^ hash32 ((uint32_t) ev.step + (L == 0 ? 0x68e31da4u : 0x2545F491u)));
        ln.engine.startStutter (st);

        ln.firingStep = idx;
        ln.curStep = idx;
        ln.firingLen = len;
        ln.firingType = s.type;
        ln.firingPattern = pat;
    }

    // The owner step's settings with the locks drawn on step 'at' applied on top.
    static StepData effectiveStep (const Lane& lane, int owner, int at) noexcept
    {
        StepData e = lane.steps[(size_t) owner];
        for (int t = 0; t < kNumModTargets; ++t)
        {
            const float v = lane.mod[(size_t) t][(size_t) at];
            if (isSet (v))
                applyMod (e, (ModTarget) t, v);
        }
        return e;
    }

    // Push the playing step's current settings (knob edits + locks) into the lane's engine.
    void refreshLaneControls (int L) noexcept
    {
        auto& ln = lanes[(size_t) L];
        if (ln.firingStep < 0 || ln.curStep < 0)
            return;
        const auto& lane = bankPtr->patterns[(size_t) ln.firingPattern].lanes[(size_t) L];
        if (lane.steps[(size_t) ln.firingStep].type != ln.firingType)
            return; // the type was changed while it plays: the new one takes over next time
        ln.engine.updateControls (controlsOf (effectiveStep (lane, ln.firingStep, ln.curStep)));
    }

    void stopLane (int L) noexcept
    {
        auto& ln = lanes[(size_t) L];
        ln.engine.stopStutter (fade);
        ln.firingStep = -1;
        ln.curStep = -1;
    }

    void stopAllLanes() noexcept
    {
        for (int L = 0; L < kMaxLanes; ++L)
        {
            stopLane (L);
            lanes[(size_t) L].tieRemaining = 0;
        }
    }

    // Every lane engine runs (idle ones just keep recording, so a lane that's
    // added later already has its history), each on the previous one's output.
    void runEngines (float* const* io, int numCh, int start, int num) noexcept
    {
        lanes[0].engine.process (io, numCh, start, num, sendPtrs());
        for (int L = 1; L < kMaxLanes; ++L)
        {
            auto& l = lanes[(size_t) L];
            float* ls[2] = { l.sendBuf[0].data(), l.sendBuf[1].data() };
            l.engine.process (io, numCh, start, num, ls, l.actBuf.data());
        }
    }

    void onBar() noexcept
    {
        if (engaged == engageTarget)
            return;

        engaged = engageTarget;
        if (! engaged && ! liveActive)
            stopAllLanes();
    }

    void onQuant (const GridEvent& ev) noexcept
    {
        if (livePending)
        {
            livePending = false;
            applyLive (ev.lateBy);
        }
    }

    //--------------------------------------------------------------------------
    void handleMidi (const MidiEvent& ev, bool running) noexcept
    {
        const int status = ev.status & 0xF0;
        const int d1 = ev.data1 & 0x7F, d2 = ev.data2 & 0x7F;
        const bool isLiveNote = d1 >= kLiveNoteBase && d1 < kLiveNoteBase + kNumStutterTypes;

        if (status == 0x90 && d2 > 0)
        {
            if (d1 >= kPatternNoteBase && d1 < kPatternNoteBase + kNumPatterns)
                patternOverride = d1 - kPatternNoteBase;
            else if (isLiveNote)
            {
                pressHeld (d1, d1 - kLiveNoteBase);
                if (running) updateLive (0.0);
            }
        }
        else if (status == 0x80 || (status == 0x90 && d2 == 0))
        {
            if (isLiveNote)
            {
                releaseHeld (d1);
                if (running) updateLive (0.0);
            }
        }
        else if (status == 0xB0)
        {
            if (d1 == kFootswitchCC)
            {
                const bool down = d2 >= 64;
                if (down && ! footDown)
                {
                    footFlip = ! footFlip;
                    engageTarget = params->engage != footFlip;
                    if (! running)
                        engaged = engageTarget;
                }
                footDown = down;
            }
            else if (d1 == 120 || d1 == 123) // all sound / all notes off
            {
                int keep = 0;
                for (int i = 0; i < heldCount; ++i)
                    if (held[(size_t) i].id == kPadId)
                        held[(size_t) keep++] = held[(size_t) i];
                heldCount = keep;
                if (running) updateLive (0.0);
            }
        }
    }

    void pressHeld (int id, int type) noexcept
    {
        releaseHeld (id);
        if (heldCount < kMaxHeld)
            held[(size_t) heldCount++] = { id, type };
    }

    void releaseHeld (int id) noexcept
    {
        int keep = 0;
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].id != id)
                held[(size_t) keep++] = held[(size_t) i];
        heldCount = keep;
    }

    // What should be playing live: every latched pad, plus the most recently
    // pressed held note/pad (releasing it falls back to the one held before).
    uint32_t desiredLive() const noexcept
    {
        uint32_t m = lastLatch;
        if (heldCount > 0)
            m |= 1u << held[(size_t) heldCount - 1].type;
        return m;
    }

    void updateLive (double lateBy) noexcept
    {
        if (desiredLive() == getLiveMask() && liveActive == (desiredLive() != 0))
        {
            livePending = false;
            return;
        }
        if (desiredLive() == 0 || std::clamp (params->liveQuant, 0, kNumQuants - 1) == 0)
            applyLive (lateBy); // letting go is never quantised
        else
            livePending = true;
    }

    // Each live type gets a slot = one of the lane engines (in series, so they stack).
    // Types already playing keep their slot and carry on undisturbed.
    void applyLive (double lateBy) noexcept
    {
        const uint32_t want = desiredLive();
        if (want == 0)
        {
            if (liveActive)
            {
                clearLive();
                stopAllLanes(); // back to the pattern from its next step
            }
            return;
        }

        if (! liveActive)
        {
            stopAllLanes(); // the pattern sits out while live is on
            liveActive = true;
        }

        for (int i = 0; i < kMaxLanes; ++i)
        {
            const int t = liveSlots[(size_t) i];
            if (t >= 0 && (want & (1u << t)) == 0)
            {
                stopLane (i);
                liveSlots[(size_t) i] = -1;
            }
        }

        // Newest held note/pad first, then latched pads in type order, while slots last.
        int order[kNumStutterTypes + 1];
        int n = 0;
        if (heldCount > 0) order[n++] = held[(size_t) heldCount - 1].type;
        for (int t = 0; t < kNumStutterTypes; ++t)
            if ((lastLatch & (1u << t)) != 0 && (n == 0 || order[0] != t))
                order[n++] = t;

        for (int k = 0; k < n; ++k)
        {
            const int t = order[k];
            bool playing = false;
            for (int s2 : liveSlots) playing |= s2 == t;
            if (playing)
                continue;
            for (int i = 0; i < kMaxLanes; ++i)
                if (liveSlots[(size_t) i] < 0)
                {
                    startLive (i, t, lateBy);
                    break;
                }
        }
    }

    void clearLive() noexcept
    {
        liveSlots.fill (-1);
        liveActive = livePending = false;
    }

    void startLive (int slot, int type, double lateBy) noexcept
    {
        StutterSettings st;
        st.type = (StutterType) std::clamp (type, 0, kNumStutterTypes - 1);
        st.sliceLen = kDivisionPpq[std::clamp (params->liveDivision, 0, kNumDivisions - 1)] * spq;
        st.stepLen = 1.0e15; // until released
        st.lateBy = lateBy;
        st.minPeriod = spq / 32.0;
        st.fadeSamples = fade;
        const StepData& pad = bankPtr->live[(size_t) st.type]; // the pad's own settings
        st.p1 = pad.p1;
        st.p2 = pad.p2;
        st.level = levelGain (pad.levelDb);
        st.pan = pad.pan;
        st.filter = pad.filter;
        st.bits = pad.bits;
        st.rate = pad.rate;
        st.send = pad.send;
        st.seed = hash32 (seedBase ^ (0xA5A5A5A5u + ++liveCounter));

        auto& ln = lanes[(size_t) slot];
        ln.engine.startStutter (st);
        ln.firingStep = ln.curStep = -1;
        ln.tieRemaining = 0;
        liveSlots[(size_t) slot] = type;
    }

    static LiveControls controlsOf (const StepData& s) noexcept
    {
        return { s.p1, s.p2, levelGain (s.levelDb), s.pan, s.filter, s.bits, s.rate, s.send };
    }

    float* const* sendPtrs() noexcept
    {
        sendPtrArray[0] = sendBuf[0].data();
        sendPtrArray[1] = sendBuf[1].data();
        return sendPtrArray;
    }

    // Linear ramp toward a target over ~20 ms (one step per sample).
    float rampTo (float now, float target) const noexcept
    {
        const float step = (float) (1.0 / (0.02 * sr));
        return now < target ? std::min (target, now + step) : std::max (target, now - step);
    }

    // Global crusher, then the delay and reverb returns.
    void applyFx (float* const* io, int numCh, int numSamples) noexcept
    {
        const auto& p = *params;
        numCh = std::min (numCh, channels);

        // ---- Global crusher (everything) -----------------------------------
        const bool crushOn = p.crushBits > 0.001f || p.crushRate > 0.001f;
        const float crushTarget = crushOn ? std::clamp (p.crushMix, 0.0f, 1.0f) : 0.0f;
        if (crushMixNow > 0.0f || crushTarget > 0.0f)
        {
            const double bits = p.crushBits > 0.001f ? crushBits (p.crushBits) : 24.0;
            const double factor = p.crushRate > 0.001f ? crushRateFactor (p.crushRate) : 1.0;
            for (int i = 0; i < numSamples; ++i)
            {
                crushMixNow = rampTo (crushMixNow, crushTarget);
                float frame[2] { io[0][i], numCh > 1 ? io[1][i] : 0.0f };
                float crushed[2] { frame[0], frame[1] };
                globalCrusher.processFrame (crushed, numCh, factor, bits);
                for (int c = 0; c < numCh; ++c)
                    io[c][i] = blend (frame[c], crushed[c], crushMixNow);
            }
            if (exactly (crushMixNow, 0.0f)) globalCrusher.reset();
        }

        // ---- Delay & reverb --------------------------------------------------
        const float delayTarget = std::clamp (p.delayMix, 0.0f, 1.0f);
        const float reverbTarget = std::clamp (p.reverbMix, 0.0f, 1.0f);
        const bool runDelay = delayMixNow > 0.0f || delayTarget > 0.0f;
        const bool runReverb = reverbMixNow > 0.0f || reverbTarget > 0.0f;
        if (! runDelay && delayActive)   { delay.reset(); delayActive = false; }
        if (! runReverb && reverbActive) { reverb.reset(); reverbActive = false; }
        if (! runDelay && ! runReverb)
            return;

        // While a later lane glitches, the effects hear its output (which already contains the earlier lanes' glitches).
        if (p.fxSource == 0)
            for (int L = 1; L < kMaxLanes; ++L)
            {
                const auto& l = lanes[(size_t) L];
                for (int c = 0; c < numCh; ++c)
                    for (int i = 0; i < numSamples; ++i)
                        sendBuf[(size_t) c][(size_t) i] = sendBuf[(size_t) c][(size_t) i] * (1.0f - l.actBuf[(size_t) i])
                                                          + l.sendBuf[(size_t) c][(size_t) i];
            }

        const float* src[2] = { p.fxSource == 1 ? io[0] : sendBuf[0].data(),
                                p.fxSource == 1 ? (numCh > 1 ? io[1] : io[0]) : sendBuf[1].data() };
        float* dOut[2] = { delayOut[0].data(), delayOut[1].data() };
        float* rOut[2] = { reverbOut[0].data(), reverbOut[1].data() };

        if (runDelay)
        {
            const double dt = kDelayTimePpq[std::clamp (p.delayTime, 0, kNumDelayTimes - 1)] * spq;
            delay.process (src, dOut, numCh, 0, numSamples, dt, p.delayFeedback, p.delayTone, p.delayPingPong);
            delayActive = true;
        }
        if (runReverb)
        {
            reverb.process (src, rOut, numCh, 0, numSamples, p.reverbSize, p.reverbDamp);
            reverbActive = true;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            delayMixNow = runDelay ? rampTo (delayMixNow, delayTarget) : 0.0f;
            reverbMixNow = runReverb ? rampTo (reverbMixNow, reverbTarget) : 0.0f;
            for (int c = 0; c < numCh; ++c)
                io[c][i] += (runDelay ? dOut[c][i] * delayMixNow : 0.0f) + (runReverb ? rOut[c][i] * reverbMixNow : 0.0f);
        }
    }

    //--------------------------------------------------------------------------
    struct LaneState
    {
        StutterEngine engine;
        int tieRemaining = 0;   // steps left in the tie that's running
        int firingStep = -1;    // the step whose glitch is playing (-1 = none)
        int curStep = -1;       // the step the playhead is on inside it (whose locks apply)
        int firingLen = 1, firingType = -1, firingPattern = 0;
        std::array<std::vector<float>, 2> sendBuf; // this lane's glitch audio for the FX (lane 1 uses the core's)
        std::vector<float> actBuf;                 // how much this lane is glitching, per sample

        void clear() noexcept { tieRemaining = 0; firingStep = curStep = -1; firingLen = 1; firingType = -1; firingPattern = 0; }
    };

    std::array<LaneState, kMaxLanes> lanes;
    TempoDelay delay;
    Reverb reverb;
    Crusher globalCrusher;
    std::array<std::vector<float>, 2> sendBuf, delayOut, reverbOut;
    float* sendPtrArray[2] {};
    std::array<MidiEvent, 512> chunkMidi {};
    int channels = 2;
    bool delayActive = false, reverbActive = false;
    float delayMixNow = 0.0f, reverbMixNow = 0.0f, crushMixNow = 0.0f;
    GridScheduler stepSched, barSched, quantSched;
    std::array<GridEvent, GridScheduler::kMaxEvents> stepEvents {}, barEvents {}, quantEvents {};
    std::array<Held, kMaxHeld> held {};

    const CoreParams* params = nullptr;
    const PatternBank* bankPtr = nullptr;

    double sr = 48000.0, spq = 24000.0, stepPpq = 0.25, samplesPerStep = 6000.0, swing = 0.0, barPpq = 4.0;
    double internalPpq = 0.0, effectiveBpm = 120.0;
    int fade = 0;
    uint32_t seedBase = 0, liveCounter = 0;
    bool hostClock = false;

    int playingStep = -1;
    int heldCount = 0, lastPad = -1;
    std::array<int, kMaxLanes> liveSlots { -1, -1, -1, -1 }; // live type playing on each engine (-1 = none)
    uint32_t lastLatch = 0;
    bool liveActive = false, livePending = false, liveChanged = false;
    int patternOverride = -1, lastParamPattern = -1;
    bool footFlip = false, footDown = false, engaged = true, engageTarget = true, engageInit = false;
};

} // namespace gn
