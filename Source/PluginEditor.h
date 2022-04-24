/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class LUFSMeter_BasicAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    LUFSMeter_BasicAudioProcessorEditor (LUFSMeter_BasicAudioProcessor&);
    ~LUFSMeter_BasicAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
//    void sliderValueChanged(juce::Slider* slider);

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    LUFSMeter_BasicAudioProcessor& audioProcessor;
    
    juce::ToggleButton recordButton { "find" };
    juce::ToggleButton adjustButton { "match" };
    juce::Slider lufsSlider;
    juce::Slider gainSlider;
    juce::Label lufsLabel {"lufs"};
    juce::Label gainLabel {"gain"};
    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LUFSMeter_BasicAudioProcessorEditor)
    
public:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lufsSliderValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainSliderValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> adjustButtonValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> recordButtonValue;
};
