#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CloPlayerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit CloPlayerAudioProcessorEditor (CloPlayerAudioProcessor&);
    ~CloPlayerAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseCloFile();
    void loadAdjacentClo (int direction);
    void rebuildCloSequence (const juce::File& selectedFile);
    void updateArrowState();
    void updateStatus();

    CloPlayerAudioProcessor& processor;

    juce::Label title;
    juce::Label subtitle;
    juce::TextButton previousButton { "<" };
    juce::TextButton loadButton { "Load CLO..." };
    juce::TextButton nextButton { ">" };
    juce::Label fileLabel;
    juce::Label statusLabel;

    juce::Slider gainSlider;
    juce::Slider volumeSlider;
    juce::Label gainLabel;
    juce::Label volumeLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Array<juce::File> cloSequence;
    int currentCloIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloPlayerAudioProcessorEditor)
};
