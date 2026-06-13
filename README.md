# Ambience

![Release](https://img.shields.io/badge/release-v1.1.0-blue)
![License](https://img.shields.io/badge/license-AGPLv3-green)
![JUCE](https://img.shields.io/badge/JUCE-8.0.x-blue)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)
![Downloads](https://img.shields.io/github/downloads/OTODESK4193/Ambience1.0.1/total.svg)

##
<img src="Source/Assets/Screenshot1.jpg" width="600">

## Changelog

### v1.1.0

**Bug Fixes:**
- **PreDelay fix**: Fixed PreDelay parameter not being applied to DSP. Now correctly feeds both ER and FDN paths.
- **Metallic comb-filter artifact fix**: Addressed metallic ringing at long DecayTime values with four countermeasures: DC blocker in FDN loop, decay-dependent micro-saturation blend, modulation depth scaling, and dynamic nested allpass modulation.
- **Preset PRO Mode fix**: Fixed PRO Mode state being incorrectly restored on preset load. Now always resets to Normal mode.

**Sound Quality Improvements:**
- **Chorus-style pitch modulation**: Added sine-wave LFO (ChorusLFO) per FDN channel with golden-ratio phase/rate distribution, layered on top of the existing noise LFO for richer, more organic tail texture.
- **3-stage serial allpass chain**: Expanded nested allpass from 1 stage to 3 serial stages per FDN channel with varied delay times and modulation depths, greatly increasing late-field echo density.
- **ER→Late reverb transition smoothing**: Early reflection output is now fed into the FDN input at 15% blend, simulating the natural transition from early reflections to late reverberation.
- **Frequency-dependent modulation**: Modulation depth now scales per FDN channel (1.5× for short-delay/HF channels, 0.5× for long-delay/LF channels), matching the physical behavior of air turbulence.
- **Soft-knee RMS compression in FDN loop**: Added per-channel RMS envelope follower with soft-knee compression (threshold 0.35), providing transparent level control without harmonic distortion.
- **Thiran allpass fractional delay interpolation**: Replaced linear interpolation with 1st-order Thiran allpass for FDN main delay lines, achieving flat magnitude response (|H(ω)|=1) and preserving high-frequency clarity in the feedback loop.

**CPU Optimizations:**
- Replaced `std::sin()` in chorus LFO with parabolic sine approximation (5-10× faster, <0.1% error).
- Moved `std::sqrt()` in soft-knee compression inside threshold branch (only computed when compression is active).
- Precomputed all loop-invariant values: frequency-dependent modulation scales, input diffuser delays, allpass base delays (16ch × 3 stages), ER tap gains, and allpass gain stage.
- Cached sample rate as float to eliminate repeated double→float casts in the hot path.

## Overview

**Ambience** is a high-quality, open-source algorithmic reverb VST3 plugin built on a **16-channel Feedback Delay Network (FDN)** architecture. Designed with professional audio standards in mind, it delivers rich, natural-sounding reverberation ranging from intimate studio rooms to vast concert halls and beyond — with the precision and stability demanded by real-world production environments.

Ambience ships with **21 factory presets** modeled after some of the world's most iconic acoustic spaces, including Abbey Road Studio 1 & 2, Vienna Musikverein, Amsterdam Concertgebouw, Boston Symphony Hall, Carnegie Hall, and more. Whether you need a tight drum room, a lush orchestral hall, a vintage plate, or an infinite ambient space, Ambience covers it all.

👉 **[Watch the Demo Video (動作デモ動画はこちら！)](https://x.com/kijyoumusic/status/2055967062325944741?s=20)**


## Key Features

### 🏛️ 16-Channel FDN Reverb Engine

A research-grade Feedback Delay Network forms the acoustic core of Ambience:

* **16-channel FWHT Feedback Matrix:** Fast Walsh-Hadamard Transform ensures dense, colorless diffusion with optimal mode distribution.
* **Nearest-Prime Delay Allocation:** Delay lines are tuned to unique prime numbers distributed on a logarithmic scale, guaranteeing mutual coprimality across all 16 channels and eliminating comb-filter artifacts at any room size.
* **7 Reverb Algorithms:** ROOM1 / ROOM2 / HALL1 / HALL2 / PLATE / SPRING / GOLDFOIL — each with distinct topological routing, Allpass gain, and ER patterns.

### 🎛️ Professional DSP Modules

* **Stage 2 GEQ Absorption (Välimäki-Liski):** A 10-band biquad Graphic EQ cascade per FDN channel, solving a Weighted Least Squares system to achieve accurate, frequency-dependent RT60 targets across the full audible spectrum.
* **ISM-Based Early Reflections:** Image Source Method patterns tuned per algorithm, providing perceptually accurate pre-echo with full stereo imaging control.
* **BandlimitedNoise LFO:** Each of the 16 FDN channels is driven by an independent, mutually asynchronous low-frequency oscillator using a white noise source filtered by a 1st-order IIR — initialized via the Golden Ratio Weyl sequence to guarantee non-periodic modulation.
* **ADAA Saturator (4 Modes):** Anti-Derivative Anti-Aliasing saturation applied to the wet path. Modes: **Warm** (Vicanek x/√(1+x²)), **Tape** (Padé rational polynomial), **Tube** (asymmetric ADAA for even-order harmonics), **Hard** (hard clip + ADAA).
* **Micro-Saturation (FDN Loop):** An internal Padé saturator in the FDN feedback loop acts as a safety limiter, suppressing limit cycles without audible coloration.
* **Output EQ:** Linkwitz-Riley 12 dB/oct Lo Cut (20–500 Hz) and Hi Cut (1 kHz–20 kHz) applied exclusively to the wet path.
* **Brick-Wall Output Limiter:** -0.5 dBFS brick-wall limiter as the final stage, transparent under normal use.
* **Ducking:** Sidechain-style envelope follower with independent Threshold, Amount, Attack, and Release controls.

### 📊 Real-Time Visualizers

* **RT60 Graph:** Displays 10-band RT60 curves (31 Hz – 16 kHz) in logarithmic scale. The orange curve shows the actual effective RT60 (including HF Damping and LF Absorption), while the gray curve shows the raw preset reference. The Y-axis dynamically scales to the current decay time.
* **Decay Curve Visualizer:** A split time-axis display showing Early Reflections (0–200 ms, expanded 2×) and Late Reverb decay curve side-by-side, with color-coded ER tap markers and envelope fill.
* **Acoustic Metrics:** Live readout of D50 (%), C50 (dB), C80 (dB), and EDT (s) derived from real-time signal analysis.

### 🎚️ Pro Mode

##
<img src="Source/Assets/Screenshot2.jpg" width="600">

Unlock deep per-band control:

* **10-Band RT60 Multiplier:** Fine-tune the RT60 curve at each octave band (31 Hz – 16 kHz) independently.
* **Tilt EQ × 3:** Broad spectral tilt controls for Low / Mid / High regions.
* **Saturation Type Selector:** Choose from Warm / Tape / Tube / Hard within Pro Mode.
* **Output EQ:** Lo Cut and Hi Cut knobs available in both Normal and Pro Mode panels.

### 💾 Preset Management

* **File-Based Preset System:** Presets are saved as `.ambpreset` files — a standard binary XML format compatible with JUCE's state management system.
* **21 Factory Presets:** Modeled after real-world acoustic spaces across all 7 reverb algorithm types.
* **SAVE / LOAD / DELETE / PREV / NEXT:** Full preset browser UI integrated directly into the main panel.
* **Shareable:** Simply share `.ambpreset` files directly with other users — no installation required.

### ⚡ Real-Time Safety & DAW Compatibility

Built to the strictest real-time audio standards, with specific hardening for Ableton Live:

* **Zero Heap Allocation on Audio Thread:** All buffers pre-allocated in `prepareToPlay()`. No `new` / `malloc` / `std::vector::resize()` calls in `processBlock()`.
* **Dirty-Flag Parameter Dispatch:** `setParams()` is called only when DSP parameters change, preventing the expensive Stage 2 GEQ WLS matrix computation (16 ch × 10-band LDLT) from running unnecessarily every buffer.
* **Ableton Live Sample-Rate Jitter Protection:** A mismatch guard at the top of `processBlock()` detects asynchronous sample-rate changes (a known Ableton Live edge case) and triggers a safe internal reset.
* **ScopedNoDenormals:** Applied at the entry of every `processBlock()` call to suppress denormal CPU spikes.
* **SmoothedValue Gain:** Wet and Dry gain changes are interpolated sample-accurately to eliminate zipper noise under automation.
* **Safe Editor Destruction:** `stopTimer()` and `setLookAndFeel(nullptr)` are called explicitly in the Editor destructor to prevent Ableton Live cleanup-order crashes.


## Preset Guide

### Factory Presets (21)

Ambience ships with 21 factory presets organized by reverb algorithm type:

| Algorithm | Preset Name | Reference Space |
|---|---|---|
| ROOM1 | Abbey Road Studio2 | Abbey Road Studio 2, London |
| ROOM1 | Drums in a Box | Small dead studio room |
| ROOM1 | Tracking Room | Medium tracking room |
| ROOM2 | Abbey Road Studio1 | Abbey Road Studio 1 (large hall) |
| ROOM2 | Capitol Studio A | Capitol Studios Studio A, Los Angeles |
| ROOM2 | Skywalker Sound | Skywalker Sound, California |
| HALL1 | Carnegie Hall | Carnegie Hall, New York |
| HALL1 | Tokyo Opera City | Tokyo Opera City Concert Hall |
| HALL1 | Berlin Konzerthaus | Konzerthaus Berlin |
| HALL2 | Vienna Musikverein | Wiener Musikverein Goldener Saal |
| HALL2 | Boston Symphony | Boston Symphony Hall |
| HALL2 | Concertgebouw | Amsterdam Concertgebouw |
| PLATE | EMT140 Vocal | EMT 140 plate — vocal setting |
| PLATE | EMT140 Snare | EMT 140 plate — snare setting |
| PLATE | Dark Plate | Dark plate, enhanced low end |
| SPRING | Surf Guitar | Guitar amp spring tank |
| SPRING | Vintage Studio | Fender-style spring tank |
| SPRING | Deep Tank | AKG BX-20 style deep spring |
| GOLDFOIL | Gothic Cathedral | Cologne Cathedral reference |
| GOLDFOIL | Stone Chamber | Stone underground chamber |
| GOLDFOIL | Infinite Space | Synthetic infinite ambient space |

### Preset File Location

Factory presets and user presets are stored at:

```
Windows: C:\Users\<YourName>\Documents\Ambience\Presets\
```

Each preset is saved as a `.ambpreset` file (binary XML containing all plugin parameters).

### Sharing Presets

To share presets with other users:

1. Navigate to `Documents\Ambience\Presets\`
2. Copy the desired `.ambpreset` file(s)
3. Send the file(s) to the recipient

Recipients simply place the `.ambpreset` files into their own `Documents\Ambience\Presets\` folder. The presets will appear automatically in the Ambience preset browser on the next plugin load.

> **Tip:** You can also ZIP the entire `Presets` folder to share your complete preset library at once.

### Preset Compatibility

Presets use JUCE's standard state serialization. Future versions of Ambience will remain forward-compatible — unknown parameters in older presets are silently ignored, and missing parameters fall back to their default values.


## Parameter Reference

### Normal Mode

| Parameter | Range | Default | Description |
|---|---|---|---|
| PRE-DELAY | 0 – 500 ms | 10 ms | Pre-delay before reverb onset |
| ROOM SIZE | 0.3 – 2.0 | 1.0 | Scales FDN delay times (room volume) |
| DECAY | 0.1 – 20.0 s | 1.5 s | Mid-band RT60 target |
| HF DAMP | 0.0 – 1.0 | 0.0 | High-frequency absorption (reduces high RT60) |
| LF ABSORB | 0.0 – 1.0 | 0.0 | Low-frequency absorption (reduces low RT60) |
| DIFFUSION | 0.0 – 1.0 | 0.7 | Controls Input Diffuser and Nested Allpass gain |
| MOD AMT | 0.0 – 1.0 | 0.25 | LFO modulation depth |
| MOD RATE | 0.05 – 2.0 Hz | 0.5 Hz | LFO base frequency |
| WIDTH | 0.0 – 1.0 | 0.8 | Stereo width (L/R decorrelation) |
| ER LEVEL | 0.0 – 1.0 | 0.6 | Early reflections level |
| SATURATE | 0.0 – 1.0 | 0.0 | Wet path saturation amount |
| WET | -60 – 0 dB | -4 dB | Wet signal level (-1 dB internal offset applied) |
| DRY | -60 – 0 dB | 0 dB | Dry signal level (unprocessed) |
| LO CUT | 20 – 500 Hz | 20 Hz | Wet-path high-pass filter (bypass at 20 Hz) |
| HI CUT | 1k – 20k Hz | 20 kHz | Wet-path low-pass filter (bypass at 20 kHz) |
| AMOUNT | 0 – 20 dB | 0 dB | Ducking reduction amount |
| THRESH | -60 – 0 dB | -20 dB | Ducking threshold |
| ATTACK | 0.5 – 100 ms | 10 ms | Ducking envelope attack |
| RELEASE | 10 – 2000 ms | 200 ms | Ducking envelope release |

### Pro Mode (additional)

| Parameter | Range | Default | Description |
|---|---|---|---|
| RT 31–16k Hz | 0.5 – 2.0× | 1.0× | Per-band RT60 multiplier (10 bands) |
| TILT LOW | 0.5 – 2.0× | 1.0× | RT60 multiplier for low bands (31–125 Hz) |
| TILT MID | 0.5 – 2.0× | 1.0× | RT60 multiplier for mid bands (250 Hz–2 kHz) |
| TILT HIGH | 0.5 – 2.0× | 1.0× | RT60 multiplier for high bands (4–16 kHz) |
| SAT TYPE | Warm/Tape/Tube/Hard | Warm | Saturation character |

### Special Controls

| Control | Description |
|---|---|
| PRO | Toggle Pro Mode panel (RT60 bands + Tilt EQ) |
| ER SOLO | Solo the Early Reflections path (Late = silent) |
| Algorithm Buttons | Select reverb topology: ROOM1/2, HALL1/2, PLATE, SPRING, GOLDFOIL |


## Installation

1. Download the latest `Ambience.vst3` from the [Releases](https://github.com/OTODESK4193/Ambience1.0.1/releases/latest) page.
2. Copy the `.vst3` folder to your VST3 plugin directory:
   ```
   C:\Program Files\Common Files\VST3\
   ```
3. (Optional) Download the factory preset pack and extract to:
   ```
   C:\Users\<YourName>\Documents\Ambience\Presets\
   ```
4. Rescan your plugins in Ableton Live (or your DAW of choice).

## 📚 User Guide

A comprehensive manual covering detailed technical specifications and operational guidelines is included with this repository.

[ ![Manual PDF (JP)](https://img.shields.io/badge/Manual-PDF_(JP)-red?style=for-the-badge&logo=adobe-acrobat-reader) ](Source/Assets/Ambience_UserManual_JP.pdf)

[ ![Manual PDF (EN)](https://img.shields.io/badge/Manual-PDF_(EN)-red?style=for-the-badge&logo=adobe-acrobat-reader) ](Source/Assets/Ambience_UserManual_EN.pdf)

### Requirements

* **JUCE** 8.0.x — place at `C:/JUCE` or update the path in `CMakeLists.txt`
* **CMake** 3.22 or higher
* **Visual Studio** 2022 (MSVC, C++20)
* **AVX2-capable CPU** (required for SIMD optimizations)


## System Requirements

* **OS:** Windows 10 / Windows 11 (64-bit)
* **Format:** VST3 / Standalone
* **CPU:** AVX2 support required
* **Tested Host:** Ableton Live 11 / 12

> ⚠️ **Compatibility Notice:** This plugin is compiled and optimized exclusively for Windows with AVX2. Verified operation is confirmed in **Ableton Live**. Other DAWs (FL Studio, Bitwig, Studio One, Cubase, etc.) may work but are currently unverified. Use at your own risk outside of Ableton Live.


## Technical Architecture

```
Input (Stereo L/R)
    │
    ├─► [Mid/Side Split]
    │
    ├─► [Input Diffuser × 4 stages] (Hall / Goldfoil)
    │
    ├─► [ISM Early Reflections] ──────────────────────────┐
    │                                                      │
    └─► [16-ch FDN]                                        │
         ├── FWHT + Sign Flip (feedback matrix)            │
         ├── BandlimitedNoise LFO (per channel)            │
         ├── Stage 2 GEQ Absorption (10-band, per channel) │
         ├── Micro-Saturation (Padé, loop safety)          │
         └── Nested Allpass (per channel)                  │
                    │                                      │
                    └──► [ER Mix + Late Mix] ◄─────────────┘
                                │
                         [ADAA Saturator]
                                │
                         [Output EQ: Lo/Hi Cut]
                                │
                         [Brick-Wall Limiter]
                                │
                         Wet Output × SmoothedWetGain
                                │
                    [Dry × SmoothedDryGain] ─────────────────┐
                                                             ▼
                                                      Final Output (Stereo)
```


## Acoustic Metrics Reference

| Metric | Description | Ideal Range |
|---|---|---|
| **D50** | Definition — ratio of early (0–50ms) to total energy | >0.5 for speech clarity |
| **C50** | Clarity (speech) — early-to-late energy ratio at 50ms | >0 dB for speech |
| **C80** | Clarity (music) — early-to-late energy ratio at 80ms | -2 to +4 dB for music |
| **EDT** | Early Decay Time — RT60 estimated from the first 10 dB of decay | ≈ RT60 mid-band |


## Disclaimer

This software is provided "as-is", without any warranty of any kind. While extreme care has been taken to ensure real-time safety and audio stability through lock-free parameter dispatch, dirty-flag optimization, and Ableton Live-specific fail-safes, unexpected behavior may still occur in edge cases or unsupported hosts.


## License

This project is licensed under the GNU Affero General Public License v3.0 (AGPLv3) - see the [LICENSE](LICENSE) file for details.

This software is built using the **JUCE 8** framework. In accordance with JUCE 8's open-source licensing terms, this entire project is distributed under the AGPLv3.


## Credits

**Developer:** @kijyoumusic (OTODESK)

**Music Production Background:** Electronic Music, Sound Design, DSP Engineering

**Target DAW:** Ableton Live 11 / 12

**Framework:** JUCE 8.0.x

**DSP References:**
- Välimäki & Liski — *"Accurate Cascade Graphic Equalizer"* (2017)
- Vicanek — *"Matched Second Order Digital Filters"* (2016)
- Schlecht & Habets — *"On Lossless Feedback Delay Networks"* (2017)
- Parker et al. — *"Modelling plate and spring reverberation using DSP-informed DNN"* (2019)


## Support

* **Social / Demo:** [@kijyoumusic](https://x.com/kijyoumusic)
* [![Website](https://img.shields.io/badge/Official%20Website-OTODESK-blue?style=for-the-badge)](https://otodesk4193.github.io/OTODESK_SITE/)
*