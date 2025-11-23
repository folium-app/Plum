//
//  AudioOutput.h
//  Plum
//
//  Created by Jarrod Norwell on 12/11/2025.
//

#import <Foundation/Foundation.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>

#include "core/clowncommon/clowncommon.h"

#include "common/mixer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

class AudioDevice
{
private:
    const cc_u8f channels;
    const std::size_t SIZE_OF_FRAME = channels * sizeof(cc_s16l);

    SDL_AudioStream* stream;

public:
    AudioDevice(cc_u8f channels, cc_u32f sample_rate);
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    void QueueFrames(const cc_s16l *buffer, cc_u32f total_frames);
    cc_u32f GetTotalQueuedFrames();
    void SetPlaybackSpeed(const cc_u32f numerator, const cc_u32f denominator)
    {
        SDL_SetAudioStreamFrequencyRatio(stream, static_cast<float>(numerator) / denominator);
    }
};

class AudioOutput
{
private:
    AudioDevice device;
    cc_u32f total_buffer_frames;

    bool pal_mode = false;
    std::array<cc_u32f, 0x10> rolling_average_buffer = {0};
    cc_u8f rolling_average_buffer_index = 0;

    Mixer mixer = Mixer(pal_mode);

public:
    AudioOutput();
    void MixerBegin();
    void MixerEnd();
    cc_s16l* MixerAllocateFMSamples(std::size_t total_frames);
    cc_s16l* MixerAllocatePSGSamples(std::size_t total_frames);
    cc_s16l* MixerAllocatePCMSamples(std::size_t total_frames);
    cc_s16l* MixerAllocateCDDASamples(std::size_t total_frames);
    cc_u32f GetAverageFrames() const;
    cc_u32f GetTargetFrames() const { return std::max<cc_u32f>(total_buffer_frames * 2, MIXER_OUTPUT_SAMPLE_RATE / 20); } // 50ms
    cc_u32f GetTotalBufferFrames() const { return total_buffer_frames; }
    cc_u32f GetSampleRate() const { return MIXER_OUTPUT_SAMPLE_RATE; }

    void SetPALMode(bool enabled);
    bool GetPALMode() const { return pal_mode; }
};
