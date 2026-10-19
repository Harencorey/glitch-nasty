#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::Identifier idPatterns ("PATTERNS"), idP ("P"), idS ("S"), idI ("i"), idL ("L"), idM ("M"), idT ("t"), idV ("v"),
                       idActiveLane ("activeLane"), idBars ("bars");
const juce::Identifier idOn ("on"), idChance ("chance"), idDiv ("div"), idType ("type"), idLen ("len"),
                       idLevel ("level"), idPan ("pan"), idFilter ("filter"), idP1 ("p1"), idP2 ("p2"),
                       idBits ("bits"), idRate ("rate"), idSend ("send"), idLive ("LIVE");

juce::ValueTree stepToTree (const gn::StepData& s, int index)
{
    juce::ValueTree st (idS);
    st.setProperty (idI, index, nullptr);
    st.setProperty (idOn, s.on, nullptr);
    st.setProperty (idChance, s.chance, nullptr);
    st.setProperty (idDiv, s.division, nullptr);
    st.setProperty (idType, s.type, nullptr);
    st.setProperty (idLen, s.length, nullptr);
    st.setProperty (idLevel, s.levelDb, nullptr);
    st.setProperty (idPan, s.pan, nullptr);
    st.setProperty (idFilter, s.filter, nullptr);
    st.setProperty (idP1, s.p1, nullptr);
    st.setProperty (idP2, s.p2, nullptr);
    st.setProperty (idBits, s.bits, nullptr);
    st.setProperty (idRate, s.rate, nullptr);
    st.setProperty (idSend, s.send, nullptr);
    return st;
}

void stepFromTree (const juce::ValueTree& st, gn::StepData& s)
{
    s.on       = st.getProperty (idOn, false);
    s.chance   = juce::jlimit (0.0f, 1.0f, (float) st.getProperty (idChance, 1.0f));
    s.division = juce::jlimit (0, gn::kNumDivisions - 1, (int) st.getProperty (idDiv, gn::kDiv32));
    s.type     = juce::jlimit (0, gn::kNumStutterTypes - 1, (int) st.getProperty (idType, 0));
    s.length   = juce::jlimit (1, gn::kMaxSteps, (int) st.getProperty (idLen, 1));
    s.levelDb  = juce::jlimit (gn::kLevelMinDb, gn::kLevelMaxDb, (float) st.getProperty (idLevel, 0.0f));
    s.pan      = juce::jlimit (-1.0f, 1.0f, (float) st.getProperty (idPan, 0.0f));
    s.filter   = juce::jlimit (-1.0f, 1.0f, (float) st.getProperty (idFilter, 0.0f));
    s.p1       = juce::jlimit (0.0f, 1.0f, (float) st.getProperty (idP1, gn::kTypeInfo[(size_t) s.type].p1Default));
    s.p2       = juce::jlimit (0.0f, 1.0f, (float) st.getProperty (idP2, gn::kTypeInfo[(size_t) s.type].p2Default));
    s.bits     = juce::jlimit (0.0f, 1.0f, (float) st.getProperty (idBits, 0.0f));
    s.rate     = juce::jlimit (0.0f, 1.0f, (float) st.getProperty (idRate, 0.0f));
    s.send     = juce::jlimit (0.0f, 1.0f, (float) st.getProperty (idSend, 1.0f));
}
} // namespace

GlitchNastyProcessor::GlitchNastyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "GlitchNasty", gn::createParameterLayout())
{
    using namespace gn;
    pSync      = apvts.getRawParameterValue (ids::sync);
    pBpm       = apvts.getRawParameterValue (ids::bpm);
    pGrid      = apvts.getRawParameterValue (ids::grid);
    pSteps     = apvts.getRawParameterValue (ids::steps);
    pXfade     = apvts.getRawParameterValue (ids::xfade);
    pMix       = apvts.getRawParameterValue (ids::mix);
    pOutput    = apvts.getRawParameterValue (ids::output);
    pSwing     = apvts.getRawParameterValue (ids::swing);
    pDensity   = apvts.getRawParameterValue (ids::density);
    pSeed      = apvts.getRawParameterValue (ids::seed);
    pPattern   = apvts.getRawParameterValue (ids::pattern);
    pEngage    = apvts.getRawParameterValue (ids::engage);
    pLiveDiv   = apvts.getRawParameterValue (ids::liveDiv);
    pLiveQuant = apvts.getRawParameterValue (ids::liveQuant);
    pCrushBits = apvts.getRawParameterValue (ids::crushBits);
    pCrushRate = apvts.getRawParameterValue (ids::crushRate);
    pCrushMix  = apvts.getRawParameterValue (ids::crushMix);
    pFxSource  = apvts.getRawParameterValue (ids::fxSource);
    pDlyTime   = apvts.getRawParameterValue (ids::dlyTime);
    pDlyFb     = apvts.getRawParameterValue (ids::dlyFb);
    pDlyTone   = apvts.getRawParameterValue (ids::dlyTone);
    pDlyPing   = apvts.getRawParameterValue (ids::dlyPing);
    pDlyMix    = apvts.getRawParameterValue (ids::dlyMix);
    pRevSize   = apvts.getRawParameterValue (ids::revSize);
    pRevDamp   = apvts.getRawParameterValue (ids::revDamp);
    pRevMix    = apvts.getRawParameterValue (ids::revMix);

    bank = gn::makeDefaultBank();
    bankBuffer.reset (bank);
}

bool GlitchNastyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void GlitchNastyProcessor::prepareToPlay (double sampleRate, int)
{
    // Everything that allocates happens here, never in processBlock.
    const int channels = juce::jlimit (1, 2, getTotalNumOutputChannels());
    core.prepare (sampleRate, channels);

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
}

void GlitchNastyProcessor::readParameters()
{
    params.syncMode     = (int) pSync->load();
    params.internalBpm  = (double) pBpm->load();
    params.gridIndex    = (int) pGrid->load();
    params.numSteps     = (int) pSteps->load();
    params.crossfadeMs  = pXfade->load();
    params.mix          = pMix->load() * 0.01f;
    params.snapToBeat   = snapRequest.exchange (false);
    params.swing        = pSwing->load() * 0.01f;
    params.density      = pDensity->load() * 0.01f;
    params.seed         = (int) pSeed->load();
    params.pattern      = (int) pPattern->load();
    params.engage       = pEngage->load() > 0.5f;
    params.liveDivision = (int) pLiveDiv->load();
    params.liveQuant    = (int) pLiveQuant->load();
    params.crushBits     = pCrushBits->load() * 0.01f;
    params.crushRate     = pCrushRate->load() * 0.01f;
    params.crushMix      = pCrushMix->load() * 0.01f;
    params.fxSource      = (int) pFxSource->load();
    params.delayTime     = (int) pDlyTime->load();
    params.delayFeedback = pDlyFb->load() * 0.01f;
    params.delayTone     = pDlyTone->load() * 0.01f;
    params.delayPingPong = pDlyPing->load() > 0.5f;
    params.delayMix      = pDlyMix->load() * 0.01f;
    params.reverbSize    = pRevSize->load() * 0.01f;
    params.reverbDamp    = pRevDamp->load() * 0.01f;
    params.reverbMix     = pRevMix->load() * 0.01f;
}

void GlitchNastyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    readParameters();

    // Host clock (absent in Standalone, so the core falls back to the internal clock).
    gn::HostTime host;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())          { host.hasBpm = true; host.bpm = *bpm; }
            if (auto ppq = pos->getPpqPosition())  { host.hasPpq = true; host.ppq = *ppq; }
            if (auto sig = pos->getTimeSignature()) { host.timeSigNum = sig->numerator; host.timeSigDen = sig->denominator; }
            host.playing = pos->getIsPlaying();
        }
    }

    // MIDI in: copy the 3-byte channel messages into a fixed array (no allocation).
    int numMidi = 0;
    for (const auto meta : midi)
    {
        if (numMidi >= (int) midiEvents.size())
            break;
        if (meta.numBytes < 2)
            continue;
        const auto* d = meta.data;
        midiEvents[(size_t) numMidi++] = { juce::jlimit (0, juce::jmax (0, numSamples - 1), meta.samplePosition),
                                           d[0], d[1], meta.numBytes > 2 ? d[2] : (juce::uint8) 0 };
    }

    const int channels = juce::jmin (2, numOut);
    core.process (buffer.getArrayOfWritePointers(), channels, numSamples, params, host, bankBuffer.read(),
                  midiEvents.data(), numMidi, livePad.load (std::memory_order_relaxed));

    // Output trim (smoothed so automation never zippers).
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    if (outputGain.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outputGain.getNextValue();
            for (int ch = 0; ch < channels; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }
    else if (std::abs (outputGain.getTargetValue() - 1.0f) > 1.0e-6f)
    {
        buffer.applyGain (0, numSamples, outputGain.getTargetValue());
    }

    uiPlayingStep.store (core.getPlayingStep(), std::memory_order_relaxed);
    for (int l = 0; l < gn::kNumLanes; ++l)
    {
        uiFiringStep[(size_t) l].store (core.getFiringStep (l), std::memory_order_relaxed);
        uiFiringLen[(size_t) l].store (core.getFiringLength (l), std::memory_order_relaxed);
    }
    uiLiveType.store (core.getLiveType(), std::memory_order_relaxed);
    uiPattern.store (core.getEffectivePattern(), std::memory_order_relaxed);
    uiEngaged.store (core.isEngaged(), std::memory_order_relaxed);
    uiEngagePending.store (core.isEngagePending(), std::memory_order_relaxed);
    uiBpm.store ((float) core.getEffectiveBpm(), std::memory_order_relaxed);
    uiHostClock.store (core.isUsingHostClock(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* GlitchNastyProcessor::createEditor()
{
    return new GlitchNastyEditor (*this);
}

//==============================================================================
// Pattern editing
gn::Pattern GlitchNastyProcessor::getPattern (int pattern) const
{
    const juce::ScopedLock sl (bankLock);
    return bank.patterns[(size_t) juce::jlimit (0, gn::kNumPatterns - 1, pattern)];
}

namespace
{
size_t patIdx (int p)  { return (size_t) juce::jlimit (0, gn::kNumPatterns - 1, p); }
size_t laneIdx (int l) { return (size_t) juce::jlimit (0, gn::kNumLanes - 1, l); }
size_t stepIdx (int s) { return (size_t) juce::jlimit (0, gn::kMaxSteps - 1, s); }
size_t modIdx (int t)  { return (size_t) juce::jlimit (0, gn::kNumModTargets - 1, t); }
}

gn::StepData GlitchNastyProcessor::getStep (int pattern, int lane, int step) const
{
    const juce::ScopedLock sl (bankLock);
    return bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)].steps[stepIdx (step)];
}

int GlitchNastyProcessor::getEditPattern() const
{
    return juce::jlimit (0, gn::kNumPatterns - 1, (int) pPattern->load());
}

int GlitchNastyProcessor::getNumSteps() const
{
    return juce::jlimit (1, gn::kMaxSteps, (int) pSteps->load());
}

double GlitchNastyProcessor::getGridPpq() const
{
    return gn::kGridPpq[juce::jlimit (0, gn::kNumGrids - 1, (int) pGrid->load())];
}

void GlitchNastyProcessor::publishBank (bool notifyHost)
{
    bankBuffer.publish (bank);
    if (notifyHost)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true)); // so the host knows the session changed
}

void GlitchNastyProcessor::editStep (int pattern, int lane, int step, const std::function<void (gn::StepData&)>& change)
{
    const juce::ScopedLock sl (bankLock);
    change (bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)].steps[stepIdx (step)]);
    publishBank (true);
}

float GlitchNastyProcessor::getMod (int pattern, int lane, int target, int step) const
{
    const juce::ScopedLock sl (bankLock);
    return bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)].mod[modIdx (target)][stepIdx (step)];
}

void GlitchNastyProcessor::setMod (int pattern, int lane, int target, int step, float value)
{
    const juce::ScopedLock sl (bankLock);
    bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)].mod[modIdx (target)][stepIdx (step)]
        = gn::isSet (value) ? juce::jlimit (0.0f, 1.0f, value) : gn::kModUnset;
    publishBank (true);
}

void GlitchNastyProcessor::clearMods (int pattern, int lane, int target)
{
    const juce::ScopedLock sl (bankLock);
    bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)].mod[modIdx (target)].fill (gn::kModUnset);
    publishBank (true);
}

gn::StepData GlitchNastyProcessor::getLiveStep (int type) const
{
    const juce::ScopedLock sl (bankLock);
    return bank.live[(size_t) juce::jlimit (0, gn::kNumStutterTypes - 1, type)];
}

void GlitchNastyProcessor::editLiveStep (int type, const std::function<void (gn::StepData&)>& change)
{
    const juce::ScopedLock sl (bankLock);
    auto& s = bank.live[(size_t) juce::jlimit (0, gn::kNumStutterTypes - 1, type)];
    change (s);
    s.type = juce::jlimit (0, gn::kNumStutterTypes - 1, type); // a pad's type never changes
    s.on = true;
    publishBank (true);
}

void GlitchNastyProcessor::setStepSpan (int pattern, int lane, int oldStart, int newStart, int newEnd)
{
    const int n = getNumSteps();
    const juce::ScopedLock sl (bankLock);
    auto& steps = bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)].steps;
    oldStart = juce::jlimit (0, n - 1, oldStart);
    newEnd = juce::jlimit (0, n - 1, newEnd);
    newStart = juce::jlimit (0, newEnd, newStart);

    auto owner = steps[(size_t) oldStart];
    if (newStart != oldStart)
        steps[(size_t) oldStart].on = false; // its settings move to the new start

    owner.on = true;
    owner.length = newEnd - newStart + 1;
    steps[(size_t) newStart] = owner;
    publishBank (true);
}

void GlitchNastyProcessor::copyPattern (int pattern)
{
    clipboard = getPattern (pattern);
    clipboardValid = true;
}

void GlitchNastyProcessor::pastePattern (int pattern)
{
    if (! clipboardValid)
        return;
    const juce::ScopedLock sl (bankLock);
    bank.patterns[(size_t) juce::jlimit (0, gn::kNumPatterns - 1, pattern)] = clipboard;
    publishBank (true);
}

void GlitchNastyProcessor::shiftPattern (int pattern, int lane, int direction)
{
    const int n = getNumSteps();
    const juce::ScopedLock sl (bankLock);
    auto& ln = bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)];
    auto rot = [n, direction] (auto& arr)
    {
        if (direction > 0) std::rotate (arr.begin(), arr.begin() + (n - 1), arr.begin() + n); // one step later
        else               std::rotate (arr.begin(), arr.begin() + 1, arr.begin() + n);       // one step earlier
    };
    rot (ln.steps);
    for (auto& row : ln.mod) rot (row);
    publishBank (true);
}

void GlitchNastyProcessor::reversePattern (int pattern, int lane)
{
    const int n = getNumSteps();
    const juce::ScopedLock sl (bankLock);
    auto& ln = bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)];
    const auto old = ln;

    for (int j = 0; j < n; ++j)
    {
        ln.steps[(size_t) j] = old.steps[(size_t) (n - 1 - j)];
        for (int t = 0; t < gn::kNumModTargets; ++t)
            ln.mod[(size_t) t][(size_t) j] = old.mod[(size_t) t][(size_t) (n - 1 - j)];
    }

    // A tie now has to start at the other end of its span.
    for (int i = 0; i < n; ++i)
    {
        const auto& s = old.steps[(size_t) i];
        const int len = std::min (s.length, n - i);
        if (s.on && len > 1)
        {
            std::swap (ln.steps[(size_t) (n - 1 - i)], ln.steps[(size_t) (n - 1 - (i + len - 1))]);
            i += len - 1;
        }
    }
    publishBank (true);
}

void GlitchNastyProcessor::clearPattern (int pattern, int lane)
{
    const juce::ScopedLock sl (bankLock);
    bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)] = gn::Lane {};
    publishBank (true);
}

void GlitchNastyProcessor::randomizePattern (int pattern, int lane, const RandomLocks& locks)
{
    using T = gn::StutterType;
    const int n = getNumSteps();
    const juce::ScopedLock sl (bankLock);
    auto& pat = bank.patterns[patIdx (pattern)].lanes[laneIdx (lane)].steps;

    // Musically weighted: plain repeats and reverses more often than freezes.
    constexpr float typeWeights[gn::kNumStutterTypes] = { 3.0f, 2.0f, 1.5f, 1.5f, 1.5f, 1.0f, 1.0f, 1.5f, 0.7f, 1.0f, 1.0f };
    const int divisions[] = { gn::kDiv16, gn::kDiv16, gn::kDiv16T, gn::kDiv32, gn::kDiv32, gn::kDiv32, gn::kDiv32T, gn::kDiv64, gn::kDiv16D, gn::kDiv8 };
    const float semis[] = { -12.0f, -7.0f, -5.0f, 5.0f, 7.0f, 12.0f, 12.0f };

    for (int i = 0; i < n; ++i)
    {
        auto& s = pat[(size_t) i];

        if (! locks.onOff)
            s.on = random.nextFloat() < (lane == 0 ? 0.4f : 0.2f); // lane 2 sparser: it's the seasoning

        if (! locks.type)
        {
            float total = 0.0f;
            for (float w : typeWeights) total += w;
            float r = random.nextFloat() * total;
            int t = 0;
            while (t < gn::kNumStutterTypes - 1 && r >= typeWeights[t]) { r -= typeWeights[t]; ++t; }
            if (locks.shape) s.type = t;   // keep the controls as they are
            else gn::setType (s, t);
        }

        if (! locks.repeat)
            s.division = divisions[random.nextInt ((int) std::size (divisions))];

        if (! locks.chance)
            s.chance = random.nextFloat() < 0.6f ? 1.0f : 0.05f * (float) (8 + random.nextInt (12)); // 40-95 %

        if (! locks.shape)
        {
            s.p1 = random.nextFloat();
            s.p2 = random.nextFloat();
            if ((T) s.type == T::pitch)
            {
                s.p1 = 0.5f + semis[random.nextInt ((int) std::size (semis))] / 48.0f;
                s.p2 = random.nextFloat() < 0.75f ? 0.5f : random.nextFloat();
            }
            s.length = random.nextFloat() < 0.15f ? 2 + random.nextInt (3) : 1;
            s.levelDb = 0.0f;
            s.pan = random.nextFloat() < 0.3f ? random.nextFloat() * 1.2f - 0.6f : 0.0f;
            s.filter = random.nextFloat() < 0.25f ? (random.nextBool() ? -1.0f : 1.0f) * (0.3f + 0.5f * random.nextFloat()) : 0.0f;
        }
    }
    publishBank (true);
}

//==============================================================================
// Session state: the parameters, plus every pattern as a PATTERNS child.
juce::ValueTree GlitchNastyProcessor::patternsToTree (const gn::PatternBank& b) const
{
    // PATTERNS > P(i) > L(i) > S(i) steps + M(t, v = comma-separated locks, "" = none)
    juce::ValueTree root (idPatterns);
    root.setProperty (idActiveLane, activeLane, nullptr);
    root.setProperty (idBars, barsTarget, nullptr);
    for (int p = 0; p < gn::kNumPatterns; ++p)
    {
        juce::ValueTree pt (idP);
        pt.setProperty (idI, p, nullptr);
        for (int l = 0; l < gn::kNumLanes; ++l)
        {
            const auto& ln = b.patterns[(size_t) p].lanes[(size_t) l];
            juce::ValueTree lt (idL);
            lt.setProperty (idI, l, nullptr);
            for (int i = 0; i < gn::kMaxSteps; ++i)
                lt.appendChild (stepToTree (ln.steps[(size_t) i], i), nullptr);
            for (int t = 0; t < gn::kNumModTargets; ++t)
            {
                bool any = false;
                juce::StringArray vals;
                for (int i = 0; i < gn::kMaxSteps; ++i)
                {
                    const float v = ln.mod[(size_t) t][(size_t) i];
                    any |= gn::isSet (v);
                    vals.add (gn::isSet (v) ? juce::String (v, 4) : juce::String());
                }
                if (! any)
                    continue;
                juce::ValueTree mt (idM);
                mt.setProperty (idT, t, nullptr);
                mt.setProperty (idV, vals.joinIntoString (","), nullptr);
                lt.appendChild (mt, nullptr);
            }
            pt.appendChild (lt, nullptr);
        }
        root.appendChild (pt, nullptr);
    }

    juce::ValueTree live (idLive);
    for (int t = 0; t < gn::kNumStutterTypes; ++t)
        live.appendChild (stepToTree (b.live[(size_t) t], t), nullptr);
    root.appendChild (live, nullptr);
    return root;
}

void GlitchNastyProcessor::migrateLayersToLane2 (gn::Pattern& pat)
{
    auto& l1 = pat.lanes[0].steps;
    auto& l2 = pat.lanes[1].steps;
    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        const int len = std::max (1, std::min (l1[(size_t) i].length, gn::kMaxSteps - i));
        if (! l1[(size_t) i].on || len < 2)
            continue;
        for (int j = i + 1; j < i + len; ++j)
            if (l1[(size_t) j].on)
            {
                l2[(size_t) j] = l1[(size_t) j];
                l2[(size_t) j].length = 1;  // 0.4 layers always lasted one step
                l1[(size_t) j] = gn::StepData {};
            }
        i += len - 1;
    }
}

void GlitchNastyProcessor::patternsFromTree (const juce::ValueTree& root, gn::PatternBank& b)
{
    b = gn::PatternBank {};
    activeLane = juce::jlimit (0, gn::kNumLanes - 1, (int) root.getProperty (idActiveLane, 0));
    barsTarget = juce::jlimit (-1, gn::kNumModTargets - 1, (int) root.getProperty (idBars, -1));

    for (const auto& pt : root)
    {
        const int p = pt.getProperty (idI, -1);
        if (! pt.hasType (idP) || p < 0 || p >= gn::kNumPatterns)
            continue;
        auto& pat = b.patterns[(size_t) p];
        bool oldFormat = false;

        for (const auto& child : pt)
        {
            if (child.hasType (idS)) // 0.2 - 0.4: steps straight under P = lane 1
            {
                const int i = child.getProperty (idI, -1);
                if (i >= 0 && i < gn::kMaxSteps)
                    stepFromTree (child, pat.lanes[0].steps[(size_t) i]);
                oldFormat = true;
                continue;
            }
            const int l = child.getProperty (idI, -1);
            if (! child.hasType (idL) || l < 0 || l >= gn::kNumLanes)
                continue;
            auto& ln = pat.lanes[(size_t) l];
            for (const auto& st : child)
            {
                if (st.hasType (idS))
                {
                    const int i = st.getProperty (idI, -1);
                    if (i >= 0 && i < gn::kMaxSteps)
                        stepFromTree (st, ln.steps[(size_t) i]);
                }
                else if (st.hasType (idM))
                {
                    const int t = st.getProperty (idT, -1);
                    if (t < 0 || t >= gn::kNumModTargets)
                        continue;
                    const auto vals = juce::StringArray::fromTokens (st.getProperty (idV).toString(), ",", "");
                    for (int i = 0; i < juce::jmin (vals.size(), gn::kMaxSteps); ++i)
                        ln.mod[(size_t) t][(size_t) i] = vals[i].trim().isEmpty() ? gn::kModUnset
                                                                                 : juce::jlimit (0.0f, 1.0f, vals[i].getFloatValue());
                }
            }
        }

        if (oldFormat)
            migrateLayersToLane2 (pat);
    }

    // Live pad settings (sessions from before 0.3 keep the factory pads).
    const auto defaults = gn::makeDefaultBank();
    b.live = defaults.live;
    const auto live = root.getChildWithName (idLive);
    for (const auto& st : live)
    {
        const int t = st.getProperty (idI, -1);
        if (! st.hasType (idS) || t < 0 || t >= gn::kNumStutterTypes)
            continue;
        stepFromTree (st, b.live[(size_t) t]);
        b.live[(size_t) t].type = t;
        b.live[(size_t) t].on = true;
    }
}

// Sessions saved with 0.1.x kept the steps as parameters (s1_on, s1_prob, s1_div,
// s1_type ...). Bring them into pattern A so old sessions open as they were.
bool GlitchNastyProcessor::migrateOldStepParams (const juce::XmlElement& xml, gn::PatternBank& b)
{
    auto findParam = [&xml] (const juce::String& id, float& value)
    {
        for (auto* e : xml.getChildWithTagNameIterator ("PARAM"))
            if (e->getStringAttribute ("id") == id)
            {
                value = (float) e->getDoubleAttribute ("value");
                return true;
            }
        return false;
    };

    // 0.1.3 types: Repeat, Reverse, Gate, Roll, Pitch+12, Pitch-12, Tape Stop, Fade
    constexpr int typeMap[8] = { 0, 1, 2, 3, 4, 4, 5, 6 };

    bool found = false;
    gn::Pattern pat {};
    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        const juce::String pre = "s" + juce::String (i + 1) + "_";
        float on = 0.0f, prob = 100.0f, div = (float) gn::kDiv32, type = 0.0f;
        if (! findParam (pre + "on", on))
            continue;
        found = true;
        findParam (pre + "prob", prob);
        findParam (pre + "div", div);
        findParam (pre + "type", type);

        auto& s = pat[(size_t) i];
        const int oldType = juce::jlimit (0, 7, (int) type);
        gn::setType (s, typeMap[oldType]);
        if (oldType == 5) s.p1 = 0.25f; // Pitch -12
        s.on = on > 0.5f;
        s.chance = juce::jlimit (0.0f, 1.0f, prob * 0.01f);
        s.division = juce::jlimit (0, gn::kNumDivisions - 1, (int) div);
    }

    if (found)
        b.patterns[0] = pat;
    return found;
}

void GlitchNastyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock sl (bankLock);
        state.appendChild (patternsToTree (bank), nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GlitchNastyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    auto loaded = gn::makeDefaultBank();

    auto patterns = tree.getChildWithName (idPatterns);
    if (patterns.isValid())
    {
        patternsFromTree (patterns, loaded);
        tree.removeChild (patterns, nullptr);
    }
    else
    {
        migrateOldStepParams (*xml, loaded);
    }

    apvts.replaceState (tree);

    const juce::ScopedLock sl (bankLock);
    bank = loaded;
    publishBank (false);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GlitchNastyProcessor();
}
