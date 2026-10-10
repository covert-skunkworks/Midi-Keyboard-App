# PC MIDI Keyboard

### About

On-screen piano keyboard (25 to 128 keys, default 88) that sends MIDI note events to a chosen MIDI output port.
C99 + SDL2. MIDI goes through the OS API: WinMM (Windows), ALSA sequencer (Linux), CoreMIDI (macOS).

## Build

Windows (MSYS2 MinGW-w64, run from PowerShell, cmd or a bash shell):

```
C:\msys64\mingw64\bin\mingw32-make.exe
```

Output: `build/pc_midi_keyboard.exe`, statically linked. It needs no DLLs and can be copied to any Windows PC.

Linux (Debian/Ubuntu):

```bash
sudo apt install build-essential pkg-config libsdl2-dev libasound2-dev
make
```

macOS (Xcode command line tools + Homebrew):

```bash
brew install sdl2 pkg-config
make
```

Output: `build/pc_midi_keyboard`.

## Packages and releases (GitHub Actions)

On every push, `.github/workflows/build.yml` builds all three platforms, starts each once as a smoke test, and packages
them. The packages are downloadable from the run's page (Actions tab, "Artifacts").

To publish a release, push a version tag:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The workflow then creates a GitHub Release for that tag with:

| File | Contents |
|---|---|
| `pc_midi_keyboard-windows-x64.zip` | `pc_midi_keyboard.exe` (static, no DLLs needed) + README |
| `pc_midi_keyboard-linux-x64.tar.gz` | binary + README; needs the system SDL2 (`sudo apt install libsdl2-2.0-0`) |
| `pc_midi_keyboard-macos-arm64.zip` | `PC MIDI Keyboard.app` with SDL2 bundled, Apple Silicon only |

The macOS app is not signed with an Apple developer ID, so on first launch use right-click → Open (or run
`xattr -dr com.apple.quarantine "PC MIDI Keyboard.app"`).

## Use

| Input | Action |
|---|---|
| Mouse click / drag | play, glissando. Velocity comes from where the key is struck (front = loud) unless a fixed velocity is set |
| Right click on a key | latch it (stays on, drawn amber). The next click (either button) on it releases it |
| Root row | pick the chord root (C … B); ROOT OCTAVE sets its octave; mouse wheel over the row steps by a semitone |
| Chord row | left click plays the chord type on the root while held; right click latches it until the next click |
| UNLATCH | release all latched keys and chords (played notes and sustain are left alone) |
| Touch | one note per finger |
| `Z S X D C V G B H N J M , L . ; /` | octave shown in "PC KEYS" |
| `Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ = ]` | one octave higher |
| Left / Right | PC-keys octave |
| Up / Down | velocity (down to TOUCH = position-sensitive) |
| PgUp / PgDn | MIDI channel |
| Space (hold) | sustain pedal, CC64 |
| Esc | panic: all notes off on all channels |
| F5 | rescan ports (keeps the current port if it still exists) |
| F2 / F3 | fewer / more keys (also the KEYS box in the top bar) |

Click the port box (or its `<` `>` arrows, or use the mouse wheel over it) to change port. To drive a DAW on Windows,
create a virtual port with a tool such as loopMIDI, then press F5.

## Chord presets

MAJ, MIN, DIM, AUG, SUS2, SUS4, 6, MIN6, 7, MAJ7, MIN7, HDIM (half-diminished, m7b5), DIM7, ADD9, 9, MAJ9, MIN9,
5 (power chord with octave). Notes that fall outside the
selected keyboard range are skipped. Esc/Panic and changing the key count release all latches.

## Window

The window opens no larger than the screen's usable area and can't be resized past it. The maximum height follows the
width and key count: it is exactly the height at which the keys have piano proportions, so no empty space appears
below them. Minimum size is 1000 x 280.

## Icon

`icon.ico` (exe icon, via `pc_midi_keyboard.rc`) and `src/icon_data.h` window icon on all platforms.

## Test options

`pc_midi_keyboard.exe --screenshot out.bmp [--keys N] [--demo]` draws one frame off screen into `out.bmp` and exits.
MIDI stays off. `--demo` latches a chord and a key and plays one note, so all key states are visible.

## Keyboard sizes

| Keys | Range | |
|---|---|---|
| 25 | C3–C5 | 2 octaves, mini controller |
| 37 | C3–C6 | 3 octaves, Korg Prophecy |
| 49 | C2–C6 | controller |
| 61 | C2–C7 | synth |
| 73 | E1–E7 | stage piano |
| 88 | A0–C8 | piano |
| 92 | F0–C8 | Bösendorfer 225 |
| 97 | C0–C8 | Bösendorfer 290 Imperial |
| 128 | C-1–G9 | full MIDI range |

## Files

- `src/main.c`: window, top bar, input handling, note reference counting
- `src/piano.c`: key layout, hit testing, gradient key rendering
- `src/midi_out.c`: MIDI output backends
- `src/font.c`: built-in 5x7 bitmap font (no font files needed)
- `src/icon_data.h`: generated window icon pixels

## Pricing

This tool is provided to everyone for free, however if you would like to say thanks or help support continued development of this and projects to come, feel free to send a coffee:

[<img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="BuyMeACoffee" width="100">](https://www.buymeacoffee.com/covert.skunkworks)

### License
This project is released under the GPLv3.

