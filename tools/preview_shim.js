// Runs the jsui script with a recording mgraphics, prints draw ops as JSON.
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');
const ops = []; let path = null; let src_col = [0,0,0,1]; let font = 10, bold = false; let pos = [0,0];
const mg = {
  init(){}, redraw(){},
  set_source_rgba(r,g,b,a){ src_col=[r,g,b,a===undefined?1:a]; },
  set_source(p){ src_col = p.stops[0][1]; src_col.grad = p; },
  pattern_create_linear(x0,y0,x1,y1){ return {x0,y0,x1,y1,stops:[], add_color_stop_rgba(o,r,g,b,a){this.stops.push([o,[r,g,b,a]]);} }; },
  rectangle(x,y,w,h){ path={t:'rect',x,y,w,h}; },
  rectangle_rounded(x,y,w,h,r){ path={t:'rrect',x,y,w,h,r}; },
  ellipse(x,y,w,h){ path={t:'ell',x,y,w,h}; },
  fill(){ ops.push(Object.assign({col:src_col, grad: src_col.grad ? {y0:src_col.grad.y0,y1:src_col.grad.y1,stops:src_col.grad.stops}:null}, path)); src_col = src_col.slice(); },
  select_font_face(f){ bold = /Bold/.test(f); },
  set_font_size(s){ font = s; },
  text_measure(t){ return [t.length * font * (bold?0.6:0.55), font]; },
  move_to(x,y){ pos=[x,y]; },
  show_text(t){ ops.push({t:'text', x:pos[0], y:pos[1], s:t, size:font, bold, col:src_col}); },
};
global.mgraphics = mg; global.outlet = ()=>{}; global.post=()=>{};
global.arrayfromargs = (a)=>Array.prototype.slice.call(a);
global.Task = function(){ this.schedule=()=>{}; };
global.max = { launchbrowser(){} };
eval(src + `
;(function(){
  pattern(5, 16, 0x110, 0x103|64, 0x200, 0x107|128, 0x105|0x20, 0x000, 0x10a|64, 0x10c, 0x111, 0x200, 0x103, 0x000, 0x107|64|128, 0x203, 0x100, 0x10a);
  chain(1, 3, 0, 0, 1);
  playstep(6); playing(5);
  paint();
  if (process.argv[3]==='about'){ showAbout=1; paint(); }
})();`);
console.log(JSON.stringify(ops));
