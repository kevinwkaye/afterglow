# Afterglow

A piano-style sustain and sostenuto VST3 for Ableton Live, recreating the Gamechanger Audio PLUS Pedal II (2026) in the Flip Fella style. Press the Brass Pedal, play, and every new note becomes a sustained layer that rises, holds and fades like strings left ringing.

Each layer is rebuilt from the notes it heard, partial by partial, so held notes stay in tune (under a cent), sound clean rather than grainy, and hold a steady level. A slow, tiny drift in each partial and a wide stereo image keep them alive.

## Install

1. Open the **builds** branch on GitHub and download `Afterglow-Windows.zip`.
2. Unzip it and copy `Afterglow.vst3` into `C:\Program Files\Common Files\VST3`.
3. In Ableton, open Preferences > Plug-Ins, turn on "Use VST3 Plug-In System Folders" and press Rescan.

`Afterglow-standalone.exe` in the same zip runs without Ableton if you want to try it on its own.

## Playing it

1. Pick your **Input**: Guitar, Synth or Bass. Synth listens for chord changes on pads with no attack. Bass analyses with twice the detail, so low notes stay in tune.
2. Hold the **Brass Pedal** (bottom strip, footswitch A, your keyboard's sustain pedal or the expression pedal) and play. Each new note is caught as a layer.
3. Let go and the layers fade, or keep holding and they stay.

The flip cards show what the pedal is doing and how many layers are sounding. Pebble shows it too: its eyes pop on each catch, it holds its breath while layers are held, and rings stack above its head as layers build.

## Controls

| Control | What it does |
| --- | --- |
| Brass Pedal | Click and hold to sustain. Drag up to ease off: depth sets how loud held layers sit. Two quick presses clear everything. Right-click to choose a key or touch strip for it |
| Layers | 1 to 10 layers, or infinite: older layers fold into one so the wall never cuts off. The ticks round the dial light with each layer |
| Catch: Single, Group, All | Single catches one layer per press, even quiet tails. Group catches up to the Layers count, then lets you play over it. All catches every new note, replacing the oldest |
| Clear | Clears every layer |
| Rise, Decay | Each layer's fade in (80 ms to 5 s) and fade out (30 ms to 20 s, then infinite) |
| Tone | Darkens each layer as it ages, like a real string. Fully up keeps the full timbre |
| Resonance | Off at 12 o'clock. Clockwise adds upper harmonics, anticlockwise adds lower ones, drawn from the layers themselves |
| Hold: None, First, All | None lets every layer rise then fade. First holds the first note of each press while the rest fade around it. All holds everything while the pedal is down |
| Level, Input gain | Layer level and input trim |
| Layer lanes | One bar per layer slot: white while rising, brass while held, grey while fading |
| FX Loop: Verb, Delay, Chorus, Shimmer | The loop effect, with Amount and Mix |
| Pre, Loop, Post | Where the effect sits: before the layers, on the layers only, or on everything |
| Kill Dry | Layers only |
| Aux: Wet, Latch, Hide | The pedal's optional footswitch. Wet removes the dry, Latch holds the Brass Pedal down for you, Hide hides the layers while the pedal is down. On and Off engage it |

### Piano modes

Set Hold to None and turn Tone fully down. Catch then picks the mode, and the ring and Pebble's halo change colour:

- **All: Real Piano** (yellow). New notes ring out while the pedal is down. Lifting it damps them, like dampers falling on strings.
- **Group: Piano Group** (yellow). The same, but only the Layers count of notes ring.
- **Single: Piano Sostenuto** (blue). Press while playing a chord. It rings straight away and stays open as a resonator, and later notes that share its frequencies make it ring again.

In these modes Tone sets how fast the dampers fall.

## Presets

Sixteen slots in red, yellow, green and blue banks. Tap a slot to load it, hold it to save. Everything saves with your Ableton set.

## Footswitches and expression (optional)

Everything works from the screen, so you don't need any pedals. If you have a MIDI footswitch:

1. Set its first four switches to send **momentary CC 20, 21, 22 and 23 on MIDI channel 16** (127 when pressed, 0 when released). Or use **Learn** on the Pedal page to use whatever yours sends.
2. Set an expression pedal input to **CC 11 on channel 16**, or click the expression bar and choose Learn.
3. Click the expression bar, choose **Calibrate heel and toe**, then rock the pedal from heel to toe once.

These settings are shared by every Flip Fella plugin on your computer, so you set them up once.

The footswitches follow the PLUS Pedal II: the Brass Pedal under your left foot, the pedal's own Aux switch next to it.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Brass Pedal: down while your foot is down. Double tap clears | |
| B | Aux on or off | Next Aux mode (Wet, Latch, Hide) |
| C | Kill Dry on or off | Clear every layer |
| D | Next preset | Previous preset |
| A+B | Bypass, layers ring out | |
| C+D | Hand the footswitches to the next Afterglow | Hold a second: save a Keeper |

The A to D squares in the bottom strip do exactly what the footswitches do, and light while their job is on.

The expression pedal plays the **Brass Pedal** to start with: rock heel to toe and layers swell in and fade away, like the pedal's own gradual sensor. Half way is a half pedal. Click the expression bar to send it to Level, Decay, FX Mix, Tone, Resonance, Rise or FX Amount instead.

### Your keyboard's sustain pedal

A sustain pedal plugged into your MIDI keyboard plays the Brass Pedal too, through a MIDI track in Ableton (MIDI To: the Afterglow track). Turn it off with **Keyboard sustain** on the Pedal page, or by right-clicking the Brass Pedal. Right-click the Brass Pedal and choose **Learn a key or touch strip** to play it from a key or a strip (the Maschine Mikro's strip makes a good half pedal).

### Getting footswitch MIDI to the plugin in Ableton

Afterglow reads your footswitch straight from its USB cable, so every copy you load works with no MIDI track. The dot next to **FOOTSWITCHES** in the bottom strip turns green when it's connected, and Pebble says FOOTSWITCHES READY.

1. In Ableton's Preferences, Link Tempo MIDI, turn **Track**, **Sync** and **Remote** off for your footswitch's input. Windows lets only one program use a MIDI input at a time, so Ableton has to let go of it.
2. Load Afterglow. That's it.

If the dot stays grey, click **FOOTSWITCHES**. The menu says what's wrong, and you can pick your footswitch from the list if it wasn't found automatically.

With several copies loaded, the footswitches drive the one you opened or clicked last. Tap C+D to hand them to the next copy.

Prefer routing through Ableton? Choose **Only from a MIDI track in Ableton** in that menu, then turn **Track** back on for the footswitch, make a MIDI track with **MIDI From** set to the footswitch and **MIDI To** set to Afterglow, and arm it. The copy that track feeds takes the footswitches automatically.

### Kevin's setup (M-Vave Chocolate Plus and M-Audio EX-P)

In CubeSuite: footswitches A to D as momentary CC 20 to 23, the pedal jack set to expression input on CC 11. It always sends on channel 1 whatever CubeSuite says; read directly from its cable, Afterglow accepts any channel. Put the Chocolate Plus switch on **U** and connect with the USB cable, not Bluetooth. The EX-P needs a 3.5 mm to 6.35 mm TRS adapter and reads backwards on the Chocolate Plus, which Calibrate fixes.

## Original pedal MIDI map

Click the expression bar, open **Original pedal MIDI map** and pick a channel. Afterglow then answers the PLUS Pedal II's own CC chart and program changes on that channel, so a controller set up for the pedal works here. It is off by default: the chart uses CC 11 for Decay, the same number as the expression pedal.

CC 8 Level, 9 Layers, 10 Rise, 11 Decay, 12 Tone, 13 Resonance, 14 and 64 the Brass Pedal, 15 Catch, 16 Hold, 17 Kill Dry, 18 FX Loop, 19 Aux. Program changes load the user presets. Every control can also be MIDI-mapped in Ableton.

## Building

GitHub Actions builds the Windows VST3 on every push to `main` and publishes it to the `builds` branch. To build locally you need CMake 3.22+ and a C++17 compiler; JUCE downloads automatically.

```
cmake -S . -B build
cmake --build build --config Release
```

`afterglow_render_test` plays a guitar phrase through every catch, hold, piano and effect mode with the pedal scripted, and fails on NaNs, clipping, clicks or silence. It checks that held notes stay in tune and pure on guitar and bass, that soft playing, pads with no attack and phrases that start after silence are all caught, and ends with a CPU report at 48 kHz with 64 and 128 sample buffers. Pass a folder to write WAV files.

`afterglow_pedal_test` drives the real plugin with footswitch MIDI and checks every job in the footswitch table, the combos, Keeper, the expression pedal, the keyboard sustain pedal, and that the original pedal map stays off the footswitch numbers.

GitHub Actions also builds a Linux ARM64 VST3 for the Raspberry Pi live rig and publishes it to the `builds-linux-arm64` branch.
