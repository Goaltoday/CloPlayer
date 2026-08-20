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
constexpr auto cloPathProperty = "cloPath";
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


juce::Array<juce::File> CloPlayerAudioProcessor::getSiblingCloFiles() const
{
    const juce::ScopedLock lock (modelLock);

    juce::Array<juce::File> files;
    if (! currentCloFile.existsAsFile())
        return files;

    const auto parent = currentCloFile.getParentDirectory();
    if (! parent.isDirectory())
        return files;

    parent.findChildFiles (files, juce::File::findFiles, false, "*.clo");

    for (int i = 0; i < files.size() - 1; ++i)
        for (int j = i + 1; j < files.size(); ++j)
            if (files.getReference (i).getFileName().compareNatural (files.getReference (j).getFileName()) > 0)
                files.swap (i, j);

    return files;
}

int CloPlayerAudioProcessor::getCurrentCloIndex (const juce::Array<juce::File>& files) const
{
    const juce::ScopedLock lock (modelLock);
    for (int i = 0; i < files.size(); ++i)
        if (files.getReference (i) == currentCloFile)
            return i;

    return -1;
}

bool CloPlayerAudioProcessor::canLoadAdjacentInternal (int direction) const
{
    const auto files = getSiblingCloFiles();
    const auto index = getCurrentCloIndex (files);
    if (index < 0)
        return false;

    const auto target = index + (direction < 0 ? -1 : 1);
    return juce::isPositiveAndBelow (target, files.size());
}

bool CloPlayerAudioProcessor::canLoadPreviousClo() const
{
    return canLoadAdjacentInternal (-1);
}

bool CloPlayerAudioProcessor::canLoadNextClo() const
{
    return canLoadAdjacentInternal (1);
}

juce::Result CloPlayerAudioProcessor::loadAdjacentClo (int direction)
{
    const auto files = getSiblingCloFiles();
    const auto index = getCurrentCloIndex (files);
    if (index < 0)
        return juce::Result::fail ("Sequential browsing is only available for CLO files loaded from disk.");

    const auto target = index + (direction < 0 ? -1 : 1);
    if (! juce::isPositiveAndBelow (target, files.size()))
        return juce::Result::fail (direction < 0 ? "There is no previous CLO in this folder."
                                                  : "There is no next CLO in this folder.");

    return loadCloFile (files.getReference (target));
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

    auto result = loadCloData (data.getData(), data.getSize(), file.getFileName());
    if (result.wasOk())
    {
        const juce::ScopedLock lock (modelLock);
        currentCloFile = file;
    }
    return result;
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

    if (displayName != currentCloFile.getFileName())
        currentCloFile = {};

    suspendProcessing (false);
    return juce::Result::ok();
}

void CloPlayerAudioProcessor::clearPlayers()
{
    for (auto& player : players)
        player = std::make_unique<gp200::GP200CloPlayer>();
    cloLoaded.store (false);
    currentCloFile = {};
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

juce::String CloPlayerAudioProcessor::getLoadedCloFormatText() const
{
    const juce::ScopedLock lock (modelLock);
    if (! cloLoaded.load() || players[0] == nullptr || ! players[0]->isLoaded())
        return {};

    const auto& info = players[0]->getModelInfo();
    switch (players[0]->getFormat())
    {
        case gp200::GP200CloPlayer::CloFormat::gp200B1024:
            return "GP-200 CLO · A 128 · B 1024";
        case gp200::GP200CloPlayer::CloFormat::hotoneAmperoB2048:
            return "Hotone / Ampero CLO · A 128 · B 2048";
        default:
            return "CLO · A " + juce::String (info.countA) + " · B " + juce::String (info.countB);
    }
}

void CloPlayerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    const juce::ScopedLock lock (modelLock);
    state.setProperty (cloNameProperty, cloName, nullptr);
    state.setProperty (cloDataProperty, cloData.getSize() == 0 ? juce::String() : cloData.toBase64Encoding(), nullptr);
    state.setProperty (cloPathProperty, currentCloFile.getFullPathName(), nullptr);

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
    const auto restoredPath = state.getProperty (cloPathProperty).toString();

    state.removeProperty (cloNameProperty, nullptr);
    state.removeProperty (cloDataProperty, nullptr);
    state.removeProperty (cloPathProperty, nullptr);
    parameters.replaceState (state);

    if (restoredBase64.isEmpty())
        return;

    juce::MemoryBlock restored;
    if (! restored.fromBase64Encoding (restoredBase64))
        return;

    loadCloData (restored.getData(), restored.getSize(), restoredName);

    if (restoredPath.isNotEmpty())
    {
        const juce::File restoredFile (restoredPath);
        if (restoredFile.existsAsFile())
        {
            const juce::ScopedLock lock (modelLock);
            currentCloFile = restoredFile;
        }
    }
}

juce::AudioProcessorEditor* CloPlayerAudioProcessor::createEditor()
{
    return new CloPlayerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CloPlayerAudioProcessor();
}
