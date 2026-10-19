#pragma once

#include "GlitchCore.h"
#include "Params.h"
#include "TripleBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>

class GlitchNastyProcessor : public juce::AudioProcessor
{
public:
    GlitchNastyProcessor();
    ~GlitchNastyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; } // delay / reverb tails

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Pattern editing (message thread). Every edit publishes a fresh copy of the
    // whole bank to the audio thread, lock-free.
    struct RandomLocks { bool onOff = false, type = false, repeat = false, chance = false, shape = false; };

    gn::Pattern getPattern (int pattern) const;
    gn::StepData getStep (int pattern, int lane, int step) const;
    int getEditPattern() const;   // the pattern the Pattern control points at
    int getNumSteps() const;
    double getGridPpq() const;

    void editStep (int pattern, int lane, int step, const std::function<void (gn::StepData&)>& change);
    gn::StepData getLiveStep (int type) const;
    void editLiveStep (int type, const std::function<void (gn::StepData&)>& change);
    // Drag a step's edges: it now spans newStart..newEnd. Moving the start moves the
    // step's settings with it. Steps it covers are kept (they come back when it shrinks).
    void setStepSpan (int pattern, int lane, int oldStart, int newStart, int newEnd);

    // Lanes: each pattern has 1..gn::kMaxLanes. Adding appends an empty lane;
    // removing deletes that lane (its steps and locks) and moves the ones below up.
    int getNumLanes (int pattern) const;
    void addLane (int pattern);
    void removeLane (int pattern, int lane);

    // Parameter locks: value 0..1, or gn::kModUnset to clear.
    float getMod (int pattern, int lane, int target, int step) const;
    void setMod (int pattern, int lane, int target, int step, float value);
    void clearMods (int pattern, int lane, int target);

    // Whole-pattern tools (copy/paste take both lanes; the rest act on one lane, locks included).
    void copyPattern (int pattern);
    void pastePattern (int pattern);
    bool hasClipboard() const noexcept { return clipboardValid; }
    void shiftPattern (int pattern, int lane, int direction);
    void reversePattern (int pattern, int lane);
    void clearPattern (int pattern, int lane);
    void randomizePattern (int pattern, int lane, const RandomLocks& locks);

    // Editor state (message thread; saved with the session so the editor reopens as you left it).
    int getActiveLane() const;   // clamped to the edited pattern's lanes
    void setActiveLane (int lane) noexcept { activeLane = juce::jlimit (0, gn::kMaxLanes - 1, lane); }
    int getBarsTarget() const noexcept { return barsTarget; }          // -1 = Chance, else a gn::ModTarget
    void setBarsTarget (int t) noexcept { barsTarget = juce::jlimit (-1, gn::kNumModTargets - 1, t); }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // UI -> audio thread: TAP asks to snap the internal clock to the nearest beat;
    // a held live pad (type index, or -1); latched live pads (bit = type).
    std::atomic<bool> snapRequest { false };
    std::atomic<int> livePad { -1 };
    std::atomic<uint32_t> liveLatch { 0 };

    // Audio thread -> UI (read on a timer, never blocks either side).
    std::atomic<int> uiPlayingStep { -1 }, uiPattern { 0 };
    std::atomic<uint32_t> uiLiveMask { 0 }; // live types playing right now (bit = type)
    std::array<std::atomic<int>, gn::kMaxLanes> uiFiringStep { -1, -1, -1, -1 }, uiFiringLen { 1, 1, 1, 1 };
    std::atomic<bool> uiEngaged { true }, uiEngagePending { false }, uiHostClock { false };
    std::atomic<float> uiBpm { 120.0f };

private:
    gn::GlitchCore core;
    gn::CoreParams params;
    juce::SmoothedValue<float> outputGain;

    // Pattern data: 'bank' is the master copy (message thread, under bankLock);
    // the audio thread reads its own copy from the triple buffer.
    gn::PatternBank bank;
    gn::TripleBuffer<gn::PatternBank> bankBuffer;
    juce::CriticalSection bankLock;
    gn::Pattern clipboard {};
    bool clipboardValid = false;
    int activeLane = 0, barsTarget = -1;
    juce::Random random;

    std::array<gn::MidiEvent, 512> midiEvents {};

    // Cached raw parameter pointers (reading them is a lock-free atomic load).
    std::atomic<float>* pSync = nullptr;
    std::atomic<float>* pBpm = nullptr;
    std::atomic<float>* pGrid = nullptr;
    std::atomic<float>* pSteps = nullptr;
    std::atomic<float>* pXfade = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pSwing = nullptr;
    std::atomic<float>* pDensity = nullptr;
    std::atomic<float>* pSeed = nullptr;
    std::atomic<float>* pPattern = nullptr;
    std::atomic<float>* pEngage = nullptr;
    std::atomic<float>* pLiveDiv = nullptr;
    std::atomic<float>* pLiveQuant = nullptr;
    std::atomic<float>* pCrushBits = nullptr;
    std::atomic<float>* pCrushRate = nullptr;
    std::atomic<float>* pCrushMix = nullptr;
    std::atomic<float>* pFxSource = nullptr;
    std::atomic<float>* pDlyTime = nullptr;
    std::atomic<float>* pDlyFb = nullptr;
    std::atomic<float>* pDlyTone = nullptr;
    std::atomic<float>* pDlyPing = nullptr;
    std::atomic<float>* pDlyMix = nullptr;
    std::atomic<float>* pRevSize = nullptr;
    std::atomic<float>* pRevDamp = nullptr;
    std::atomic<float>* pRevMix = nullptr;

    void readParameters();
    void publishBank (bool notifyHost); // call with bankLock held
    juce::ValueTree patternsToTree (const gn::PatternBank&) const;
    void patternsFromTree (const juce::ValueTree&, gn::PatternBank&);
public:
    // 0.4 kept "layers" as on-steps inside a tie; 0.5 moves them to lane 2. (Public for the tests.)
    static void migrateLayersToLane2 (gn::Pattern&);
private:
    static bool migrateOldStepParams (const juce::XmlElement&, gn::PatternBank&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchNastyProcessor)
};
