// Compares a host-rendered Omni dial frame with a mockup reference frame, and writes it as a PNG (Stage 3 Task 6's
// shot measurement; Task 7 can feed it a device shot the same way).
//   OMNI_SHOT_OUT=/tmp/dial.raw build-host/test_omni_dial      (the raw frame: 466 x 466 RGB565 little-endian)
//   node tools/omni_host_shot.mjs /tmp/dial.raw design/shots/omni-ref-dial-rest.png design/shots/omni-host-dial-rest.png
// The input may also be a PNG. Pixels compared: those the reference draws (alpha > 0: the dial's disc), each the
// largest channel difference in 8-bit levels, the reference composited over black. Prints the share within 6 levels,
// per region and per 8 x 8 grid cell, and the worst of each.
import fs from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');
const W = 466;

function load(path) {
  const buf = fs.readFileSync(path);
  if (path.endsWith('.png')) {
    const p = PNG.sync.read(buf);
    if (p.width !== W || p.height !== W) throw new Error(`${path}: ${p.width}x${p.height}, want ${W}x${W}`);
    const out = new Uint8Array(W * W * 4);
    for (let i = 0; i < W * W; i++) {
      const a = p.data[i * 4 + 3];
      for (let k = 0; k < 3; k++) out[i * 4 + k] = Math.round(p.data[i * 4 + k] * a / 255);
      out[i * 4 + 3] = a;
    }
    return out;
  }
  if (buf.length !== W * W * 2) throw new Error(`${path}: ${buf.length} bytes, want ${W * W * 2}`);
  const out = new Uint8Array(W * W * 4);
  for (let i = 0; i < W * W; i++) {
    const v = buf[i * 2] | buf[i * 2 + 1] << 8, r = v >> 11, g = (v >> 5) & 63, b = v & 31;
    out[i * 4] = r << 3 | r >> 2;
    out[i * 4 + 1] = g << 2 | g >> 4;
    out[i * 4 + 2] = b << 3 | b >> 2;
    out[i * 4 + 3] = 255;
  }
  return out;
}

const [src, ref, outPng] = process.argv.slice(2);
if (!src || !ref) {
  console.error('usage: node tools/omni_host_shot.mjs <frame.raw|.png> <reference.png> [out.png]');
  process.exit(2);
}
const a = load(src), b = load(ref);
const regions = [
  ['globe disc (r < 112)', (r) => r < 112],
  ['lock rings (112..126)', (r) => r >= 112 && r < 126],
  ['date rings (126..197)', (r) => r >= 126 && r < 197],
  ['bezel and case (197..233)', (r) => r >= 197],
];
const special = [
  ['magnifier (207..259 x 18..122)', (x, y) => x >= 207 && x < 259 && y >= 18 && y < 122],
  ['red lens (104..147 x 28..66)', (x, y) => x >= 104 && x < 147 && y >= 28 && y < 66],
  ['green lens (319..362 x 28..66)', (x, y) => x >= 319 && x < 362 && y >= 28 && y < 66],
];
const stat = () => ({ n: 0, ok: 0, sum: 0, ok12: 0, okq: 0 });
let ceilT = 0, ceilR = 0;   /* the reference itself through RGB565 (truncated, rounded), scored on the same measure */
const all = stat(), reg = regions.map(stat), sp = special.map(stat), grid = Array.from({ length: 64 }, stat);
const q = (v, bits) => { const t = v >> (8 - bits); return bits === 5 ? (t << 3 | t >> 2) : (t << 2 | t >> 4); };
for (let y = 0; y < W; y++)
  for (let x = 0; x < W; x++) {
    const i = (y * W + x) * 4;
    if (b[i + 3] === 0) continue;
    let e = 0, eq = 0;
    for (let k = 0; k < 3; k++) {
      e = Math.max(e, Math.abs(a[i + k] - b[i + k]));
      eq = Math.max(eq, Math.abs(a[i + k] - q(b[i + k], k === 1 ? 6 : 5)));   /* the reference as the panel would hold it */
    }
    let et = 0, er = 0;
    for (let k = 0; k < 3; k++) {
      const bits = k === 1 ? 6 : 5, top = (1 << bits) - 1, r = Math.round(b[i + k] * top / 255);
      et = Math.max(et, Math.abs(q(b[i + k], bits) - b[i + k]));
      er = Math.max(er, Math.abs((bits === 5 ? (r << 3 | r >> 2) : (r << 2 | r >> 4)) - b[i + k]));
    }
    ceilT += et <= 6;
    ceilR += er <= 6;
    const add = (s) => { s.n++; s.ok += e <= 6; s.sum += e; s.ok12 += e <= 12; s.okq += eq <= 6; };
    add(all);
    const r = Math.hypot(x + 0.5 - 233, y + 0.5 - 233);
    regions.forEach(([, f], k) => f(r) && add(reg[k]));
    special.forEach(([, f], k) => f(x, y) && add(sp[k]));
    add(grid[Math.min(7, Math.floor(y / 59)) * 8 + Math.min(7, Math.floor(x / 59))]);
  }
const pct = (s) => (100 * s.ok / s.n).toFixed(1);
const mean = (s) => (s.sum / s.n).toFixed(1);
console.log(`all: ${all.n} px, ${pct(all)} % within 6 levels, mean error ${mean(all)}; ` +
  `${(100 * all.ok12 / all.n).toFixed(1)} % within 12; ${(100 * all.okq / all.n).toFixed(1)} % within 6 of the reference ` +
  `truncated to RGB565`);
console.log(`  ceiling: the reference itself scores ${(100 * ceilT / all.n).toFixed(1)} % within 6 truncated to RGB565, ` +
  `${(100 * ceilR / all.n).toFixed(1)} % rounded`);
const rows = [...regions.map(([n], k) => [n, reg[k]]), ...special.map(([n], k) => [n, sp[k]])];
for (const [n, s] of rows) console.log(`  ${n}: ${s.n} px, ${pct(s)} % within 6, mean ${mean(s)}, ${(100 * s.okq / s.n).toFixed(1)} % vs RGB565`);
const worstRegion = rows.filter(([, s]) => s.n).sort((p, q) => p[1].ok / p[1].n - q[1].ok / q[1].n)[0];
console.log(`worst region: ${worstRegion[0]} (${pct(worstRegion[1])} %)`);
const cells = grid.map((s, k) => [k, s]).filter(([, s]) => s.n > 200).sort((p, q) => p[1].ok / p[1].n - q[1].ok / q[1].n);
const [wk, ws] = cells[0];
console.log(`worst grid cell: x ${(wk % 8) * 59}..${(wk % 8) * 59 + 59}, y ${Math.floor(wk / 8) * 59}..${Math.floor(wk / 8) * 59 + 59}: ${pct(ws)} % within 6, mean ${mean(ws)}`);
if (outPng) {
  const p = new PNG({ width: W, height: W });
  p.data.set(a);
  fs.writeFileSync(outPng, PNG.sync.write(p));
  console.log(`wrote ${outPng}`);
}
