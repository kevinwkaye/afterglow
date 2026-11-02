# Afterglow

A piano-style sustain and sostenuto VST3 for Ableton Live, recreating the Gamechanger Audio PLUS Pedal II (2026) in the Flip Fella style. Press the brass pedal, play, and every new note becomes a sustained layer that rises, holds and fades like strings left ringing.

## Install

1. Open the **builds** branch on GitHub and download `Afterglow-Windows.zip`.
2. Unzip it and copy `Afterglow.vst3` into `C:\Program Files\Common Files\VST3`.
3. In Ableton, open Preferences > Plug-Ins, turn on "Use VST3 Plug-In System Folders" and press Rescan.

`Afterglow-standalone.exe` in the same zip runs without Ableton if you want to try it on its own.

## The pedal

| Control | What it does |
| --- | --- |
| Brass pedal | Click and hold to sustain. Drag up to ease off: depth sets how loud held layers sit. Two quick presses clear everything |
| Layers | 1 to 10 layers, or infinite: older layers fold into one so the wall never cuts off. The ticks round the dial light with each layer |
| Catch: Single, Group, All | Single catches one layer per press, even quiet tails. Group catches up to the Layers count, then lets you play over it. All catches every new note, replacing the oldest |
| Hold: None, First, All | None lets every layer rise then fade. First holds the first note of each press while the rest fade around it. All holds everything while the pedal is down |
| Rise, Decay | Each layer's fade in (80 ms to 5 s) and fade out (30 ms to 20 s, then infinite) |
| Tone | Darkens each layer as it ages, like a real string. Fully up keeps the full timbre |
| Resonance | Off at 12 o'clock. Clockwise adds upper harmonics, anticlockwise adds lower ones, drawn from the layers themselves |
| Level, Input | Layer level and input trim |
| FX Loop: Pre, Loop, Post | Where the effect sits: before the engine, on the layers only, or on the whole mix |
| Verb, Delay, Chorus, Shimmer | The loop effect, with Amount and Mix |
| Kill Dry | Layers only |
| Footswitch: Wet, Latch, Hide | Pick a mode, tap it again to engage. Wet removes the dry, Latch holds the pedal down for you, Hide mutes the dry while the pedal is down |
| A / Live / B | Hold A or B to save the panel, tap to recall |

### Piano modes

Set Hold to None and turn Tone fully down. Catch then picks the mode, and the ring and Pebble's halo change colour:

- **All: Real Piano** (yellow). New notes ring out while the pedal is down. Lifting it damps them, like dampers falling on strings.
- **Group: Piano Group** (yellow). The same, but only the Layers count of notes ring.
- **Single: Piano Sostenuto** (blue). Press while playing a chord. It stays open as a resonator, and later notes that share its frequencies make it ring again.

In these modes Tone sets how fast the dampers fall.

## MIDI

Send a MIDI track's output to Afterglow (MIDI To: the Afterglow track).

- **Sustain pedal (CC64)** works straight away. CC14 does too, as on the PLUS Pedal II.
- **Pedal key**: note C1 holds the pedal down. Press LEARN, then play a note to pick another.
- **Half pedal**: press LEARN and move a touch strip or fader (the Maschine Mikro's strip, say). That controller then sets pedal depth.
- **PLUS Pedal II chart**: CC8 Level, CC9 Layers, CC10 Rise, CC11 Decay, CC12 Tone, CC13 Resonance, CC15 Catch, CC16 Hold, CC17 Kill Dry, CC18 FX Loop, CC19 Footswitch.

Every control can also be MIDI-mapped in Ableton, so Launchpad pads can work Latch, Clear or the modes.

## Building

GitHub Actions builds the Windows VST3 on every push to `main` and publishes it to the `builds` branch. To build locally you need CMake 3.22+ and a C++17 compiler; JUCE downloads automatically.

```
cmake -S . -B build
cmake --build build --config Release
```

`afterglow_render_test` plays a guitar phrase through every catch, hold, piano and effect mode with the pedal scripted, and fails on NaNs, clipping, clicks or silence. Pass a folder to write WAV files.
