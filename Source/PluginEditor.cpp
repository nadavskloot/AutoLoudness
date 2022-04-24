/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
LUFSMeter_BasicAudioProcessorEditor::LUFSMeter_BasicAudioProcessorEditor (LUFSMeter_BasicAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);
    
//    recordButton.setClickingTogglesState(true);
    
    lufsSliderValue = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.treeState, "lufs", lufsSlider);
    lufsSlider.setSliderStyle(juce::Slider::LinearVertical);
    lufsSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 50, 50);
    addAndMakeVisible(lufsSlider);
    
    gainSliderValue = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.treeState, "gain", gainSlider);
    gainSlider.setSliderStyle(juce::Slider::LinearVertical);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 50, 50);
    addAndMakeVisible(gainSlider);
    
    recordButtonValue = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.treeState, "record", recordButton);
    recordButton.changeWidthToFitText();
    adjustButtonValue = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.treeState, "adjust", adjustButton);
    adjustButton.changeWidthToFitText();
    
    addAndMakeVisible(recordButton);
    addAndMakeVisible(adjustButton);
    
    lufsLabel.setText("lufs", juce::NotificationType::dontSendNotification);
    gainLabel.setText("gain", juce::NotificationType::dontSendNotification);
    addAndMakeVisible(lufsLabel);
    addAndMakeVisible(gainLabel);
}

LUFSMeter_BasicAudioProcessorEditor::~LUFSMeter_BasicAudioProcessorEditor()
{
}

//==============================================================================
void LUFSMeter_BasicAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
//    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void LUFSMeter_BasicAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    
    recordButton.setBounds(getWidth()/2-100, 10, 100, 50);
    adjustButton.setBounds(getWidth()/2+100, 10, 100, 50);

    lufsSlider.setBounds(getWidth()/2-40, 10, 50, 290);
    gainSlider.setBounds(getWidth()/2+40, 10, 50, 290);
    
    lufsLabel.setBounds(getWidth()/2-38, 210, 100, 100);
    gainLabel.setBounds(getWidth()/2+38, 210, 100, 100);
}

//void LUFSMeter_BasicAudioProcessorEditor::sliderValueChanged(juce::Slider* slider) {
//    if (slider == &lufsSlider) {
//        audioProcessor.lufsTarget = lufsSlider.getValue();
//    }
//}
