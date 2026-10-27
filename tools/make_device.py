#!/usr/bin/env python3
"""Builds "TRoss 303.amxd" (a Max for Live instrument) from this description.

The device: tross303~ (the engine), a jsui panel (device/tross303_panel.js) and the live.*
controls laid over it, all parameters automatable and saved with the Live set. The 64
patterns are kept in the Live set through a hidden pattr.

    python3 tools/make_device.py [out.amxd]
"""
import json
import struct
import sys
from pathlib import Path

W, H = 760, 169
RX = 450  # right block, matches tross303_panel.js

boxes, lines = [], []
params = {}
_n = [0]


def nid():
    _n[0] += 1
    return f"obj-{_n[0]}"


def box(maxclass, rect, pres=None, **kw):
    b = {"id": nid(), "maxclass": maxclass, "patching_rect": list(map(float, rect))}
    if pres is not None:
        b["presentation"] = 1
        b["presentation_rect"] = list(map(float, pres))
    b.update(kw)
    boxes.append({"box": b})
    return b["id"]


def obj(text, rect, nin=1, nout=1, otypes=None, **kw):
    return box("newobj", rect, text=text, numinlets=nin, numoutlets=nout,
               outlettype=otypes if otypes is not None else [""] * nout, **kw)


def msg(text, rect):
    return box("message", rect, text=text, numinlets=2, numoutlets=1, outlettype=[""])


def wire(src, so, dst, di):
    lines.append({"patchline": {"source": [src, so], "destination": [dst, di]}})


def live_param(maxclass, rect, pres, longname, short, ptype, mmin=None, mmax=None, initial=None,
               enum=None, unitstyle=None, units=None, nout=2, otypes=None, extra=None, exponent=None):
    v = {"parameter_longname": longname, "parameter_shortname": short, "parameter_type": ptype}
    if mmin is not None:
        v["parameter_mmin"] = float(mmin)
    if mmax is not None:
        v["parameter_mmax"] = float(mmax)
    if enum is not None:
        v["parameter_enum"] = enum
    if initial is not None:
        v["parameter_initial_enable"] = 1
        v["parameter_initial"] = [initial]
    if unitstyle is not None:
        v["parameter_unitstyle"] = unitstyle
    if units is not None:
        v["parameter_units"] = units
    if exponent is not None:
        v["parameter_exponent"] = float(exponent)
    kw = {"numinlets": 1, "numoutlets": nout, "outlettype": otypes or ["", "float"][:nout] + [""] * (nout - 2),
          "parameter_enable": 1, "saved_attribute_attributes": {"valueof": v}, "varname": longname}
    if extra:
        kw.update(extra)
    i = box(maxclass, rect, pres, **kw)
    params[i] = [longname, short, 0]
    return i


# ---- Engine and I/O ------------------------------------------------------------------------

eng = obj("tross303~", (40, 520, 120, 22), 1, 3, ["signal", "", ""])
phasor = obj("plugphasor~", (40, 470, 80, 22), 1, 1, ["signal"])
wire(phasor, 0, eng, 0)
out = obj("plugout~", (40, 580, 60, 22), 2, 0, [])
wire(eng, 0, out, 0)
wire(eng, 0, out, 1)

midiin = obj("midiin", (200, 400, 50, 22), 1, 1, ["int"])
mparse = obj("midiparse", (200, 430, 100, 22), 1, 8, ["", "", "", "int", "int", "", "int", ""])
pnote = obj("prepend note", (200, 460, 80, 22))
wire(midiin, 0, mparse, 0)
wire(mparse, 0, pnote, 0)
wire(pnote, 0, eng, 0)

# The 64 patterns, saved in the Live set (blob parameter, not automatable).
store = box("newobj", (400, 580, 160, 22), text="pattr tross303bank", numinlets=1, numoutlets=3,
            outlettype=["", "", ""], parameter_enable=1,
            saved_object_attributes={"parameter_enable": 1, "parameter_mappable": 0},
            saved_attribute_attributes={"valueof": {"parameter_longname": "tross303bank",
                                                    "parameter_shortname": "bank",
                                                    "parameter_type": 3,
                                                    "parameter_invisible": 1}},
            varname="tross303bank")
params[store] = ["tross303bank", "bank", 0]
wire(eng, 2, store, 0)
wire(store, 0, eng, 0)

# Panel
panel = box("jsui", (600, 40, W, H), (0, 0, W, H), filename="tross303_panel.js", numinlets=1,
            numoutlets=1, outlettype=[""], border=0, parameter_enable=0, background=1)
wire(panel, 0, eng, 0)
wire(eng, 1, panel, 0)

# Device start: every control sends its value, then the panel asks for the patterns.
thisdev = obj("live.thisdevice", (40, 20, 90, 22), 1, 3, ["bang", "int", "int"])
trig = obj("t b b", (40, 50, 40, 22), 1, 2, ["bang", "bang"])
refresh = msg("refresh", (40, 80, 50, 22))
wire(thisdev, 0, trig, 0)
wire(trig, 0, refresh, 0)
wire(refresh, 0, eng, 0)

# Live's play state: live_set is_playing -> "transport 0/1".
lpath_msg = msg("path live_set", (200, 50, 80, 22))
lpath = obj("live.path", (200, 80, 80, 22), 1, 3, ["", "", ""])
lprop = msg("property is_playing", (300, 80, 110, 22))
lobs = obj("live.observer", (200, 110, 100, 22), 2, 2, ["", ""])
ptrans = obj("prepend transport", (200, 140, 100, 22))
wire(trig, 1, lpath_msg, 0)
wire(trig, 1, lprop, 0)
wire(lpath_msg, 0, lpath, 0)
wire(lpath, 0, lobs, 1)
wire(lprop, 0, lobs, 0)
wire(lobs, 0, ptrans, 0)
wire(ptrans, 0, eng, 0)

ctl = []  # (box id, message) pairs: each control's value goes to the engine as "<message> <value>"
          # and to the panel as "ctl <message> <value>" so it can draw the knob or switch.

# The panel draws the knobs, switches and lock keys; these live.* controls lie invisibly on top
# (automatable, mappable, saved with the set). Menus and number boxes are visible readouts.
CLEAR = [0.0, 0.0, 0.0, 0.0]
invisible_dial = {k: CLEAR for k in ("dialcolor", "activedialcolor", "needlecolor", "activeneedlecolor",
                                     "panelcolor", "focusbordercolor", "textcolor", "tricolor")}
invisible_dial.update(showname=0, shownumber=0)
invisible_button = {k: CLEAR for k in ("bgcolor", "bgoncolor", "activebgcolor", "activebgoncolor", "bordercolor",
                                       "focusbordercolor", "textcolor", "textoncolor", "activetextcolor",
                                       "activetextoncolor")}
readout_style = {"activebgcolor": [0.08, 0.04, 0.03, 1.0], "bgcolor": [0.08, 0.04, 0.03, 1.0],
                 "textcolor": [1.0, 0.27, 0.16, 1.0], "tricolor": [1.0, 0.27, 0.16, 1.0],
                 "activeslidercolor": [0.77, 0.61, 0.24, 1.0], "bordercolor": CLEAR, "focusbordercolor": CLEAR,
                 "fontsize": 7.5, "fontname": "Arial Bold"}

# ---- Sound knobs (centres match knobX() in tross303_panel.js) --------------------------------

knobs = [("Tuning", "tuning", 5.0), ("Cutoff", "cutoff", 5.0), ("Resonance", "resonance", 5.0),
         ("Env Mod", "envmod", 5.0), ("Decay", "decay", 5.0), ("Accent", "accent", 5.0),
         ("Volume", "volume", 7.0)]
for k, (name, m, init) in enumerate(knobs):
    cx, cy = 34 + k * 50, 42
    i = live_param("live.dial", (40 + k * 60, 140, 34, 34), (cx - 17, cy - 17, 34, 34), name, name, 0, 0, 10,
                   init, unitstyle=1, extra=dict(invisible_dial))
    ctl.append((i, m))

SX = 388
wave = live_param("live.tab", (500, 140, 28, 14), (SX + 2, 31, 28, 14), "Waveform", "Wave", 2,
                  enum=["SAW", "SQR"], initial=0, nout=3, otypes=["", "", "float"],
                  extra=dict(invisible_button, num_lines_patching=1, num_lines_presentation=1))
ctl.append((wave, "wave"))

transp = live_param("live.numbox", (560, 140, 24, 11), (SX + 36, 32, 24, 11), "Transpose", "Transp", 1, -24, 24, 0,
                    unitstyle=7, extra=dict(readout_style))
ctl.append((transp, "transpose"))

seq = live_param("live.text", (620, 140, 22, 19), (SX + 37, 48, 22, 19), "Sequencer", "Seq", 2,
                 enum=["off", "on"], initial=1, extra=dict(invisible_button, text="", texton="", mode=1))
ctl.append((seq, "seq"))

cutmax = live_param("live.numbox", (680, 140, 40, 11), (294, 9.5, 40, 11), "Cut Max", "CutMax", 0, 1200, 3000, 2300,
                    unitstyle=3, extra=dict(readout_style))
ctl.append((cutmax, "cutmax"))

# ---- Random creator (matches drawRandomBlock() in tross303_panel.js) -------------------------

scales = ["Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Mixolydian", "Harm Minor", "Min Penta",
          "Blues", "Acid"]
rscale = live_param("live.menu", (40, 220, 54, 12), (650, 85, 54, 12), "Random Scale", "Scale", 2,
                    enum=scales, initial=9, nout=3, otypes=["", "", "float"], extra=dict(readout_style))
ctl.append((rscale, "rscale"))
roots = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
rroot = live_param("live.menu", (130, 220, 30, 12), (708, 85, 30, 12), "Random Root", "Root", 2,
                   enum=roots, initial=0, nout=3, otypes=["", "", "float"], extra=dict(readout_style))
ctl.append((rroot, "rroot"))

for k, (name, m, init) in enumerate([("Rnd Density", "rdensity", 70.0), ("Rnd Accent", "raccent", 30.0),
                                     ("Rnd Slide", "rslide", 25.0), ("Rnd Octave", "rspread", 30.0)]):
    cx, cy = 478 + k * 42, 103
    i = live_param("live.dial", (40 + k * 60, 260, 28, 28), (cx - 14, cy - 14, 28, 28), name, name.split()[1], 0,
                   0, 100, init, unitstyle=5, extra=dict(invisible_dial))
    ctl.append((i, m))

locks = [("Lock Notes", "notes", "NOTE"), ("Lock Octaves", "octaves", "OCT"), ("Lock Accents", "accents", "ACC"),
         ("Lock Slides", "slides", "SLIDE"), ("Lock Time", "time", "TIME")]
for k, (name, lane, label) in enumerate(locks):
    i = live_param("live.text", (40 + k * 60, 320, 38, 12), (462 + k * 42, 145, 38, 12), name, label, 2,
                   enum=["off", "on"], initial=0, extra=dict(invisible_button, text="", texton="", mode=1))
    ctl.append((i, "rlock " + lane))

toggles = {seq} | {i for i, m in ctl if m.startswith("rlock ")}

# Wire every control: value -> [prepend msg] -> engine, value -> [prepend ctl msg] -> panel,
# device start -> bang -> control (so everything starts in sync).
for k, (i, m) in enumerate(ctl):
    x = 40 + k * 120
    p = obj("prepend " + m, (x, 380, 110, 22))
    q = obj("prepend ctl " + m, (x, 410, 110, 22))
    wire(i, 0, p, 0)
    wire(p, 0, eng, 0)
    wire(i, 0, q, 0)
    wire(q, 0, panel, 0)
    # A bang makes dials, menus and number boxes report their value, but it FLIPS a live.text
    # toggle, so the toggles are left alone: Live sends their stored value itself at load.
    if i not in toggles:
        wire(trig, 1, i, 0)

# ---- Patcher ---------------------------------------------------------------------------------

patcher = {
    "patcher": {
        "fileversion": 1,
        "appversion": {"major": 8, "minor": 6, "revision": 0, "architecture": "x64", "modernui": 1},
        "classnamespace": "box",
        "rect": [100.0, 100.0, 1400.0, 700.0],
        "openrect": [0.0, 0.0, 0.0, float(H)],
        "bglocked": 0,
        "openinpresentation": 1,
        "default_fontsize": 10.0,
        "default_fontface": 0,
        "default_fontname": "Arial Bold",
        "gridonopen": 1,
        "gridsize": [8.0, 8.0],
        "gridsnaponopen": 1,
        "objectsnaponopen": 1,
        "statusbarvisible": 2,
        "toolbarvisible": 1,
        "lefttoolbarpinned": 0,
        "toptoolbarpinned": 0,
        "righttoolbarpinned": 0,
        "bottomtoolbarpinned": 0,
        "toolbars_unpinned_last_save": 0,
        "tallnewobj": 0,
        "boxanimatetime": 200,
        "enablehscroll": 1,
        "enablevscroll": 1,
        "devicewidth": float(W),
        "description": "TRoss 303 — TB-303 style bass synth and sequencer. Ticco Ross.",
        "digest": "",
        "tags": "",
        "style": "",
        "subpatcher_template": "",
        "assistshowspatchername": 0,
        "boxes": boxes,
        "lines": lines,
        "parameters": dict(params, **{"parameterbanks": {}, "inherited_shortname": 1}),
        "autosave": 0,
    }
}


def write_amxd(path):
    data = json.dumps(patcher, indent="\t").encode("utf-8") + b"\n\x00"
    # Unfrozen device: 'ampf' header, device type ('iiii' = instrument), meta, then the patcher.
    blob = (b"ampf" + struct.pack("<I", 4) + b"iiii" + b"meta" + struct.pack("<I", 4) + b"\x00\x00\x00\x00"
            + b"ptch" + struct.pack("<I", len(data)) + data)
    Path(path).write_bytes(blob)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else str(Path(__file__).resolve().parent.parent / "device" / "TRoss 303.amxd")
    write_amxd(out)
    print(f"wrote {out}: {len(boxes)} boxes, {len(lines)} lines, {len(params)} parameters")
