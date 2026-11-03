# digigrain collection

Custom firmware mods for the Elektron Digitakt (mk1, OS 1.53 and 1.54), built with
[elekloader](../elekloader). One directory per mod under `mods/`; shared code in `common/`.
Open work and test status are in the GitHub issue tracker. Design notes for
contributors and agents are in `.claude/skills/` and each mod's `CLAUDE.md`.

| mod | what |
|---|---|
| [`digigrain`](mods/digigrain) | a granular playback SRC machine (id 6, GRANULAR); 0.0.x verified in the emulator on OS 1.53, 0.1.0 (machine-pages, OS 1.54) emulator-checked only as noted in the commit log |

## Prerequisites

- Python 3.9+ (`.python-version` pins 3.12.0)
- m68k binutils + gcc: `brew install m68k-elf-gcc m68k-elf-binutils` (scripts
  set `ELEKLOADER_CROSS=m68k-elf-` automatically), or a container with
  Debian's `gcc-m68k-linux-gnu` and the repo mounted
- Sibling checkouts: `../elekloader` (core 3.0+ and its `machine-pages` mod) and
  the stock OS, by default `../stock/Digitakt_OS1.54_dist.zip` (override with
  `ELEKLOADER_DIR`, `ELEKLOADER_STOCK`; a 1.53 file builds the 1.53 variant)
- Optional, for emulator tests: `pip install -r requirements-dev.txt`

## Quickstart

```sh
make build   # core + every mod -> out/core, out/<mod>/
make lint    # dependency-rule check, then each mod alone and combined with core
make patch   # out/<mod>/test.syx = stock + core + that mod
make test    # unit tests (stdlib only)
make check   # all of the above
make check MOD=digigrain    # one mod only
make new-mod NAME=my-mod    # scaffold a new mod from templates/mod
```

## Layout

- `mods/<name>/` — one self-contained mod: `mod.json`, `src/`, `tests/` (host tests; `tests/emu/` digiemu probes), optional `tools/` generators, `CLAUDE.md`
- `common/` — code shared by mods (`include/`, `src/`, `tests/`); mods opt in per file. Mods depend on `common/`, never on each other (enforced by `tools/check_boundaries.py` in `make lint`)
- `scripts/` — mod-agnostic build/lint/patch/test/check wrappers (`MOD=<name>` selects one mod)
- `tests/` — checks that apply to every mod (`test_build.py`)
- `tools/` — `stage_mod.py` (stages a mod with `common/` for the SDK), `check_boundaries.py`
- `templates/mod/` — scaffold used by `make new-mod`
- `.claude/skills/` — agent notes for mod development (platform knowledge shared by all mods)

## Flashing and recovery

Back up first. If a build misbehaves: hold FUNC while powering on for the
startup menu, then send the stock `.syx`. Never commit firmware (`*.syx`,
`*.bin`, `*.elemod` are ignored).

## Granular Parameters

- A. TUNE: Pitch of each grain. Independent of grain length.
- B. RATE: Grain spawn rate. Nothing at noon, counter-clockwise for periodic
  rates, clockwise for random.
- C. SPRD: Random grain variation. None at noon, counter-clockwise randomizes
  POS, clockwise randomizes TUNE.
- D. SAMP: Sample selection.
- E. POS: Grain playhead, the position where grains start when spawned.
- F. RTIO: Grain length as a ratio of the spawn period, 0.25 to 8.00 (1:4 to
  8:1). At the maximum RTIO with a periodic RATE, 8 grains play at once.
- G. ENV: Grain envelope. Sine at noon, counter-clockwise for gate, clockwise
  for a quick decay.
- H. LEV: Source level.

### Implementation notes

- Grains play forward only. There is no PLAY parameter: the stock voice the
  machine borrows is forced to loop, so a held note sustains.
- Grain length is RTIO times the spawn period, independent of pitch, so a
  voice never has more than 8 grains (the per-voice pool size). Length is
  capped at 65535 frames (1.37 s).
- A new GRANULAR track starts with RATE 36 (periodic, about 24 Hz), SPRD 64
  (none), POS 0, RTIO 1.00 and ENV 64 (sine).
- The SRC and LFO pages are drawn by elekloader's `machine-pages` mod from the
  descriptor in `src/machine.c` (core 3.0): the labels, ranges, defaults and the
  value texts (RATE in Hz / OFF, SPRD as POS or PIT percent, RTIO as a ratio,
  ENV as SINE / GATE / DCAY percent). The one site the mod still hooks is the
  call before the stock voice synth, where the shadow voice's words are set.
- Knob k is the sound's slot 17 + k. 0.0.x sounds, which kept RATE, SPRD and
  ENV in other slots, read differently after 0.1.0.
- Open work (further optimisation, hardware tests) is in the issue tracker.
