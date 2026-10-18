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
    gn::StepData getStep (int pattern, int step) const;
    int getEditPattern() const;   // the pattern the Pattern control points at
    int getNumSteps() const;
    double getGridPpq() const;

    void editStep (int pattern, int step, const std::function<void (gn::StepData&)>& change);
    gn::StepData getLiveStep (int type) const;
    void editLiveStep (int type, const std::function<void (gn::StepData&)>& change);
    void copyPattern (int pattern);
    void pastePattern (int pattern);
    bool hasClipboard() const noexcept { return clipboardValid; }
    void shiftPattern (int pattern, int direction);
    void reversePattern (int pattern);
    void clearPattern (int pattern);
    void randomizePattern (int pattern, const RandomLocks& locks);

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // UI -> audio thread: TAP asks to snap the internal clock to the nearest beat;
    // a held live pad (type index, or -1).
    std::atomic<bool> snapRequest { false };
    std::atomic<int> livePad { -1 };

    // Audio thread -> UI (read on a timer, never blocks either side).
    std::atomic<int> uiPlayingStep { -1 }, uiFiringStep { -1 }, uiFiringLen { 1 }, uiLiveType { -1 }, uiPattern { 0 };
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
    static void patternsFromTree (const juce::ValueTree&, gn::PatternBank&);
    static bool migrateOldStepParams (const juce::XmlElement&, gn::PatternBank&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchNastyProcessor)
};
