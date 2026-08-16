#pragma once

#include <JuceHeader.h>
#include "GP200CloPlayer.h"

#include <array>
#include <atomic>
#include <memory>

class CloPlayerAudioProcessor final : public juce::AudioProcessor
{
public:
    CloPlayerAudioProcessor();
    ~CloPlayerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    juce::Result loadCloFile (const juce::File& file);
    juce::Result loadCloData (const void* data, size_t size, const juce::String& displayName);

    bool hasLoadedClo() const noexcept { return cloLoaded.load(); }
    juce::String getLoadedCloName() const;
    juce::String getLastError() const;
    juce::String getLoadedCloFormatText() const;
    bool isNativeSampleRate() const noexcept;
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

private:
    static constexpr double nativeSampleRate = 44100.0;

    void reloadPlayersFromStoredData();
    void clearPlayers();

    juce::AudioProcessorValueTreeState parameters;
    std::array<std::unique_ptr<gp200::GP200CloPlayer>, 2> players;

    juce::MemoryBlock cloData;
    juce::String cloName;
    juce::String lastError;
    mutable juce::CriticalSection modelLock;

    std::atomic<bool> cloLoaded { false };
    std::atomic<double> currentSampleRate { nativeSampleRate };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloPlayerAudioProcessor)
};
