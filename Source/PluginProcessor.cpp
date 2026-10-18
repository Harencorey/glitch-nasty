#include "PluginProcessor.h"
#include "PluginEditor.h"

GlitchNastyProcessor::GlitchNastyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "GlitchNasty", gn::createParameterLayout())
{
    using namespace gn;
    pSync   = apvts.getRawParameterValue (ids::sync);
    pBpm    = apvts.getRawParameterValue (ids::bpm);
    pGrid   = apvts.getRawParameterValue (ids::grid);
    pSteps  = apvts.getRawParameterValue (ids::steps);
    pXfade  = apvts.getRawParameterValue (ids::xfade);
    pMix    = apvts.getRawParameterValue (ids::mix);
    pOutput = apvts.getRawParameterValue (ids::output);

    for (int i = 0; i < kMaxSteps; ++i)
    {
        pStepOn[(size_t) i]   = apvts.getRawParameterValue (ids::stepOn (i));
        pStepProb[(size_t) i] = apvts.getRawParameterValue (ids::stepProb (i));
        pStepDiv[(size_t) i]  = apvts.getRawParameterValue (ids::stepDiv (i));
    }
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
    params.syncMode    = (int) pSync->load();
    params.internalBpm = (double) pBpm->load();
    params.gridIndex   = (int) pGrid->load();
    params.numSteps    = (int) pSteps->load();
    params.crossfadeMs = pXfade->load();
    params.mix         = pMix->load() * 0.01f;
    params.snapToBeat  = snapRequest.exchange (false);

    for (size_t i = 0; i < (size_t) gn::kMaxSteps; ++i)
    {
        auto& s = params.steps[i];
        s.on          = pStepOn[i]->load() > 0.5f;
        s.probability = pStepProb[i]->load() * 0.01f;
        s.division    = (int) pStepDiv[i]->load();
    }
}

void GlitchNastyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
            host.playing = pos->getIsPlaying();
        }
    }

    const int channels = juce::jmin (2, numOut);
    core.process (buffer.getArrayOfWritePointers(), channels, numSamples, params, host);

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
    uiFiringStep.store (core.getFiringStep(), std::memory_order_relaxed);
    uiBpm.store ((float) core.getEffectiveBpm(), std::memory_order_relaxed);
    uiHostClock.store (core.isUsingHostClock(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* GlitchNastyProcessor::createEditor()
{
    return new GlitchNastyEditor (*this);
}

void GlitchNastyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GlitchNastyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GlitchNastyProcessor();
}
