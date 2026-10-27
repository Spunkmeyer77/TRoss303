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

# ---- Knobs (303 row) -------------------------------------------------------------------------

dial_style = {"showname": 0, "shownumber": 0,
              "dialcolor": [0.17, 0.17, 0.18, 1.0], "activedialcolor": [0.8, 0.2, 0.1, 1.0],
              "needlecolor": [0.95, 0.95, 0.93, 1.0], "activeneedlecolor": [0.95, 0.95, 0.93, 1.0],
              "panelcolor": [0.0, 0.0, 0.0, 0.0]}
knobs = [("Tuning", "tuning", 5.0), ("Cutoff", "cutoff", 5.0), ("Resonance", "resonance", 5.0),
         ("Env Mod", "envmod", 5.0), ("Decay", "decay", 5.0), ("Accent", "accent", 5.0),
         ("Volume", "volume", 7.0)]
for k, (name, m, init) in enumerate(knobs):
    i = live_param("live.dial", (40 + k * 60, 140, 44, 36), (8 + k * 44, 17, 44, 38), name, name, 0, 0, 10,
                   init, unitstyle=1, extra=dict(dial_style))
    ctl.append((i, m))

tab_style = {"activebgcolor": [0.17, 0.17, 0.18, 1.0], "activebgoncolor": [0.8, 0.2, 0.1, 1.0],
             "textcolor": [0.9, 0.9, 0.88, 1.0], "textoncolor": [1, 1, 1, 1.0],
             "fontsize": 8.0, "fontname": "Arial Bold"}
wave = live_param("live.tab", (500, 140, 50, 16), (328, 43, 48, 13), "Waveform", "Wave", 2,
                  enum=["SAW", "SQR"], initial=0, nout=3, otypes=["", "", "float"],
                  extra=dict(tab_style, num_lines_patching=1, num_lines_presentation=1))
ctl.append((wave, "wave"))

numbox_style = {"activebgcolor": [0.17, 0.17, 0.18, 1.0], "textcolor": [1.0, 0.6, 0.15, 1.0],
                "activeslidercolor": [0.8, 0.2, 0.1, 1.0], "fontsize": 8.0, "fontname": "Arial Bold"}
transp = live_param("live.numbox", (560, 140, 48, 15), (380, 43, 48, 13), "Transpose", "Transp", 1, -24, 24, 0,
                    unitstyle=7, extra=dict(numbox_style))
ctl.append((transp, "transpose"))

seq = live_param("live.text", (620, 140, 48, 15), (328, 25, 48, 13), "Sequencer", "Seq", 2,
                 enum=["off", "on"], initial=1,
                 extra=dict(text="SEQ", texton="SEQ ON", mode=1,
                            bgcolor=[0.17, 0.17, 0.18, 1.0], bgoncolor=[0.8, 0.2, 0.1, 1.0],
                            textcolor=[0.9, 0.9, 0.88, 1.0], textoncolor=[1, 1, 1, 1.0],
                            fontsize=8.0, fontname="Arial Bold"))
ctl.append((seq, "seq"))

cutmax = live_param("live.numbox", (680, 140, 48, 15), (380, 25, 48, 13), "Cut Max", "CutMax", 0, 1200, 3000, 2300,
                    unitstyle=3, extra=dict(numbox_style))
ctl.append((cutmax, "cutmax"))

# ---- Random creator ---------------------------------------------------------------------------

menu_style = {"activebgcolor": [0.24, 0.24, 0.25, 1.0], "textcolor": [0.95, 0.95, 0.93, 1.0],
              "tricolor": [0.8, 0.2, 0.1, 1.0], "fontsize": 8.0, "fontname": "Arial Bold"}
scales = ["Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Mixolydian", "Harm Minor", "Min Penta",
          "Blues", "Acid"]
rscale = live_param("live.menu", (40, 220, 80, 15), (RX + 74, 82, 80, 15), "Random Scale", "Scale", 2,
                    enum=scales, initial=9, nout=3, otypes=["", "", "float"], extra=dict(menu_style))
ctl.append((rscale, "rscale"))
roots = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
rroot = live_param("live.menu", (130, 220, 40, 15), (RX + 160, 82, 44, 15), "Random Root", "Root", 2,
                   enum=roots, initial=0, nout=3, otypes=["", "", "float"], extra=dict(menu_style))
ctl.append((rroot, "rroot"))

small_dial = dict(dial_style)
for k, (name, m, init) in enumerate([("Rnd Density", "rdensity", 70.0), ("Rnd Accent", "raccent", 30.0),
                                     ("Rnd Slide", "rslide", 25.0), ("Rnd Octave", "rspread", 30.0)]):
    cx = RX + 92 + k * 44
    i = live_param("live.dial", (40 + k * 60, 260, 36, 32), (cx - 18, 101, 36, 32), name, name.split()[1], 0,
                   0, 100, init, unitstyle=5, extra=dict(small_dial))
    ctl.append((i, m))

lock_style = dict(mode=1, bgcolor=[0.24, 0.24, 0.25, 1.0], bgoncolor=[0.92, 0.55, 0.12, 1.0],
                  textcolor=[0.9, 0.9, 0.88, 1.0], textoncolor=[0.1, 0.1, 0.1, 1.0],
                  fontsize=7.0, fontname="Arial Bold")
locks = [("Lock Notes", "notes", "NOTE"), ("Lock Octaves", "octaves", "OCT"), ("Lock Accents", "accents", "ACC"),
         ("Lock Slides", "slides", "SLIDE"), ("Lock Time", "time", "TIME")]
for k, (name, lane, label) in enumerate(locks):
    i = live_param("live.text", (40 + k * 60, 320, 40, 15), (RX + 30 + k * 44, 148, 40, 14), name, label, 2,
                   enum=["off", "on"], initial=0, extra=dict(lock_style, text=label, texton=label))
    ctl.append((i, "rlock " + lane))

# Wire every control: value -> [prepend msg] -> engine; device start -> bang -> control.
for k, (i, m) in enumerate(ctl):
    y = 380 + (k % 2) * 26
    p = obj("prepend " + m, (40 + (k // 2) * 110, y, 100, 22))
    wire(i, 0, p, 0)
    wire(p, 0, eng, 0)
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
