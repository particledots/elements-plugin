# Unofficial Elements — AU / VST3 port (work in progress)

A plugin wrapper around the Mutable Instruments **Elements** DSP, taken from the open-source release
(`pichenettes/eurorack`, `elements/dsp` + `elements/resources.cc`, and `stmlib/` at the commit that repo pins, `e3bd7c9`).
Not a Mutable Instruments product. Don't contact Emilie Gillet or Mutable Instruments about it.

## Easiest: let GitHub build it
`.github/workflows/build.yml` builds the AU and VST3 on a macOS runner; download `Elements-macOS` from the run's
Artifacts (Actions tab > latest run). `INSTALL.txt` inside covers the install.
Builds are universal (arm64 + x86_64), minimum macOS 11.

## Build it yourself (macOS, for Logic Pro)

```
brew install cmake        # if you don't have it
cd elements-plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```
First configure downloads JUCE 8.0.6. With `COPY_PLUGIN_AFTER_BUILD` on, the AU lands in
`~/Library/Audio/Plug-Ins/Components/` and the VST3 in `~/Library/Audio/Plug-Ins/VST3/`.
It shows up as "particledots: Unofficial Elements". Like the Unofficial Rings plug-in, the AU is declared as a
*MIDI-controlled effect* (`aumf`), so in Logic:

* **To play it from a MIDI keyboard:** create a **Software Instrument** track, click the **Instrument** slot, and choose
  *Audio Units > AU MIDI-controlled Effects > particledots: Unofficial Elements*.
* **To excite it with another track's audio:** pick a routing in the Audio Input menu and choose the source track in the
  plug-in's Side Chain menu. Untested. Loading it in an ordinary **Audio FX** slot also works as a plain effect, but it gets
  no MIDI notes there.

Headless DSP tests (any OS, no JUCE needed):
`cmake --build build --target engine_test sweep_test && build/engine_test && JUNK=0xFF build/sweep_test`

## Controls

Names in the exciter section come from the firmware's `Patch` struct (`exciter_bow_level`, `exciter_blow_meta`, ...), not from
the panel silkscreen.

| Plugin | Module / firmware |
|---|---|
| Contour | `exciter_envelope_shape`: below 0.4 a percussive envelope with no sustain, 0.4 to 0.6 a sustain level that rises with the knob, above 0.6 full sustain with the attack and release getting shorter as the knob goes up (read from `voice.cc`, not listened to) |
| Bow Level / Bow Timbre | the bow exciter's level and timbre |
| Blow Level / Blow Meta / Blow Timbre | the blow exciter (granular sample player + the tube model at high levels) |
| Strike Level / Strike Meta / Strike Timbre | the strike exciter (sample player through mallet, plectrum, particles). Above ~0.8 Strike Level, the raw exciter bleeds into the output |
| Geometry, Brightness, Damping, Position | the four resonator controls. In the String models Geometry selects the chord or the string dispersion |
| Space | stereo spread, then reverb, as the firmware's `space` metaparameter. The panel pot spans 0 to 1 |
| Reverb Freeze | the firmware freezes the reverb when `space` is 1.75 or more. On the module that needs CV on top of the Space knob, so here it is a button |
| Transpose | semitones added to the MIDI note, ±36 |
| Fine | ±2 semitones (plain linear, not the module's quadratic fine-tune law) |
| Pitch Snap (0.2.0, my addition, not in the module) | **Off** (the sound is exactly 0.1.0), **Steps** or **Semitones**. Acts on Transpose + Fine taken together. Steps are octaves and fifths: 0, 7, 12, 19, 24, 31, 36, 43, 48, both signs. Semitones rounds to whole notes. With it on, Fine only matters when it tips the total over a step. The Transpose readout shows the offset that plays. Old projects load with it Off. |
| Strength | the firmware's `strength` (the PRESSURE input). 0.5 is unity gain in its accent table; 1.0 is about +15 dB, 0 about −15 dB |
| Velocity > Strength | when on, MIDI velocity (0..1) drives Strength; the Strength knob is used until the first MIDI note, and whenever this is off. This mapping is my choice, not the module's |
| Resonator Model | **Modal resonator**, **String**, **Strings (chords)**: the three models of the firmware's `ResonatorModel` |
| Audio Input | **L = Strike, R = Blow** (how the module's two jacks are wired: left is strike, right is blow), **Mono to Strike**, **Mono to Blow** |
| Gate | the module's GATE button, OR-ed with the MIDI gate |
| Ominous Voice | the firmware's easter egg voice (`set_easter_egg`). I have not studied what each knob does in this mode |
| Mono Out | sums the two outputs. By default **left = aux, right = main**, as the firmware's mixdown names them |

Pitch and gate: MIDI note-on raises the gate and sets the pitch (the DSP takes a real MIDI note number, so note 69 is 440 Hz);
releasing the last held key drops the gate. Keys are last-note priority. A legato note-on (a new key while another is held)
forces the gate low for one 0.5 ms block so the exciters and envelope see a fresh attack. That retrigger is my addition: on the
module the gate is whatever the patch cable does. Before any MIDI note arrives the pitch is MIDI 48 plus Transpose, so you can
use the GATE button without a keyboard.

Audio inputs: the firmware adds the **strike** input straight into the resonator, so it rings with no gate. The **blow** input is
multiplied by the contour envelope, so it is silent unless a gate is high. Both inputs pass through the firmware's noise gate.

## How it works / honest limits

* The module DSP is called exactly as the firmware does it: fixed 32 kHz, 16-sample blocks. A windowed-sinc resampler converts to
  and from the host rate (bypassed at 32 kHz, which no host uses). Reported latency is 64 frames at 32 kHz (2 ms),
  converted to host samples.
* Controls are smoothed the way the firmware smooths its pots (a 0.01 one-pole per 16-sample block, then the per-control
  second stage and clamp from `cv_scaler.cc`). Unlike the module at power-up, the smoothers start at the knob positions.
* Not modelled: the CV inputs and per-channel attenuverters, the FM input (without CV it would be a static pitch offset, which is
  what Transpose and Fine already do), calibration, the coarse-tuning quantizer, the settings/UI/LED/metering code, and bypass.
  The module's `Part::Seed` (which scatters a few constants per serial number: modulation rate/offset, reverb diffusion and
  damping, exciter signature) is not called, so a fixed set of start-up values is used: those from `Part::Init`.
* The module has one voice (`kNumVoices = 1`), so this is monophonic.
* MIDI note-ons are placed sample-accurately, but the DSP sees the gate on 16-sample block boundaries (0.5 ms), as the module does.
* **Verified here:** the DSP wrapper (`engine_test`, 80 combinations at 44.1/48/96 kHz: all three models with velocity on/off, bow-only
  and blow-only, three audio routings, the GATE button, ominous voice, space 0/1/freeze, knob and transpose extremes) produces finite,
  bounded, non-silent output. The string model's pitch measures +1.4 cents from 220 Hz for MIDI 57 (autocorrelation), and a legato
  retrigger produces a new attack. `sweep_test` (model/voice switching on junk-filled heap memory at three sample rates) shows no
  silent, stuck or non-finite segments.
* **Not yet verified:** the GitHub Actions macOS build (AU + VST3) of this repo, loading in Logic on the M4 Mac mini and on the Intel Mac
  (macOS 12.7), MIDI playing in Logic, the Side Chain route, and above all a careful A/B of how closely it sounds like the hardware.
  The automated tests check "not broken" and "in tune", not "sounds right".
* The control names and the strike/blow descriptions come from the firmware source, not from listening.
* The UI is a basic custom panel (knobs, menus). It is functional rather than polished.

## Licensing

`third_party/` is MIT-licensed code by Emilie Gillet (copyright notices kept in the file headers; `third_party/stmlib/LICENSE`).
Keep those notices if you redistribute. The hardware design is CC-BY-SA; none of it is used here. The wrapper code in `src/` has
no licence assigned yet. JUCE is used under its own licence terms (AGPLv3 / commercial); check them before distributing builds.
