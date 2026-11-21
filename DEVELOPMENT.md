# Developing North Pole

North Pole is a real After Dark module: 68000 code in an `'ADgm'` resource, the same kind
of file Berkeley Systems shipped in 1991, so it runs inside Afterglow's 68k emulator like
the originals. The scene is drawn at Atari 2600 resolution (160 x 120, each pixel a 4 x 4
block on Afterglow's 640 x 480 screen).

It is shared as an `.hqx` (BinHex) file because a Mac module keeps its code in a resource
fork, which some copies, zips and downloads lose; an `.hqx` cannot.

## What it does

- Santa strolls, stops, turns around. Elves scurry, some carrying a present. Reindeer
  wander and stop to graze; one of them is always Rudolph. Bunnies hop and sit. Things
  pass in front of and behind each other by depth.
- The big tree's lights twinkle, the workshop windows glow and flicker, the chimney smokes.
- Stars twinkle and a shooting star crosses now and then. The moon shows tonight's real
  phase, worked out from the Mac's clock (which Afterglow passes through).
- Snow comes and goes, building up and tapering off over several seconds. Clouds roll in
  from the upwind side as it starts, dimming the moon and stars, and roll on the same way
  when it stops. A flake that lands lies still for a second, then vanishes.
- The sleigh is how a scene ends: Santa and Rudolph walk off the same edge, the sleigh
  takes off from beyond it and flies back across the sky, and a few seconds after it has
  gone a new scene is built (a different layout, moon and crowd). A scene runs over its
  set time by however long that takes, usually 10 to 30 seconds.

## Settings

| Control | Effect |
|---|---|
| Snowfall | Never ... Always. How often it snows. |
| New scene every | 1 to 10 minutes. |
| Crowd | Quiet ... Packed. How many elves, reindeer and bunnies. |
| Santa's sleigh | The sleigh flight at the end of each scene, on or off. |

## Build

```bash
brew install m68k-elf-gcc m68k-elf-binutils ffmpeg
make            # build/North Pole and build/North Pole.hqx
make preview    # build/preview.mp4 + build/preview.gif, rendered natively
make sheet      # build/sheet.png, every sprite
make check      # diff-painter self-check across 8 seeds and scene changes
```

Rez and SetFile come with the Xcode command line tools.

Run it in the real emulator, headless (Afterglow has a CLI):

```bash
/Applications/Afterglow.app/Contents/MacOS/Afterglow --cli --frames 600 \
    --resolution 640x480 --depth 32 --dump /tmp/np --dump-interval 150 "build/North Pole"
```

The CLI calls DrawFrame far faster than 60 a second while `TickCount` follows the wall
clock, so most dumped frames repeat; the module paces itself by ticks, not calls.

## Publishing a new version

Copy the built `.hqx` into `dist/`, commit, and attach it to a new release under the name
the README's download link uses (the link always serves the latest release):

```bash
cp "build/North Pole.hqx" "dist/North Pole.hqx"
cp "dist/North Pole.hqx" /tmp/NorthPole.hqx
gh release create v1.1 /tmp/NorthPole.hqx --title "North Pole 1.1"
```

## Editing the art

Sprites are ASCII art in `art/sprites.txt` (the legend is at its top: one character per
color, `1`-`4` for per-character colors like an elf's suit, `*` for a twinkling bulb, `!`
for a lit window). `tools/gen_sprites.py` turns them into `src/sprites_gen.h`; `make sheet`
draws them all so you can look before building. The palette lives in the generator.

## How it works

| File | Role |
|---|---|
| `src/entry.S` | The code resource's first byte: takes After Dark's Pascal call and calls C. |
| `src/module.c` | After Dark messages, the locked state handle, scaling to the screen. |
| `src/scene.c` | The scene: layout, walkers, weather, sky, depth-sorted drawing. Plain C, no Mac. |
| `src/present.c` | Paints only what changed since the last frame, as runs of `PaintRect`. |
| `src/mac.h` | The Toolbox traps used, as inline `A-line` traps. |
| `src/lib68.S` | 32-bit multiply/divide for the 68000, PC-relative (libgcc's are not). |
| `src/module.r` | Resources: name, help text, sliders, heap request. |
| `tools/preview.c` | Runs the same scene natively; checks the diff painter frame by frame. |
| `tools/binhex.py` | BinHex 4.0 encoder for the shareable `.hqx`. |

The scene draws into a 160 x 120 buffer of palette indices every tick; the module compares
it with the previous frame and paints only the changed runs, so there is no flicker and a
still frame costs nothing.

### The After Dark interface (there is no SDK online; this was read off Berkeley's own example)

Berkeley's "Bouncing Ball" (After Dark 2.0) is the sample module they shipped to
developers. Disassembling it gives:

- A module is a file of type `ADgm`, creator `ADrk`. Its code is resource `'ADgm'` id 0,
  entered at byte 0 as
  `pascal OSErr main(Handle *storage, RgnHandle blankRgn, short message, GMParamBlockPtr params)`.
- Messages: 0 Initialize, 1 Close, 2 Blank, 3 DrawFrame; 8 and up are setup buttons.
- The parameter block: `controlValues[4]` at 0 (resources 1000..1003), a monitors list
  at 8 (count, then 10-byte entries whose first field is the bounds `Rect`), `colorQDAvail`
  at 12, `systemConfig` at 14, the QuickDraw globals at 16 (After Dark's order: `thePort`,
  then `white`, `black` ... so `black` is at +12), and an error `Str255` pointer at 30.
- Blank fills `blankRgn` with black. DrawFrame draws into the current port.
- Controls: `sVal` is a 0..100 slider (resource name = label, data = default), `sUnt` its
  tick labels (a count, then value + Pascal string pairs), `xVal` a checkbox. `sysz` asks for
  heap. `TEXT` 1000 is the help text, `ADrk` 0 the name and credit.

The code is one position-independent blob with no writable data: `make` links it at two
addresses and fails if the bytes differ, and fails if any `.data`/`.bss` appears.
