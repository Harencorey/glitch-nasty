# Glitch Nasty (Norka DSP)

A tempo-synced, real-time glitch/stutter plug-in. Zero latency, so it works on a live guitar as well as on tracks.
8 patterns of 16 steps, 11 glitch types each with their own controls, per-step level/pan/filter/crush/send, ties,
swing, steerable randomness, a crusher, tempo-synced delay and reverb, and live triggering from pads, MIDI notes
and a footswitch.

Coming next: presets / A-B / undo.

## Install (easiest)

Download **GlitchNasty-Installer.pkg** from the latest release: https://github.com/Harencorey/glitch-nasty/releases/tag/latest
It isn't Apple-notarized yet, so the first time macOS blocks it: **System Settings > Privacy & Security > Open Anyway**
(or right-click the .pkg and choose **Open**). The Standalone app is **GlitchNasty-Standalone.app.zip** on the same page.
Every past version stays downloadable at https://github.com/Harencorey/glitch-nasty/releases.

**Standalone / live:**
- The first time, macOS asks for microphone access. Say yes, or the input is silent.
- JUCE's standalone starts with **input muted** (the yellow bar) to stop laptop-mic feedback. Click **Settings…**, pick your interface, and untick mute. It remembers.
- To use MIDI (pad controller, keyboard, footswitch), enable the MIDI input in the same **Settings…** window.
- Keep the buffer at 64–128 samples for playing live. The plug-in adds no latency of its own.

## How it works

Each step on the grid is one division of the bar (Grid = 1/16 → 16 steps = one bar). When a step that's on comes
around and wins its dice roll (**Chance**), the plug-in grabs the audio from the start of that step and does what the
step's **Type** says for the rest of the step (or longer, if it's tied), using slices of **Repeat** length.
Then it's straight back to the live signal.

- **Zero latency:** the first pass of every repeat *is* the live input; only the replays come from the buffer.
  Types that rearrange audio (Shuffle, Scratch, Freeze) only ever use audio that has already happened - tested across
  every type and setting.
- **On the grid:** loop points land within 1 sample of the true (swung) grid at any tempo and sample rate.
  Output is bit-identical whatever buffer size the host uses.
- **Between glitches** the signal is untouched (bit-identical).

## Global controls

| Control | What it does |
|---|---|
| **Sync** | **Host:** follows the DAW's tempo, position and time signature; re-locks after loop/cycle jumps; passes audio straight through while stopped. **Internal:** always runs on the BPM knob (use this to glitch live input in a DAW with the transport stopped). Standalone always runs Internal. |
| **BPM / TAP** | Internal tempo. TAP averages your last 4 taps; every tap also snaps the grid to the nearest beat. |
| **Grid** | Step length: 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T. |
| **Steps** | Pattern length (1–16). Odd lengths drift against the bar for polyrhythms. |
| **Swing** | 50 % = straight, 66 % = triplet feel, 75 % = hard shuffle (every second step pushed late, MPC-style). |
| **X-Fade** | Crossfade at every loop point, 0–20 ms (3 ms default = click-free). |
| **Mix** | How much glitch you hear while a step fires. |
| **Density** | Scales every step's chance: −100 % = nothing fires, +100 % = every on-step fires. Automate it into a drop. |
| **Seed** | The dice behind Chance, Variation, Shuffle and Freeze texture. Same seed = same decisions at the same song position, so a happy accident bounces the same every time. |
| **Output** | −24 to +12 dB trim. |
| **Engage** | Pattern on/off, applied on the next bar line. A sustain pedal / footswitch (MIDI CC64) toggles it too. |

## Patterns

Tabs **A–H** pick the pattern that plays and that you're editing (automatable as *Pattern*; MIDI C2–G2 also switch).
Factory patterns: **A** basics, **B** build-up, **C** trance gate, **D** breaks, **E** pitch riser/dropper, **F** textures,
**G** ping-pong echoes, **H** empty. Tools: **COPY / PASTE**, **< >** shift the pattern a step, **REV** reverse it,
**CLEAR**, and **RANDOM** - roll a new pattern, keeping whatever you **LOCK** (on/off, type, repeat, chance, shape).

Patterns are saved with the session. Sessions from 0.1.x open with their old steps in pattern A
(their per-step automation lanes don't carry over - the steps are no longer separate parameters).

## Steps

**Grid:** click a pad to switch a step on/off (drag to paint). Click the **type** or **repeat** box for a menu. Drag a
**chance** bar (drag sideways to draw; double-click = 100 %). Click a step's **number** to edit it below.

**Length (ties):** every on step has **[ ]** brackets at its edges - drag either one (the cursor turns into a resize
arrow) to make the glitch last more or fewer steps. Dragging the left edge moves where it starts.

**Layers (stacking):** inside a stretched step, click any covered step to switch on a **layer** (a pink block on the
bar). It's a second glitch that plays on top of the long one *and processes it* - e.g. Pitch down over 4 steps with a
Roll layer on step 3 rolls the pitched-down sound, then the pitch carries on underneath. Layers have their own type,
repeat, chance and controls (click the step number to edit; the editor says "LAYER ON STEP n"), last one step, and
stop when the stretched step ends. Pattern **E** shows it off.

**Step editor:** two controls that depend on the type (knobs, or a switch when the control is a choice like
Reverse/Ping-Pong or Speed Up/Slow Down), then **Level** (all the way down = mute), **Pan**, **Filter** (left = low-pass to 200 Hz, right = high-pass to 4 kHz;
only on the glitch, never the dry signal), **Bits** and **Rate** (this glitch's crusher - set them per step to
sequence the crush), and **Send** (how much of this glitch feeds the delay/reverb).
Changes apply straight away, even to a glitch that's already playing (they glide over the X-Fade time, no clicks).
The header shows how many repeats fit in the step: Roll, Fade, Ping-Pong decay and Shuffle need 2 or more to be heard.

| Type | Control 1 | Control 2 |
|---|---|---|
| **Repeat** | Offset: which slice of the step gets looped | Variation: each repeat starts a little earlier, at random |
| **Reverse** | Mode: Reverse / Ping-Pong (back to the slice start, then forward again, inside every repeat) | Offset |
| **Gate** | Duty: 5–95 % open | Shape: Hard / Soft / Pump (ducks then swells, like sidechain) |
| **Roll** | Direction: Speed Up / Slow Down | Curve: how fast it changes (×0.5 … ×0.9 per repeat) |
| **Pitch** | Pitch: −24 … +24 semitones | Glide: −24 … +24 st bent across the step (risers / droppers) |
| **Tape Stop** | Direction: Stop / Start (spins up from nothing) | Curve: linear → exponential |
| **Fade** | Decay: dB per repeat | Direction: Fade Out / Fade In (swell) |
| **Shuffle** | Chaos: how many slices get swapped for earlier ones | Reverse: chance a swapped slice plays backwards |
| **Freeze** | Grain: 10–200 ms | Texture: how much the grain wanders |
| **Scratch** | Depth: how far it drags back | Speed: ½× / 1× / 2× per repeat length |
| **Ping-Pong** | Width: how far left/right | Decay: dB per repeat |

## Live

Hold a **pad** (or play a **MIDI note**) to glitch right now with that type - it overrides the pattern until you let go.
**Repeat** sets the slice length; **Quantize** sets when it starts (Off = instantly, or on the next 1/16, 1/8, 1/4 or bar).
Each pad has **its own settings**: clicking a pad opens them in the step editor (outlined pink), so you can set, say,
the Pitch pad to -7 st with a high-pass and half send. Click a step number to go back to editing the pattern.
Pad settings are saved with the session.

| MIDI | Does |
|---|---|
| C3 … A#3 (60–70) | Hold to glitch: Repeat, Reverse, Gate, Roll, Pitch, Tape Stop, Fade, Shuffle, Freeze, Scratch, Ping-Pong |
| C2 … G2 (48–55) | Switch to pattern A … H |
| CC64 (sustain pedal) | Footswitch: toggle Engage on the next bar |

## Effects

| Section | Controls |
|---|---|
| **Crush** (global) | **Bits** (off, 16 → 1 bit), **Rate** (off, ÷1 → ÷64 sample rate, aliasing on purpose), **Mix**. Crushes the whole output, dry included. For crush on just the glitches, use Bits/Rate in the step editor. |

Both crushers quantise relative to the signal's level (a peak follower with a 250 ms release), so "4 bits" is
4-bit crunch on a quiet guitar as much as on a loud drum bus - measured: no level drop at −6, −18 or −30 dBFS, and
about 6 dB more crunch per bit lost.
| **Delay** | **Time** (tempo-synced 1/32 … 1/2, dotted and triplet), **Feedback** (soft-clipped, so high settings stay musical), **Tone** (filter in the feedback loop: darker or thinner repeats), **Ping-Pong**, **Mix** (0 = off). |
| **Reverb** | **Size** (room → huge hall, about 0.4–9 s), **Damp** (bright → dark), **Mix** (0 = off). An 8-line feedback delay network. |
| **Send / Feed** | **Glitches**: only the glitched audio (times each step's Send) feeds the delay and reverb, so tails spill out of the glitches while the straight signal stays dry. **Everything**: the whole output. |

Tails keep ringing when the transport stops. With every Mix at 0 the effects are a true bypass.

## Build it yourself (Mac)

```bash
bash build_mac.sh                   # downloads JUCE 8.0.15 automatically
# or: bash build_mac.sh ~/path/to/JUCE
```

Needs the Xcode command line tools and CMake (`brew install cmake`). It builds a universal **AU, VST3 and Standalone**,
installs the plug-ins into `~/Library/Audio/Plug-Ins/`, and runs the engine tests. Look for **Norka DSP > Glitch Nasty**.
Logic: `auval -v aumf Gnst Nrka` (a *music effect*, because it takes MIDI).

## Automated builds

Every push to `main` builds on a GitHub macOS runner (`.github/workflows/build-mac.yml`), the same pipeline as Glow:
engine tests, universal-binary check, **GlitchNasty-Installer.pkg** (`packaging/make_pkg.sh`), a test install,
Apple `auval` and pluginval at strictness 10. Then it publishes the **latest** release plus a permanent version release
with that commit's notes. The version shows top-right in the plug-in window.

## Files

- `Source/Pattern.h`: step/pattern data, the type table and how every control maps to real values (shared by engine and UI).
- `Source/GlitchCore.h`: the whole audio path - clock, swing grid, patterns, ties, density, seed, MIDI/live, engage.
- `Source/StutterEngine.h`: the always-recording buffer, the 11 types, crossfades, level/pan/filter/crush/send.
- `Source/Effects.h`: crusher, tempo-synced delay, FDN reverb.
- `Source/GridScheduler.h`: finds the exact sample of each (swung) grid line in a block; handles loop jumps.
- `Source/TripleBuffer.h`: hands pattern edits from the UI to the audio thread without locks.
- `Source/Params.h`: automatable parameter IDs (don't rename after release: it breaks sessions).
- `Source/PluginProcessor.*`: JUCE wrapper, session save/load (and 0.1.x migration), pattern tools.
- `Source/PluginEditor.*`, `StepGrid.*`, `StepEditor.*`, `PatternBar.*`, `LivePanel.*`, `FxPanel.*`, `Theme.h`: the UI.
- Layers run in a second engine in series with the first (`GlitchCore.h`), so they process the tied glitch's output.
- `tests/test_engine.cpp`: offline tests - grid/swing accuracy, every type and control, level/pan/filter, ties,
  chance/density/seed, MIDI live/quantize/pattern switch/footswitch, never-the-future across 132 setting combinations,
  click-free crossfades, block-size independence, bypasses, DAW loops, zero allocations on the audio thread,
  live-pad settings, knob moves during a glitch, crusher levels/hold, delay echo timing/feedback/ping-pong,
  glitch-only sends, reverb decay vs. design, FX bypass at mix 0.
