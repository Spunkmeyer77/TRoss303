// Panel preview, part 1: runs the jsui script with a recording mgraphics and prints the draw
// operations as JSON for tools/preview_draw.py.
//   node tools/preview_shim.js device/tross303_panel.js [about|smiley] > ops.json
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');
const mode = process.argv[3] || '';
const ops = [];
let path = [], cur = null, source = { col: [0, 0, 0, 1] }, lw = 1, font = 10, bold = false, pos = [0, 0];
const mg = {
	init() {}, redraw() {},
	set_source_rgba(r, g, b, a) { source = { col: [r, g, b, a === undefined ? 1 : a] }; },
	set_source(p) { source = { grad: p }; },
	pattern_create_linear(x0, y0, x1, y1) { return { kind: 'lin', x0, y0, x1, y1, stops: [], add_color_stop_rgba(o, r, g, b, a) { this.stops.push([o, [r, g, b, a]]); } }; },
	pattern_create_radial(x0, y0, r0, x1, y1, r1) { return { kind: 'rad', x0, y0, r0, x1, y1, r1, stops: [], add_color_stop_rgba(o, r, g, b, a) { this.stops.push([o, [r, g, b, a]]); } }; },
	rectangle(x, y, w, h) { path.push({ t: 'rect', x, y, w, h }); },
	rectangle_rounded(x, y, w, h, ow) { path.push({ t: 'rrect', x, y, w, h, r: ow / 2 }); },
	ellipse(x, y, w, h) { path.push({ t: 'ell', x, y, w, h }); },
	move_to(x, y) { pos = [x, y]; cur = { t: 'poly', pts: [[x, y]], closed: false }; path.push(cur); },
	line_to(x, y) { if (!cur) mg.move_to(x, y); else cur.pts.push([x, y]); },
	close_path() { if (cur) cur.closed = true; },
	set_line_width(w) { lw = w; },
	fill() { mg.fill_preserve(); path = []; cur = null; },
	fill_preserve() { ops.push({ op: 'fill', shapes: path.filter(s => s.t !== 'poly' || s.pts.length > 2), src: source }); },
	stroke() { ops.push({ op: 'stroke', shapes: path, src: source, lw }); path = []; cur = null; },
	select_font_face(f) { bold = /Bold/.test(f); },
	set_font_size(s) { font = s; },
	text_measure(t) { return [t.length * font * (bold ? 0.62 : 0.56), font]; },
	show_text(t) { ops.push({ op: 'text', x: pos[0], y: pos[1], s: t, size: font, bold, src: source }); path = []; cur = null; },
};
global.mgraphics = mg;
global.outlet = () => {};
global.post = () => {};
global.arrayfromargs = (a) => Array.prototype.slice.call(a);
global.Task = function () { this.schedule = () => {}; };
global.max = { launchbrowser() {} };
eval(src + `
;(function(){
  pattern(0, 16, 3, 0x110|64, 0x110, 0x11c|128, 0x210, 0x113|64, 0x010, 0x117|128, 0x11a, 0x110|64, 0x010, 0x125, 0x113|128, 0x110|64, 0x207, 0x11a, 0x110);
  ctl('cutoff', 3.2); ctl('resonance', 8.6); ctl('envmod', 7); ctl('decay', 4.2); ctl('accent', 7.5); ctl('volume', 7);
  ctl('rlock', 'notes', 1);
  playstep(6); playing(0); chain(1, 3, 0, 0, 1);
  if ('${mode}'==='smiley') smileyUntil = Date.now() + 100000;
  if ('${mode}'==='about') showAbout = 1;
  if ('${mode}'==='pressed') { pressed = {x: 650, y: 108, w: 94, h: 14, since: 0}; smileyText='VARIED'; smileyUntil = Date.now() + 100000; }
  if ('${mode}'==='smiley') smileyText = 'NEW';
  paint();
})();`);
console.log(JSON.stringify(ops));
