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

    addAndMakeVisible (previousButton);
    previousButton.onClick = [this] { loadAdjacentClo (-1); };

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { chooseCloFile(); };

    addAndMakeVisible (nextButton);
    nextButton.onClick = [this] { loadAdjacentClo (1); };

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

    updateArrowState();
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
    previousButton.setBounds (fileRow.removeFromLeft (34));
    fileRow.removeFromLeft (8);
    loadButton.setBounds (fileRow.removeFromLeft (120));
    fileRow.removeFromLeft (8);
    nextButton.setBounds (fileRow.removeFromLeft (34));
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
                                  else
                                  {
                                      rebuildCloSequence (file);
                                  }
                                  updateStatus();
                              });
}


void CloPlayerAudioProcessorEditor::rebuildCloSequence (const juce::File& selectedFile)
{
    cloSequence.clearQuick();
    currentCloIndex = -1;

    const auto folder = selectedFile.getParentDirectory();
    if (! folder.isDirectory())
    {
        updateArrowState();
        return;
    }

    folder.findChildFiles (cloSequence, juce::File::findFiles, false, "*.clo");

    for (int i = 0; i < cloSequence.size() - 1; ++i)
        for (int j = i + 1; j < cloSequence.size(); ++j)
            if (cloSequence.getReference (i).getFileName().compareNatural (cloSequence.getReference (j).getFileName()) > 0)
                cloSequence.swap (i, j);

    for (int i = 0; i < cloSequence.size(); ++i)
    {
        if (cloSequence.getReference (i) == selectedFile)
        {
            currentCloIndex = i;
            break;
        }
    }

    updateArrowState();
}

void CloPlayerAudioProcessorEditor::updateArrowState()
{
    previousButton.setEnabled (currentCloIndex > 0);
    nextButton.setEnabled (currentCloIndex >= 0 && currentCloIndex + 1 < cloSequence.size());
}

void CloPlayerAudioProcessorEditor::loadAdjacentClo (int direction)
{
    if (currentCloIndex < 0)
        return;

    const auto targetIndex = currentCloIndex + (direction < 0 ? -1 : 1);
    if (! juce::isPositiveAndBelow (targetIndex, cloSequence.size()))
        return;

    const auto result = processor.loadCloFile (cloSequence.getReference (targetIndex));
    if (result.failed())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Could not load CLO",
            result.getErrorMessage());
        return;
    }

    currentCloIndex = targetIndex;
    updateArrowState();
    updateStatus();
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
