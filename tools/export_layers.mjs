// Export the device's pre-rendered layers from the mockup code: serve the repo on 127.0.0.1, open
// tools/export/export.html in headless Chrome (playwright-core), run omniExport() and write
// tools/export/out/<name>.png plus tools/export/out/manifest.json, and the reference frames to
// design/shots/<prefix><name>.png (each ref carries its prefix: omni-ref- for the Omni's, omni-ref-s5- for Stage 5's). It runs the export twice, in two fresh contexts, and fails unless both agree.
// Usage: CHROME=/usr/bin/google-chrome node tools/export_layers.mjs   (deps: npm --prefix tools ci; tools/fonts.sh)
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const outDir = path.join(root, 'tools/export/out');
const chrome = process.env.CHROME || '/usr/bin/google-chrome';
// the TTFs export.html and export.js load (Stage 5's layers and frames use the IBM Plex ones)
const fonts = ['CormorantSC-Bold.ttf', 'IBMPlexSans-VF.ttf', 'IBMPlexMono-Medium.ttf'].map(f => path.join(root, 'tools/fonts', f));
if (!fs.existsSync(chrome)) throw new Error(`no Chrome at ${chrome} (set CHROME)`);
for (const font of fonts) if (!fs.existsSync(font)) throw new Error(`missing ${font}: run tools/fonts.sh`);

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.ttf': 'font/ttf', '.jpg': 'image/jpeg' };
const server = http.createServer((req, res) => {
  if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }   // Chrome asks; not an error
  const p = path.normalize(path.join(root, decodeURIComponent(new URL(req.url, 'http://x').pathname)));
  if (!p.startsWith(root + path.sep) || !fs.existsSync(p) || !fs.statSync(p).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': TYPES[path.extname(p)] || 'application/octet-stream' });
  fs.createReadStream(p).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));

// Software raster only, so the pixels do not depend on the host's GPU.
const browser = await chromium.launch({ executablePath: chrome, headless: true,
  args: ['--disable-gpu', '--disable-gpu-compositing', '--force-color-profile=srgb', '--font-render-hinting=none'] });
// One export in a fresh context. faces-omni.js's askFonts() links Google Fonts' UnifrakturMaguntia: every request to
// fonts.googleapis.com and fonts.gstatic.com is aborted, so only the local tools/export/fonts copy (export.html) can
// win and the export works offline (C13).
const GOOGLE_FONTS = /^https?:\/\/fonts\.(googleapis|gstatic)\.com\//;
async function exportOnce() {
  // The clock faces read local time; pin the zone to the home city's so the thumbnails do not depend on the host.
  const context = await browser.newContext({ timezoneId: 'America/New_York', locale: 'en-US' });
  let blocked = 0;
  await context.route(GOOGLE_FONTS, route => { blocked++; return route.abort(); });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  page.on('console', m => { if (m.type() === 'error' && !/net::ERR_FAILED/.test(m.text())) errors.push(m.text()); });
  await page.goto(`http://127.0.0.1:${server.address().port}/tools/export/export.html`, { waitUntil: 'load' });
  if (errors.length) throw new Error('page errors:\n' + errors.join('\n'));
  // OMNI_EXPORT_IRIS=1: also the iris's RLE subset (Task 10's bench input for ringpack --iris; RLE is retired, R99)
  const r = await page.evaluate(iris => { window.omniExportIris = iris; return window.omniExport(); }, !!process.env.OMNI_EXPORT_IRIS);
  if (errors.length) throw new Error('page errors:\n' + errors.join('\n'));
  await context.close();
  return { r, blocked };
}

let result, blocked;
try {
  ({ r: result, blocked } = await exportOnce());
  // Determinism: a second export in a new context must give the same bytes, every layer, raw layer and frame.
  const { r: again } = await exportOnce();
  const items = x => [...x.layers.map(l => [l.name, JSON.stringify(l)]), ...x.raw.map(l => [l.name, l.rgba]),
    ...x.refs.map(l => [l.prefix + l.name, JSON.stringify(l)])];
  const a = items(result), b = new Map(items(again));
  const differ = a.filter(([n, v]) => b.get(n) !== v).map(([n]) => n);
  if (differ.length || a.length !== b.size) throw new Error('two exports differ: ' + (differ.join(', ') || 'item count'));
  console.log(`determinism: two exports byte-identical (${a.length} items); Google Fonts requests blocked: ${blocked}`);
} finally {
  await browser.close();
  server.close();
}

fs.rmSync(outDir, { recursive: true, force: true });
fs.mkdirSync(outDir, { recursive: true });
const assets = [];
for (const l of result.layers) {
  fs.writeFileSync(path.join(outDir, l.name + '.png'), Buffer.from(l.png.split(',')[1], 'base64'));
  const { png, ...meta } = l;
  assets.push({ file: l.name + '.bin', ...meta });
}
// Raw RGBA layers (the ring engravings, the iris's RLE subset): tools/ringpack's input, not partition assets, so not in
// the manifest.
for (const r of result.raw) {
  const b = Buffer.from(r.rgba, 'base64');
  if (b.length !== r.w * r.h * 4) throw new Error(`${r.name}: ${b.length} bytes, expected ${r.w * r.h * 4}`);
  fs.writeFileSync(path.join(outDir, r.name + '.rgba'), b);
}
// The textures are packed by tools/gen_tex.mjs; listed here so the manifest names every file in the partition.
for (const w of [512])   // earth1024 retired (R69)
  assets.push({ file: `earth${w}.tex`, name: `earth${w}`, cf: 'RAW_TEX', w, h: w / 2, face: ['globe-live', 'globe-spin', 'globe-analog'],
    format: 'u32 LE: day RGB565 | night RGB565 << 16, row-major equirectangular' });
assets.push({ file: 'omni512.tex', name: 'omni512', cf: 'RAW_TEX', w: 512, h: 256, face: 'omni-dial',
  format: 'u32 LE: day RGB565 graded as the mockup grades it (gen_tex.mjs --grade omni) | 0 << 16 (no night plane)' });
// The reference frames (committed): the mockup frames the later tasks' device shots are compared with.
const shots = path.join(root, 'design/shots');
for (const r of result.refs) {
  if (!/^[a-z0-9-]+$/.test(r.prefix + r.name)) throw new Error(`bad reference frame name ${r.prefix}${r.name}`);
  fs.writeFileSync(path.join(shots, `${r.prefix}${r.name}.png`), Buffer.from(r.png.split(',')[1], 'base64'));
}
fs.writeFileSync(path.join(outDir, 'manifest.json'), JSON.stringify({ generator: 'tools/export_layers.mjs', assets }, null, 1) + '\n');
console.log(`D3: pole ${result.d3.poleAng.toFixed(3)} deg, meridian pin ${result.d3.pinAng.toFixed(3)} deg (diff ${result.d3.diff.toFixed(4)})`);
for (const a of assets) console.log(`  ${a.file.padEnd(16)} ${a.cf.padEnd(9)} ${a.w}x${a.h}`);
for (const r of result.raw) console.log(`  ${(r.name + '.rgba').padEnd(16)} RGBA      ${r.w}x${r.h} (ringpack input)`);
for (const r of result.refs) console.log(`  design/shots/${r.prefix}${r.name}.png  ${r.id} t ${r.t.join(' / ')}`);
console.log(`flip-60: theta 60 deg at t ${result.refInfo.t60.toFixed(1)} ms (theta ${result.refInfo.flipTheta60.toFixed(4)}); red lens tapped at ${result.refInfo.red.map(v => v.toFixed(2)).join(', ')}`);
console.log(`wrote ${result.layers.length} PNGs, ${result.raw.length} raw layers + manifest.json to ${path.relative(root, outDir)}/`);
