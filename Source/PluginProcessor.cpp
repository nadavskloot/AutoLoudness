/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

// static member constants
// -----------------------
const float LUFSMeter_BasicAudioProcessor::minimalReturnValue = -300.0f;
const double LUFSMeter_BasicAudioProcessor::absoluteThreshold = -70.0;
// Specification for the histograms.
const double LUFSMeter_BasicAudioProcessor::lowestBlockLoudnessToConsider = -100.0; // LUFS


//==============================================================================
LUFSMeter_BasicAudioProcessor::LUFSMeter_BasicAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
    bufferForMeasurement (2, 2048), // Initialise the buffer with some common values.
      // Also initialise the two filters with the coefficients for a sample
      // rate of 44100 Hz. These values are given in the ITU-R BS.1770-2.
      preFilter (1.53512485958697,  // b0
                 -2.69169618940638, // b1
                 1.19839281085285,  // b2
                 -1.69065929318241, // a1
                 0.73248077421585), // a2
      revisedLowFrequencyBCurveFilter (1.0,               // b0
                                       -2.0,              // b1
                                       1.0,               // b2
                                       -1.99004745483398, // a1
                                       0.99007225036621), // a2
      numberOfBins (0),
      numberOfSamplesPerBin (0),
      numberOfSamplesInAllBins (0),
      numberOfBinsToCover400ms (0),
      numberOfSamplesIn400ms (0),
      numberOfBinsToCover100ms (0),
      numberOfBinsSinceLastGateMeasurementForI (1),
      // millisecondsSinceLastGateMeasurementForLRA (0),
      measurementDuration (0),
      numberOfBlocksToCalculateRelativeThreshold (0),
      sumOfAllBlocksToCalculateRelativeThreshold (0.0),
      relativeThreshold (absoluteThreshold),
      numberOfBlocksToCalculateRelativeThresholdLRA (0),
      sumOfAllBlocksToCalculateRelativeThresholdLRA (0.0),
      relativeThresholdLRA (absoluteThreshold),
      integratedLoudness (minimalReturnValue),
      shortTermLoudness (minimalReturnValue),
      maximumShortTermLoudness (minimalReturnValue),
      momentaryLoudness (minimalReturnValue),
      maximumMomentaryLoudness (minimalReturnValue),
      loudnessRangeStart (minimalReturnValue),
      loudnessRangeEnd (minimalReturnValue),
      freezeLoudnessRangeOnSilence (false),
      currentBlockIsSilent (false),

      
      treeState(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    
}


juce::AudioProcessorValueTreeState::ParameterLayout LUFSMeter_BasicAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>("gain", "Gain", -20.0f, 20.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("lufs", "Lufs", -50.0f, -10.0f, -20.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>("adjust", "Adjust", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("record", "Record", false));
    
    return layout;
}

LUFSMeter_BasicAudioProcessor::~LUFSMeter_BasicAudioProcessor()
{
}

//==============================================================================
const juce::String LUFSMeter_BasicAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LUFSMeter_BasicAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool LUFSMeter_BasicAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool LUFSMeter_BasicAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double LUFSMeter_BasicAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int LUFSMeter_BasicAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int LUFSMeter_BasicAudioProcessor::getCurrentProgram()
{
    return 0;
}

void LUFSMeter_BasicAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String LUFSMeter_BasicAudioProcessor::getProgramName (int index)
{
    return {};
}

void LUFSMeter_BasicAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void LUFSMeter_BasicAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    DBG("preparing to play");
    wasPlaying = 0;
    auto numberOfInputChannels  = getTotalNumInputChannels();
    auto expectedRequestRate = 10; // Setting expected request rate to arbitrary value. Make program dependent in future.
    auto estimatedSamplesPerBlock = samplesPerBlock;
    // Resize the buffer.
    bufferForMeasurement.setSize (numberOfInputChannels, estimatedSamplesPerBlock);
    
    // Set up the two filters for the K-Filtering.
    preFilter.prepareToPlay (sampleRate, numberOfInputChannels);
    revisedLowFrequencyBCurveFilter.prepareToPlay (sampleRate, numberOfInputChannels);
    
    // Modify the expectedRequestRate if needed.
    // It needs to be at least 10 and a multiple of 10 because
    //                --------------------------------
    // exactly every 0.1 second a gating block needs to be measured
    // (for the integrated loudness measurement).
    if (expectedRequestRate < 10)
        expectedRequestRate = 10;
    else
    {
        expectedRequestRate = (((expectedRequestRate-1) / 10) + 1) * 10;
            // examples
            //  19 -> 20
            //  20 -> 20
            //  21 -> 30
    }
    // It also needs to be a divisor of the samplerate for accurate
    // M and S values (the integrated loudness (I value) would not be
    // affected by this inaccuracy.
    while (int (sampleRate) % expectedRequestRate != 0)
    {
        expectedRequestRate += 10;
        
        if (expectedRequestRate > sampleRate/2)
        {
            expectedRequestRate = 10;
            // was DEB
            DBG ("Not possible to make expectedRequestRate a multiple of 10 and "
                 "a divisor of the samplerate.");
            break;
        }
    }
    
    
    // Figure out how many bins are needed.
    const int timeOfAccumulationForShortTerm = 3; // seconds.
        //Needed for the short term loudness measurement.
    numberOfBins = expectedRequestRate * timeOfAccumulationForShortTerm;
    numberOfSamplesPerBin = int (sampleRate / expectedRequestRate);
    numberOfSamplesInAllBins = numberOfBins * numberOfSamplesPerBin;
    
    numberOfBinsToCover100ms = int (0.1 * expectedRequestRate);
    numberOfBinsToCover400ms = int (0.4 * expectedRequestRate);
    numberOfSamplesIn400ms = numberOfBinsToCover400ms * numberOfSamplesPerBin;
    
    currentBin = 0;
    numberOfSamplesInTheCurrentBin = 0;
    numberOfBinsSinceLastGateMeasurementForI = 1;
    // millisecondsSinceLastGateMeasurementForLRA = 0;
    measurementDuration = 0;
    
    // Initialize the bins.
    bin.assign (numberOfInputChannels, vector<double> (numberOfBins, 0.0));

    averageOfTheLast3s.assign (numberOfInputChannels, 0.0);
    averageOfTheLast400ms.assign (numberOfInputChannels, 0.0);

    // Initialize the channel weighting.
    channelWeighting.clear();
    for (int k = 0; k != numberOfInputChannels; ++k)
    {
        if (k == 3 || k == 4)
            // The left and right surround channels have a higher weight
            // because they seem louder to the human ear.
            channelWeighting.push_back (1.41);
        else
            channelWeighting.push_back (1.0);
    }
    
    // Momentary loudness for the individual channels.
    momentaryLoudnessForIndividualChannels.assign (numberOfInputChannels, minimalReturnValue);
    
    reset();
    resetStates();
}


void LUFSMeter_BasicAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool LUFSMeter_BasicAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void LUFSMeter_BasicAudioProcessor::detectSilence(juce::AudioBuffer<float>& buffer) {
    if (freezeLoudnessRangeOnSilence)
    {
        // Detect if the block is silent.
        // ------------------------------
        const float silenceThreshold = std::pow (10, 0.1 * -120);
            // -120dB -> approx. 1.0e-12

        const float magnitude = buffer.getMagnitude (0, buffer.getNumSamples());
        if (magnitude < silenceThreshold)
        {
            currentBlockIsSilent = true;
            DBG ("Silence detected.");
        }
        else
            currentBlockIsSilent = false;
    }
}

void LUFSMeter_BasicAudioProcessor::applyFilters(juce::AudioBuffer<float>& bufferForMeasurement) {
    // Apply the pre-filter.
    // Used to account for the acoustic effects of the head.
    // This is the first part of the so called K-weighted filtering.
    preFilter.processBlock (bufferForMeasurement);
    
    // Apply the RLB filter (a simple highpass filter).
    // This is the second part of the so called K-weighted filtering.
    // Its name is in accordance to ITU-R BS.1770-2
    // (In ITU-R BS.1770-3 it's called 'a simple highpass filter').
    revisedLowFrequencyBCurveFilter.processBlock (bufferForMeasurement);
    
    // TEMP
    // Copy back the buffer to listen to the filtered audio.
    //   buffer = bufferForMeasurement;
    // END TEMP
}

void LUFSMeter_BasicAudioProcessor::squareChannelData(juce::AudioBuffer<float>& bufferForMeasurement) {
    for (int k = 0; k != bufferForMeasurement.getNumChannels(); ++k)
    {
        float* theKthChannelData = bufferForMeasurement.getWritePointer (k);
        
        for (int i = 0; i != bufferForMeasurement.getNumSamples(); ++i)
            theKthChannelData[i] = theKthChannelData[i] * theKthChannelData[i];
    }
}

int LUFSMeter_BasicAudioProcessor::getNumChannels(juce::AudioBuffer<float>& bufferForMeasurement) {
    // To prevent EXC_BAD_ACCESS when the number of channels in the buffer
    // suddenly changes without calling prepareToPlay() in advance.
    const int numberOfChannels = juce::jmin (bufferForMeasurement.getNumChannels(),
                                       int (bin.size()),
                                       int (averageOfTheLast400ms.size()),
                                       juce::jmin (int (averageOfTheLast3s.size()),
                                             int (channelWeighting.size())));
    jassert (bufferForMeasurement.getNumChannels() == int (bin.size()));
    jassert (bufferForMeasurement.getNumChannels() == int (averageOfTheLast400ms.size()));
    jassert (bufferForMeasurement.getNumChannels() == int (averageOfTheLast3s.size()));
    jassert (bufferForMeasurement.getNumChannels() == int (channelWeighting.size()));
    
    return numberOfChannels;
}

void LUFSMeter_BasicAudioProcessor::calculateShortTermLoudness(int k, int numberOfChannels) {
    double sumOfAllBins = 0.0;
        // which covers the last 3s.
    
    for (int b = 0; b != numberOfBins; ++b)
        sumOfAllBins += bin[k][b];

    averageOfTheLast3s[k] = sumOfAllBins / numberOfSamplesInAllBins;

    // Short term loudness
    // ===================
    {
        double weightedSum = 0.0;

        for (int k = 0; k != numberOfChannels; ++k)
            weightedSum += channelWeighting[k] * averageOfTheLast3s[k];
        
        if (weightedSum > 0.0)
            // This refers to equation (2) in ITU-R BS.1770-2
            shortTermLoudness = juce::jmax (float (-0.691 + 10.* std::log10(weightedSum)), minimalReturnValue);
        else
            // Since returning a value of -nan most probably would lead to
            // a malfunction, return the minimal return value.
            shortTermLoudness = minimalReturnValue;

        // Maximum
        if (shortTermLoudness > maximumShortTermLoudness)
            maximumShortTermLoudness = shortTermLoudness;
    }
}

void LUFSMeter_BasicAudioProcessor::calculateMomentaryLoudness(int k, int numberOfChannels) {
    double sumOfBinsToCoverTheLast400ms = 0.0;

    for (int d = 0; d != numberOfBinsToCover400ms; ++d)
    {
        // The index for the bin.
        int b = currentBin - d;
            // this might be negative right now.
        int n = numberOfBins;
        b = (b % n + n) % n;
            // b = b mod n (in the mathematical sense).
            // Not negative anymore.
            //
            // Now 0 <= b < numberOfBins.
            // Example: b=-5, n=30
            //  b%n = -5
            //  (b%n +n)%n = 25%30 = 25
            //
            // Example: b=16, n=30
            //  b%n = 16
            //  (b%n +n)%n = 46%30 = 16
        
        sumOfBinsToCoverTheLast400ms += bin[k][b];
    }

    averageOfTheLast400ms[k] = sumOfBinsToCoverTheLast400ms / numberOfSamplesIn400ms;

    // Momentary loudness
    // ==================
    {
        double weightedSum = 0.0;

        for (int k = 0; k != int (averageOfTheLast400ms.size()); ++k)
            weightedSum += channelWeighting[k] * averageOfTheLast400ms[k];
        
        if (weightedSum > 0.0)
            // This refers to equation (2) in ITU-R BS.1770-2
            momentaryLoudness = juce::jmax (float (-0.691 + 10. * std::log10(weightedSum)), minimalReturnValue);
        else
            // Since returning a value of -nan most probably would lead to
            // a malfunction, return a minimal return value.
            momentaryLoudness = minimalReturnValue;

        // Maximum
        if (momentaryLoudness > maximumMomentaryLoudness)
            maximumMomentaryLoudness = momentaryLoudness;
    }
}

void LUFSMeter_BasicAudioProcessor::calculateIntegratedLoudness(int numberOfChannels) {
    // INTEGRATED LOUDNESS
    // ===================
    // For the integrated loudness measurement we have to observe a
    // gating window of length 400ms every 100ms.
    // We call this window 'gating block', according to BS.1770-3
    if (numberOfBinsSinceLastGateMeasurementForI != numberOfBinsToCover100ms)
        ++numberOfBinsSinceLastGateMeasurementForI;
    else
    {
        // Every 100ms this section is reached.

        // The next time the condition above is checked, one bin has already been filled.
        // Therefore this is set to 1 (and not to 0).
        numberOfBinsSinceLastGateMeasurementForI = 1;
        
        ++measurementDuration;
        
        // Figure out if the current 400ms gated window (loudnessOfCurrentBlock =) l_j > /Gamma_a
        // ( see ITU-R BS.1770-3 equation (4) ).
        
        // Calculate the weighted sum of the current block,
        // (in 120725_integrated_loudness_revisited.tif, I call
        // this s_j)
        double weightedSumOfCurrentBlock = 0.0;

        for (int k = 0; k != numberOfChannels; ++k)
        {
            weightedSumOfCurrentBlock += channelWeighting[k] * averageOfTheLast400ms[k];
        }
        
        // Calculate the j'th gating block loudness l_j
        const double loudnessOfCurrentBlock = -0.691 + 10.*std::log10 (weightedSumOfCurrentBlock);
        
        if (loudnessOfCurrentBlock > absoluteThreshold)
        {
            // Recalculate the relative threshold.
            // -----------------------------------
            ++numberOfBlocksToCalculateRelativeThreshold;
            sumOfAllBlocksToCalculateRelativeThreshold += weightedSumOfCurrentBlock;
            
            // According to the definition of the relative
            // threshold in ITU-R BS.1770-3, page 6.
            relativeThreshold = -10.691 + 10.0 * std::log10 (sumOfAllBlocksToCalculateRelativeThreshold / numberOfBlocksToCalculateRelativeThreshold);
        }
        
        // Add the loudness of the current block to the histogram
        if (loudnessOfCurrentBlock > lowestBlockLoudnessToConsider)
        {
            histogramOfBlockLoudness[round (loudnessOfCurrentBlock * 10.0)] += 1;
            // With the + 0.5 the value is rounded to the closest bin.
            // With + 0.5: -22.26 ->
        }
        
        
        // Determine the integrated loudness.
        // ----------------------------------
        //
        // It's here instead inside of the getIntegratedLoudness() function
        // because here it's only calculated 10 times a second.
        // getIntegratedLoudness() is called at the refreshrate of the GUI,
        // which is higher (e.g. 20 times a second).

        if (histogramOfBlockLoudness.size() > 0)
        {
            const double biggestLoudnessInHistogram = (--histogramOfBlockLoudness.end())->first * 0.1;
            // DEB ("biggestLoudnessInHistogram = " + String(biggestLoudnessInHistogram))
            if (relativeThreshold < biggestLoudnessInHistogram)
            {
                int closestBinAboveRelativeThresholdKey = int (relativeThreshold * 10.0);
                while (histogramOfBlockLoudness.find (closestBinAboveRelativeThresholdKey) == histogramOfBlockLoudness.end())
                    // In this context, "== histogramOfBlockLoudness.end()" means "not found".
                {
                    closestBinAboveRelativeThresholdKey++; // Go 0.1 LU higher
                }

                int nrOfAllBlocks = 0;
                double sumForIntegratedLoudness = 0.0;
                
                for (map<int,int>::iterator currentBin = histogramOfBlockLoudness.find (closestBinAboveRelativeThresholdKey);
                     currentBin != histogramOfBlockLoudness.end();
                     ++currentBin)
                {
                    const int nrOfBlocksInBin = currentBin->second;
                    nrOfAllBlocks += nrOfBlocksInBin;
                    
                    const double weightedSumOfCurrentBin = pow (10.0, (currentBin->first * 0.1 + 0.691) * 0.1);
                    sumForIntegratedLoudness += nrOfBlocksInBin * weightedSumOfCurrentBin;
                }
                
                if (nrOfAllBlocks > 0) // nrOfAllBlocks > 0  =>  sumForIntegratedLoudness > 0.0
                {
                    integratedLoudness = float(-0.691 + 10. * std::log10 (sumForIntegratedLoudness / nrOfAllBlocks));
                }
                else
                {
                    integratedLoudness = minimalReturnValue;
                }
            }
        }
        
        
        // Loudness range
        // ==============
        // According to the specification, at least every 1000ms
        // a new 3s long LRA block needs to be started.
        //
        // Here, an interval of 100ms is used.
        // This makes measurement results equal (or very similar)
        // to ffmpeg/ebur128 and Nugen VisLM2.

        // if (millisecondsSinceLastGateMeasurementForLRA != 500)
        //     millisecondsSinceLastGateMeasurementForLRA += 100;
        // else
        {
            // Every second this section is reached.
            // This results in an overlap of the 3s gates of exactly
            // 2/3, the minimum requirement.

        //    millisecondsSinceLastGateMeasurementForLRA = 100;
            
           
            // This is very similar to the above code for the integrated loudness.
            // (But distinct enough to not put it into a single function/object.)
            
            // Calculate the weighted sum of the current block,
            // (in 120725_integrated_loudness_revisited.tif, I call
            // this s_j)
            // Using an analysis-window of 3 seconds, as specified in
            // EBU 3342-2011.
            double weightedSumOfCurrentBlockLRA = 0.0;
            
            for (int k = 0; k != numberOfChannels; ++k)
            {
                weightedSumOfCurrentBlockLRA += channelWeighting[k] * averageOfTheLast3s[k];
            }
            
            // Calculate the j'th gating block loudness l_j
            const double loudnessOfCurrentBlockLRA = -0.691 + 10.0 * std::log10 (weightedSumOfCurrentBlockLRA);
            
            if (loudnessOfCurrentBlockLRA > absoluteThreshold)
            {
                // Recalculate the relative threshold for LRA
                // ------------------------------------------
                ++numberOfBlocksToCalculateRelativeThresholdLRA;
                sumOfAllBlocksToCalculateRelativeThresholdLRA += weightedSumOfCurrentBlockLRA;
                
                // According to the definition of the relative
                // threshold in ITU-R BS.1770-3, page 6.
                // -20 LU as described in EBU 3342-2011.
                relativeThresholdLRA = -20.691 + 10.0 * std::log10 (sumOfAllBlocksToCalculateRelativeThresholdLRA / numberOfBlocksToCalculateRelativeThresholdLRA);
            }
            
            // Add the loudness of the current block to the histogram
            if (loudnessOfCurrentBlockLRA > lowestBlockLoudnessToConsider)
            {
                histogramOfBlockLoudnessLRA[round (loudnessOfCurrentBlockLRA * 10.0)] += 1;
            }
            
            // Determine the loudness range.
            // -----------------------------
            //
            // It's here instead inside of the getter functions
            // because here it's only calculated once a second.
            // The getter functions are called at the refreshrate of the GUI,
            // which is higher (e.g. 20 times a second).
            
            if (histogramOfBlockLoudnessLRA.size() > 0)
            {
                const double biggestLoudnessInHistogramLRA = (--histogramOfBlockLoudnessLRA.end())->first * 0.1;
                // DEB ("biggestLoudnessInHistogramLRA = " + String(biggestLoudnessInHistogramLRA))
                if (relativeThresholdLRA < biggestLoudnessInHistogramLRA)
                {
                    int closestBinAboveRelativeThresholdKeyLRA = int (relativeThresholdLRA * 10.0);
                    while (histogramOfBlockLoudnessLRA.find(closestBinAboveRelativeThresholdKeyLRA) == histogramOfBlockLoudnessLRA.end())
                        // In this context, "== histogramOfBlockLoudness.end()" means "not found".
                    {
                        closestBinAboveRelativeThresholdKeyLRA++;
                    }

                    // Figure out the number of blocks above the relativeThresholdLRA
                    // --------------------------------------------------------------
                    int numberOfBlocksLRA = 0;
                    
                    for (map<int,int>::iterator currentBinLRA = histogramOfBlockLoudnessLRA.find (closestBinAboveRelativeThresholdKeyLRA);
                         currentBinLRA != histogramOfBlockLoudnessLRA.end();
                         ++currentBinLRA)
                    {
                        const int nrOfBlocksInBinLRA = currentBinLRA->second;
                        numberOfBlocksLRA += nrOfBlocksInBinLRA;
                    }
                
                    // Figure out the lower bound (start) of the loudness range.
                    // ---------------------------------------------------------
                    map<int,int>::iterator startBinLRA = histogramOfBlockLoudnessLRA.find (closestBinAboveRelativeThresholdKeyLRA);
                    int numberOfBlocksBelowStartBinLRA = startBinLRA->second;
                
                    while (double (numberOfBlocksBelowStartBinLRA) < 0.10 * double (numberOfBlocksLRA))
                    {
                        ++startBinLRA;

                        numberOfBlocksBelowStartBinLRA += startBinLRA->second;
                    }
                    // DEB("numberOfBlocks = " + String (numberOfBlocksLRA))
                    // DEB("numberOfBlocksBelowStartBinLRA = " + String(numberOfBlocksBelowStartBinLRA))
                    
//                                ++startBinLRA;
                
                    if (!(freezeLoudnessRangeOnSilence && currentBlockIsSilent))
                        loudnessRangeStart = startBinLRA->first * 0.1;
                        // DEB("LRA starts at " + String (loudnessRangeStart))
                    // Else:
                    // Holding the loudnessRangeStart on silence
                    // helps reading it after the end of an audio
                    // region or if the DAW has just been stopped.
                    // The measurement does not get interrupted by
                    // this! It's only a temporary freeze.

                    // Figure out the upper bound (end) of the loudness range.
                    // -------------------------------------------------------
                    map<int,int>::iterator endBinLRA = --(histogramOfBlockLoudnessLRA.end());
                    int numberOfBlocksAboveEndBinLRA = endBinLRA->second;
                
                    while (double (numberOfBlocksAboveEndBinLRA) < 0.05 * double (numberOfBlocksLRA))
                    {
                        --endBinLRA;
                        numberOfBlocksAboveEndBinLRA += endBinLRA->second;
                    }
                
                    if (!(freezeLoudnessRangeOnSilence && currentBlockIsSilent))
                        loudnessRangeEnd = endBinLRA->first * 0.1;
                        // DEB("LRA ends at " + String (loudnessRangeEnd))
                    // Else:
                    // Holding the loudnessRangeEnd on silence
                    // helps reading it after the end of an audio
                    // region or if the DAW has just been stopped.
                    // The measurement does not get interrupted by
                    // this! It's only a temporary freeze.
                    
                    // DEB("LRA = " + String (loudnessRangeEnd - loudnessRangeStart))
                }
            }
        }
    }
}

void LUFSMeter_BasicAudioProcessor::addToBins(juce::AudioBuffer<float>& bufferForMeasurement, int numberOfChannels) {
    // If the new samples from the bufferForMeasurement can all be added
    // to the same bin.
    if (numberOfSamplesInTheCurrentBin + bufferForMeasurement.getNumSamples()
        < numberOfSamplesPerBin)
    {
        for (int k = 0; k != numberOfChannels; ++k)
        {
            float* bufferOfChannelK = bufferForMeasurement.getWritePointer (k);
            double& theBinToSumTo = bin[k][currentBin];
            
            for (int i = 0; i != bufferForMeasurement.getNumSamples(); ++i)
            {
                theBinToSumTo += bufferOfChannelK[i];
            }
        }
        
        numberOfSamplesInTheCurrentBin += bufferForMeasurement.getNumSamples();
    }
    
    // If the new samples are split up between two (or more (which would be a
    // strange setup)) bins.
    else
    {
        int positionInBuffer = 0;
        bool bufferStillContainsSamples = true;
        
        while (bufferStillContainsSamples)
        {
            // Figure out if the remaining samples in the buffer can all be
            // accumulated to the current bin.
            const int numberOfSamplesLeftInTheBuffer = bufferForMeasurement.getNumSamples()-positionInBuffer;
            int numberOfSamplesToPutIntoTheCurrentBin;
            
            if (numberOfSamplesLeftInTheBuffer
                    < numberOfSamplesPerBin - numberOfSamplesInTheCurrentBin )
            {
                // Case 1: Partially fill a bin (by using all the samples left in the buffer).
                // ---------------------------------------------------------------------------
                // If all the samples from the buffer can be added to the
                // current bin.
                numberOfSamplesToPutIntoTheCurrentBin = numberOfSamplesLeftInTheBuffer;
                bufferStillContainsSamples = false;
            }
            else
            {
                // Case 2: Completely fill a bin (most likely the buffer will still contain some samples for the next bin).
                // --------------------------------------------------------------------------------------------------------
                // Accumulate samples to the current bin until it is full.
                numberOfSamplesToPutIntoTheCurrentBin = numberOfSamplesPerBin - numberOfSamplesInTheCurrentBin;
            }
            
            // Add the samples to the bin.
            for (int k = 0; k != numberOfChannels; ++k)
            {
                float* bufferOfChannelK = bufferForMeasurement.getWritePointer (k);
                double& theBinToSumTo = bin[k][currentBin];
                for (int i = positionInBuffer;
                     i != positionInBuffer + numberOfSamplesToPutIntoTheCurrentBin;
                     ++i)
                {
                    theBinToSumTo += bufferOfChannelK[i];
                }
            }
            numberOfSamplesInTheCurrentBin += numberOfSamplesToPutIntoTheCurrentBin;
            
            // If there are some samples left in the buffer
            // => A bin has just been completely filled (case 2 above).
            if (bufferStillContainsSamples)
            {
                positionInBuffer = positionInBuffer
                                   + numberOfSamplesToPutIntoTheCurrentBin;
                
                // We have completely filled a bin.
                // This is the moment the larger sums need to be updated.
                for (int k = 0; k != numberOfChannels; ++k)
                {
                    calculateShortTermLoudness(k, numberOfChannels);
                    
                    calculateMomentaryLoudness(k, numberOfChannels);
                }
                
                calculateIntegratedLoudness(numberOfChannels);
                
                // Move on to the next bin
                currentBin = (currentBin + 1) % numberOfBins;
                // Set it to zero.
                for (int k = 0; k != numberOfChannels; ++k)
                {
                    bin[k][currentBin] = 0.0;
                }
                numberOfSamplesInTheCurrentBin = 0;
            }
        }
    }
}

void LUFSMeter_BasicAudioProcessor::calculateLufs (juce::AudioBuffer<float>& buffer) {
    bufferForMeasurement = buffer; // This copies the audio to another memory
                                   // location using memcpy.
    
    detectSilence(buffer);
    
    applyFilters(bufferForMeasurement);

    squareChannelData(bufferForMeasurement);

    int numberOfChannels = getNumChannels(bufferForMeasurement);
    
    addToBins(bufferForMeasurement, numberOfChannels);
    

}

void LUFSMeter_BasicAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data.
    
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
        buffer.clear (i, 0, buffer.getNumSamples());
    }

    
    auto playhead = getPlayHead();
    if (playhead!=nullptr)
    {
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (playhead->getCurrentPosition(info)) // even if playhead exists, it may not return valid position info
        {
            bool isPlaying = info.isPlaying;
            if (isPlaying) {
                if (not wasPlaying) {
                    wasPlaying = 1;
                    resetStates();
                }
            } else {
                wasPlaying = 0;
            }
        }
    }
    
    calculateLufs(buffer); // Does all LUFS Calculations
    
    auto isAdjust = treeState.getParameterAsValue("adjust").getValue();
    auto isRecord = treeState.getParameterAsValue("record").getValue();

    if (isAdjust or isRecord) {
        auto sliderLufsValue = treeState.getParameterAsValue("lufs");
        auto lufsDifference = abs(float(sliderLufsValue.getValue()) - shortTermLoudness);
        
//        DBG(shortTermLoudness);
//        DBG(integratedLoudness);
        
        if (measurementDuration * 0.1f == 3.0f) { // After 3 seconds
            integratedLoudnessDiff = float(sliderLufsValue.getValue()) - integratedLoudness;
            if (isRecord) {
                sliderLufsValue.setValue(integratedLoudness);
            }
            if (isAdjust) {
                treeState.getParameterAsValue("gain").setValue(integratedLoudnessDiff);
            }
        }
        
        for (int channel = 0; channel < totalNumInputChannels; ++channel) // Iterate over audio samples
        {
            auto* channelData = buffer.getWritePointer (channel);
            for (int sample = 0; sample < buffer.getNumSamples(); sample++) {
                if (measurementDuration * 0.1f < 3) {
                    channelData[sample] = 0;
                } else {
                    channelData[sample] = channelData[sample] * juce::Decibels::decibelsToGain(float(treeState.getParameterAsValue("gain").getValue()));
                }
            }
        }
    }
    else {
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            auto* channelData = buffer.getWritePointer (channel);
            for (int sample = 0; sample < buffer.getNumSamples(); sample++) {
                channelData[sample] = channelData[sample] * juce::Decibels::decibelsToGain(float(treeState.getParameterAsValue("gain").getValue()));
            }
        }
    }
}



//==============================================================================
bool LUFSMeter_BasicAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* LUFSMeter_BasicAudioProcessor::createEditor()
{
    return new LUFSMeter_BasicAudioProcessorEditor (*this);
}

//==============================================================================
void LUFSMeter_BasicAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void LUFSMeter_BasicAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}


float LUFSMeter_BasicAudioProcessor::getShortTermLoudness() const
{
    return shortTermLoudness;
}

float LUFSMeter_BasicAudioProcessor::getMaximumShortTermLoudness() const
{
    return maximumShortTermLoudness;
}

vector<float>& LUFSMeter_BasicAudioProcessor::getMomentaryLoudnessForIndividualChannels()
{
    
    // calculate the momentary loudness

    for (int k = 0; k != int (momentaryLoudnessForIndividualChannels.size()); ++k)
    {
        float kthChannelMomentaryLoudness = minimalReturnValue;
        
        if (averageOfTheLast400ms[k] > 0.0f)
        {
            // This refers to equation (2) in ITU-R BS.1770-2
            kthChannelMomentaryLoudness = juce::jmax (float (-0.691 + 10. * std::log10(averageOfTheLast400ms[k])), minimalReturnValue);
        }

        momentaryLoudnessForIndividualChannels[k] = kthChannelMomentaryLoudness;
    }
    
    return momentaryLoudnessForIndividualChannels;
}

float LUFSMeter_BasicAudioProcessor::getMomentaryLoudness() const
{
    return momentaryLoudness;
}

float LUFSMeter_BasicAudioProcessor::getMaximumMomentaryLoudness() const
{
    return maximumMomentaryLoudness;
}

float LUFSMeter_BasicAudioProcessor::getIntegratedLoudness() const
{
    return integratedLoudness;
}

float LUFSMeter_BasicAudioProcessor::getLoudnessRangeStart() const
{
    return loudnessRangeStart;
}

float LUFSMeter_BasicAudioProcessor::getLoudnessRangeEnd() const
{
    return loudnessRangeEnd;
}

float LUFSMeter_BasicAudioProcessor::getLoudnessRange() const
{
    return loudnessRangeEnd - loudnessRangeStart;
}

float LUFSMeter_BasicAudioProcessor::getMeasurementDuration() const
{
    return measurementDuration * 0.1f;
}

void LUFSMeter_BasicAudioProcessor::setFreezeLoudnessRangeOnSilence (bool freeze)
{
    freezeLoudnessRangeOnSilence = freeze;
}

void LUFSMeter_BasicAudioProcessor::resetStates()
{
    float integratedLoudnessDiff = 1;
    
    // the bins
    // It is important to use assign() (replace all values) and not
    // resize() (only set new elements to the provided value).
    bin.assign (bin.size(), vector<double> (numberOfBins, 0.0));

    // To ensure the returned momentary and short term loudness are at its
    // minimum, even if no audio is processed at the moment.
    averageOfTheLast3s.assign (averageOfTheLast400ms.size(), 0.0);
    averageOfTheLast400ms.assign (averageOfTheLast400ms.size(), 0.0);
    
    measurementDuration = 0;
    
    // momentary loudness for the individual tracks.
    momentaryLoudnessForIndividualChannels.assign (momentaryLoudnessForIndividualChannels.size(), minimalReturnValue);
    
    // Integrated loudness
    numberOfBinsSinceLastGateMeasurementForI = 1;
    numberOfBlocksToCalculateRelativeThreshold = 0;
    sumOfAllBlocksToCalculateRelativeThreshold = 0.0;
    relativeThreshold = absoluteThreshold;
    
    histogramOfBlockLoudness.clear();
    
    integratedLoudness = minimalReturnValue;
    
    
    // Loudness range
    numberOfBlocksToCalculateRelativeThresholdLRA = 0;
    sumOfAllBlocksToCalculateRelativeThresholdLRA = 0.0;
    relativeThresholdLRA = absoluteThreshold;
    
    histogramOfBlockLoudnessLRA.clear();
    
    loudnessRangeStart = minimalReturnValue;
    loudnessRangeEnd = minimalReturnValue;

    // Short term loudness
    shortTermLoudness = minimalReturnValue;
    maximumShortTermLoudness = minimalReturnValue;

    // Momentary loudness
    momentaryLoudness = minimalReturnValue;
    maximumMomentaryLoudness = minimalReturnValue;
}

int LUFSMeter_BasicAudioProcessor::round (double d)
{
    // For a negative d, int (d) will choose the next higher number,
    // therfore the - 0.5.
    return (d > 0.0) ? int (d + 0.5) : int (d - 0.5);
}





//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LUFSMeter_BasicAudioProcessor();
}
