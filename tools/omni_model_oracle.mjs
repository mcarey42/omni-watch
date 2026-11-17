// Prints the numbers firmware/test/host/test_omni_model.c pins, by running the mockup's own functions
// (design/mockups/faces-omni.js: dateVals, handAngles, spinOff, the E easings, ph, the model() settle). Usage: node tools/omni_model_oracle.mjs
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
let src = fs.readFileSync(path.join(here, '../design/mockups/faces-omni.js'), 'utf8');
src = src.replace('window.OMNI_OMNI = {', 'window.__x = { E, ph, handAngles, smooth: null };\n  window.OMNI_OMNI = {');
const win = { OMNI_FACES: [], document: { createElement: () => ({ getContext: () => ({}) }) }, Math, console };
win.window = win;
const ctx = vm.createContext(win);
vm.runInContext(src, ctx);
const { E, ph, handAngles } = win.__x;
const { dateVals, spinOff } = win.OMNI_OMNI;
const deg = r => r * 180 / Math.PI;
const out = {};
out.dateVals = [[2026, 10, 22], [1776, 7, 4], [-44, 3, 15], [-48, 8, 9], [1999, 12, 31], [2000, 1, 1], [100, 1, 1], [-2000, 1, 1]]
  .map(a => [a, dateVals(...a)]);
const hand = (h, m, s) => { const [hr, mn] = handAngles({ now: () => ({ getHours: () => h, getMinutes: () => m, getSeconds: () => s }) }); return { hour_rad: hr, min_rad: mn, hour_deg: deg(hr), min_deg: deg(mn) }; };
out.hands = { '10:10:40': hand(10, 10, 40), '3:00:00': hand(3, 0, 0), '0:00:00': hand(0, 0, 0), '11:59:59': hand(11, 59, 59), '7:30:30': hand(7, 30, 30) };
out.spinFlip = [760, 1000, 1500, 2500, 4360].map(t => [t, spinOff(t, 760, 3600, 990)]);
out.spinDial = [150, 500, 1500, 2750, 3000].map(t => [t, spinOff(t, 150, 2600, 540)]);
const K = [1.4, -2.2, 3.5, -1.2];
out.settle = [80, 300, 600, 1100].map(t => [t, 1 - E.outBack(ph(t, 80, 1100)), K.map(k => k * (1 - E.outBack(ph(t, 80, 1100))))]);
const smooth = x => { x = Math.min(1, Math.max(0, x)); return x * x * (3 - 2 * x); };
out.unsetRed = [0, 300, 597, 1194, 1790, 2388].map(t => [t, 0.25 + 0.75 * smooth(Math.sin(t / 380) * 0.5 + 0.5)]);
out.loupe = [0, 150, 300, 3000, 3600, 3750, 3900].map(t => [t, t < 300 ? t / 300 : t < 3600 ? 1 : Math.max(0, 1 - (t - 3600) / 300)]);
out.green = [2300, 2475, 2650].map(t => [t, ph(t, 2300, 2650)]);
console.log(JSON.stringify(out, null, 1));
