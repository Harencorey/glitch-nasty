# Glitch Nasty (Norka DSP)

A tempo-synced, real-time glitch/stutter plug-in. Zero latency, so it works on a live guitar as well as on tracks.
**Phase 1:** clock, step pattern with per-step chance, stutter engine. Coming next: the crusher (bits / sample rate),
glitch modifiers (reverse, gate, varispeed, tape stop), the delay/reverb send, then the step sequencer for bits/SR/pitch.

## Install (easiest)

Download **GlitchNasty-Installer.pkg** from the latest release: https://github.com/Harencorey/glitch-nasty/releases/tag/latest
It isn't Apple-notarized yet, so the first time macOS blocks it: **System Settings > Privacy & Security > Open Anyway**
(or right-click the .pkg and choose **Open**). The Standalone app is **GlitchNasty-Standalone.app.zip** on the same page.
Every past version stays downloadable at https://github.com/Harencorey/glitch-nasty/releases.

## Build it yourself (Mac)

```bash
bash build_mac.sh                   # downloads JUCE 8.0.15 automatically
# or: bash build_mac.sh ~/path/to/JUCE
```

Needs the Xcode command line tools and CMake (`brew install cmake`). It builds a universal **AU, VST3 and Standalone**,
installs the plug-ins into `~/Library/Audio/Plug-Ins/`, and runs the engine tests. Look for **Norka DSP > Glitch Nasty**.
Logic: `auval -v aumf Gnst Nrka`. It's a *music effect* (`aumf`), not `aufx`, because MIDI input is reserved for MIDI-triggered glitches later.

## Automated builds

Every push to `main` builds on a GitHub macOS runner (`.github/workflows/build-mac.yml`) as version `0.1.<build number>`,
the same pipeline as Glow: engine tests, universal-binary check, **GlitchNasty-Installer.pkg** (`packaging/make_pkg.sh`),
a test install, Apple `auval` and pluginval at strictness 10. Then it publishes the **latest** release plus a permanent
**v0.1.<build>** release with that commit's notes. The build number shows top-right in the plug-in window.

**Standalone / live:** from the release zip, or `build-mac/GlitchNasty_artefacts/Release/Standalone/` if you built it.
- The first time, macOS asks for microphone access. Say yes, or the input is silent.
- JUCE's standalone starts with **input muted** (the yellow bar) to stop laptop-mic feedback. Click **Settings…**, pick your interface, and untick mute. It remembers.
- Keep the buffer at 64–128 samples for playing live. The plug-in adds no latency of its own.

## How it works

Each step on the grid is one division of the bar (Grid = 1/16 → 16 steps = one bar). When a step that's on comes
around and wins its dice roll (**Chance**), the plug-in grabs the audio from the start of that step and loops the first
**Repeat** length of it until the step ends. Then it's straight back to the live signal.

- **Zero latency:** the first pass of every repeat *is* the live input; only the replays come from the buffer.
- **On the grid:** loop points land within 1 sample of the true grid at any tempo and sample rate (tested
  44.1–96 kHz, odd tempos like 97.3 and 133 BPM, triplet grids). Output is bit-identical whatever buffer size the host uses.
- **Crossfades:** every loop point and every return to live is an equal-power crossfade (X-FADE, default 3 ms).
  Measured on a 440 Hz sine: 0 ms gives clicks of 0.89 (full scale = 1), 3 ms gives none (no step bigger than the sine itself).
- **Between glitches** the signal is untouched (bit-identical), and with the transport stopped in Host sync it's a true bypass.

## Controls

| Control | What it does |
|---|---|
| **Sync** | **Host:** follows the DAW's tempo and position, re-locks after loop/cycle jumps, passes audio straight through while stopped. **Internal:** always runs on the BPM knob (use this to glitch live input in a DAW with the transport stopped). Standalone always runs Internal. |
| **BPM / TAP** | Internal tempo. TAP averages your last 4 taps; every tap also snaps the grid to the nearest beat so you can lock it to a drummer. A 2 s pause starts a new count. Greyed out while the host clock is driving. |
| **Grid** | Step length: 1/8, 1/16, 1/32, 1/8T, 1/16T. |
| **Steps** | Pattern length (1–16). Odd lengths drift against the bar for polyrhythms. |
| **X-Fade** | Loop crossfade, 0–20 ms. It's automatically limited to half the step/repeat so short repeats stay tight. |
| **Mix** | How much glitch you hear while a step fires (between glitches it's always 100 % dry). |
| **Output** | −24 to +12 dB trim. |

**Step grid:** click a pad to switch a step on/off (drag across to paint). Drag a **chance** bar up/down. Drag sideways to
draw across steps; double-click = 100 %. Click the **repeat** box for 1/4 … 1/64 including dotted and triplets.
The step number lights green as the playhead passes and pink while that step is glitching. Hover anything for ~3 s for help.
Every step's on/chance/repeat is a real parameter, so you can automate it.

## Files

- `Source/GlitchCore.h`: the whole audio path (clock → grid → pattern → engine). No JUCE, shared with the tests.
- `Source/GridScheduler.h`: finds the exact sample of each grid line in a block; handles loop jumps.
- `Source/StutterEngine.h`: the always-recording buffer, loop playback and crossfades.
- `Source/Params.h`: parameter IDs and ranges (don't rename IDs after release: it breaks sessions).
- `Source/PluginProcessor.*`, `Source/PluginEditor.*`, `Source/StepGrid.*`: the JUCE wrapper and UI.
- `tests/test_engine.cpp`: offline tests (grid accuracy, block-size independence, clicks, chance, bypass, DAW loops,
  zero allocations on the audio thread). They run automatically at the end of `build_mac.sh`.

## Already set up for later phases

- MIDI input is enabled now (unused), so adding MIDI-triggered glitches won't change the AU type and break sessions.
- The engine reads the buffer with fractional positions, so varispeed/pitch and reverse slot straight in.
- The dice use a seedable generator, so a "Seed" control for repeatable random patterns is a one-liner.
- The undo manager is wired into the parameters, ready for Glow's undo/redo, A/B and preset bar in the UI phase.
