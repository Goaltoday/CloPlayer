# CloPlayer

CloPlayer is an independent JUCE VST3 plug-in that plays GP-200 SnapTone/CLO models using the playback structure reconstructed from GP-200 firmware V1.8.0 and validated against captures from a physical GP-200.

## Scope

The plug-in intentionally implements only:

```text
Input
  -> Gain
  -> PRE biquad
  -> FIR A
  -> 4x oversampling
  -> asymmetric exponential waveshaper
  -> 4x downsampling
  -> POST biquad
  -> FIR B
  -> Volume
  -> Output
```

Bass / Middle / Treble from the GP-200 SnapTone wrapper are **not applied**. This is deliberate.

## Current status

### Confirmed / implemented

- CLO header fields used by the GP-200 core (`0x18..0x88`).
- PRE and POST biquads.
- FIR A and FIR B.
- Asymmetric exponential waveshaper using `Ppos`, `Pneg`, `Kpos`, `Kneg` from the CLO.
- GP-200 V1.8.0 4x all-pass oversampling coefficients.
- Gain before the CLO core and Volume after it.
- The firmware exponential control law.
- Experimental GP-200 visible-Gain -> internal-DSP mapping, validated at visible Gain 25, 50, 75 and 100:

```text
internalGain = 0.69311597 * visibleGain + 25.201331
gainLinear   = exp(-3.986313819885254 + internalGain * 0.07972627133131027)

volumeLinear = exp(-3.986313819885254 + visibleVolume * 0.07972627133131027)
```

The Gain mapping is intentionally kept separate from the CLO model itself. It reproduces the nonlinear excitation observed on a physical GP-200. Volume is post-CLO and remains on the direct firmware law.

- Mono and stereo VST3 operation. Stereo channels use independent DSP state.
- The loaded CLO is embedded in the plug-in state, so DAW sessions restore it without needing the original file path.

### Deliberate limitations

- **44.1 kHz only in v1.0.** At any other host sample rate, CloPlayer passes audio through unchanged and the UI shows a warning. The native GP-200 CLO engine analysed and validated here runs at 44.1 kHz; no unvalidated host-rate conversion is silently inserted.
- FIR A/B currently use direct causal convolution. This is mathematically equivalent to the GP-200 partitioned FFT convolution but is not expected to be last-bit identical to the Cortex-M7 FFT/FMA implementation.
- `std::exp` is used rather than a bit-exact port of the firmware expf helper.
- The plug-in is a research reimplementation and is not affiliated with or endorsed by Valeton.

## Build requirements

- CMake 3.22+
- C++20 compiler
- Windows: Visual Studio 2022/2026 with Desktop C++ workload
- Internet access on first configure (JUCE 8.0.15 is fetched automatically), or provide a local JUCE checkout.

## Build on Windows

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The VST3 bundle will be under a path similar to:

```text
build\CloPlayer_artefacts\Release\VST3\CloPlayer.vst3
```

For a local JUCE checkout:

```bat
cmake -S . -B build -DCLOPLAYER_JUCE_PATH=D:\SDKs\JUCE
cmake --build build --config Release
```

## GitHub Actions

The included workflow builds the Windows x64 VST3 on every push and pull request and uploads `CloPlayer.vst3` as a workflow artifact.

## Use

1. Set the DAW project/sample rate to **44.1 kHz**.
2. Insert `CloPlayer.vst3` on an audio track.
3. Click **Load CLO...** and select a GP-200 CLO file.
4. Set **Gain** and **Volume** using the same visible 0..100 values as the GP-200. Gain is internally remapped to the measured GP-200 DSP scale; Volume stays on the direct firmware law.
5. Process audio normally.

## Research basis

The current DSP path is based on reverse engineering of GP-200 firmware V1.8.0 and physical-hardware validation with deterministic test signals and multiple CLO files. The architecture is considered strongly validated; exact last-bit identity remains a separate goal because it requires matching Cortex-M7 FFT ordering/FMA, runtime FFT tables, floating-point details and the firmware expf implementation.

## License

GPL-3.0. See `LICENSE`.
