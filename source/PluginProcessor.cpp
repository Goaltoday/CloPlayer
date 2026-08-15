#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstring>

namespace
{
constexpr auto gainId = "gain";
constexpr auto volumeId = "volume";
constexpr auto stateType = "CloPlayerState";
constexpr auto cloNameProperty = "cloName";
constexpr auto cloDataProperty = "cloDataBase64";
}

CloPlayerAudioProcessor::CloPlayerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, createParameterLayout())
{
    for (auto& player : players)
        player = std::make_unique<gp200::GP200CloPlayer>();
}

juce::AudioProcessorValueTreeState::ParameterLayout CloPlayerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { gainId, 1 }, "Gain",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { volumeId, 1 }, "Volume",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f));

    return layout;
}

void CloPlayerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);

    const juce::ScopedLock lock (modelLock);
    for (auto& player : players)
        if (player != nullptr)
            player->reset();
}

void CloPlayerAudioProcessor::releaseResources() {}

bool CloPlayerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    return input == juce::AudioChannelSet::mono()
        || input == juce::AudioChannelSet::stereo();
}

bool CloPlayerAudioProcessor::isNativeSampleRate() const noexcept
{
    return std::abs (currentSampleRate.load() - nativeSampleRate) < 0.5;
}

void CloPlayerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalIn = getTotalNumInputChannels();
    const auto totalOut = getTotalNumOutputChannels();
    for (int channel = totalIn; channel < totalOut; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    // The reconstructed/validated GP-200 CLO engine is native 44.1 kHz.
    // At other host rates v1.0 deliberately passes audio unchanged rather
    // than silently introducing an unvalidated sample-rate conversion stage.
    if (! cloLoaded.load() || ! isNativeSampleRate())
        return;

    const float gain = parameters.getRawParameterValue (gainId)->load();
    const float volume = parameters.getRawParameterValue (volumeId)->load();

    const juce::ScopedLock lock (modelLock);
    const int channels = juce::jmin (buffer.getNumChannels(), static_cast<int> (players.size()));

    for (int channel = 0; channel < channels; ++channel)
    {
        auto& player = players[static_cast<size_t> (channel)];
        if (player == nullptr || ! player->isLoaded())
            continue;

        player->setGainControl (gain);
        player->setVolumeControl (volume);
        player->processMono (buffer.getWritePointer (channel), buffer.getNumSamples());
    }
}

juce::Result CloPlayerAudioProcessor::loadCloFile (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.existsAsFile())
        return juce::Result::fail ("The selected CLO file does not exist.");
    if (! file.loadFileAsData (data))
        return juce::Result::fail ("Could not read the selected CLO file.");

    return loadCloData (data.getData(), data.getSize(), file.getFileName());
}

juce::Result CloPlayerAudioProcessor::loadCloData (const void* data, size_t size, const juce::String& displayName)
{
    if (data == nullptr || size == 0)
        return juce::Result::fail ("CLO data is empty.");

    suspendProcessing (true);
    const juce::ScopedLock lock (modelLock);

    std::array<std::unique_ptr<gp200::GP200CloPlayer>, 2> newPlayers;
    for (auto& player : newPlayers)
    {
        player = std::make_unique<gp200::GP200CloPlayer>();
        const auto result = player->loadFromMemory (data, size);
        if (result.failed())
        {
            lastError = result.getErrorMessage();
            suspendProcessing (false);
            return result;
        }
    }

    cloData.setSize (size, false);
    std::memcpy (cloData.getData(), data, size);
    cloName = displayName;
    players = std::move (newPlayers);
    lastError.clear();
    cloLoaded.store (true);

    suspendProcessing (false);
    return juce::Result::ok();
}

void CloPlayerAudioProcessor::clearPlayers()
{
    for (auto& player : players)
        player = std::make_unique<gp200::GP200CloPlayer>();
    cloLoaded.store (false);
}

void CloPlayerAudioProcessor::reloadPlayersFromStoredData()
{
    if (cloData.getSize() == 0)
    {
        clearPlayers();
        return;
    }

    const auto result = loadCloData (cloData.getData(), cloData.getSize(), cloName);
    if (result.failed())
        clearPlayers();
}

juce::String CloPlayerAudioProcessor::getLoadedCloName() const
{
    const juce::ScopedLock lock (modelLock);
    return cloName;
}

juce::String CloPlayerAudioProcessor::getLastError() const
{
    const juce::ScopedLock lock (modelLock);
    return lastError;
}

void CloPlayerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    const juce::ScopedLock lock (modelLock);
    state.setProperty (cloNameProperty, cloName, nullptr);
    state.setProperty (cloDataProperty, cloData.getSize() == 0 ? juce::String() : cloData.toBase64Encoding(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CloPlayerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid() || ! state.hasType (parameters.state.getType()))
        return;

    const auto restoredName = state.getProperty (cloNameProperty).toString();
    const auto restoredBase64 = state.getProperty (cloDataProperty).toString();

    state.removeProperty (cloNameProperty, nullptr);
    state.removeProperty (cloDataProperty, nullptr);
    parameters.replaceState (state);

    if (restoredBase64.isEmpty())
        return;

    juce::MemoryBlock restored;
    if (! restored.fromBase64Encoding (restoredBase64))
        return;

    loadCloData (restored.getData(), restored.getSize(), restoredName);
}

juce::AudioProcessorEditor* CloPlayerAudioProcessor::createEditor()
{
    return new CloPlayerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CloPlayerAudioProcessor();
}
