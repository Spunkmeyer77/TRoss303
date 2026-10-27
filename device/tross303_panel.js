// TRoss 303 — panel: 303-style face, step editor, pattern memory, chain, About window.
// Copyright (C) 2026 Ticco Ross. AGPL-3.0-or-later.
//
// jsui (ES5). Talks to tross303~:
//   in : pattern idx len s0..s15 | playstep n | playing idx | chain on len e...
//   out: select idx | setstep i field v | length n | random | undo | copy | paste | clear
//        chain 0/1 | chainadd idx | chainclear

autowatch = 1;
inlets = 1;
outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var VERSION = "1.0.1";
var GUMROAD = "https://ticcoross.gumroad.com/";
var EMAIL = "ticcoross@gmail.com";

var W = 760, H = 169;

// ---- State -------------------------------------------------------------------------------

var sel = 0;          // selected pattern 0-63
var len = 16;
var steps = [];       // packed steps
for (var i = 0; i < 16; i++) steps.push(0x10); // C, no octave, rest
var playStep = -1;
var playingIdx = 0;
var chainOn = 0;
var chainList = [];
var chainEdit = 0;
var showAbout = 0;
var drag = null;
var flash = { name: "", until: 0 };

var NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C'"];
var GROUPS = ["I", "II", "III", "IV"];

function sNote(v) { var n = v & 15; return n > 12 ? 12 : n; }
function sOct(v) { var o = (v >> 4) & 3; return o > 2 ? 2 : o; }
function sAcc(v) { return (v & 64) ? 1 : 0; }
function sSlide(v) { return (v & 128) ? 1 : 0; }
function sTime(v) { var t = (v >> 8) & 3; return t > 2 ? 0 : t; }
function pack(n, o, a, s, t) { return (n & 15) | ((o & 3) << 4) | (a ? 64 : 0) | (s ? 128 : 0) | ((t & 3) << 8); }

function grpOf(p) { return Math.floor(p / 16); }
function patOf(p) { return Math.floor((p % 16) / 2); }
function secOf(p) { return p % 2; }
function idxOf(g, p, s) { return g * 16 + p * 2 + s; }
function nameOf(p) { return GROUPS[grpOf(p)] + "-" + (patOf(p) + 1) + (secOf(p) ? "B" : "A"); }

// ---- Messages from the external -------------------------------------------------------------

function pattern() {
	var a = arrayfromargs(arguments);
	if (a.length < 18) return;
	sel = a[0];
	len = a[1];
	for (var i = 0; i < 16; i++) steps[i] = a[2 + i];
	mgraphics.redraw();
}

function playstep(n) {
	playStep = n;
	mgraphics.redraw();
}

function playing(n) {
	playingIdx = n;
	mgraphics.redraw();
}

function chain() {
	var a = arrayfromargs(arguments);
	chainOn = a[0] ? 1 : 0;
	var n = a[1] || 0;
	chainList = [];
	for (var i = 0; i < n && 2 + i < a.length; i++) chainList.push(a[2 + i]);
	mgraphics.redraw();
}

// ---- Layout -------------------------------------------------------------------------------

// Sequencer grid
var GX = 50, GY = 70, CW = 24;
var ROWS = [
	{ key: "head", y: 70, h: 9, label: "" },
	{ key: "note", y: 80, h: 22, label: "NOTE" },
	{ key: "oct", y: 103, h: 14, label: "OCT" },
	{ key: "acc", y: 118, h: 14, label: "ACCENT" },
	{ key: "slide", y: 133, h: 14, label: "SLIDE" },
	{ key: "time", y: 148, h: 17, label: "TIME" }
];

// Right block (pattern memory)
var RX = 450;
var buttons = []; // rebuilt each paint: {x,y,w,h,label,on,act}

function addButton(x, y, w, h, label, on, act, style) {
	buttons.push({ x: x, y: y, w: w, h: h, label: label, on: on, act: act, style: style || "dark" });
}

function layoutButtons() {
	buttons = [];
	var g = grpOf(sel), p = patOf(sel), s = secOf(sel);
	var x, i;
	// Groups
	for (i = 0; i < 4; i++) {
		(function (gi) {
			addButton(RX + 46 + gi * 28, 3, 26, 15, GROUPS[gi], g === gi, function () { doSelect(gi, patOf(sel), secOf(sel)); });
		})(i);
	}
	// A / B
	for (i = 0; i < 2; i++) {
		(function (si) {
			addButton(RX + 166 + si * 24, 3, 22, 15, si ? "B" : "A", s === si, function () {
				if (chainEdit) addToChain(idxOf(grpOf(sel), patOf(sel), si));
				doSelect(grpOf(sel), patOf(sel), si);
			});
		})(i);
	}
	// Patterns 1-8
	for (i = 0; i < 8; i++) {
		(function (pi) {
			addButton(RX + pi * 30, 21, 28, 16, String(pi + 1), p === pi, function () {
				if (chainEdit) addToChain(idxOf(grpOf(sel), pi, secOf(sel)));
				doSelect(grpOf(sel), pi, secOf(sel));
			}, "key");
		})(i);
	}
	// Length and edit tools
	addButton(RX, 40, 16, 12, "-", false, function () { send("length", Math.max(1, len - 1)); });
	addButton(RX + 46, 40, 16, 12, "+", false, function () { send("length", Math.min(16, len + 1)); });
	addButton(RX + 70, 40, 40, 12, "COPY", false, function () { send("copy"); flashMsg("COPIED"); });
	addButton(RX + 112, 40, 40, 12, "PASTE", false, function () { send("paste"); });
	addButton(RX + 154, 40, 40, 12, "CLEAR", false, function () { send("clear"); });
	addButton(RX + 196, 40, 40, 12, "UNDO", false, function () { send("undo"); });
	// Chain
	addButton(RX, 54, 44, 11, "CHAIN", chainOn, function () { send("chain", chainOn ? 0 : 1); }, "red");
	addButton(RX + 46, 54, 36, 11, "EDIT", chainEdit, function () { chainEdit = chainEdit ? 0 : 1; mgraphics.redraw(); }, "red");
	addButton(RX + 84, 54, 30, 11, "CLR", false, function () { send("chainclear"); });
	// Random
	addButton(RX, 72, 66, 26, "RANDOM", false, function () { send("random"); }, "red");
	// About
	addButton(W - 18, 2, 14, 12, "i", showAbout, function () { showAbout = 1; mgraphics.redraw(); }, "about");
}

function doSelect(g, p, s) {
	send("select", idxOf(g, p, s));
}

function addToChain(idx) {
	if (chainList.length >= 16) { flashMsg("CHAIN FULL"); return; }
	send("chainadd", idx);
}

function send() {
	var a = arrayfromargs(arguments);
	outlet(0, a);
}

function flashMsg(t) {
	flash.name = t;
	flash.until = new Date().getTime() + 900;
	mgraphics.redraw();
	var tsk = new Task(function () { mgraphics.redraw(); }, this);
	tsk.schedule(950);
}

// ---- Drawing ------------------------------------------------------------------------------

function rgb(r, g, b, a) { mgraphics.set_source_rgba(r, g, b, a === undefined ? 1 : a); }

function rect(x, y, w, h) { mgraphics.rectangle(x, y, w, h); mgraphics.fill(); }

function rrect(x, y, w, h, r) { mgraphics.rectangle_rounded(x, y, w, h, r, r); mgraphics.fill(); }

function text(t, x, y, size, bold, align) {
	mgraphics.select_font_face(bold ? "Arial Bold" : "Arial");
	mgraphics.set_font_size(size);
	var m = mgraphics.text_measure(t);
	var tx = align === "c" ? x - m[0] / 2 : align === "r" ? x - m[0] : x;
	mgraphics.move_to(tx, y);
	mgraphics.show_text(t);
}

function paint() {
	layoutButtons();

	// Panel: brushed silver on top, black keyboard section below, like the original.
	var g = mgraphics.pattern_create_linear(0, 0, 0, 66);
	g.add_color_stop_rgba(0, 0.86, 0.86, 0.84, 1);
	g.add_color_stop_rgba(1, 0.74, 0.74, 0.72, 1);
	mgraphics.set_source(g);
	mgraphics.rectangle(0, 0, W, 67);
	mgraphics.fill();
	for (var ln = 0; ln < 67; ln += 2) { rgb(1, 1, 1, 0.05); rect(0, ln, W, 1); }

	rgb(0.11, 0.11, 0.12);
	rect(0, 67, W, H - 67);
	rgb(0.45, 0.45, 0.44);
	rect(0, 66, W, 1);

	// Divider between the two halves
	rgb(0.55, 0.55, 0.53);
	rect(RX - 8, 0, 1, 67);
	rgb(0.25, 0.25, 0.26);
	rect(RX - 8, 67, 1, H - 67);

	// Brand
	rgb(0.12, 0.12, 0.13);
	text("TRoss 303", 8, 13, 12, true);
	rgb(0.75, 0.15, 0.08);
	text("ACID BASS", 76, 13, 8, true);
	rgb(0.25, 0.25, 0.26);
	text("ticcoross.gumroad.com", RX - 14, 12, 8, false, "r");

	// Knob labels (the dials sit on top of this panel)
	var kl = ["TUNING", "CUT OFF", "RESO", "ENV MOD", "DECAY", "ACCENT", "VOLUME"];
	rgb(0.15, 0.15, 0.16);
	for (var k = 0; k < kl.length; k++) text(kl[k], 30 + k * 44, 63, 7, true, "c");
	text("WAVE", 352, 64, 7, true, "c");
	text("TRANSP", 404, 64, 7, true, "c");
	text("SEQ", 352, 22, 7, true, "c");
	text("CUT MAX", 404, 22, 7, true, "c");

	drawGrid();
	drawRight();
	if (showAbout) drawAbout();
}

function drawGrid() {
	var i, r, x;
	for (r = 1; r < ROWS.length; r++) {
		rgb(0.6, 0.6, 0.6);
		text(ROWS[r].label, 6, ROWS[r].y + ROWS[r].h / 2 + 3, 7, true);
	}
	for (i = 0; i < 16; i++) {
		x = GX + i * CW;
		var v = steps[i];
		var active = i < len;
		var beatCol = (Math.floor(i / 4) % 2) === 0;

		// Head: step number and playhead LED
		var isPlay = playStep === i && playingIdx === sel;
		rgb(isPlay ? 1 : 0.3, isPlay ? 0.15 : 0.08, isPlay ? 0.1 : 0.06);
		mgraphics.ellipse(x + CW / 2 - 3, 71, 6, 6);
		mgraphics.fill();
		if (!active) { rgb(0, 0, 0, 0.0); }

		var t = sTime(v);
		// NOTE: piano-key styled cells
		var nr = ROWS[1];
		if (!active) rgb(0.17, 0.17, 0.18);
		else if (t === 0) rgb(0.3, 0.3, 0.31);
		else rgb(beatCol ? 0.93 : 0.85, beatCol ? 0.93 : 0.85, beatCol ? 0.91 : 0.83);
		rrect(x + 1, nr.y, CW - 2, nr.h, 2);
		if (active) {
			if (t === 0) { rgb(0.55, 0.55, 0.55); text("-", x + CW / 2, nr.y + 14, 9, true, "c"); }
			else { rgb(0.08, 0.08, 0.09); text(NOTE_NAMES[sNote(v)], x + CW / 2, nr.y + 14, 9, true, "c"); }
		}

		// OCT
		var orow = ROWS[2];
		var o = sOct(v);
		cell(x, orow, active, o !== 1, o === 2 ? "UP" : o === 0 ? "DN" : "", [0.3, 0.55, 0.95]);
		// ACC, SLIDE
		cell(x, ROWS[3], active, sAcc(v), sAcc(v) ? "A" : "", [0.95, 0.3, 0.12]);
		cell(x, ROWS[4], active, sSlide(v), sSlide(v) ? "S" : "", [0.95, 0.75, 0.15]);
		// TIME
		var tr = ROWS[5];
		cell(x, tr, active, t !== 0, t === 1 ? "●" : t === 2 ? "—" : "", t === 2 ? [0.6, 0.6, 0.65] : [0.35, 0.8, 0.4]);
	}
}

function cell(x, row, active, on, label, col) {
	if (!active) rgb(0.15, 0.15, 0.16);
	else if (on) rgb(col[0], col[1], col[2]);
	else rgb(0.24, 0.24, 0.25);
	rrect(x + 1, row.y, CW - 2, row.h, 2);
	if (active && label) {
		rgb(0.05, 0.05, 0.05);
		text(label, x + CW / 2, row.y + row.h / 2 + 3, 7, true, "c");
	}
}

function drawButton(b) {
	if (b.style === "about") {
		rgb(0.2, 0.2, 0.22);
		mgraphics.ellipse(b.x, b.y, 12, 12);
		mgraphics.fill();
		rgb(0.9, 0.9, 0.88);
		text("i", b.x + 6, b.y + 9.5, 8, true, "c");
		return;
	}
	var onCol = b.style === "red" ? [0.85, 0.18, 0.1] : [0.92, 0.55, 0.12];
	var top = b.y < 67;
	if (b.on) rgb(onCol[0], onCol[1], onCol[2]);
	else if (b.style === "key") rgb(top ? 0.95 : 0.88, top ? 0.95 : 0.88, top ? 0.93 : 0.86);
	else if (top) rgb(0.3, 0.3, 0.32);
	else rgb(0.27, 0.27, 0.29);
	rrect(b.x, b.y, b.w, b.h, 3);
	if (b.on) rgb(1, 1, 1);
	else if (b.style === "key") rgb(0.1, 0.1, 0.1);
	else rgb(0.9, 0.9, 0.9);
	text(b.label, b.x + b.w / 2, b.y + b.h / 2 + 3.5, b.h > 20 ? 10 : 8, true, "c");
}

function drawRight() {
	var i;
	rgb(0.15, 0.15, 0.16);
	text("PATTERN", RX, 14, 8, true);
	for (i = 0; i < buttons.length; i++) drawButton(buttons[i]);

	// Selected / playing
	rgb(0.12, 0.12, 0.13);
	text("EDIT " + nameOf(sel), RX + 244, 14, 8, true);
	if (playStep >= 0) {
		rgb(0.75, 0.15, 0.08);
		text("PLAY " + nameOf(playingIdx), RX + 244, 33, 8, true);
	}

	// Length readout
	rgb(0.12, 0.12, 0.13);
	rrect(RX + 18, 40, 26, 12, 2);
	rgb(1, 0.55, 0.1);
	text(String(len), RX + 31, 49.5, 8, true, "c");

	// Chain list
	var s = "";
	for (i = 0; i < chainList.length; i++) s += (i ? " " : "") + nameOf(chainList[i]);
	rgb(0.15, 0.15, 0.16);
	if (!s) s = chainEdit ? "press patterns to add" : "no chain";
	text(s.length > 52 ? s.substr(0, 52) + "..." : s, RX + 118, 63, 7, false);

	// Random section labels
	rgb(0.6, 0.6, 0.6);
	text("SCALE", RX + 74, 79, 7, true);
	text("ROOT", RX + 160, 79, 7, true);
	var rl = ["DENSITY", "ACCENT", "SLIDE", "OCTAVE"];
	for (i = 0; i < rl.length; i++) text(rl[i], RX + 92 + i * 44, 141, 7, true, "c");
	text("LOCK", RX, 158, 7, true);

	if (flash.name && new Date().getTime() < flash.until) {
		rgb(1, 0.55, 0.1);
		text(flash.name, RX + 270, 90, 8, true, "c");
	}
}

function drawAbout() {
	// Drawn over the step editor: the live controls elsewhere stay on top of the panel.
	var bx = 4, by = 69, bw = 434, bh = 98;
	var g = mgraphics.pattern_create_linear(bx, by, bx, by + bh);
	g.add_color_stop_rgba(0, 0.92, 0.92, 0.9, 1);
	g.add_color_stop_rgba(1, 0.7, 0.7, 0.68, 1);
	mgraphics.set_source(g);
	mgraphics.rectangle_rounded(bx, by, bw, bh, 7, 7);
	mgraphics.fill();
	rgb(0.75, 0.15, 0.08);
	rect(bx, by + 24, bw, 2);
	rgb(0.1, 0.1, 0.11);
	text("TRoss 303", bx + 12, by + 19, 15, true);
	rgb(0.75, 0.15, 0.08);
	text("ACID BASS  v" + VERSION, bx + 104, by + 18, 8, true);
	rgb(0.3, 0.3, 0.31);
	text("click to close", bx + bw - 12, by + 17, 7, false, "r");
	rgb(0.12, 0.12, 0.13);
	text("by Ticco Ross", bx + 12, by + 40, 9, true);
	text(EMAIL, bx + 12, by + 53, 8, false);
	rgb(0.75, 0.15, 0.08);
	text(GUMROAD, bx + 12, by + 66, 8, true);
	rgb(0.25, 0.25, 0.26);
	text("Sound engine: Lemus ro-mono-303 by lemus-dev, a circuit model of the", bx + 12, by + 80, 7, false);
	text("TB-303 voice. Free software, GNU AGPL v3. Not affiliated with Roland.", bx + 12, by + 90, 7, false);
}

// ---- Mouse --------------------------------------------------------------------------------

function hitStep(x, y) {
	if (x < GX || x >= GX + 16 * CW) return null;
	var i = Math.floor((x - GX) / CW);
	for (var r = 1; r < ROWS.length; r++)
		if (y >= ROWS[r].y && y < ROWS[r].y + ROWS[r].h) return { i: i, row: ROWS[r].key };
	return null;
}

function setLocal(i, n, o, a, s, t) {
	steps[i] = pack(n, o, a, s, t);
	mgraphics.redraw();
}

function onclick(x, y, but, cmd, shift, capslock, option, ctrl) {
	if (showAbout) {
		if (y > 127 && y < 138 && x > 14 && x < 200) max.launchbrowser(GUMROAD);
		showAbout = 0;
		mgraphics.redraw();
		return;
	}
	var i;
	for (i = 0; i < buttons.length; i++) {
		var b = buttons[i];
		if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) { b.act(); return; }
	}
	if (y < 16 && x > RX - 120 && x < RX - 10) { max.launchbrowser(GUMROAD); return; }

	var h = hitStep(x, y);
	if (!h) return;
	i = h.i;
	if (i >= len) return;
	var v = steps[i];
	var n = sNote(v), o = sOct(v), a = sAcc(v), s = sSlide(v), t = sTime(v);
	if (h.row === "note") {
		if (t === 0) { t = 1; send("setstep", i, "time", 1); }
		// alt-click: back to rest
		if (option) { t = 0; send("setstep", i, "time", 0); setLocal(i, n, o, a, s, t); return; }
		drag = { i: i, y: y, n: n, o: o, a: a, s: s, t: t, last: n };
		setLocal(i, n, o, a, s, t);
	} else if (h.row === "oct") {
		o = o === 1 ? 2 : o === 2 ? 0 : 1;
		send("setstep", i, "oct", o);
		setLocal(i, n, o, a, s, t);
	} else if (h.row === "acc") {
		a = a ? 0 : 1;
		send("setstep", i, "acc", a);
		setLocal(i, n, o, a, s, t);
	} else if (h.row === "slide") {
		s = s ? 0 : 1;
		send("setstep", i, "slide", s);
		setLocal(i, n, o, a, s, t);
	} else if (h.row === "time") {
		t = t === 1 ? 2 : t === 2 ? 0 : 1;
		send("setstep", i, "time", t);
		setLocal(i, n, o, a, s, t);
	}
}
onclick.local = 1;

function ondrag(x, y, but, cmd, shift, capslock, option, ctrl) {
	if (!drag) return;
	if (!but) { drag = null; return; }
	var n = drag.n + Math.round((drag.y - y) / 6);
	n = n < 0 ? 0 : n > 12 ? 12 : n;
	if (n !== drag.last) {
		drag.last = n;
		send("setstep", drag.i, "note", n);
		setLocal(drag.i, n, drag.o, drag.a, drag.s, drag.t);
	}
}
ondrag.local = 1;

function ondblclick(x, y, but, cmd, shift, capslock, option, ctrl) {
	onclick(x, y, but, cmd, shift, capslock, option, ctrl);
}
ondblclick.local = 1;

function onresize(w, h) { mgraphics.redraw(); }
onresize.local = 1;
