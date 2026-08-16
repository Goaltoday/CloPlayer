#include "GP200CloPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gp200
{
namespace
{
constexpr std::size_t minimumHeaderSize = 0x88;
constexpr std::size_t coefficientBase = 0x88;
constexpr std::uint32_t maximumSupportedB = 2048;

// Coefficients recovered from GP-200 firmware 1.8.0.
constexpr float up1Branch0[] = {
    0.045728147029876709f,
    0.3325011134147644f,
    0.66320204734802246f,
    0.93385583162307739f
};
constexpr float up1Branch1[] = {
    0.16808754205703735f,
    0.50448572635650635f,
    0.80378085374832153f
};

constexpr float up2Branch0[] = {
    0.054230779409408569f,
    0.39879697561264038f,
    0.86291784048080444f
};
constexpr float up2Branch1[] = {
    0.19969958066940308f,
    0.62109684944152832f
};

constexpr float down1Branch0[] = {
    0.070765949785709381f,
    0.51316756010055542f
};
constexpr float down1Branch1[] = {
    0.25785309076309204f,
    0.81731736660003662f
};

constexpr float down2Branch0[] = {
    0.054217524826526642f,
    0.38308733701705933f,
    0.74872094392776489f
};
constexpr float down2Branch1[] = {
    0.19679796695709229f,
    0.57313638925552368f,
    0.91429370641708374f
};

template <typename T, std::size_t N>
constexpr int arrayCount (const T (&)[N]) noexcept { return static_cast<int> (N); }
}

float GP200CloPlayer::Biquad::process (float x) noexcept
{
    // Transposed direct-form II using double state/arithmetic. The firmware
    // contains reciprocal x1000/x0.001 scaling around its recurrence; those
    // factors cancel functionally and are intentionally omitted here.
    const double xd = static_cast<double> (x);
    const double y = b0 * xd + z1;
    z1 = b1 * xd - a1 * y + z2;
    z2 = b2 * xd - a2 * y;
    return static_cast<float> (y);
}

void GP200CloPlayer::Polyphase2x::configure (
    const float* b0, int n0, const float* b1, int n1)
{
    branch0.resize (static_cast<std::size_t> (n0));
    branch1.resize (static_cast<std::size_t> (n1));

    for (int i = 0; i < n0; ++i)
        branch0[static_cast<std::size_t> (i)].a = b0[i];

    for (int i = 0; i < n1; ++i)
        branch1[static_cast<std::size_t> (i)].a = b1[i];

    reset();
}

void GP200CloPlayer::Polyphase2x::reset() noexcept
{
    for (auto& s : branch0) s.reset();
    for (auto& s : branch1) s.reset();
    delayedBranch1 = 0.0f;
}

float GP200CloPlayer::Polyphase2x::run (
    std::vector<FirstOrderAllPass>& stages, float x) noexcept
{
    for (auto& stage : stages)
        x = stage.process (x);
    return x;
}

void GP200CloPlayer::Polyphase2x::upsample (
    float x, float& even, float& odd) noexcept
{
    // Half-band polyphase interpolation: each input sample is evaluated by
    // one all-pass cascade per phase, then the phases are interleaved.
    even = run (branch0, x);
    odd  = run (branch1, x);
}

float GP200CloPlayer::Polyphase2x::downsample (float even, float odd) noexcept
{
    // Coupled-allpass decimator. Firmware 1.8.0 explicitly applies 0.5f.
    // The second polyphase branch contributes with the one-sample phase delay.
    const float a = run (branch0, even);
    const float b = run (branch1, odd);
    const float y = 0.5f * (a + delayedBranch1);
    delayedBranch1 = b;
    return y;
}

void GP200CloPlayer::Fir::setTaps (const float* values, std::size_t count)
{
    taps.assign (values, values + count);
    history.assign (count, 0.0f);
    index = 0;
}

void GP200CloPlayer::Fir::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    index = 0;
}

float GP200CloPlayer::Fir::process (float x) noexcept
{
    if (taps.empty())
        return x;

    history[index] = x;

    double sum = 0.0;
    std::size_t h = index;
    for (std::size_t k = 0; k < taps.size(); ++k)
    {
        sum += static_cast<double> (taps[k]) * static_cast<double> (history[h]);
        h = (h == 0 ? history.size() - 1 : h - 1);
    }

    index = (index + 1) % history.size();
    return static_cast<float> (sum);
}

std::uint32_t GP200CloPlayer::readU32LE (const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t> (p[0])
        | (static_cast<std::uint32_t> (p[1]) << 8u)
        | (static_cast<std::uint32_t> (p[2]) << 16u)
        | (static_cast<std::uint32_t> (p[3]) << 24u);
}

float GP200CloPlayer::readFloatLE (const std::uint8_t* p) noexcept
{
    const auto bits = readU32LE (p);
    float value{};
    std::memcpy (&value, &bits, sizeof (value));
    return value;
}

double GP200CloPlayer::readDoubleLE (const std::uint8_t* p) noexcept
{
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i)
        bits |= static_cast<std::uint64_t> (p[i]) << (8u * static_cast<unsigned> (i));

    double value{};
    std::memcpy (&value, &bits, sizeof (value));
    return value;
}

juce::Result GP200CloPlayer::loadFromFile (const juce::File& cloFile)
{
    if (!cloFile.existsAsFile())
        return juce::Result::fail ("CLO file does not exist.");

    juce::MemoryBlock data;
    if (!cloFile.loadFileAsData (data))
        return juce::Result::fail ("Could not read CLO file.");

    return loadFromMemory (data.getData(), data.getSize());
}

juce::Result GP200CloPlayer::loadFromMemory (const void* rawData, std::size_t size)
{
    loaded = false;

    if (rawData == nullptr || size < minimumHeaderSize)
        return juce::Result::fail ("CLO data is too small.");

    const auto* p = static_cast<const std::uint8_t*> (rawData);
    const bool validMagic =
        (p[0] == 'V' && p[1] == 'T' && p[2] == 'S' && p[3] == 'I') ||
        (p[0] == 'H' && p[1] == 'T' && p[2] == 'S' && p[3] == 'I');

    if (!validMagic)
        return juce::Result::fail ("Unsupported CLO header; expected VTSI or HTSI.");

    pre.b0 = readDoubleLE (p + 0x18);
    pre.b1 = readDoubleLE (p + 0x20);
    pre.b2 = readDoubleLE (p + 0x28);
    pre.a1 = readDoubleLE (p + 0x30);
    pre.a2 = readDoubleLE (p + 0x38);

    post.b0 = readDoubleLE (p + 0x40);
    post.b1 = readDoubleLE (p + 0x48);
    post.b2 = readDoubleLE (p + 0x50);
    post.a1 = readDoubleLE (p + 0x58);
    post.a2 = readDoubleLE (p + 0x60);

    info.pPos = readFloatLE (p + 0x68);
    info.pNeg = readFloatLE (p + 0x6c);
    info.kPos = readFloatLE (p + 0x70);
    info.kNeg = readFloatLE (p + 0x74);
    info.startA = readU32LE (p + 0x78);
    info.countA = readU32LE (p + 0x7c);
    info.startB = readU32LE (p + 0x80);
    info.countB = readU32LE (p + 0x84);

    if (info.countA == 0 || info.countB == 0)
        return juce::Result::fail ("CLO contains an empty FIR section.");

    // The validated Valeton/Hotone CLO family uses the same core layout with
    // A=128 taps. GP-200 stores B=1024 while Ampero/Hotone CLO files use
    // B=2048. Keep the accepted layouts explicit rather than accepting an
    // arbitrary FIR length.
    if (info.startA != 0 || info.countA != 128 || info.startB != 128)
        return juce::Result::fail ("Unsupported CLO FIR layout; expected A=128 followed by B.");

    if (info.countB != 1024 && info.countB != 2048)
        return juce::Result::fail ("Unsupported CLO B length; expected 1024 (GP-200) or 2048 (Hotone/Ampero) taps.");

    if (info.countB > maximumSupportedB)
        return juce::Result::fail ("CLO B section exceeds the supported 2048-tap maximum.");

    const std::uint64_t lastA = static_cast<std::uint64_t> (info.startA) + info.countA;
    const std::uint64_t lastB = static_cast<std::uint64_t> (info.startB) + info.countB;
    const std::uint64_t requiredCoefficients = std::max (lastA, lastB);
    const std::uint64_t requiredSize = coefficientBase + requiredCoefficients * sizeof (float);

    if (requiredSize > size)
        return juce::Result::fail ("CLO coefficient arrays are truncated.");

    std::vector<float> coefficients (static_cast<std::size_t> (requiredCoefficients));
    for (std::size_t i = 0; i < coefficients.size(); ++i)
        coefficients[i] = readFloatLE (p + coefficientBase + i * sizeof (float));

    firA.setTaps (coefficients.data() + info.startA, info.countA);
    firB.setTaps (coefficients.data() + info.startB, info.countB);

    up1.configure (up1Branch0, arrayCount (up1Branch0), up1Branch1, arrayCount (up1Branch1));
    up2.configure (up2Branch0, arrayCount (up2Branch0), up2Branch1, arrayCount (up2Branch1));
    down1.configure (down1Branch0, arrayCount (down1Branch0), down1Branch1, arrayCount (down1Branch1));
    down2.configure (down2Branch0, arrayCount (down2Branch0), down2Branch1, arrayCount (down2Branch1));

    loaded = true;
    reset();
    return juce::Result::ok();
}

void GP200CloPlayer::reset()
{
    pre.reset();
    post.reset();
    firA.reset();
    firB.reset();
    up1.reset();
    up2.reset();
    down1.reset();
    down2.reset();
}

void GP200CloPlayer::setGainControl (float value) noexcept
{
    gainControl = juce::jlimit (0.0f, 100.0f, value);
}

void GP200CloPlayer::setVolumeControl (float value) noexcept
{
    volumeControl = juce::jlimit (0.0f, 100.0f, value);
}

float GP200CloPlayer::gainControlToLinear (float visibleControl) noexcept
{
    // GP-200 V1.8.0: the SnapTone wrapper applies an exponential law to an
    // internal gain-control value. Physical-GP-200 measurements with the same
    // CLO at visible Gain 25/50/75/100 show that the visible UI value is first
    // mapped to that internal value by an essentially linear transform:
    //
    // internalGain = 0.69311597 * visibleGain + 25.201331
    //
    // Measured equivalents:
    //   UI 25  -> internal 42.5287
    //   UI 50  -> internal 59.8574
    //   UI 75  -> internal 77.1860
    //   UI 100 -> internal 94.5122
    //
    // Then the firmware wrapper law is applied:
    // linear = exp(-3.986313819885254 + internalGain * 0.07972627133131027)
    constexpr float uiToInternalSlope  = 0.69311597f;
    constexpr float uiToInternalOffset = 25.201331f;
    constexpr float firmwareOffset     = -3.986313819885254f;
    constexpr float firmwareSlope      =  0.07972627133131027f;

    const float internalGain = uiToInternalSlope * visibleControl + uiToInternalOffset;
    return std::exp (firmwareOffset + internalGain * firmwareSlope);
}

float GP200CloPlayer::volumeControlToLinear (float control) noexcept
{
    // Volume is kept on the direct firmware control law. It is post-CLO and
    // does not alter the nonlinear excitation, so any remaining absolute-level
    // offset can be compensated independently during validation.
    constexpr float offset = -3.986313819885254f;
    constexpr float slope  =  0.07972627133131027f;
    return std::exp (offset + control * slope);
}

float GP200CloPlayer::getGainLinear() const noexcept
{
    return gainControlToLinear (gainControl);
}

float GP200CloPlayer::getVolumeLinear() const noexcept
{
    return volumeControlToLinear (volumeControl);
}

float GP200CloPlayer::waveshape (float x) const noexcept
{
    if (x > 0.0f)
        return info.pPos * (1.0f - std::exp (-info.kPos * x));

    return info.pNeg * (std::exp (info.kNeg * x) - 1.0f);
}

float GP200CloPlayer::processCoreSample (float x) noexcept
{
    x = pre.process (x);
    x = firA.process (x);

    float u1e{}, u1o{};
    up1.upsample (x, u1e, u1o);

    float q0{}, q1{}, q2{}, q3{};
    up2.upsample (u1e, q0, q1);
    up2.upsample (u1o, q2, q3);

    q0 = waveshape (q0);
    q1 = waveshape (q1);
    q2 = waveshape (q2);
    q3 = waveshape (q3);

    const float d1e = down1.downsample (q0, q1);
    const float d1o = down1.downsample (q2, q3);
    x = down2.downsample (d1e, d1o);

    x = post.process (x);
    x = firB.process (x);
    return x;
}

void GP200CloPlayer::processMono (float* samples, int numSamples) noexcept
{
    if (!loaded || samples == nullptr || numSamples <= 0)
        return;

    const float inputGain = getGainLinear();
    const float outputGain = getVolumeLinear();

    for (int i = 0; i < numSamples; ++i)
        samples[i] = processCoreSample (samples[i] * inputGain) * outputGain;
}

void GP200CloPlayer::processBufferOffline (juce::AudioBuffer<float>& buffer) noexcept
{
    if (!loaded)
        return;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        reset();
        processMono (buffer.getWritePointer (channel), buffer.getNumSamples());
    }
}

} // namespace gp200
