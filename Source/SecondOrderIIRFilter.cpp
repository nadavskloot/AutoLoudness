/*
  ==============================================================================

    SecondOrderIIRFilter.cpp
    Created: 8 Feb 2022 8:57:47pm
    Author:  Nadav Skloot

  ==============================================================================
*/

#include "SecondOrderIIRFilter.h"


//==============================================================================
SecondOrderIIRFilter::SecondOrderIIRFilter(double b0_at48k_,
                                           double b1_at48k_,
                                           double b2_at48k_,
                                           double a1_at48k_,
                                           double a2_at48k_)
:   b0_at48k {b0_at48k_},
    b1_at48k {b1_at48k_},
    b2_at48k {b2_at48k_},
    a1_at48k {a1_at48k_},
    a2_at48k {a2_at48k_},

    b0 {b0_at48k_},
    b1 {b1_at48k_},
    b2 {b2_at48k_},
    a1 {a1_at48k_},
    a2 {a2_at48k_},
    numberOfChannels {0}
{
    // Determine the values Q, VH, VB, VL and arctanK.
    // See 111222_my_notes_to_the_calculation_of_the_filter_coefficients.tif
    // for the derivations of these equations.
    const double KoverQ = (2. - 2. * a2_at48k) / (a2_at48k - a1_at48k + 1.);
    const double K = sqrt ((a1_at48k + a2_at48k + 1.) / (a2_at48k - a1_at48k + 1.));
    Q = K / KoverQ; 
    arctanK = atan (K);
    VB = (b0_at48k - b2_at48k)/(1. - a2_at48k);
    VH = (b0_at48k - b1_at48k + b2_at48k)/(a2_at48k - a1_at48k + 1.);
    VL = (b0_at48k + b1_at48k + b2_at48k)/(a1_at48k + a2_at48k + 1.);   
}

SecondOrderIIRFilter::~SecondOrderIIRFilter()
{
}

//==============================================================================
void SecondOrderIIRFilter::prepareToPlay (double sampleRate, 
                                          int numberOfChannels_)
{
    // DEB("prepareToPlay called.")
    
    numberOfChannels = numberOfChannels_;
    
    // Initialize z1 and z2.
    z1.calloc (numberOfChannels); // calloc: Allocates a specified amount of memory and clears it.
    z2.calloc (numberOfChannels);
    
    // Determine the filter coefficients.
    const double sampleRate48k = 48000.;
    if (sampleRate == sampleRate48k)
    {
        b0 = b0_at48k;
        b1 = b1_at48k;
        b2 = b2_at48k;
        a1 = a1_at48k;
        a2 = a2_at48k;
    }
    else
    {
        // See 111222_my_notes_to_the_calculation_of_the_filter_coefficients.tif
        // for the derivations of these equations.
        const double K = tan (arctanK * sampleRate48k / sampleRate);
        const double commonFactor = 1. / (1. + K/Q + K*K);
        b0 = (VH + VB*K/Q + VL*K*K)*commonFactor;
        b1 = 2.*(VL*K*K - VH)*commonFactor;
        b2 = (VH - VB*K/Q + VL*K*K)*commonFactor;
        a1 = 2.*(K*K - 1.)*commonFactor;
        a2 = (1. - K/Q + K*K)*commonFactor;
    }
}

void SecondOrderIIRFilter::releaseResources()
{
}

void SecondOrderIIRFilter::processBlock (juce::AudioSampleBuffer& buffer)
{
    const int numOfChannels = juce::jmin (numberOfChannels, buffer.getNumChannels());

    for (int channel = 0; channel < numOfChannels; ++channel)
    {
        float* samples = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float in = samples[i];
            
            double factorForB0 = in - a1 * z1[channel] - a2 * z2[channel];
            double out = b0 * factorForB0
            + b1 * z1[channel]
            + b2 * z2[channel];
            
            // Copied from juce_IIRFilter.cpp, processSamples(),
            #if JUCE_INTEL
            if (!(out < -1.0e-8 || out > 1.0e-8))
                out = 0.0;
            #endif
            
            z2[channel] = z1[channel];
            z1[channel] = factorForB0;
            
            samples[i] = float (out);
        }
    }
}

void SecondOrderIIRFilter::reset()
{
    z1.clear (numberOfChannels);
    z2.clear (numberOfChannels);
}
