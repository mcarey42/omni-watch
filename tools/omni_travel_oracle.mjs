// Prints the numbers firmware/test/host/test_travel.c pins, by running the mockup's own makeDial model() and
// startTravel (design/mockups/faces-omni.js 607-700): the rings, lights, marks, hands' swing and the globe's lonAt of a
// travel from 2026-10-22 (lon -83, the mockup's default home) to Philadelphia 1776-07-04, at the brief's sample times,
// plus the landing, the away timers and the trip home. Usage: node tools/omni_travel_oracle.mjs
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
let src = fs.readFileSync(path.join(here, '../design/mockups/faces-omni.js'), 'utf8');
const patch = (a, b) => { if (!src.includes(a)) throw new Error('mockup changed: ' + a); src = src.replace(a, b); };
patch('return {\n      st, reset,', 'return {\n      model, startTravel, st, reset,');
// the hands' swing and the globe's lonAt, recorded from inside model()
patch('const sw = rm ? 0 : 4 * hs * (1 - hs);', 'const sw = rm ? 0 : 4 * hs * (1 - hs); window.__sw = sw;');
patch('lon = tau > g1 ? tr.toLon', 'window.__lonAt = lonAt; lon = tau > g1 ? tr.toLon');
// the ring speed the model reports (T12 M1, R105): the same blur over the model's 32 ms step
patch('blur.push(total * (e - E.inOutQuart(s2)));',
  'blur.push(total * (e - E.inOutQuart(s2))); (window.__b32 = window.__b32 || [])[i] = total * (e - E.inOutQuart(ph(tau - 32, a0, a1)));');
const win = { OMNI_FACES: [], document: { createElement: () => ({ getContext: () => ({}) }) }, Math, console };
win.window = win;
vm.runInContext(src, vm.createContext(win));
const { makeDial, DEST, RINGS } = win.OMNI_OMNI;

const env = (rm) => ({ now: () => new Date(2026, 9, 22, 10, 10, 40), reducedMotion: rm });
const r4 = x => Math.round(x * 1e4) / 1e4;
const T0 = 10000;   // the travel's start, well after the reveal (the mockup's t)

function trip(rm, taus) {
  const d = makeDial({ spinT0: 150, spinDur: 2600, spinK: 540, greenAt: 2300, ringSettle: [80, 1100] });
  const e = env(rm);
  d.reset(e);
  d.st.lastLon = null; d.st.lastVals = null;   // fromVals = today, fromLon = home (-83)
  d.startTravel(T0, e, false);
  const tr = d.st.travel;
  return taus.map(tau => {
    const m = d.model(T0 + tau, e, rm);
    return { tau, vals: m.vals.map(r4), red: r4(m.red), green: r4(m.green), flash: r4(m.flash), markOld: r4(m.markOld),
      markNew: r4(m.markNew), sw: r4(win.__sw), lon: r4(m.lon), lonAt: r4(tr && win.__lonAt ? win.__lonAt(tau) : NaN),
      ringBlur: m.ringBlur && m.ringBlur.map(r4),
      speed32: rm ? null : win.__b32.map((b, i) => r4(-Math.min(RINGS[i].n / 3, Math.max(-RINGS[i].n / 3, b)))), marker: { a: r4(m.marker.a), lat: m.marker.lat, lon: m.marker.lon },
      caption: m.caption ? { text: m.caption.text, a: r4(m.caption.a) } : null, travelling: !!d.st.travel, away: !!d.st.away };
  });
}

const out = {};
out.dest = DEST;
out.rings = trip(false, [100, 300, 1000, 1500, 2100, 2230, 3000]).map(f => [f.tau, f.vals, f.ringBlur, f.speed32]);
out.ringsReduced = trip(true, [1000]).map(f => [f.tau, f.vals]);
out.lights = trip(false, [100, 1000, 2100, 2230, 2500, 3000]).map(f => [f.tau, f.red, f.green, f.flash, f.markOld, f.markNew]);
out.sw = trip(false, [1000, 1500]).map(f => [f.tau, f.sw]);
out.lon = trip(false, [100, 1000, 1450, 2650]).map(f => [f.tau, f.lon, f.lonAt]);
out.lonReduced = trip(true, [100, 250, 700, 1200, 1300]).map(f => [f.tau, f.lon]);
out.marker = trip(false, [2700, 2850, 3000]).map(f => [f.tau, f.marker]);
out.caption = trip(false, [2999, 3150, 3300]).map(f => [f.tau, f.caption]);
// landing, then the away state's timers, from awayT = t0 + g1 (2650): the caption fades over 9000-9600 ms and the
// trip home starts once t - awayT > 12000
{
  const d = makeDial({ spinT0: 150, spinDur: 2600, spinK: 540, greenAt: 2300, ringSettle: [80, 1100] });
  const e = env(false);
  d.reset(e);
  d.startTravel(T0, e, false);
  const seq = [];
  for (const tau of [3300, 3301, 3400, 2650 + 9000, 2650 + 9300, 2650 + 9600, 2650 + 12000, 2650 + 12001, 2650 + 12002]) {
    // model() alone (frame() draws), then frame()'s bookkeeping: lastLon, lastVals
    const mm = d.model(T0 + tau, e, false);
    d.st.lastLon = mm.lon; d.st.lastVals = mm.vals.slice();
    seq.push({ tau, vals: mm.vals.map(r4), lon: r4(mm.lon), green: r4(mm.green), caption: mm.caption && r4(mm.caption.a),
      travelling: !!d.st.travel, away: !!d.st.away, toHome: !!(d.st.travel && d.st.travel.dest.home) });
  }
  out.landAway = seq;
}
console.log(JSON.stringify(out, null, 1));
