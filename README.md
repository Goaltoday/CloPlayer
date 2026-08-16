# CloPlayer

CloPlayer is an independent JUCE VST3 plug-in that plays Valeton GP-200 and Hotone/Ampero CLO models.

## Supported CLO layouts

- GP-200: FIR A = 128 taps, FIR B = 1024 taps.
- Hotone / Ampero: FIR A = 128 taps, FIR B = 2048 taps.

Both use the same reconstructed CLO playback core. The 2048-tap B section is played in full; it is not truncated to the GP-200 1024-tap runtime limit.

## DSP path

```text
Input
  -> Gain
  -> PRE biquad
  -> FIR A
  -> 4x oversampling
  -> asymmetric exponential waveshaper
  -> 4x downsampling
  -> POST biquad
  -> FIR B (1024 or 2048 taps)
  -> Volume
  -> Output
```

Bass / Middle / Treble are intentionally not applied.

## Gain and Volume

For simplicity, v1.2 uses the experimentally validated GP-200 control mapping for both GP-200 and Hotone/Ampero CLO files:

```text
internalGain = 0.69311597 * visibleGain + 25.201331
gainLinear   = exp(-3.986313819885254 + internalGain * 0.07972627133131027)
volumeLinear = exp(-3.986313819885254 + visibleVolume * 0.07972627133131027)
```

This is confirmed for GP-200 hardware. Applying the same wrapper mapping to Ampero is a deliberate compatibility/simplicity choice, not a hardware validation claim.

## Sample rate

The current VST3 processes CLO models only when the host runs at **44.1 kHz**. At another host sample rate it deliberately bypasses processing and the UI shows a warning.

The CLO core can be used in a 48/88.2/96 kHz DAW only if the plug-in adds an internal sample-rate conversion stage: host rate -> 44.1 kHz -> CLO core -> host rate. That is not included in v1.2 because it would add resampling latency and a new element that has not yet been validated against the hardware. Simply running the existing coefficients at another sample rate would shift the PRE/POST filters and all time/frequency behaviour, so it would not be the same model.

## Implementation notes

- CLO format is detected from the validated FIR layout, especially `countB`: 1024 = GP-200, 2048 = Hotone/Ampero.
- FIR A/B use direct causal convolution.
- Stereo channels use independent DSP state.
- Loaded CLO data is embedded in the plug-in state for DAW recall.
- The all-pass oversampling/downsampling coefficients used by the core were recovered from GP-200 firmware and independently found byte-for-byte in Ampero II Stage firmware.

## Build

Requirements: CMake 3.22+, C++20, JUCE 8.0.15 (fetched automatically unless `CLOPLAYER_JUCE_PATH` is provided).

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The VST3 will be under a path similar to:

```text
build\CloPlayer_artefacts\Release\VST3\CloPlayer.vst3
```

## License

GPL-3.0. See `LICENSE`.
