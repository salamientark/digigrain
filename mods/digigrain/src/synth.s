| SPDX-License-Identifier: GPL-2.0-or-later
| digigrain: wrapper around the render ISR's first voice-synth call.
| The ISR calls 0x40075184 with five stack arguments (a2, the engine, d5, d7, 0x80001f18).
| It reads each voice's PLAY word (and decides a non-looping voice's end), so
| digigrain_granular_pre() (which neutralises the GRANULAR shadow's PLAY, STRT, LEN and LOOP
| words) must run before it: digigrain_synth_pre does that and then calls the stock routine.
| The second synth call and the render after it are machine-pages' ev_render_voices.
| Registers d2-d7/a2-a6 are kept by the C ABI and the stock routine; the caller pops its own
| arguments, so the wrapper only pops its copies. The same address on OS 1.53 and 1.54.
        .equ    SYNTH_PRE,  0x40075184

        .section .run, "ax"
        .globl  digigrain_synth_pre
digigrain_synth_pre:
        jsr     digigrain_granular_pre
        .rept   5
        move.l  20(%sp), -(%sp)
        .endr
        jsr     SYNTH_PRE
        lea     20(%sp), %sp
        rts
