"""Host tests for mods/digigrain/src/machine.c (the core 3.0 descriptor and its machine-pages page) and for the
machine's identity across src/digigrain.h and mod.json.

The real digitakt-mk1/core3.h lives in ../elekloader, which the host tests do not use. The C tests compile machine.c
(included into a test translation unit, so its static functions are reachable) against a small stub core3.h written
here. The stub supplies only names and types machine.c uses; it is NOT the real layout. So these tests verify the
behaviour of gran_on_switch, fmt_rtio, gfx_rtio and the knob table's values, but not the layout of struct cm_machine /
cm_ui / cm_knob, the value of FW_SOUND_PARAM, the argument order of fw_set_param or the value of CM_UI_TAG. Those are
checked only by the cross build against the real header (make build) and on the emulator."""
import ctypes
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

MOD_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(MOD_DIR, "src")

# The README's "Granular Parameters": a new track starts with RATE 36, SPRD 64, POS 0, RTIO 1.00, ENV 64 (8.8 words);
# RTIO spans 0.25 .. 8.00. Knobs A TUNE, D SAMP and H LEV keep their stock behaviour.
README_DEFAULTS = {1: 36 << 8, 2: 64 << 8, 4: 0, 5: 0x100, 6: 64 << 8}
STOCK_KNOBS = (0, 3, 7)
RTIO_LO, RTIO_HI = 0x40, 0x800          # 0.25 and 8.00 in 8.8
KNOB_SLOT0 = 17                         # knob k is the sound's slot 17 + k
# ONESHOT's values the stock machine switch writes (PLAY FWD, BR 0, STRT 0, LEN 120, LOOP 0), by knob
STOCK_RESET = {1: 0x0300, 2: 0, 4: 0, 5: 0x7800, 6: 0}
LEN_ARC = 120 << 8                      # LEN's graphic, which RTIO reuses
STOCK_BYTE_RANGE = (0, 0x7f00)          # a stock 0..127 knob as an 8.8 word

STUB_CORE3 = r"""#pragma once
/* TEST STUB, not elekloader's core3.h: names and types only, layout guessed. */
#include <stdint.h>
#define CM_RANGE   1
#define CM_DEFAULT 2
#define CM_UI_TAG  0x55490000
#define cm_ui_v3   3
#define STUB_PARAM_OFS 0x40
#define FW_SOUND_PARAM(n) (STUB_PARAM_OFS + 2 * (n))
struct cm_knob {
    const char *name, *lname;
    int32_t look, flags, min, max, def;
    int32_t (*fmt)(char *buf, int32_t v, int32_t ctx, int32_t m);
    int32_t (*gfx)(int32_t v);
};
struct cm_ui {
    int32_t abi, page_from;
    const char *group;
    void (*on_switch)(int32_t track, int32_t from, uint8_t *sound);
    struct cm_knob knob[8];
};
struct cm_machine {
    int32_t id;
    const char *name, *short_name;
    const void *icon;
    int32_t params;
    int32_t render;
    int32_t tag;
    const struct cm_ui *ui;
};
extern const char fw_bitmap_vt[];
void fw_set_param(int32_t value, int32_t track, int32_t slot);
"""

HARNESS = r"""
#include "digitakt-mk1/core3.h"
#include "machine.c"

const char fw_bitmap_vt[4];
int32_t t_calls[16][3];
int t_ncalls;
void fw_set_param(int32_t value, int32_t track, int32_t slot)
{
    if (t_ncalls < 16) { t_calls[t_ncalls][0] = value; t_calls[t_ncalls][1] = track; t_calls[t_ncalls][2] = slot; }
    t_ncalls++;
}
int t_param_ofs(void) { return FW_SOUND_PARAM(0); }
void t_on_switch(int32_t track, uint8_t *sound) { t_ncalls = 0; gran_page.on_switch(track, 0, sound); }
int32_t t_fmt_rtio(int32_t v, char *buf) { return gran_page.knob[5].fmt(buf, v, 0, 0); }
int32_t t_gfx_rtio(int32_t v) { return gran_page.knob[5].gfx(v); }
int32_t t_knob(int k, int field)
{
    const struct cm_knob *n = &gran_page.knob[k];
    switch (field) {
    case 0: return n->flags; case 1: return n->min; case 2: return n->max; case 3: return n->def;
    case 4: return n->name != 0; default: return n->fmt != 0;
    }
}
int32_t t_machine_id(void) { return digigrain_granular_machine.id; }
int t_machine_ui_is_page(void) { return digigrain_granular_machine.ui == &gran_page; }
const char *t_machine_name(void) { return digigrain_granular_machine.name; }
"""
F_FLAGS, F_MIN, F_MAX, F_DEF, F_NAMED, F_FMT = range(6)
CM_RANGE, CM_DEFAULT = 1, 2


def load_mod():
    with open(os.path.join(MOD_DIR, "mod.json")) as f:
        return json.load(f)


def header_machine_id():
    with open(os.path.join(SRC, "digigrain.h")) as f:
        m = re.search(r"^#define\s+DIGIGRAIN_MACHINE_ID\s+(\w+)", f.read(), re.M)
    return int(m.group(1), 0) if m else None


class Identity(unittest.TestCase):
    """mod.json, the header and the sources agree on the machine (pure Python, needs no compiler)."""

    def test_header_id_equals_mod_json(self):
        mod = load_mod()
        self.assertEqual(len(mod["machines"]), 1)
        self.assertIsNotNone(header_machine_id(), "DIGIGRAIN_MACHINE_ID not found in digigrain.h")
        self.assertEqual(header_machine_id(), mod["machines"][0]["id"])

    def test_description_names_the_id(self):
        self.assertIn("(id %d)" % load_mod()["machines"][0]["id"], load_mod()["description"])

    def test_descriptor_is_defined_non_static(self):
        mod = load_mod()
        name = mod["machines"][0]["descriptor"]
        pat = re.compile(r"^(?!static)[^\n;]*\bstruct\s+cm_machine\s+%s\s*=" % re.escape(name), re.M)
        hits = []
        for s in mod["sources"]:
            with open(os.path.join(MOD_DIR, s)) as f:
                if pat.search(f.read()):
                    hits.append(s)
        self.assertEqual(len(hits), 1, "descriptor %s defined in %s" % (name, hits))

    def test_requires_core_and_machine_pages(self):
        req = load_mod()["requires"]
        self.assertIn("core", req)
        self.assertIn("machine-pages", req)


@unittest.skipUnless(shutil.which("cc"), "no host C compiler")
class Machine(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        os.makedirs(os.path.join(cls.tmp, "digitakt-mk1"))
        with open(os.path.join(cls.tmp, "digitakt-mk1", "core3.h"), "w") as f:
            f.write(STUB_CORE3)
        harness = os.path.join(cls.tmp, "harness.c")
        with open(harness, "w") as f:
            f.write(HARNESS)
        lib = os.path.join(cls.tmp, "libmachine.so")
        subprocess.check_call(["cc", "-O2", "-shared", "-fPIC", "-Wall", "-Werror", "-I", cls.tmp, "-I", SRC,
                               "-o", lib, harness, os.path.join(SRC, "readout.c")])
        cls.lib = ctypes.CDLL(lib)
        cls.lib.t_on_switch.argtypes = [ctypes.c_int32, ctypes.c_void_p]
        cls.lib.t_fmt_rtio.argtypes = [ctypes.c_int32, ctypes.c_char_p]
        cls.lib.t_gfx_rtio.argtypes = [ctypes.c_int32]
        cls.lib.t_machine_name.restype = ctypes.c_char_p
        cls.ofs = cls.lib.t_param_ofs()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    # --- gran_on_switch -------------------------------------------------------------------------------------
    def sound(self, words):
        buf = (ctypes.c_uint8 * (self.ofs + 64))(*([0xa5] * (self.ofs + 64)))
        w = (ctypes.c_uint16 * 8).from_buffer(buf, self.ofs)
        for k, v in enumerate(words):
            w[k] = v
        return buf, w

    def calls(self):
        n = ctypes.c_int.in_dll(self.lib, "t_ncalls").value
        arr = ((ctypes.c_int32 * 3) * 16).in_dll(self.lib, "t_calls")
        return [tuple(arr[i]) for i in range(min(n, 16))], n

    def reset_words(self):
        words = [0x1111, 0, 0, 0x3333, 0, 0, 0, 0x7777]      # stock knobs hold arbitrary values
        for k, v in STOCK_RESET.items():
            words[k] = v
        return words

    def test_switch_from_stock_reset_writes_defaults(self):
        words = self.reset_words()
        buf, w = self.sound(words)
        self.lib.t_on_switch(5, buf)
        for k in range(8):
            self.assertEqual(w[k], README_DEFAULTS.get(k, words[k]), k)
        calls, n = self.calls()
        self.assertEqual(n, len(README_DEFAULTS))
        self.assertEqual(sorted(calls), sorted((v, 5, KNOB_SLOT0 + k) for k, v in README_DEFAULTS.items()))
        self.assertEqual(bytes(buf[:self.ofs]), b"\xa5" * self.ofs)            # nothing before the words
        self.assertEqual(bytes(buf[self.ofs + 16:]), b"\xa5" * 48)             # nor after them

    def test_switch_leaves_a_customised_sound_alone(self):
        for k in STOCK_RESET:
            for delta in (1, 0x100):
                with self.subTest(knob=k, delta=delta):
                    words = self.reset_words()
                    words[k] = (words[k] + delta) & 0xffff
                    buf, w = self.sound(words)
                    self.lib.t_on_switch(2, buf)
                    self.assertEqual(list(w), words)
                    self.assertEqual(self.calls()[1], 0)

    def test_switch_ignores_stock_knobs(self):
        for other in (0, 0xffff):
            words = self.reset_words()
            for k in STOCK_KNOBS:
                words[k] = other
            buf, w = self.sound(words)
            self.lib.t_on_switch(0, buf)
            self.assertEqual(self.calls()[1], len(README_DEFAULTS), other)

    # --- RTIO readout and graphic ---------------------------------------------------------------------------
    def rtio(self, v):
        buf = ctypes.create_string_buffer(b"\xaa" * 16)
        r = self.lib.t_fmt_rtio(v, buf)
        return r, buf.value.decode()

    def test_rtio_text_known_values(self):
        self.assertEqual(self.rtio(RTIO_LO), (1, "0.25"))
        self.assertEqual(self.rtio(RTIO_HI), (1, "8.00"))
        self.assertEqual(self.rtio(0x100), (1, "1.00"))
        self.assertEqual(self.rtio(0x180), (1, "1.50"))

    def test_rtio_text_clamps_out_of_range(self):
        for v, s in ((0, "0.25"), (RTIO_LO - 1, "0.25"), (-1, "0.25"), (-0x8000, "0.25"),
                     (RTIO_HI + 1, "8.00"), (0x7fff, "8.00"), (0x10000, "8.00")):
            self.assertEqual(self.rtio(v), (1, s), v)

    def test_rtio_text_is_the_rounded_ratio(self):
        for v in range(RTIO_LO, RTIO_HI + 1):
            hundredths = (v * 100 * 2 + 256) // 512                     # round half up of v / 256 * 100
            self.assertEqual(self.rtio(v)[1], "%d.%02d" % divmod(hundredths, 100), v)

    def test_rtio_graphic_spans_the_len_arc(self):
        g = self.lib.t_gfx_rtio
        self.assertEqual(g(RTIO_LO), 0)
        self.assertEqual(g(RTIO_HI), LEN_ARC)
        self.assertEqual(g(0), 0)
        self.assertEqual(g(-1), 0)
        self.assertEqual(g(0x7fff), LEN_ARC)
        one = (0x100 - RTIO_LO) * LEN_ARC / (RTIO_HI - RTIO_LO)
        self.assertLessEqual(abs(g(0x100) - one), 1)
        prev = -1
        for v in range(RTIO_LO, RTIO_HI + 1):
            self.assertGreaterEqual(g(v), prev, v)
            prev = g(v)

    # --- knob table and descriptor --------------------------------------------------------------------------
    def test_knob_defaults_match_readme_and_lie_in_range(self):
        k = self.lib.t_knob
        for i in STOCK_KNOBS:
            self.assertEqual((k(i, F_FLAGS), k(i, F_NAMED)), (0, 0), "knob %d is stock" % i)
        for i, d in README_DEFAULTS.items():
            with self.subTest(knob=i):
                flags = k(i, F_FLAGS)
                if flags & CM_DEFAULT:
                    self.assertEqual(k(i, F_DEF), d)
                lo, hi = (k(i, F_MIN), k(i, F_MAX)) if flags & CM_RANGE else STOCK_BYTE_RANGE
                self.assertLessEqual(lo, d)
                self.assertLessEqual(d, hi)
        self.assertEqual((k(5, F_MIN), k(5, F_MAX)), (RTIO_LO, RTIO_HI))
        for i in (1, 2, 5, 6):
            self.assertTrue(k(i, F_FMT), "knob %d has a value text" % i)

    def test_descriptor(self):
        self.assertEqual(self.lib.t_machine_id(), header_machine_id())
        self.assertEqual(self.lib.t_machine_id(), load_mod()["machines"][0]["id"])
        self.assertEqual(self.lib.t_machine_name(), b"GRANULAR")
        self.assertEqual(self.lib.t_machine_ui_is_page(), 1)


if __name__ == "__main__":
    unittest.main()
