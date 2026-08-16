#include "PluginEditor.h"

namespace
{
void configureKnob (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 22);
    slider.setNumDecimalPlacesToDisplay (2);
}
}

CloPlayerAudioProcessorEditor::CloPlayerAudioProcessorEditor (CloPlayerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (520, 330);

    title.setText ("CloPlayer", juce::dontSendNotification);
    title.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    subtitle.setText ("GP-200 + Hotone/Ampero CLO · Gain → CLO → Volume", juce::dontSendNotification);
    subtitle.setFont (juce::FontOptions (14.0f));
    subtitle.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (subtitle);

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { chooseCloFile(); };

    fileLabel.setText ("No CLO loaded", juce::dontSendNotification);
    fileLabel.setJustificationType (juce::Justification::centredLeft);
    fileLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (fileLabel);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (statusLabel);

    configureKnob (gainSlider);
    configureKnob (volumeSlider);
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (volumeSlider);

    gainLabel.setText ("GAIN", juce::dontSendNotification);
    volumeLabel.setText ("VOLUME", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
    volumeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (gainLabel);
    addAndMakeVisible (volumeLabel);

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getParameters(), "gain", gainSlider);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getParameters(), "volume", volumeSlider);

    updateStatus();
    startTimerHz (4);
}

void CloPlayerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (25, 27, 31));

    auto bounds = getLocalBounds().toFloat().reduced (18.0f);
    bounds.removeFromTop (82.0f);
    g.setColour (juce::Colour::fromRGB (42, 45, 51));
    g.fillRoundedRectangle (bounds, 10.0f);
}

void CloPlayerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (22);

    title.setBounds (area.removeFromTop (38));
    subtitle.setBounds (area.removeFromTop (26));
    area.removeFromTop (18);

    auto fileRow = area.removeFromTop (34);
    loadButton.setBounds (fileRow.removeFromLeft (120));
    fileRow.removeFromLeft (12);
    fileLabel.setBounds (fileRow);

    statusLabel.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);

    auto knobs = area;
    const int knobWidth = 160;
    const int totalWidth = knobWidth * 2 + 40;
    knobs = knobs.withSizeKeepingCentre (totalWidth, knobs.getHeight());

    auto left = knobs.removeFromLeft (knobWidth);
    knobs.removeFromLeft (40);
    auto right = knobs.removeFromLeft (knobWidth);

    gainLabel.setBounds (left.removeFromTop (24));
    volumeLabel.setBounds (right.removeFromTop (24));
    gainSlider.setBounds (left);
    volumeSlider.setBounds (right);
}

void CloPlayerAudioProcessorEditor::chooseCloFile()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load CLO", juce::File(), "*.clo");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (file == juce::File())
                                      return;

                                  const auto result = processor.loadCloFile (file);
                                  if (result.failed())
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (
                                          juce::MessageBoxIconType::WarningIcon,
                                          "Could not load CLO",
                                          result.getErrorMessage());
                                  }
                                  updateStatus();
                              });
}

void CloPlayerAudioProcessorEditor::timerCallback()
{
    updateStatus();
}

void CloPlayerAudioProcessorEditor::updateStatus()
{
    fileLabel.setText (processor.hasLoadedClo() ? processor.getLoadedCloName() : "No CLO loaded",
                       juce::dontSendNotification);

    if (! processor.isNativeSampleRate())
    {
        statusLabel.setText ("BYPASS: set the DAW/session to 44.1 kHz (current: "
                                 + juce::String (processor.getCurrentSampleRate(), 0) + " Hz)",
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
        return;
    }

    if (! processor.hasLoadedClo())
    {
        statusLabel.setText ("Load a GP-200 B1024 or Hotone/Ampero B2048 CLO.", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        return;
    }

    statusLabel.setText ("ACTIVE · " + processor.getLoadedCloFormatText() + " · 44.1 kHz · GP-200 Gain mapping",
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgreen);
}
