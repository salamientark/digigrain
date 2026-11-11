---
name: digitakt-mod-json
description: Source mod.json schema for elekloader Digitakt mods: fields, subscribe, sites ops, contribute/collections, resources, memory sections. Use when writing or editing mod.json or choosing sections.
---

# mod.json (source format, consumed by `elekloader.sdk.build`)

Authoritative: `../elekloader/elekloader/sdk/build.py` and `docs/FORMAT.md`; template `../elekloader/examples/hello-marker/`. Unknown fields are rejected.

```json
{
 "id": "digigrain", "version": "0.1.0",
 "title": "...", "category": "Machines", "author": "...",
 "license": "GPL-2.0-or-later", "description": "...",
 "device": "digitakt-mk1", "os": "1.53",
 "sources": ["src/granular.c", "src/machine.c", "src/synth.s"],
 "requires": ["core", "machine-pages"],
 "machines": [{"id": 10, "descriptor": "digigrain_granular_machine"}],
 "resources": {"core": "3.0"},
 "subscribe": [{"event": "ev_render_voices", "fn": "digigrain_granular_render", "order": 20}],
 "ports": {"1.54": {"cflags": ["-DDIGIGRAIN_SMP_TAB=0x4031a3a0"], "sites": ["..."]}}
}
```
(Shape abridged from mods/digigrain/mod.json; a mod with no machine needs only `requires: ["core"]` and no `machines`.)

| field | notes |
|---|---|
| `id` | lowercase, unique; prefix of every global symbol |
| `device`/`os` | checked against the stock file |
| `sources` | `.c` (gcc, profile cflags, `-I moddir`) and anything else via `as` |
| `defsym` | `{NAME: value}` passed to `as` only |
| `cflags` | extra gcc flags |
| `name_string` | emits `str_name` = "<name_string> <version>" in `.run` |
| `subscribe` | `{event, fn, order}`; `fn` is a global you define; order default 50, ascending, ties by mod id |
| `sites` | patches to stock (below) |
| `collections` | `{"table": entry_size}` tables you declare (size multiple of 4) |
| `contribute` | `{to, order, data(hex), relocs:[[off,"abs32\|pc32\|pc16","sym:NAME\|sec:NAME\|abs",addend]], claims:[[lo,hi]]}` adds entries to another mod's table; `claims` are image ranges rewritten at run time |
| `weak` | imports that may be missing (resolve to `core_zero`) |
| `copied` | image block the mod copies elsewhere at run time |
| `resources` | `{"names":[...], "regions":[{name,lo,hi}]}`; mods/digigrain/mod.json also has `"core": "3.0"` |
| `requires`/`conflicts` | `requires` always has `"core"`; mods/digigrain/mod.json has `["core", "machine-pages"]`; `tools/check_boundaries.py` allows only those two |
| `machines` | as used in mods/digigrain/mod.json: `[{"id": 10, "descriptor": "digigrain_granular_machine"}]` (a global `struct cm_machine`, see `digitakt-machines`) |
| `ports` | per-OS overrides, e.g. mods/digigrain/mod.json `"1.54": {"cflags": [...], "sites": [...]}` (digislicer has one too); `templates/mod/mod.json` has an empty `"1.54": {}` for a mod that names no firmware address |

## sites
`{"addr":"0x...","stock":"<hex of whole stock insns>","op":...,"target":"sym" | "new":"hex","kind":"code"}`

| op | result |
|---|---|
| `jsr` / `jmp` | `4eb9`/`4ef9` + target, `4e71` nop-padded to stock length (stock must be >= 6 bytes, even) |
| `keep2` | stock opcode word + new 32-bit address (redirect a `jsr.l`/`lea.l`) |
| `ptr` | 4-byte address as data (vtable slot) |
| `bytes` | `new` hex, same length as `stock`; add `"kind":"code"` for instructions |

The SDK checks stock bytes at build time. Your routine must redo the displaced instructions' work and preserve every register the surrounding code relies on. A mod with only `sites` and no sources needs no compiler.

## Sections and budgets
- Only `.run` (code+data+rodata, in RAM), `.fast`, `.bss` (and core's `.boot`). Initialised data in `.run` is fine; `.bss` is zeroed by core at boot.
- RAM: DDR 0x47BE0000-0x47C00000 = **128 KB shared by all mods** (`.run` + tables + `.fast` load images + `.bss`). ~100 KB free with digi mods loaded.
- `.fast` SRAM 0x8000F700-0x80010000 = 2304 B, needs the `fast-audio` mod (`fa_copies`); otherwise use `.run`.
- Other free areas: sram-block 0x80003360-0x80008000 (FAST AUDIO's copy area).
- No printf/malloc/libc. Firmware routines are called through function pointers at fixed addresses (they need no relocation).

## resources.names examples
`sysex:0x7d`, `settings:MY ROW`, `drive:/cfw/mine.bin`, `ui:SRC page:hold YES`, `machine:5`, `param:SLICE PLAY=4`. Core claims `timer:DTIM0`.
