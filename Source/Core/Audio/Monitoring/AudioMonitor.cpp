/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "AudioMonitor.h"
#include "AudioCore.h"

class ClippingWarningAsyncCallback final : public AsyncUpdater
{
public:
    
    explicit ClippingWarningAsyncCallback(AudioMonitor &parentSpectrumCallback) :
        audioMonitor(parentSpectrumCallback) {}
    
    void handleAsyncUpdate() override
    {
        this->audioMonitor.getListeners().
            call(&AudioMonitor::ClippingListener::onClippingWarning);
    }

private:
    
    AudioMonitor &audioMonitor;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClippingWarningAsyncCallback)
};

class OversaturationWarningAsyncCallback final : public AsyncUpdater
{
public:
    
    explicit OversaturationWarningAsyncCallback(AudioMonitor &parentSpectrumCallback) :
        audioMonitor(parentSpectrumCallback) {}
    
    void handleAsyncUpdate() override
    {
        this->audioMonitor.getListeners().
            call(&AudioMonitor::ClippingListener::onOversaturationWarning);
    }
    
private:
    
    AudioMonitor &audioMonitor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OversaturationWarningAsyncCallback)
};

AudioMonitor::AudioMonitor() : fft()
{
    this->asyncClippingWarning = make<ClippingWarningAsyncCallback>(*this);
    this->asyncOversaturationWarning = make<OversaturationWarningAsyncCallback>(*this);
}

//===----------------------------------------------------------------------===//
// AudioIODeviceCallback
//===----------------------------------------------------------------------===//

void AudioMonitor::audioDeviceAboutToStart(AudioIODevice *device)
{
    this->sampleRate = device->getCurrentSampleRate();
}

void AudioMonitor::audioDeviceIOCallback(const float **inputChannelData, int numInputChannels,
    float **outputChannelData, int numOutputChannels, int numSamples)
{
    const int minNumChannels = jmin(AudioMonitor::numChannels, numOutputChannels);
    
    for (int channel = 0; channel < minNumChannels; ++channel)
    {
        this->fft.computeSpectrum(outputChannelData[channel], 0, numSamples,
            this->spectrum[channel], AudioMonitor::spectrumSize,
            channel, numOutputChannels);
    }
    
    for (int channel = 0; channel < minNumChannels; ++channel)
    {
        float pcmSquaresSum = 0.f;
        float pcmPeak = 0.f;
        for (int samplePosition = 0; samplePosition < numSamples; ++samplePosition)
        {
            const float &pcmData = outputChannelData[channel][samplePosition];
            pcmSquaresSum += (pcmData * pcmData);
            pcmPeak = jmax(pcmPeak, pcmData);
        }
        
        const float rootMeanSquare = sqrtf(pcmSquaresSum / numSamples);
        this->rms[channel] = rootMeanSquare;
        this->peak[channel] = pcmPeak;
        
        if (pcmPeak > AudioMonitor::clipThreshold)
        {
            this->asyncClippingWarning->triggerAsyncUpdate();
        }
        
        if (pcmPeak > AudioMonitor::oversaturationThreshold &&
            (pcmPeak / rootMeanSquare) > AudioMonitor::oversaturationRate)
        {
            this->asyncOversaturationWarning->triggerAsyncUpdate();
        }
    }

    for (int i = 0; i < numOutputChannels; ++i)
    {
        FloatVectorOperations::clear(outputChannelData[i], numSamples);
    }
}

//===----------------------------------------------------------------------===//
// Spectrum data
//===----------------------------------------------------------------------===//

float AudioMonitor::getInterpolatedSpectrumAtFrequency(float frequency) const
{
    const float resolution = 
        float(this->sampleRate.get() / 2.f) / float(AudioMonitor::spectrumSize);
    
    const int index1 = roundToInt(frequency / resolution);
    const int safeIndex1 = jlimit(0, AudioMonitor::spectrumSize, index1);
    const float f1 = index1 * resolution;
    const float y1 = (this->spectrum[0][safeIndex1].get() +
                      this->spectrum[1][safeIndex1].get()) / 2.f;
    
    const int index2 = index1 + 1;
    const int safeIndex2 = jlimit(0, AudioMonitor::spectrumSize, index2);
    const float f2 = index2 * resolution;
    const float y2 = (this->spectrum[0][safeIndex2].get() +
                      this->spectrum[1][safeIndex2].get()) / 2.f;
    
    return y1 + ((AudioCore::fastLog10(frequency) - AudioCore::fastLog10(f1)) /
                 (AudioCore::fastLog10(f2) - AudioCore::fastLog10(f1))) * (y2 - y1);
}

//===----------------------------------------------------------------------===//
// Clipping data
//===----------------------------------------------------------------------===//

void AudioMonitor::addClippingListener(ClippingListener *const listener)
{
    this->clippingListeners.add(listener);
}

void AudioMonitor::removeClippingListener(ClippingListener *const listener)
{
    this->clippingListeners.remove(listener);
}

ListenerList<AudioMonitor::ClippingListener> &AudioMonitor::getListeners() noexcept
{
    return this->clippingListeners;
}

//===----------------------------------------------------------------------===//
// Volume data
//===----------------------------------------------------------------------===//

float AudioMonitor::getPeak(int channel) const
{
    return this->peak[channel].get();
}

float AudioMonitor::getRootMeanSquare(int channel) const
{
    return this->rms[channel].get();
}
