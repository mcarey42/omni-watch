// Prints the numbers firmware/test/host/test_lid.c pins, by running the mockup's own drawLid (design/mockups/faces-omni.js
// 893) with its dome height Hd set to 0 (plan D4: the flat lid), its proj(), its per-strip shading (recorded from the
// globalAlpha it sets, through a stand-in canvas context), and the flip entry's flipTheta and dim. Usage:
// node tools/omni_lid_oracle.mjs
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
let src = fs.readFileSync(path.join(here, '../design/mockups/faces-omni.js'), 'utf8');
const patch = (a, b) => { if (!src.includes(a)) throw new Error('mockup changed: ' + a); src = src.replace(a, b); };
patch('Hh = -6, F = 800, Hd = 22, step = 3', 'Hh = -6, F = 800, Hd = 0, step = 3');
patch('if (!K.lid) K.lid = buildLid();', 'if (!K.lid) K.lid = { img: 1, dark: 2, spec: 3 };');
patch('let [yPrev, sPrev] = proj(0);', 'window.__proj = proj; let [yPrev, sPrev] = proj(0);');
patch('window.OMNI_OMNI = {', 'window.__x = { E, ph, drawLid };\n  window.OMNI_OMNI = {');
const win = { OMNI_FACES: [], document: { createElement: () => ({ getContext: () => ({}) }) }, Math, console };
win.window = win;
vm.runInContext(src, vm.createContext(win));
const { E, ph, drawLid } = win.__x;
const D2R = Math.PI / 180, C = 233;

// drawLid at theta through a context that records each strip: its source row y, and the dark and spec alphas
function strips(theta) {
  const rec = [];
  let cur = null;
  const ctx = {
    globalAlpha: 1, globalCompositeOperation: 'source-over',
    drawImage(img, sx, sy) {
      if (img === 1) { cur = { y: sy / 2, dark: 0, spec: 0 }; rec.push(cur); }
      else if (img === 2) cur.dark = this.globalAlpha;
      else if (img === 3) cur.spec = this.globalAlpha;
    },
  };
  drawLid(ctx, theta);
  return { rec, proj: win.__proj };
}

// the per-strip shading as drawLid computes it, at a source row's centre v (checked against the recorded strips below)
function shade(theta, v) {
  const clamp = (x, a, b) => x < a ? a : x > b ? b : x;
  const yr = clamp((v - C) / 226, -1, 1), beta = Math.asin(yr) * .55, n = theta + beta;
  const dark = clamp(1 - Math.cos(n + .5) / Math.cos(beta + .5), 0, .6);
  const spec = (Math.exp(-Math.pow((n - .38) / .14, 2)) * .5 + Math.exp(-Math.pow((n - 1.05) / .22, 2)) * .22) *
    clamp(theta * 6, 0, 1);
  return { dark, spec };
}

const out = { flat: {}, shade: {}, shadeCheck: [], dim: {}, theta: {} };
for (const deg of [30, 60]) {
  const { proj } = strips(deg * D2R);
  out.flat[deg] = [100, 233, 366].map(y => { const [Y, s] = proj(y); return { src: y, screen: Y, scale: s }; });
}
for (const [deg, v] of [[10, 233], [30, 233], [30, 100], [60, 366]]) out.shade[`${deg}@${v}`] = shade(deg * D2R, v);
for (const deg of [10, 30, 60]) {   // the replica above against the strips drawLid drew (centre y + 1.5)
  const { rec } = strips(deg * D2R);
  let worst = 0;
  for (const r of rec) {
    const s = shade(deg * D2R, r.y + 1.5);
    const d = Math.max(Math.abs((s.dark > .01 ? s.dark : 0) - r.dark), Math.abs((s.spec > .01 ? s.spec : 0) - r.spec));
    worst = Math.max(worst, d);
  }
  out.shadeCheck.push({ deg, strips: rec.length, worstDiff: worst });
}
const dim = th => .92 * (1 - E.outCubic(ph(th, 2 * D2R, 80 * D2R)));
for (const deg of [1, 10, 45, 80]) out.dim[deg] = dim(deg * D2R);
// flipTheta, the mockup's (makeRevealEntry, flip) and the device timeline of plan D3 (pop from 0, swing from 150)
const flipMock = tt => 4 * D2R * E.outCubic(ph(tt, 280, 380)) - 1.5 * D2R * E.inOutCubic(ph(tt, 380, 540)) +
  108 * D2R * E.inOutCubic(ph(tt, 580, 1640));
const flipDev = tt => 4 * D2R * E.outCubic(ph(tt, 0, 60)) - 1.5 * D2R * E.inOutCubic(ph(tt, 60, 150)) +
  108 * D2R * E.inOutCubic(ph(tt, 150, 750));
out.theta.mockup = Object.fromEntries([380, 540, 1110, 1640].map(t => [t, flipMock(t) / D2R]));
out.theta.device = Object.fromEntries([0, 60, 150, 300, 750].map(t => [t, flipDev(t) / D2R]));
let t90 = 0;
while (flipDev(t90) < 90 * D2R) t90++;
out.theta.device_first90 = t90;
console.log(JSON.stringify(out, null, 1));
