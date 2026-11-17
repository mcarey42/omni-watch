// The Mining Ship layer export (rd wave 1 Task 7; spec section 12.1, step 1): reads the designers' mockups in place and
// draws the theme's shared atlases, grime and thumbnails. It serves the repo and the design directory on 127.0.0.1
// (read only), opens tools/export/rd_export.html in headless Chrome (playwright-core), hands the page the forbidden
// list (tools/export/rd_terms.mjs's RD_FORBIDDEN source), runs window.rdExport() twice in fresh contexts, fails unless
// both agree, and writes <out>/<name>.png, <out>/glyphs/<name>_<code>[_g|_c].png and <out>/layers.json (each row's
// record: size, glyphs, origins, advances). Nothing is written unless every row exported and both runs agree.
//
// Usage: node tools/rd_export.mjs [--out DIR] [--table T] [--shared-only]
//                                     the table's rows (tools/export/rd_layers.json, or T), into tools/export/out-rd/
//                                     by default; --shared-only: only the rows whose face is "shared"
//        node tools/rd_export.mjs --selftest
//                                     the drawn-text guard on the list's own corpus and the exposure shim, on a page
//                                     with no mockups
// The drawn-text guard (spec section 13.2): a row whose glyphs or text field matches the list is refused before
// Chrome starts, and the page throws on any drawn text that matches (rd_export.js); either way the export exits
// non-zero and writes nothing (PR37).
// The design directory is $RD_DESIGN (the theme's directory: mockups/), or, when unset, design/themes/rd in the main
// checkout (this repo's git common dir's parent; public release Task 6: the theme id, which the owner's checkout links
// to the design package), resolved through any symlink. It is read only, and a missing one is a clear error.
// The list (public release Task 6) is loaded through tools/export/terms.mjs: tools/export/rd_terms.mjs (private), or,
// in a public tree where it is absent, the neutral mark alone (the self-test then runs on a neutral corpus).
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
const die = msg => { console.error('rd_export: ' + msg); process.exit(1); };
for (const a of args) if (a.startsWith('--') && !['--out', '--table', '--shared-only', '--selftest'].includes(a)) die('unknown option ' + a);

const SELFTEST = flag('--selftest');
let rt;
try { rt = await loadTerms(root, 'rd_terms'); } catch (e) { die(e.message); }
// No list (a public tree): only the neutral mark is forbidden, and the self-test's corpus is one hit and one miss.
const RD_FORBIDDEN = rt ? rt.RD_FORBIDDEN : new RegExp(NEUTRAL_MARK, 'i');
const RD_FORBIDDEN_SRC = rt ? rt.RD_FORBIDDEN_SRC : NEUTRAL_MARK;
const rdForbidden = rt ? rt.rdForbidden : (v => RD_FORBIDDEN.test(String(v)));
const rdSelftestCorpus = rt ? rt.rdSelftestCorpus : (() => ['a ' + NEUTRAL_MARK + ' b', 'Bowl']);
// The tests' neutral marker (tools/rd_terms.py test_marker): one more term, only ever adding a refusal, so a planted
// test never writes a real term. Unset in any real run.
const MARK = process.env.RD_TERMS_TEST_MARKER || '';
if (MARK && (!/^[a-z]{8,32}$/.test(MARK) || (rt && RD_FORBIDDEN.test(MARK)))) die(`RD_TERMS_TEST_MARKER ${MARK}: 8 to 32 lower-case letters, no term of the list`);
const forbidden = v => rdForbidden(v) || (MARK !== '' && v.toLowerCase().includes(MARK));   // R164: wrapped too
const PAGE_SRC = RD_FORBIDDEN_SRC + (MARK ? '|' + MARK : '');
const outDir = path.resolve(opt('--out') || path.join(root, 'tools/export/out-rd'));
const tablePath = path.resolve(opt('--table') || path.join(root, 'tools/export/rd_layers.json'));
const FONTS = ['SairaStencilOne-Regular.ttf', 'VT323-Regular.ttf', 'ChakraPetch-SemiBold.ttf', 'CormorantSC-Bold.ttf',
  'CormorantSC-Medium.ttf', 'IBMPlexSans-Medium.ttf', 'IBMPlexSans-Regular.ttf', 'IBMPlexSans-SemiBold.ttf',
  'IBMPlexMono-Regular.ttf', 'IBMPlexMono-Medium.ttf'];

// The table first: names and the drawn-text guard, before anything starts (PR37: refused, nothing written).
let table = null, rows = [];
if (!SELFTEST) {
  try { table = JSON.parse(fs.readFileSync(tablePath, 'utf8')); } catch (e) { die(`cannot read ${tablePath}: ${e.message}`); }
  rows = (table.rows || []).filter(r => !r.crc32);
  if (flag('--shared-only')) rows = rows.filter(r => r.face === 'shared');
  const NAME = /^rd_[a-z0-9_]{1,12}$/;
  const texts = (o, out = []) => {
    if (Array.isArray(o)) o.forEach(v => texts(v, out));
    else if (o && typeof o === 'object') for (const [k, v] of Object.entries(o)) {
      if ((k === 'glyphs' || k === 'text') && typeof v === 'string') out.push([k, v]); else texts(v, out);
    }
    return out;
  };
  for (const r of rows) {
    if (!NAME.test(r.name || '')) die(`${r.name}: names are rd_ + at most 12 characters`);
    for (const [k, v] of texts(r)) if (forbidden(v)) die(`${r.name}: a forbidden term in its ${k} field (tools/export/rd_terms.mjs); nothing written`);
  }
  if (!rows.length) die(`no rows to export in ${tablePath}`);
  if (!table.pinned || !table.pinned.zone || !table.pinned.utc) die(`${tablePath}: no pinned {utc, zone, thumb_t_ms}`);
  for (const f of FONTS) if (!fs.existsSync(path.join(root, 'tools/fonts', f))) die(`missing tools/fonts/${f}: run tools/fonts.sh`);
}

const chrome = process.env.CHROME || '/usr/bin/google-chrome';
if (!fs.existsSync(chrome)) die(`no Chrome at ${chrome} (set CHROME)`);
let chromium;
try { ({ chromium } = await import('playwright-core')); } catch { die('playwright-core is missing: npm --prefix tools ci'); }

function designDir() {
  if (process.env.RD_DESIGN) {
    const d = path.resolve(process.env.RD_DESIGN);
    return fs.existsSync(d) ? fs.realpathSync(d) : d;
  }
  let common;
  try { common = execFileSync('git', ['-C', root, 'rev-parse', '--path-format=absolute', '--git-common-dir'], { encoding: 'utf8' }).trim(); }
  catch { die('RD_DESIGN is unset and this is not a git checkout to derive it from'); }
  const d = path.join(path.dirname(common), 'design', 'themes', 'rd');
  return fs.existsSync(d) ? fs.realpathSync(d) : d;
}
let design = null;
if (!SELFTEST) {
  design = designDir();
  if (!fs.existsSync(path.join(design, 'mockups'))) die(`no mockups in the design directory ${design} (set RD_DESIGN)`);
}
// The mockups, in their index.html's order, then the shared digital face (in this repo). Only the reveal mockup's
// stencil(), timeMetrics(), grime(), (Task 10) buildStasisDoor(), (wave 2 Task 3) bayBase() and hazardRing(), (Task 4)
// buildAirDoors() and (Task 5) buildHullTile() and washMe() are published (rd_export.js's exposure shim).
const MOCKUPS = [
  { url: '/rd-design/mockups/faces-reveal.js', key: 'faces-reveal.js',
    expose: ['stencil', 'timeMetrics', 'grime', 'buildStasisDoor', 'bayBase', 'hazardRing', 'buildAirDoors',
             'buildHullTile', 'washMe'] },
  { url: '/rd-design/mockups/faces-bridge.js', key: 'faces-bridge.js', expose: [] },
  { url: '/rd-design/mockups/faces-quarters.js', key: 'faces-quarters.js', expose: [] },
  { url: '/design/mockups/faces-practical.js', key: 'faces-practical.js', expose: [] },
];

// Read-only server: /rd-design/... is the design directory, everything else the repo.
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.ttf': 'font/ttf', '.jpg': 'image/jpeg', '.png': 'image/png' };
const server = http.createServer((req, res) => {
  if (req.method !== 'GET') { res.writeHead(405); res.end(); return; }
  if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); return; }
  const u = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const [base, rel] = u.startsWith('/rd-design/') && design ? [design, u.slice('/rd-design/'.length)] : [root, u];
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

async function openPage() {
  const context = await browser.newContext({ timezoneId: SELFTEST ? 'UTC' : table.pinned.zone, locale: 'en-US' });
  await context.route(GOOGLE_FONTS, route => route.abort());
  await context.addInitScript({ content: 'window.RD_FORBIDDEN_SRC = ' + JSON.stringify(PAGE_SRC) + ';' });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  page.on('console', m => { if (m.type() === 'error' && !/net::ERR_FAILED/.test(m.text())) errors.push(m.text()); });
  await page.goto(`${base}/tools/export/rd_export.html`, { waitUntil: 'load' });
  return { context, page, errors };
}
async function exportOnce() {
  const { context, page, errors } = await openPage();
  if (errors.length) throw new Error('page errors:\n' + errors.join('\n'));
  const r = await page.evaluate(job => window.rdExport(job), { rows, pinned: table.pinned, mockups: MOCKUPS });
  if (errors.length) throw new Error('page errors:\n' + errors.join('\n'));
  await context.close();
  return r;
}

let result;
try {
  if (SELFTEST) {
    const { context, page, errors } = await openPage();
    const corpus = rdSelftestCorpus().map(text => ({ text, hit: RD_FORBIDDEN.test(text) }));
    const res = await page.evaluate(c => window.rdSelftest(c), corpus);
    await context.close();
    let bad = errors.length;
    for (const e of errors) console.log('SELFTEST FAIL page error: ' + e);
    for (const t of res) { if (!t.ok) bad++; console.log(`${t.ok ? 'ok  ' : 'FAIL'} ${t.what}`); }
    if (!corpus.some(c => c.hit) || !corpus.some(c => !c.hit)) { bad++; console.log('FAIL the corpus has no hit or no miss'); }
    console.log(`rd_export selftest: ${bad ? 'FAIL (' + bad + ')' : 'OK'} (${res.length} checks)`);
    process.exitCode = bad ? 1 : 0;
  } else {
    result = await exportOnce();
    const again = await exportOnce();   // determinism: a second export in a new context must give the same bytes
    const items = x => x.layers.map(l => [l.name, JSON.stringify(l)]);
    const a = items(result), b = new Map(items(again));
    const differ = a.filter(([n, v]) => b.get(n) !== v).map(([n]) => n);
    if (differ.length || a.length !== b.size) throw new Error('two exports differ: ' + (differ.join(', ') || 'item count'));
    console.log(`determinism: two exports byte-identical (${a.length} layers); drawn texts guarded: ${result.checks.drawnTexts}`);
  }
} catch (e) {
  console.error('rd_export: ' + e.message);
  process.exitCode = 1;
} finally {
  await browser.close();
  server.close();
}

if (result && !process.exitCode) {
  const b64 = d => Buffer.from(d.split(',')[1], 'base64');
  fs.rmSync(outDir, { recursive: true, force: true });
  fs.mkdirSync(path.join(outDir, 'glyphs'), { recursive: true });
  const recs = [];
  for (const l of result.layers) {
    const rec = { ...l };
    if (l.cells) {
      rec.cells = l.cells.map(c => {
        const code = c.ch.codePointAt(0).toString(16).padStart(2, '0');
        const r = { ...c };
        for (const [k, sfx] of [['png', ''], ['glow', '_g'], ['core', '_c']]) {
          if (!c[k]) continue;
          r[k] = `glyphs/${l.name}_${code}${sfx}.png`;
          fs.writeFileSync(path.join(outDir, r[k]), b64(c[k]));
        }
        return r;
      });
      console.log(`  ${l.name.padEnd(14)} ${l.kind.padEnd(7)} ${l.cells.length} glyphs`);
    } else {
      rec.png = `${l.name}.png`;
      fs.writeFileSync(path.join(outDir, rec.png), b64(l.png));
      console.log(`  ${(l.name + '.png').padEnd(18)} ${l.kind.padEnd(7)} ${l.w}x${l.h}`);
    }
    recs.push(rec);
  }
  fs.writeFileSync(path.join(outDir, 'layers.json'), JSON.stringify({ generator: 'tools/rd_export.mjs', rows: recs }, null, 1) + '\n');
  console.log(`wrote ${recs.length} layers + layers.json to ${outDir}`);
}
