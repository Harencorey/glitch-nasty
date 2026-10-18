#pragma once

#include "GlitchCore.h"
#include "Params.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

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
    bool acceptsMidi() const override { return true; }   // reserved for MIDI-triggered glitches (option C)
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // Audio thread -> UI (read on a timer, never blocks either side).
    std::atomic<int> uiPlayingStep { -1 };
    std::atomic<int> uiFiringStep { -1 };
    std::atomic<float> uiBpm { 120.0f };
    std::atomic<bool> uiHostClock { false };

    // UI -> audio thread: TAP asks to snap the internal clock to the nearest beat.
    std::atomic<bool> snapRequest { false };

private:
    gn::GlitchCore core;
    gn::CoreParams params;
    juce::SmoothedValue<float> outputGain;

    // Cached raw parameter pointers (reading them is a lock-free atomic load).
    std::atomic<float>* pSync = nullptr;
    std::atomic<float>* pBpm = nullptr;
    std::atomic<float>* pGrid = nullptr;
    std::atomic<float>* pSteps = nullptr;
    std::atomic<float>* pXfade = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::array<std::atomic<float>*, gn::kMaxSteps> pStepOn {}, pStepProb {}, pStepDiv {}, pStepType {};

    void readParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchNastyProcessor)
};
