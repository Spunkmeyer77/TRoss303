// TRoss 303 — panel, "Silver Classic": brushed metal, chicken-head knobs, rubber keys, LEDs,
// a green LCD (with an acid smiley when RANDOM is pressed), the step editor, pattern memory,
// chain and the About window.
// Copyright (C) 2026 Ticco Ross. AGPL-3.0-or-later.
//
// jsui (ES5), drawn in the background layer. The automatable controls are live.* objects laid
// invisibly over the knobs and switches drawn here; their values come in as "ctl <name> <v>".
//
// From tross303~:  pattern idx len s0..s15 | playstep n | playing idx | chain on len e...
// To tross303~:    select idx | setstep i field v | length n | random | undo | copy | paste
//                  clear | chain 0/1 | chainadd idx | chainclear

autowatch = 1;
inlets = 1;
outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var VERSION = "1.1.1";
var GUMROAD = "https://ticcoross.gumroad.com/";
var EMAIL = "ticcoross@gmail.com";

var W = 760, H = 169, OX = 4, DIV = 452, LOW = 70;

// ---- Colours ------------------------------------------------------------------------------

var C = {
	metal: [196, 196, 192], low: [26, 26, 28], seam: [90, 90, 92], screw: [170, 170, 168],
	ink: [22, 22, 24], ink2: [70, 70, 72], accent: [200, 30, 20], knob: [28, 28, 30],
	btn: [46, 46, 50], btnink: [225, 225, 220], white: [238, 238, 232], gold: [196, 156, 62],
	goldink: [40, 30, 12], lcd: [150, 175, 110], lcdink: [25, 35, 20], lowink: [170, 170, 165],
	red: [255, 40, 20], orange: [255, 140, 20], yellow: [255, 190, 30], blue: [60, 120, 220]
};

// ---- State --------------------------------------------------------------------------------

var sel = 0, len = 16, steps = [];
for (var i0 = 0; i0 < 16; i0++) steps.push(0x10);
var playStep = -1, playingIdx = 0;
var chainOn = 0, chainList = [], chainEdit = 0;
var showAbout = 0, drag = null;
var smileyUntil = 0, msgText = "", msgUntil = 0;

// Values of the live.* controls, for drawing them.
var ctlv = {
	tuning: 5, cutoff: 5, resonance: 5, envmod: 5, decay: 5, accent: 5, volume: 7,
	wave: 0, transpose: 0, seq: 1, cutmax: 2300,
	rdensity: 70, raccent: 30, rslide: 25, rspread: 30,
	"lock_notes": 0, "lock_octaves": 0, "lock_accents": 0, "lock_slides": 0, "lock_time": 0
};

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
function now() { return new Date().getTime(); }

// ---- Messages in --------------------------------------------------------------------------

function pattern() {
	var a = arrayfromargs(arguments);
	if (a.length < 18) return;
	sel = a[0];
	len = a[1];
	for (var i = 0; i < 16; i++) steps[i] = a[2 + i];
	mgraphics.redraw();
}
function playstep(n) { playStep = n; mgraphics.redraw(); }
function playing(n) { playingIdx = n; mgraphics.redraw(); }
function chain() {
	var a = arrayfromargs(arguments);
	chainOn = a[0] ? 1 : 0;
	chainList = [];
	for (var i = 0; i < (a[1] || 0) && 2 + i < a.length; i++) chainList.push(a[2 + i]);
	mgraphics.redraw();
}
// ctl <name> <value> (or ctl rlock <lane> <value> from the lock toggles)
function ctl() {
	var a = arrayfromargs(arguments);
	if (a[0] === "rlock" && a.length >= 3) ctlv["lock_" + a[1]] = a[2];
	else if (a.length >= 2) ctlv[a[0]] = a[1];
	mgraphics.redraw();
}

// ---- Drawing helpers ----------------------------------------------------------------------

function rgb(c, a) { mgraphics.set_source_rgba(c[0] / 255, c[1] / 255, c[2] / 255, a === undefined ? 1 : a); }
function shade(c, k) { return [Math.max(0, Math.min(255, c[0] + k)), Math.max(0, Math.min(255, c[1] + k)), Math.max(0, Math.min(255, c[2] + k))]; }
function frect(x, y, w, h) { mgraphics.rectangle(x, y, w, h); mgraphics.fill(); }
function rrect(x, y, w, h, r) { mgraphics.rectangle_rounded(x, y, w, h, r * 2, r * 2); mgraphics.fill(); }
function oval(cx, cy, r) { mgraphics.ellipse(cx - r, cy - r, r * 2, r * 2); mgraphics.fill(); }
function line(x0, y0, x1, y1, w) {
	mgraphics.set_line_width(w);
	mgraphics.move_to(x0, y0);
	mgraphics.line_to(x1, y1);
	mgraphics.stroke();
}
function text(t, x, y, size, bold, align) {
	mgraphics.select_font_face(bold === false ? "Arial" : "Arial Bold");
	mgraphics.set_font_size(size);
	var m = mgraphics.text_measure(t);
	var tx = align === "l" ? x : align === "r" ? x - m[0] : x - m[0] / 2;
	mgraphics.move_to(tx, y + size * 0.36); // y is the text's middle
	mgraphics.show_text(t);
}

// Brushed grain: fixed random strokes so the metal doesn't shimmer between redraws.
var grain = [];
(function () {
	var seed = 7;
	function rnd() { seed = (seed * 16807) % 2147483647; return seed / 2147483647; }
	for (var y = 0; y < H; y++) grain.push(rnd() * 2 - 1);
})();

function brushed(y0, y1, base, amount) {
	rgb(base);
	frect(0, y0, W, y1 - y0);
	for (var y = y0; y < y1; y++) {
		var g = grain[y];
		rgb(g > 0 ? [255, 255, 255] : [0, 0, 0], Math.abs(g) * amount);
		frect(0, y, W, 1);
	}
}

function screw(cx, cy) {
	rgb(C.screw);
	oval(cx, cy, 2.6);
	rgb([60, 60, 60]);
	mgraphics.set_line_width(0.5);
	mgraphics.ellipse(cx - 2.6, cy - 2.6, 5.2, 5.2);
	mgraphics.stroke();
	line(cx - 1.8, cy - 1, cx + 1.8, cy + 1, 0.8);
}

function led(cx, cy, on, col, r) {
	r = r || 2.4;
	if (on) {
		rgb(col, 0.18); oval(cx, cy, r * 3);
		rgb(col, 0.3); oval(cx, cy, r * 2);
		rgb(col); oval(cx, cy, r);
		rgb([255, 225, 205], 0.9); oval(cx - r * 0.25, cy - r * 0.35, r * 0.32);
	} else {
		rgb([10, 10, 10]); oval(cx, cy, r + 0.4);
		rgb([Math.floor(col[0] / 4), Math.floor(col[1] / 4), Math.floor(col[2] / 4)]); oval(cx, cy, r);
	}
}

// Rubber key with a soft shadow and a lit top edge.
function key(x, y, w, h, col, label, lcol, size) {
	rgb([0, 0, 0], 0.28); rrect(x + 0.8, y + 1.6, w, h, 2.2);
	rgb([0, 0, 0], 0.18); rrect(x + 1.4, y + 2.4, w, h, 2.6);
	rgb(col); rrect(x, y, w, h, 2);
	var g = mgraphics.pattern_create_linear(x, y, x, y + h * 0.5);
	var top = shade(col, 28);
	g.add_color_stop_rgba(0, top[0] / 255, top[1] / 255, top[2] / 255, 1);
	g.add_color_stop_rgba(1, col[0] / 255, col[1] / 255, col[2] / 255, 1);
	mgraphics.set_source(g);
	mgraphics.rectangle_rounded(x + 1, y + 0.5, w - 2, h * 0.5, 3, 3);
	mgraphics.fill();
	if (label) { rgb(lcol); text(label, x + w / 2, y + h / 2 + 0.3, size || 6.2); }
}

// Chicken-head knob; v 0..1 maps to -135..+135 degrees.
function knob(cx, cy, r, v, inkCol) {
	var i, a;
	rgb(inkCol);
	for (i = 0; i < 11; i++) {
		a = (-135 + 27 * i - 90) * Math.PI / 180;
		line(cx + Math.cos(a) * (r + 3), cy + Math.sin(a) * (r + 3), cx + Math.cos(a) * (r + 5.5), cy + Math.sin(a) * (r + 5.5), 0.8);
	}
	rgb([0, 0, 0], 0.3); oval(cx + 1.5, cy + 2.2, r + 0.5);
	rgb([0, 0, 0], 0.15); oval(cx + 2.2, cy + 3.2, r + 1.2);
	rgb([18, 18, 20]); oval(cx, cy, r);
	var g = mgraphics.pattern_create_radial(cx - r * 0.3, cy - r * 0.45, 0, cx, cy, r * 0.9);
	g.add_color_stop_rgba(0, 0.36, 0.36, 0.38, 1);
	g.add_color_stop_rgba(1, C.knob[0] / 255, C.knob[1] / 255, C.knob[2] / 255, 1);
	mgraphics.set_source(g);
	mgraphics.ellipse(cx - r * 0.86, cy - r * 0.9, r * 1.72, r * 1.72);
	mgraphics.fill();
	// the pointer bar
	a = (-135 + 270 * Math.max(0, Math.min(1, v)) - 90) * Math.PI / 180;
	var px = Math.cos(a), py = Math.sin(a), qx = -py, qy = px, L = r * 1.15, wv = r * 0.36;
	mgraphics.move_to(cx + px * L + qx * wv * 0.5, cy + py * L + qy * wv * 0.5);
	mgraphics.line_to(cx + px * L - qx * wv * 0.5, cy + py * L - qy * wv * 0.5);
	mgraphics.line_to(cx - px * L * 0.7 - qx * wv, cy - py * L * 0.7 - qy * wv);
	mgraphics.line_to(cx - px * L * 0.7 + qx * wv, cy - py * L * 0.7 + qy * wv);
	mgraphics.close_path();
	rgb([32, 32, 34]);
	mgraphics.fill_preserve();
	rgb([70, 70, 74]);
	mgraphics.set_line_width(0.6);
	mgraphics.stroke();
	rgb([235, 235, 230]);
	line(cx, cy, cx + px * L * 0.95, cy + py * L * 0.95, 1.4);
}

// ---- Layout (shared with tools/make_device.py) ----------------------------------------------

var KNOBS = ["tuning", "cutoff", "resonance", "envmod", "decay", "accent", "volume"];
var KNOB_NAMES = ["TUNING", "CUT OFF FREQ", "RESONANCE", "ENV MOD", "DECAY", "ACCENT", "VOLUME"];
function knobX(k) { return OX + 30 + k * 50; }
var KNOB_Y = 42, KNOB_R = 13;
var SX = OX + 384; // waveform / transpose / seq block
var RKNOBS = ["rdensity", "raccent", "rslide", "rspread"];
var RKNOB_NAMES = ["DENSITY", "ACCENT", "SLIDE", "OCTAVE"];
function rknobX(k) { return 478 + k * 42; }
var RKNOB_Y = 103, RKNOB_R = 10;
var LOCKS = [["notes", "NOTE"], ["octaves", "OCT"], ["accents", "ACC"], ["slides", "SLIDE"], ["time", "TIME"]];

var GX = OX + 40, CW = 25, KW = 21;
var ROWS = [
	{ key: "note", y: 86, h: 20, label: "NOTE" },
	{ key: "oct", y: 109, h: 12, label: "UP/DN" },
	{ key: "acc", y: 124, h: 12, label: "ACCENT" },
	{ key: "slide", y: 139, h: 12, label: "SLIDE" },
	{ key: "time", y: 154, h: 12, label: "TIME" }
];
var LED_Y = 78;

var buttons = [];
function addButton(x, y, w, h, act) { buttons.push({ x: x, y: y, w: w, h: h, act: act }); }

// ---- Paint --------------------------------------------------------------------------------

function paint() {
	var i, k;
	buttons = [];

	brushed(0, LOW, C.metal, 0.055);
	brushed(LOW, H, C.low, 0.02);
	rgb(C.seam); frect(0, LOW - 0.6, W, 1.2);
	rgb(C.seam); frect(DIV - 0.4, 0, 0.8, H);
	screw(OX + 4, 5); screw(W - OX - 4, 5); screw(OX + 4, H - 5); screw(W - OX - 4, H - 5);

	// Brand (click: About) and store link
	rgb(C.ink); text("TRoss", OX + 12, 15, 13, true, "l");
	rgb(C.accent); text("303", OX + 60, 15, 13, true, "l");
	rgb(C.ink2); text("ACID BASS SYNTHESIZER", OX + 92, 15.5, 6, true, "l");
	addButton(OX + 8, 6, 82, 18, function () { showAbout = 1; mgraphics.redraw(); });
	rgb(C.ink2); text("ticcoross.gumroad.com", 446, 15, 6, false, "r");
	addButton(370, 9, 78, 12, function () { max.launchbrowser(GUMROAD); });
	rgb(C.ink); text("CUT MAX", 290, 15, 5.4, true, "r");
	rgb([20, 10, 8]); rrect(294, 9.5, 40, 11, 1.5); // the CUT MAX live.numbox sits on this

	// Sound knobs
	for (k = 0; k < KNOBS.length; k++) {
		knob(knobX(k), KNOB_Y, KNOB_R, ctlv[KNOBS[k]] / 10, C.ink2);
		rgb(C.ink); text(KNOB_NAMES[k], knobX(k), 63, 5.6);
	}

	// Waveform switch, transpose, SEQ
	rgb(C.ink); text("WAVEFORM", SX + 16, 26, 5.4);
	rgb([30, 30, 32]); rrect(SX + 4, 33, 24, 10, 5);
	var kx = ctlv.wave ? SX + 18 : SX + 5;
	rgb([0, 0, 0], 0.3); oval(kx + 5.5, 39, 4.6);
	rgb([200, 200, 196]); oval(kx + 5, 38, 4.5);
	rgb([235, 235, 232]); oval(kx + 4.2, 37, 2.2);
	rgb(C.ink2); text("SAW", SX + 5, 50, 5); text("SQR", SX + 28, 50, 5);
	rgb(C.ink); text("TRANSP", SX + 48, 26, 5.4);
	rgb([20, 10, 8]); rrect(SX + 36, 32, 24, 11, 1.5); // the live.numbox sits on this
	led(SX + 48, 55, ctlv.seq, C.red, 2.2);
	rgb(C.ink); text("SEQ", SX + 48, 63, 5.2);

	drawPatternBlock();
	drawSteps();
	drawRandomBlock();
	if (showAbout) drawAbout();
}

function drawPatternBlock() {
	var i, x;
	var g = grpOf(sel), p = patOf(sel), s = secOf(sel);
	rgb(C.ink); text("PATTERN", 462, 10, 5.6, true, "l");
	for (i = 0; i < 4; i++) {
		x = 500 + i * 30;
		key(x, 5, 26, 10, C.btn, GROUPS[i], C.btnink);
		led(x + 13, 19, g === i, C.orange, 1.8);
		(function (gi) { addButton(x, 4, 26, 18, function () { send("select", idxOf(gi, patOf(sel), secOf(sel))); }); })(i);
	}
	for (i = 0; i < 2; i++) {
		x = 628 + i * 26;
		key(x, 5, 22, 10, C.btn, i ? "B" : "A", C.btnink);
		led(x + 11, 19, s === i, C.orange, 1.8);
		(function (si) {
			addButton(x, 4, 22, 18, function () {
				if (chainEdit) addToChain(idxOf(grpOf(sel), patOf(sel), si));
				send("select", idxOf(grpOf(sel), patOf(sel), si));
			});
		})(i);
	}
	for (i = 0; i < 8; i++) {
		x = 462 + i * 27;
		key(x, 25, 23, 13, C.white, String(i + 1), [20, 20, 20]);
		led(x + 11.5, 43, p === i, C.red, 1.8);
		(function (pi) {
			addButton(x, 24, 23, 22, function () {
				if (chainEdit) addToChain(idxOf(grpOf(sel), pi, secOf(sel)));
				send("select", idxOf(grpOf(sel), pi, secOf(sel)));
			});
		})(i);
	}
	var tools = [
		["COPY", C.btn, function () { send("copy"); say("COPIED"); }],
		["PASTE", C.btn, function () { send("paste"); say("PASTED"); }],
		["CLEAR", C.btn, function () {
			if (chainEdit) { send("chainclear"); say("CHAIN CLR"); }
			else { send("clear"); say("CLEARED"); }
		}],
		["UNDO", C.btn, function () { send("undo"); say("UNDO"); }],
		["CHAIN", C.gold, function () { send("chain", chainOn ? 0 : 1); }],
		["EDIT", C.btn, function () { chainEdit = chainEdit ? 0 : 1; mgraphics.redraw(); }]
	];
	for (i = 0; i < tools.length; i++) {
		x = 462 + i * 48;
		var gold = tools[i][1] === C.gold;
		key(x, 51, 44, 12, tools[i][1], tools[i][0], gold ? C.goldink : C.btnink);
		if (i === 4) led(x + 39, 57, chainOn, C.orange, 1.6);
		if (i === 5) led(x + 39, 57, chainEdit, C.orange, 1.6);
		addButton(x, 50, 44, 15, tools[i][2]);
	}
	drawLcd();
}

// A 12x12 dot smiley for the LCD.
var SMILEY = ["....XXXX....", "..XX....XX..", ".X........X.", ".X..X..X..X.", "X...X..X...X", "X..........X",
	"X.X......X.X", "X..X....X..X", ".X..XXXX..X.", ".X........X.", "..XX....XX..", "....XXXX...."];

function drawLcd() {
	var x0 = 684, y0 = 4, x1 = W - OX - 6, y1 = 32;
	rgb([40, 52, 30]); rrect(x0, y0, x1 - x0, y1 - y0, 2);
	rgb(C.lcd); rrect(x0 + 2, y0 + 2, x1 - x0 - 4, y1 - y0 - 4, 1.5);
	rgb([255, 255, 255], 0.08); frect(x0 + 3, y0 + 3, x1 - x0 - 6, 5);
	rgb(C.lcdink);
	var t = now();
	if (t < smileyUntil) {
		var cs = 1.75, sx = 689, sy = 7.5, r, c;
		for (r = 0; r < 12; r++)
			for (c = 0; c < 12; c++) {
				if (SMILEY[r].charAt(c) === "X") rgb(C.lcdink);
				else rgb([143, 168, 105]);
				frect(sx + c * cs, sy + r * cs, cs - 0.35, cs - 0.35);
			}
		rgb(C.lcdink);
		text("ACID!", 712, 13.5, 7.2, true, "l");
		text("NEW " + nameOf(sel), 712, 24, 5, false, "l");
		return;
	}
	text(nameOf(sel), 690, 13, 8, true, "l");
	text("L" + len, x1 - 6, 13, 5.4, true, "r");
	var line2;
	if (t < msgUntil) line2 = msgText;
	else if (chainEdit) line2 = "CHAIN " + chainList.length + (chainList.length ? " >" + nameOf(chainList[chainList.length - 1]) : "");
	else if (playStep >= 0) line2 = "PLAY " + nameOf(playingIdx);
	else line2 = chainOn && chainList.length ? "CHAIN " + chainList.length : "STOP";
	text(line2, 690, 24, 5.2, false, "l");
}

function drawSteps() {
	var i, r, x;
	rgb(C.lowink); text("STEP", OX + 8, LED_Y, 5.2, true, "l");
	for (r = 0; r < ROWS.length; r++) { rgb(C.lowink); text(ROWS[r].label, OX + 8, ROWS[r].y + ROWS[r].h / 2, 5.2, true, "l"); }
	for (i = 0; i < 16; i++) {
		x = GX + i * CW;
		var v = steps[i], active = i < len;
		// Playhead LED; clicking it sets the pattern length to this step.
		led(x + KW / 2, LED_Y, playStep === i && playingIdx === sel, C.red, active ? 2.2 : 1.5);
		(function (si) { addButton(x, LED_Y - 5, KW, 10, function () { send("length", si + 1); }); })(i);

		var t = sTime(v), n = sNote(v), o = sOct(v);
		var black = NOTE_NAMES[n].indexOf("#") >= 0;
		if (t !== 0) key(x, 86, KW, 20, black ? [30, 30, 32] : C.white, NOTE_NAMES[n], black ? [230, 230, 230] : [20, 20, 20], 6.6);
		else key(x, 86, KW, 20, [70, 70, 74], "-", [150, 150, 150]);
		key(x, 109, KW, 12, o === 1 ? C.btn : C.blue, o === 2 ? "UP" : o === 0 ? "DN" : "", [255, 255, 255]);
		key(x, 124, KW, 12, C.btn); led(x + KW / 2, 130, sAcc(v), [255, 60, 20], 2.2);
		key(x, 139, KW, 12, C.btn); led(x + KW / 2, 145, sSlide(v), C.yellow, 2.2);
		key(x, 154, KW, 12, C.btn, t === 1 ? "●" : t === 2 ? "—" : "", t === 1 ? [120, 230, 120] : [190, 190, 190]);
		if (!active) { rgb(C.low, 0.72); frect(x - 1, 84, KW + 3, 84); }
		if (i % 4 === 0) { rgb(C.seam); frect(x - 2.3, 84, 0.6, 83); }
	}
}

function drawRandomBlock() {
	var k, x;
	rgb(C.lowink); text("RANDOM CREATOR", 462, 79, 5.6, true, "l");
	for (k = 0; k < RKNOBS.length; k++) {
		knob(rknobX(k), RKNOB_Y, RKNOB_R, ctlv[RKNOBS[k]] / 100, C.lowink);
		rgb(C.lowink); text(RKNOB_NAMES[k], rknobX(k), 120, 5);
	}
	rgb(C.lowink); text("SCALE", 650, 79, 5, true, "l"); text("ROOT", 708, 79, 5, true, "l");
	rgb([20, 10, 8]); rrect(650, 85, 54, 12, 1.5); rrect(708, 85, 30, 12, 1.5); // the menus sit on these
	key(650, 108, W - OX - 8 - 650, 14, C.gold, "RANDOM", C.goldink, 7);
	addButton(650, 107, W - OX - 8 - 650, 16, function () {
		send("random");
		smileyUntil = now() + 1500;
		mgraphics.redraw();
		later(1550);
	});
	rgb(C.lowink); text("LOCK", 462, 140, 5.2, true, "l");
	for (k = 0; k < LOCKS.length; k++) {
		x = 462 + k * 42;
		key(x, 145, 38, 12, C.btn, LOCKS[k][1], C.btnink, 5.6);
		led(x + 33, 151, ctlv["lock_" + LOCKS[k][0]], C.orange, 1.6);
	}
}

function drawAbout() {
	// Over the step editor: the live controls elsewhere stay on top of the panel.
	var bx = 8, by = 72, bw = 434, bh = 94;
	rgb([0, 0, 0], 0.4); rrect(bx + 2, by + 3, bw, bh, 7);
	var g = mgraphics.pattern_create_linear(bx, by, bx, by + bh);
	g.add_color_stop_rgba(0, 0.86, 0.86, 0.84, 1);
	g.add_color_stop_rgba(1, 0.68, 0.68, 0.66, 1);
	mgraphics.set_source(g);
	mgraphics.rectangle_rounded(bx, by, bw, bh, 12, 12);
	mgraphics.fill();
	rgb(C.gold); frect(bx, by + 24, bw, 2);
	screw(bx + 6, by + 6); screw(bx + bw - 6, by + 6); screw(bx + 6, by + bh - 6); screw(bx + bw - 6, by + bh - 6);
	rgb(C.ink); text("TRoss", bx + 14, by + 13, 14, true, "l");
	rgb(C.accent); text("303", bx + 66, by + 13, 14, true, "l");
	rgb(C.ink2); text("ACID BASS SYNTHESIZER  v" + VERSION, bx + 102, by + 13.5, 6.5, true, "l");
	rgb(C.ink2); text("click to close", bx + bw - 14, by + 13, 5.6, false, "r");
	rgb(C.ink); text("by Ticco Ross", bx + 14, by + 37, 8.5, true, "l");
	text(EMAIL, bx + 14, by + 49, 7.5, false, "l");
	rgb([150, 110, 30]); text(GUMROAD, bx + 14, by + 61, 7.5, true, "l");
	rgb(C.ink2);
	text("Sound engine: Lemus ro-mono-303 by lemus-dev, a circuit model of the TB-303 voice.", bx + 14, by + 74, 6, false, "l");
	text("Free software under the GNU AGPL v3. Not affiliated with Roland.", bx + 14, by + 84, 6, false, "l");
}

// ---- Actions ------------------------------------------------------------------------------

function send() { outlet(0, arrayfromargs(arguments)); }

function say(t) {
	msgText = t;
	msgUntil = now() + 1000;
	mgraphics.redraw();
	later(1050);
}

function later(ms) {
	var tsk = new Task(function () { mgraphics.redraw(); }, this);
	tsk.schedule(ms);
}

function addToChain(idx) {
	if (chainList.length >= 16) { say("CHAIN FULL"); return; }
	send("chainadd", idx);
}

// ---- Mouse --------------------------------------------------------------------------------

function hitStep(x, y) {
	if (x < GX || x >= GX + 16 * CW) return null;
	var i = Math.floor((x - GX) / CW);
	if (x - GX - i * CW > KW + 1) return null;
	for (var r = 0; r < ROWS.length; r++)
		if (y >= ROWS[r].y - 1 && y < ROWS[r].y + ROWS[r].h + 1) return { i: i, row: ROWS[r].key };
	return null;
}

function setLocal(i, n, o, a, s, t) { steps[i] = pack(n, o, a, s, t); mgraphics.redraw(); }

function onclick(x, y, but, cmd, shift, capslock, option, ctrl) {
	var i;
	if (showAbout) {
		if (y > 72 + 55 && y < 72 + 67 && x > 20 && x < 160) max.launchbrowser(GUMROAD);
		showAbout = 0;
		mgraphics.redraw();
		return;
	}
	for (i = 0; i < buttons.length; i++) {
		var b = buttons[i];
		if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) { b.act(); return; }
	}
	var h = hitStep(x, y);
	if (!h || h.i >= len) return;
	i = h.i;
	var v = steps[i], n = sNote(v), o = sOct(v), a = sAcc(v), s = sSlide(v), t = sTime(v);
	if (h.row === "note") {
		if (option) { send("setstep", i, "time", 0); setLocal(i, n, o, a, s, 0); return; } // alt-click: rest
		if (t === 0) { t = 1; send("setstep", i, "time", 1); }
		drag = { i: i, y: y, n: n, o: o, a: a, s: s, t: t, last: n };
		setLocal(i, n, o, a, s, t);
	} else if (h.row === "oct") {
		o = o === 1 ? 2 : o === 2 ? 0 : 1;
		send("setstep", i, "oct", o); setLocal(i, n, o, a, s, t);
	} else if (h.row === "acc") {
		a = a ? 0 : 1;
		send("setstep", i, "acc", a); setLocal(i, n, o, a, s, t);
	} else if (h.row === "slide") {
		s = s ? 0 : 1;
		send("setstep", i, "slide", s); setLocal(i, n, o, a, s, t);
	} else if (h.row === "time") {
		t = t === 1 ? 2 : t === 2 ? 0 : 1;
		send("setstep", i, "time", t); setLocal(i, n, o, a, s, t);
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

function ondblclick(x, y, but, cmd, shift, capslock, option, ctrl) { onclick(x, y, but, cmd, shift, capslock, option, ctrl); }
ondblclick.local = 1;

function onresize(w, h) { mgraphics.redraw(); }
onresize.local = 1;
