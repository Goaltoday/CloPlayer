#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gp200
{

// Functional GP-200 SnapTone/CLO player for firmware 1.8.0 research.
//
// Deliberately implements only:
//   Gain -> CLO core -> Volume
//
// The external SnapTone Bass/Middle/Treble EQ is intentionally NOT applied.
// FIR A/B use direct convolution rather than the GP-200 partitioned FFT. This
// is mathematically equivalent but is not intended to be bit-identical to the
// Cortex-M7 implementation.
class GP200CloPlayer final
{
public:
    GP200CloPlayer() = default;

    juce::Result loadFromFile (const juce::File& cloFile);
    juce::Result loadFromMemory (const void* data, std::size_t size);

    void reset();

    bool isLoaded() const noexcept { return loaded; }

    // GP-200 SnapTone controls, nominal range 0..100. 50 is approximately
    // unity. These are external to the CLO model itself.
    void setGainControl (float value) noexcept;
    void setVolumeControl (float value) noexcept;

    float getGainControl() const noexcept   { return gainControl; }
    float getVolumeControl() const noexcept { return volumeControl; }

    float getGainLinear() const noexcept;
    float getVolumeLinear() const noexcept;

    // Process mono samples in place. The native CLO model is 44.1 kHz.
    void processMono (float* samples, int numSamples) noexcept;

    // Convenience helper. Every channel is processed independently with its
    // own state by resetting between channels, so this should only be used for
    // offline/reference work. Real-time stereo use should instantiate one
    // player per channel.
    void processBufferOffline (juce::AudioBuffer<float>& buffer) noexcept;

    struct ModelInfo
    {
        std::uint32_t startA{};
        std::uint32_t countA{};
        std::uint32_t startB{};
        std::uint32_t countB{};
        float pPos{};
        float pNeg{};
        float kPos{};
        float kNeg{};
    };

    const ModelInfo& getModelInfo() const noexcept { return info; }

private:
    struct Biquad
    {
        double b0{1.0}, b1{}, b2{}, a1{}, a2{};
        double z1{}, z2{};

        void reset() noexcept { z1 = z2 = 0.0; }
        float process (float x) noexcept;
    };

    struct FirstOrderAllPass
    {
        float a{};
        float state{};

        void reset() noexcept { state = 0.0f; }
        float process (float x) noexcept
        {
            const float y = state + a * x;
            state = x - a * y;
            return y;
        }
    };

    struct Polyphase2x
    {
        std::vector<FirstOrderAllPass> branch0;
        std::vector<FirstOrderAllPass> branch1;
        float delayedBranch1{};

        void configure (const float* b0, int n0, const float* b1, int n1);
        void reset() noexcept;
        void upsample (float x, float& even, float& odd) noexcept;
        float downsample (float even, float odd) noexcept;

    private:
        static float run (std::vector<FirstOrderAllPass>& stages, float x) noexcept;
    };

    struct Fir
    {
        std::vector<float> taps;
        std::vector<float> history;
        std::size_t index{};

        void setTaps (const float* values, std::size_t count);
        void reset() noexcept;
        float process (float x) noexcept;
    };

    static float controlToLinear (float control) noexcept;
    static double readDoubleLE (const std::uint8_t* p) noexcept;
    static float readFloatLE (const std::uint8_t* p) noexcept;
    static std::uint32_t readU32LE (const std::uint8_t* p) noexcept;

    float processCoreSample (float x) noexcept;
    float waveshape (float x) const noexcept;

    bool loaded{};
    ModelInfo info{};
    Biquad pre;
    Biquad post;
    Fir firA;
    Fir firB;
    Polyphase2x up1;
    Polyphase2x up2;
    Polyphase2x down1;
    Polyphase2x down2;

    float gainControl{50.0f};
    float volumeControl{50.0f};
};

} // namespace gp200
