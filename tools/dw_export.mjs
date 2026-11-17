// The dw layer export (dw wave 1 Task 9, WD11): reads the designers' mockups in place and draws the generic
// art. It serves the repo and the design directory on 127.0.0.1 (read only), opens tools/export/dw_export.html in
// headless Chrome (playwright-core), adds the mockups, runs window.dwExport() twice in fresh contexts, fails unless
// both agree, and writes <out>/<asset>.png plus <out>/layers.json (each row's record: size, cells, glyphs).
//
// Usage: node tools/dw_export.mjs [--out DIR]          the table's rows (tools/export/dw_layers.json), into
//                                                      tools/export/out-dw/ by default
//        node tools/dw_export.mjs --overlay ART        the overlay's rows (ART/layers.json, named by ART/overlay.json)
//                                                      into ART/out/ (never the main repo: call-box art goes nowhere
//                                                      tracked here, G6)
//        node tools/dw_export.mjs --generic-only --out DIR   only the generic rows (dw_generic.js; no mockups needed),
//                                                      the re-render of tools/dw_assets.sh --check (G1)
//        node tools/dw_export.mjs --selftest           the drawn-text guard and the canvas tags on a test page
// The design directory is $DW_DESIGN (the theme's directory: mockups/, shots/), or, when unset, design/themes/dw in the
// main checkout (this repo's git common dir's parent; public release Task 6: the theme id, which the owner's checkout
// links to the design package), resolved through any symlink. It is read only, and a missing one is a clear error.
// The drawn-text list (public release Task 6) is tools/export/dw_terms.mjs's (private), found through
// tools/export/terms.mjs: the page gets window.DW_FORBIDDEN_SRC (its pattern plus the neutral mark) and
// window.DW_SELFTEST_WORDS before it loads. In a public tree (no module) both are the neutral mark alone.
// $DW_FACE_ALIAS maps table face ids to mockup face ids: a JSON object {tableId: mockupId}, or the path of a file
// holding one (the private art repo's export/face_alias.json). The page drives alias[row.face] || row.face.
// Needs: node, Chrome (CHROME, default /usr/bin/google-chrome), npm --prefix tools ci, tools/fonts/ (tools/fonts.sh).
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { NEUTRAL_MARK, loadTerms } from './export/terms.mjs';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = k => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : undefined; };
const flag = k => args.includes(k);
const die = msg => { console.error('dw_export: ' + msg); process.exit(1); };
for (const a of args) if (a.startsWith('--') && !['--out', '--overlay', '--generic-only', '--selftest'].includes(a)) die('unknown option ' + a);

let dwTerms;
try { dwTerms = await loadTerms(root, 'dw_terms'); } catch (e) { die(e.message); }
if (dwTerms && (typeof dwTerms.DW_FORBIDDEN_SRC !== 'string' || !Array.isArray(dwTerms.DW_SELFTEST_WORDS)))
  die('tools/export/dw_terms.mjs exports no DW_FORBIDDEN_SRC (string) and DW_SELFTEST_WORDS (array)');
const PAGE_SRC = dwTerms ? dwTerms.DW_FORBIDDEN_SRC + '|' + NEUTRAL_MARK : NEUTRAL_MARK;
const PAGE_WORDS = dwTerms ? [...dwTerms.DW_SELFTEST_WORDS, NEUTRAL_MARK] : [NEUTRAL_MARK];

function faceAlias() {
  const v = process.env.DW_FACE_ALIAS;
  if (!v) return {};
  let a;
  try { a = JSON.parse(v.trim().startsWith('{') ? v : fs.readFileSync(path.resolve(v), 'utf8')); }
  catch (e) { die(`DW_FACE_ALIAS: ${e.message}`); }
  const ID = /^[a-z0-9-]{1,40}$/;
  if (!a || typeof a !== 'object' || Array.isArray(a)) die('DW_FACE_ALIAS: not a JSON object {tableId: mockupId}');
  for (const [k, m] of Object.entries(a))
    if (!ID.test(k) || typeof m !== 'string' || !ID.test(m)) die(`DW_FACE_ALIAS: ${k}: ids are 1 to 40 of a-z, 0-9 and -`);
  return a;
}
const alias = faceAlias();

const chrome = process.env.CHROME || '/usr/bin/google-chrome';
if (!fs.existsSync(chrome)) die(`no Chrome at ${chrome} (set CHROME)`);
let chromium;
try { ({ chromium } = await import('playwright-core')); } catch { die('playwright-core is missing: npm --prefix tools ci'); }

const MODE = flag('--selftest') ? 'selftest' : flag('--generic-only') ? 'generic' : opt('--overlay') ? 'overlay' : 'main';
const ART = opt('--overlay') && path.resolve(opt('--overlay'));
const outDir = path.resolve(opt('--out') || (ART ? path.join(ART, 'out') : path.join(root, 'tools/export/out-dw')));
if (MODE !== 'selftest') for (const f of ['CormorantSC-Bold.ttf', 'IBMPlexSans-Medium.ttf', 'IBMPlexMono-Regular.ttf'])
  if (!fs.existsSync(path.join(root, 'tools/fonts', f))) die(`missing tools/fonts/${f}: run tools/fonts.sh`);

function designDir() {
  let d = process.env.DW_DESIGN && path.resolve(process.env.DW_DESIGN);
  if (!d) {
    let common;
    try { common = execFileSync('git', ['-C', root, 'rev-parse', '--path-format=absolute', '--git-common-dir'], { encoding: 'utf8' }).trim(); }
    catch { die('DW_DESIGN is unset and this is not a git checkout to derive it from'); }
    d = path.join(path.dirname(common), 'design', 'themes', 'dw');
  }
  return fs.existsSync(d) ? fs.realpathSync(d) : d;
}
const table = JSON.parse(fs.readFileSync(path.join(root, 'tools/export/dw_layers.json'), 'utf8'));
let rows = table.rows, design = null;
// M4: every checkout of this repo (this worktree, the main checkout, sibling worktrees) and the git common dir.
function repoRoots() {
  const git = a => execFileSync('git', ['-C', root, ...a], { encoding: 'utf8' });
  const out = [root, git(['rev-parse', '--path-format=absolute', '--git-common-dir']).trim()];
  for (const l of git(['worktree', 'list', '--porcelain']).split('\n')) if (l.startsWith('worktree ')) out.push(l.slice(9));
  return out.filter(p => fs.existsSync(p)).map(p => fs.realpathSync(p));
}
const real = p => { let q = path.resolve(p), tail = ''; while (!fs.existsSync(q)) { tail = path.join(path.basename(q), tail); q = path.dirname(q); } return path.join(fs.realpathSync(q), tail); };
const under = (p, roots) => { const r = real(p) + path.sep; return roots.find(x => r.startsWith(x + path.sep)); };
if (MODE === 'overlay') {
  const roots = repoRoots();
  const hit = under(ART, roots);
  if (hit) die(`the overlay must not live inside this repo or any of its checkouts (${ART} is under ${hit})`);
  const ov = JSON.parse(fs.readFileSync(path.join(ART, 'overlay.json'), 'utf8'));
  rows = JSON.parse(fs.readFileSync(path.join(ART, ov.layers || 'layers.json'), 'utf8')).rows;
  // N1: the overlay's assets/ and out/ are no symlinks and resolve outside every checkout too.
  for (const sub of ['assets', 'out']) {
    const q = path.join(ART, sub);
    if (fs.existsSync(q) || fs.lstatSync(q, { throwIfNoEntry: false })) {
      if (fs.lstatSync(q).isSymbolicLink()) die(`the overlay's ${sub}/ must not be a symlink (${q})`);
      const h = under(q, roots);
      if (h) die(`the overlay's ${sub}/ resolves inside a checkout of this repo (${q} is under ${h})`);
    }
  }
  if (fs.lstatSync(outDir, { throwIfNoEntry: false })?.isSymbolicLink()) die(`the export dir must not be a symlink (${outDir})`);
  const ohit = under(outDir, roots);
  if (ohit) die(`overlay exports never land in this repo or its checkouts (${outDir} is under ${ohit})`);
} else if (MODE === 'generic') rows = rows.filter(r => r.out === 'generic');
const needDesign = MODE === 'main' || MODE === 'overlay';
if (needDesign) {
  design = designDir();
  if (!fs.existsSync(path.join(design, 'mockups'))) die(`no mockups in the design directory ${design} (set DW_DESIGN)`);
}
rows = rows.filter(r => r.out !== 'gen');   // wave 2 Task 13: a generator's rows (tools/dw_vortex.py), never drawn here
const NAME = /^dw_[a-z0-9_]{1,12}$/;
for (const r of rows) if (!r.crc32 && !NAME.test(r.asset)) die(`${r.asset}: names are dw_ + at most 12 characters`);

// Read-only server: /dw-design/... is the design directory, everything else the repo.
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.ttf': 'font/ttf', '.jpg': 'image/jpeg', '.png': 'image/png' };
const server = http.createServer((req, res) => {
  if (req.method !== 'GET') { res.writeHead(405); res.end(); return; }
  if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
  const u = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const [base, rel] = u.startsWith('/dw-design/') && design ? [design, u.slice('/dw-design/'.length)] : [root, u];
  const p = path.normalize(path.join(base, rel));
  if (!p.startsWith(base + path.sep) || !fs.existsSync(p) || !fs.statSync(p).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': TYPES[path.extname(p)] || 'application/octet-stream' });
  fs.createReadStream(p).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

const browser = await chromium.launch({ executablePath: chrome, headless: true,
  args: ['--disable-gpu', '--disable-gpu-compositing', '--force-color-profile=srgb', '--font-render-hinting=none'] });
const GOOGLE_FONTS = /^https?:\/\/fonts\.(googleapis|gstatic)\.com\//;
const SCRIPTS = ['/dw-design/mockups/faces-reveal.js', '/dw-design/mockups/faces-classic.js', '/dw-design/mockups/faces-modern.js',
  '/design/mockups/faces-practical.js'];

async function openPage() {
  const context = await browser.newContext({ timezoneId: table.pinned.zone, locale: 'en-US' });
  await context.route(GOOGLE_FONTS, route => route.abort());
  await context.addInitScript({ content: 'window.DW_FORBIDDEN_SRC = ' + JSON.stringify(PAGE_SRC) + ';\n' +
    'window.DW_SELFTEST_WORDS = ' + JSON.stringify(PAGE_WORDS) + ';' });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  page.on('console', m => { if (m.type() === 'error' && !/net::ERR_FAILED/.test(m.text())) errors.push(m.text()); });
  await page.goto(`${base}/tools/export/dw_export.html`, { waitUntil: 'load' });
  return { context, page, errors };
}
async function exportOnce() {
  const { context, page, errors } = await openPage();
  if (needDesign) for (const s of SCRIPTS) await page.addScriptTag({ url: base + s });
  if (errors.length) throw new Error('page errors:\n' + errors.join('\n'));
  const r = await page.evaluate(job => window.dwExport(job), { rows, geometry: table.geometry, pinned: table.pinned, alias });
  if (errors.length) throw new Error('page errors:\n' + errors.join('\n'));
  await context.close();
  return r;
}

let result;
try {
  if (MODE === 'selftest') {
    const { context, page, errors } = await openPage();
    const res = await page.evaluate(() => window.dwSelftest());
    await context.close();
    let bad = errors.length;
    for (const e of errors) console.log('SELFTEST FAIL page error: ' + e);
    for (const t of res) { if (!t.ok) bad++; console.log(`${t.ok ? 'ok  ' : 'FAIL'} ${t.what}`); }
    console.log(`dw_export selftest: ${bad ? 'FAIL (' + bad + ')' : 'OK'} (${res.length} checks)`);
    process.exitCode = bad ? 1 : 0;
  } else {
    result = await exportOnce();
    const again = await exportOnce();   // determinism: a second export in a new context must give the same bytes
    const items = x => x.layers.map(l => [l.asset, JSON.stringify(l)]);
    const a = items(result), b = new Map(items(again));
    const differ = a.filter(([n, v]) => b.get(n) !== v).map(([n]) => n);
    if (differ.length || a.length !== b.size) throw new Error('two exports differ: ' + (differ.join(', ') || 'item count'));
    console.log(`determinism: two exports byte-identical (${a.length} layers); drawn texts guarded: ${result.checks.drawnTexts}` +
      (result.checks.b5 ? `; #5 geometry: ${result.checks.b5} panes, the lens and the windows where the table says` : ''));
  }
} catch (e) {
  console.error('dw_export: ' + e.message);
  process.exitCode = 1;
} finally {
  await browser.close();
  server.close();
}

if (result && !process.exitCode) {
  fs.rmSync(outDir, { recursive: true, force: true });
  fs.mkdirSync(outDir, { recursive: true });
  const recs = [];
  for (const l of result.layers) {
    fs.writeFileSync(path.join(outDir, l.asset + '.png'), Buffer.from(l.png.split(',')[1], 'base64'));
    const { png, ...meta } = l;
    recs.push(meta);
    console.log(`  ${(l.asset + '.png').padEnd(18)} ${l.cf.padEnd(9)} ${l.w}x${l.h} ${l.out}`);
  }
  fs.writeFileSync(path.join(outDir, 'layers.json'), JSON.stringify({ generator: 'tools/dw_export.mjs', mode: MODE, rows: recs }, null, 1) + '\n');
  console.log(`wrote ${recs.length} PNGs + layers.json to ${outDir}`);
}
