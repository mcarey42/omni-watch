/* Omni Watch mockups \u2014 group 'Setup'
   wifi-qr       Wi-Fi provisioning via phone (real, scannable QR for ESP BLE Provisioning)
   rim-keyboard  Fallback text entry: characters around the rim, tap or dial-drag with a loupe
   nav-map       Navigation model: a turnable bezel of faces under an Omni-style magnifier window
   settings      Compact settings: drum list in a magnifier window, the rim sets the value
   netlist       Wi-Fi network list: the Settings drum, the rim shows the windowed network's signal
   faces-edit    Carousel editor: the picker's ring of every face, the centre orders and shows or hides
   Plain JS, Canvas 2D, no libraries. */
(function () {
  'use strict';
  window.OMNI_FACES = window.OMNI_FACES || [];

  /* ------------------------------------------------------------------ shared */
  const TAU = Math.PI * 2, W = 466, CX = 233, CY = 233;
  const FD = '"Cormorant SC", "Cormorant Garamond", Georgia, serif';
  const FS = '"IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif';
  const FM = '"IBM Plex Mono", ui-monospace, Menlo, monospace';
  const C = {
    ink: '#ece6dc', dim: '#a39a8c', faint: '#6d655a', hair: '#34302b',
    brass: '#c9a35b', brassHi: '#f0d595', brassLo: '#7a5c2a', cream: '#f3e6c6',
    red: '#d8443a', green: '#4fd06a', glass: '#0b0a08',
  };
  const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
  const smooth = t => { t = clamp(t, 0, 1); return t * t * (3 - 2 * t); };
  const easeOut = t => { t = clamp(t, 0, 1); return 1 - Math.pow(1 - t, 3); };
  const now = () => performance.now();
  const dist = (x, y) => Math.hypot(x - CX, y - CY);
  const angle = (x, y) => Math.atan2(y - CY, x - CX);
  const wrap = a => { while (a <= -Math.PI) a += TAU; while (a > Math.PI) a -= TAU; return a; };
  const inRect = (b, x, y) => x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
  function mulberry(s) { return function () { s |= 0; s = (s + 0x6D2B79F5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
  function rrect(ctx, x, y, w, h, r, cont) {   // cont: append to the current path (for even-odd frames)
    r = Math.min(r, w / 2, h / 2);
    if (!cont) ctx.beginPath();
    ctx.moveTo(x + r, y); ctx.arcTo(x + w, y, x + w, y + h, r); ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath();
  }
  function text(ctx, s, x, y, font, color, align = 'center', base = 'alphabetic') {
    ctx.font = font; ctx.fillStyle = color; ctx.textAlign = align; ctx.textBaseline = base; ctx.fillText(s, x, y);
  }
  function tracked(ctx, s, x, y, font, color, track) {   // letter-spaced centred caps
    ctx.font = font; ctx.fillStyle = color; ctx.textAlign = 'left'; ctx.textBaseline = 'alphabetic';
    const ws = [...s].map(ch => ctx.measureText(ch).width);
    let total = ws.reduce((a, b) => a + b, 0) + track * (s.length - 1), px = x - total / 2;
    [...s].forEach((ch, i) => { ctx.fillText(ch, px, y); px += ws[i] + track; });
  }
  /* curved text centred on angle a (radians, canvas convention), reading left\u2192right */
  function arcText(ctx, s, r, a, font, color, track = 0) {
    ctx.save(); ctx.font = font; ctx.fillStyle = color; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    const bottom = Math.sin(a) > 0.25;
    const ws = [...s].map(ch => ctx.measureText(ch).width + track);
    const total = ws.reduce((p, q) => p + q, 0) - track;
    let cur = bottom ? a + total / 2 / r : a - total / 2 / r;
    [...s].forEach((ch, i) => {
      const w = ws[i], am = bottom ? cur - (w - track) / 2 / r : cur + (w - track) / 2 / r;
      ctx.save(); ctx.translate(CX + Math.cos(am) * r, CY + Math.sin(am) * r);
      ctx.rotate(bottom ? am - Math.PI / 2 : am + Math.PI / 2); ctx.fillText(ch, 0, 0); ctx.restore();
      cur = bottom ? cur - w / r : cur + w / r;
    });
    ctx.restore();
  }

  /* offscreen cache, rendered at 2x so it stays crisp on high-dpi review screens */
  const cache = {};
  function cached(key, draw) {
    if (cache[key]) return cache[key];
    const c = document.createElement('canvas'); c.width = c.height = W * 2;
    const g = c.getContext('2d'); g.scale(2, 2); draw(g); cache[key] = c; return c;
  }
  const blit = (ctx, c) => ctx.drawImage(c, 0, 0, W, W);

  /* brass annulus: linear base + conic brushed sheen + radial bevel + tarnish speckle */
  function brassBand(g, r0, r1, o = {}) {
    const rnd = mulberry(o.seed || 7);
    g.save();
    g.beginPath(); g.arc(CX, CY, r1, 0, TAU); g.arc(CX, CY, r0, 0, TAU, true); g.clip();
    const lg = g.createLinearGradient(CX - r1 * 0.6, CY - r1, CX + r1 * 0.6, CY + r1);
    lg.addColorStop(0, '#dcc07e'); lg.addColorStop(0.32, '#a7833f'); lg.addColorStop(0.62, '#7a5c2a'); lg.addColorStop(1, '#46321a');
    g.fillStyle = lg; g.fillRect(0, 0, W, W);
    if (g.createConicGradient) {
      const cg = g.createConicGradient(-0.7, CX, CY);
      [[0, .18], [.07, -.14], [.17, .12], [.29, -.16], [.4, .1], [.53, -.12], [.66, .16], [.79, -.14], [.9, .07], [1, .18]]
        .forEach(([p, a]) => cg.addColorStop(p, a > 0 ? `rgba(255,236,190,${a})` : `rgba(25,12,0,${-a})`));
      g.fillStyle = cg; g.fillRect(0, 0, W, W);
    }
    const rg = g.createRadialGradient(CX, CY, r0, CX, CY, r1);
    rg.addColorStop(0, 'rgba(0,0,0,.6)'); rg.addColorStop(0.12, 'rgba(255,238,200,.32)'); rg.addColorStop(0.3, 'rgba(0,0,0,0)');
    rg.addColorStop(0.75, 'rgba(0,0,0,.06)'); rg.addColorStop(0.9, 'rgba(255,235,190,.22)'); rg.addColorStop(1, 'rgba(0,0,0,.65)');
    g.fillStyle = rg; g.fillRect(0, 0, W, W);
    const n = o.speck == null ? Math.round((r1 * r1 - r0 * r0) / 40) : o.speck;
    for (let i = 0; i < n; i++) {
      const a = rnd() * TAU, r = r0 + rnd() * (r1 - r0);
      g.fillStyle = rnd() < 0.72 ? `rgba(30,16,2,${0.05 + rnd() * 0.14})` : `rgba(255,240,200,${0.03 + rnd() * 0.08})`;
      g.fillRect(CX + Math.cos(a) * r, CY + Math.sin(a) * r, 0.8 + rnd() * 1.6, 0.8 + rnd() * 1.6);
    }
    if (o.dark) { g.fillStyle = `rgba(10,6,2,${o.dark})`; g.fillRect(0, 0, W, W); }
    g.restore();
    g.lineWidth = 1;
    g.strokeStyle = 'rgba(0,0,0,.8)'; g.beginPath(); g.arc(CX, CY, r0 + 0.5, 0, TAU); g.stroke();
    g.strokeStyle = 'rgba(255,232,180,.25)'; g.beginPath(); g.arc(CX, CY, r0 + 1.5, 0, TAU); g.stroke();
  }
  /* the thin brass bezel every Setup screen shares (case edge stand-in) */
  function thinBezel() {
    return cached('thinBezel', g => {
      brassBand(g, 222, 233, { seed: 3 });
      g.strokeStyle = 'rgba(201,163,91,.22)'; g.lineWidth = 1;
      g.beginPath(); g.arc(CX, CY, 214.5, 0, TAU); g.stroke();
      for (let i = 0; i < 60; i++) {           // engraved minute ticks on the chapter ring
        const a = i / 60 * TAU, l = i % 5 ? 3 : 6;
        g.strokeStyle = i % 5 ? 'rgba(201,163,91,.18)' : 'rgba(201,163,91,.4)';
        g.beginPath(); g.moveTo(CX + Math.cos(a) * 214.5, CY + Math.sin(a) * 214.5);
        g.lineTo(CX + Math.cos(a) * (214.5 - l), CY + Math.sin(a) * (214.5 - l)); g.stroke();
      }
    });
  }
  /* jewel lens (Omni red ~11 o'clock, green ~1 o'clock) */
  function lens(ctx, x, y, r, hue, lit) {
    ctx.save();
    const bz = ctx.createLinearGradient(x - r, y - r, x + r, y + r);
    bz.addColorStop(0, '#e6c886'); bz.addColorStop(0.5, '#8a6a33'); bz.addColorStop(1, '#3d2c14');
    ctx.fillStyle = bz; ctx.beginPath(); ctx.arc(x, y, r + 3, 0, TAU); ctx.fill();
    const base = hue === 'red' ? [216, 68, 58] : [79, 208, 106];
    const k = 0.18 + 0.82 * lit;
    const gg = ctx.createRadialGradient(x - r * 0.35, y - r * 0.35, r * 0.1, x, y, r);
    gg.addColorStop(0, `rgba(${Math.min(255, base[0] * k + 90 * lit)},${Math.min(255, base[1] * k + 90 * lit)},${Math.min(255, base[2] * k + 70 * lit)},1)`);
    gg.addColorStop(1, `rgba(${base[0] * k * 0.45},${base[1] * k * 0.45},${base[2] * k * 0.45},1)`);
    ctx.fillStyle = gg; ctx.beginPath(); ctx.arc(x, y, r, 0, TAU); ctx.fill();
    if (lit > 0.05) {
      const glow = ctx.createRadialGradient(x, y, r, x, y, r * 3.2);
      glow.addColorStop(0, `rgba(${base[0]},${base[1]},${base[2]},${0.35 * lit})`); glow.addColorStop(1, 'rgba(0,0,0,0)');
      ctx.fillStyle = glow; ctx.beginPath(); ctx.arc(x, y, r * 3.2, 0, TAU); ctx.fill();
    }
    ctx.fillStyle = 'rgba(255,255,255,.55)'; ctx.beginPath(); ctx.ellipse(x - r * 0.35, y - r * 0.4, r * 0.32, r * 0.18, -0.6, 0, TAU); ctx.fill();
    ctx.restore();
  }
  /* dark pill / key with brass hairline */
  function key(ctx, b, opts = {}) {
    const pressed = opts.pressed, primary = opts.primary;
    ctx.save();
    rrect(ctx, b.x, b.y, b.w, b.h, opts.radius == null ? b.h / 2 : opts.radius);
    if (primary) {
      const gr = ctx.createLinearGradient(0, b.y, 0, b.y + b.h);
      gr.addColorStop(0, pressed ? '#a7833f' : '#e3c582'); gr.addColorStop(1, pressed ? '#6d5024' : '#a07b38');
      ctx.fillStyle = gr; ctx.fill();
      ctx.strokeStyle = 'rgba(255,240,200,.5)'; ctx.lineWidth = 1; ctx.stroke();
    } else {
      ctx.fillStyle = pressed ? '#2a2317' : opts.on ? '#1f1a12' : '#12100c'; ctx.fill();
      ctx.strokeStyle = opts.on ? 'rgba(201,163,91,.85)' : 'rgba(201,163,91,.38)'; ctx.lineWidth = 1; ctx.stroke();
    }
    ctx.restore();
  }

  /* shared state between the setup screens */
  const S = {
    ssid: 'Home-Fibre',
    qr: { step: 0, stepT: 0, prevStep: 0, via: 'phone' },
    kb: { set: 'abc', shift: 0, text: '', lastT: 0, reveal: false, active: null, press: null, pressT: 0, repT: 0, lastShiftT: 0, shakeT: -1e9 },
    settings: { brightness: 0.7, sleep: 2, face: 0, reveal: 0, sound: 0, tz: 0 },
  };

  /* ------------------------------------------------------------------ QR encoder
     Byte mode, ECC L/M, versions 1\u201310. Follows ISO/IEC 18004 (structure after Nayuki's reference
     implementation): RS over GF(256)/0x11D, block interleave, all 8 masks scored with the 4 penalty rules. */
  const QR = (function () {
    const ECC = {
      L: { fmt: 1, cw: [-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18], blocks: [-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4] },
      M: { fmt: 0, cw: [-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26], blocks: [-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5] },
    };
    function rawModules(v) { let r = (16 * v + 128) * v + 64; if (v >= 2) { const n = Math.floor(v / 7) + 2; r -= (25 * n - 10) * n - 55; if (v >= 7) r -= 36; } return r; }
    const dataCw = (v, e) => Math.floor(rawModules(v) / 8) - ECC[e].cw[v] * ECC[e].blocks[v];
    function gmul(x, y) { let z = 0; for (let i = 7; i >= 0; i--) { z = (z << 1) ^ ((z >>> 7) * 0x11D); z ^= ((y >>> i) & 1) * x; } return z & 0xFF; }
    function rsDiv(deg) {
      const r = new Array(deg).fill(0); r[deg - 1] = 1; let root = 1;
      for (let i = 0; i < deg; i++) { for (let j = 0; j < deg; j++) { r[j] = gmul(r[j], root); if (j + 1 < deg) r[j] ^= r[j + 1]; } root = gmul(root, 2); }
      return r;
    }
    function rsRem(data, div) {
      const r = div.map(() => 0);
      for (const b of data) { const f = b ^ r.shift(); r.push(0); div.forEach((c, i) => { r[i] ^= gmul(c, f); }); }
      return r;
    }
    function penalty(M, n) {
      let p = 0;
      for (let pass = 0; pass < 2; pass++) for (let a = 0; a < n; a++) {
        let run = 0, prev = null; const line = [];
        for (let b = 0; b < n; b++) {
          const v = pass ? M[b][a] : M[a][b]; line.push(v);
          if (v === prev) { run++; if (run === 5) p += 3; else if (run > 5) p++; } else { prev = v; run = 1; }
        }
        for (let b = 0; b <= n - 7; b++) {
          if (line[b] && !line[b + 1] && line[b + 2] && line[b + 3] && line[b + 4] && !line[b + 5] && line[b + 6]) {
            const before = [1, 2, 3, 4].every(k => b - k < 0 || !line[b - k]);
            const after = [7, 8, 9, 10].every(k => b + k >= n || !line[b + k]);
            if (before || after) p += 40;
          }
        }
      }
      for (let y = 0; y < n - 1; y++) for (let x = 0; x < n - 1; x++) { const c = M[y][x]; if (c === M[y][x + 1] && c === M[y + 1][x] && c === M[y + 1][x + 1]) p += 3; }
      let dark = 0; for (const row of M) for (const v of row) if (v) dark++;
      p += Math.max(0, Math.ceil(Math.abs(dark * 20 - n * n * 10) / (n * n)) - 1) * 10;
      return p;
    }
    function encode(str, ecl) {
      ecl = ecl || 'M';
      const bytes = Array.from(new TextEncoder().encode(str));
      let v; for (v = 1; v <= 10; v++) if (4 + (v < 10 ? 8 : 16) + bytes.length * 8 <= dataCw(v, ecl) * 8) break;
      if (v > 10) throw new Error('QR payload too long');
      const bits = []; const push = (val, n) => { for (let i = n - 1; i >= 0; i--) bits.push((val >>> i) & 1); };
      push(4, 4); push(bytes.length, v < 10 ? 8 : 16); bytes.forEach(b => push(b, 8));
      const cap = dataCw(v, ecl) * 8;
      push(0, Math.min(4, cap - bits.length)); push(0, (8 - bits.length % 8) % 8);
      for (let pad = 0xEC; bits.length < cap; pad ^= 0xEC ^ 0x11) push(pad, 8);
      const data = []; for (let i = 0; i < bits.length; i += 8) { let b = 0; for (let j = 0; j < 8; j++) b = (b << 1) | bits[i + j]; data.push(b); }
      const nb = ECC[ecl].blocks[v], el = ECC[ecl].cw[v], raw = Math.floor(rawModules(v) / 8);
      const nShort = nb - raw % nb, shortLen = Math.floor(raw / nb), div = rsDiv(el), blocks = [];
      for (let i = 0, k = 0; i < nb; i++) {
        const dat = data.slice(k, k + shortLen - el + (i < nShort ? 0 : 1)); k += dat.length;
        const ecc = rsRem(dat, div); if (i < nShort) dat.push(0); blocks.push(dat.concat(ecc));
      }
      const cw = []; for (let i = 0; i < blocks[0].length; i++) blocks.forEach((b, j) => { if (i !== shortLen - el || j >= nShort) cw.push(b[i]); });
      const size = v * 4 + 17;
      const M = Array.from({ length: size }, () => new Array(size).fill(false));
      const F = Array.from({ length: size }, () => new Array(size).fill(false));
      const set = (x, y, d) => { M[y][x] = d; F[y][x] = true; };
      for (let i = 0; i < size; i++) { set(6, i, i % 2 === 0); set(i, 6, i % 2 === 0); }
      const finder = (x, y) => { for (let dy = -4; dy <= 4; dy++) for (let dx = -4; dx <= 4; dx++) { const d = Math.max(Math.abs(dx), Math.abs(dy)), xx = x + dx, yy = y + dy; if (xx >= 0 && xx < size && yy >= 0 && yy < size) set(xx, yy, d !== 2 && d !== 4); } };
      finder(3, 3); finder(size - 4, 3); finder(3, size - 4);
      let pos = [];
      if (v > 1) { const n = Math.floor(v / 7) + 2, step = Math.ceil((v * 4 + 4) / (n * 2 - 2)) * 2; pos = [6]; for (let p = size - 7; pos.length < n; p -= step) pos.splice(1, 0, p); }
      const L = pos.length;
      for (let i = 0; i < L; i++) for (let j = 0; j < L; j++) {
        if ((i === 0 && j === 0) || (i === 0 && j === L - 1) || (i === L - 1 && j === 0)) continue;
        for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) set(pos[i] + dx, pos[j] + dy, Math.max(Math.abs(dx), Math.abs(dy)) !== 1);
      }
      const drawFormat = mask => {
        const d = (ECC[ecl].fmt << 3) | mask; let rem = d;
        for (let i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
        const b = ((d << 10) | rem) ^ 0x5412, g = i => ((b >>> i) & 1) === 1;
        for (let i = 0; i <= 5; i++) set(8, i, g(i));
        set(8, 7, g(6)); set(8, 8, g(7)); set(7, 8, g(8));
        for (let i = 9; i < 15; i++) set(14 - i, 8, g(i));
        for (let i = 0; i < 8; i++) set(size - 1 - i, 8, g(i));
        for (let i = 8; i < 15; i++) set(8, size - 15 + i, g(i));
        set(8, size - 8, true);
      };
      drawFormat(0);
      if (v >= 7) {
        let rem = v; for (let i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >>> 11) * 0x1F25);
        const b = (v << 12) | rem;
        for (let i = 0; i < 18; i++) { const bit = ((b >>> i) & 1) === 1, a = size - 11 + i % 3, c = Math.floor(i / 3); set(a, c, bit); set(c, a, bit); }
      }
      let bi = 0;
      for (let right = size - 1; right >= 1; right -= 2) {
        if (right === 6) right = 5;
        for (let vert = 0; vert < size; vert++) for (let j = 0; j < 2; j++) {
          const x = right - j, up = ((right + 1) & 2) === 0, y = up ? size - 1 - vert : vert;
          if (!F[y][x] && bi < cw.length * 8) { M[y][x] = ((cw[bi >>> 3] >>> (7 - (bi & 7))) & 1) === 1; bi++; }
        }
      }
      const mf = [(x, y) => (x + y) % 2 === 0, (x, y) => y % 2 === 0, x => x % 3 === 0, (x, y) => (x + y) % 3 === 0,
        (x, y) => (Math.floor(x / 3) + Math.floor(y / 2)) % 2 === 0, (x, y) => x * y % 2 + x * y % 3 === 0,
        (x, y) => (x * y % 2 + x * y % 3) % 2 === 0, (x, y) => ((x + y) % 2 + x * y % 3) % 2 === 0];
      const apply = m => { for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) if (!F[y][x] && mf[m](x, y)) M[y][x] = !M[y][x]; };
      let best = 0, bestP = Infinity;
      for (let m = 0; m < 8; m++) { apply(m); drawFormat(m); const p = penalty(M, size); if (p < bestP) { bestP = p; best = m; } apply(m); }
      apply(best); drawFormat(best);
      return { size, modules: M, version: v, mask: best, ecl };
    }
    return { encode };
  })();
  window.OMNI_SETUP_QR = QR;   // exposed for the decode test

  /* ESP-IDF network_provisioning QR payload (the format the ESP BLE Provisioning app scans) */
  const PROV = { name: 'PROV_OMNI_3F2A', pop: '7c1e94b2' };
  const QR_PAYLOAD = JSON.stringify({ ver: 'v1', name: PROV.name, pop: PROV.pop, transport: 'ble' });
  let qrCode = null;
  const QR_MOD = 7, QR_QUIET = 3;

  /* ------------------------------------------------------------------ 1. wifi-qr */
  const typeBtn = { x: 149, y: 396, w: 168, h: 46 };
  const STEPS = ['Waiting for phone', 'Phone linked', 'Joining network', 'Time set'];

  function setStep(n) {
    S.qr.prevStep = S.qr.step; S.qr.step = n; S.qr.stepT = now();
  }
  function drawQRPlate(ctx, k, alpha) {   // k = scale (1 = full), around centre
    if (!qrCode) qrCode = QR.encode(QR_PAYLOAD, 'L');
    const n = qrCode.size, full = (n + QR_QUIET * 2) * QR_MOD;
    ctx.save(); ctx.globalAlpha = alpha;
    ctx.translate(CX, CY); ctx.scale(k, k);
    const x0 = -Math.floor(full / 2), y0 = x0;
    // brass frame + warm-white plate
    rrect(ctx, x0 - 5, y0 - 5, full + 10, full + 10, 14);
    const fr = ctx.createLinearGradient(x0, y0, x0 + full, y0 + full);
    fr.addColorStop(0, '#e6c886'); fr.addColorStop(0.5, '#8d6c34'); fr.addColorStop(1, '#4a3518');
    ctx.fillStyle = fr; ctx.fill();
    rrect(ctx, x0, y0, full, full, 10); ctx.fillStyle = '#f6f3ec'; ctx.fill();
    ctx.fillStyle = '#000';
    const q = QR_QUIET * QR_MOD;
    for (let y = 0; y < n; y++) {
      let x = 0;
      while (x < n) {   // merge horizontal runs: fewer rects, no hairline seams
        if (!qrCode.modules[y][x]) { x++; continue; }
        let e = x; while (e < n && qrCode.modules[y][e]) e++;
        ctx.fillRect(x0 + q + x * QR_MOD, y0 + q + y * QR_MOD, (e - x) * QR_MOD, QR_MOD);
        x = e;
      }
    }
    ctx.restore();
  }
  function phoneGlyph(ctx, x, y, s, col) {
    ctx.save(); ctx.strokeStyle = col; ctx.lineWidth = 2.2;
    rrect(ctx, x - 13 * s, y - 22 * s, 26 * s, 44 * s, 5 * s); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(x - 4 * s, y + 17 * s); ctx.lineTo(x + 4 * s, y + 17 * s); ctx.stroke();
    ctx.restore();
  }
  function wifiGlyph(ctx, x, y, s, col, t) {
    ctx.save(); ctx.lineCap = 'round';
    for (let i = 0; i < 3; i++) {
      const on = t == null ? 1 : 0.3 + 0.7 * smooth(Math.sin((t / 260) - i * 0.9) * 0.5 + 0.5);
      ctx.strokeStyle = col; ctx.globalAlpha = on; ctx.lineWidth = 3.2;
      ctx.beginPath(); ctx.arc(x, y + 14 * s, (10 + i * 11) * s, -Math.PI * 0.78, -Math.PI * 0.22); ctx.stroke();
    }
    ctx.globalAlpha = 1; ctx.fillStyle = col; ctx.beginPath(); ctx.arc(x, y + 14 * s, 3.4 * s, 0, TAU); ctx.fill();
    ctx.restore();
  }
  function checkGlyph(ctx, x, y, s, k) {
    ctx.save(); ctx.strokeStyle = C.green; ctx.lineWidth = 4; ctx.lineCap = 'round'; ctx.lineJoin = 'round';
    ctx.globalAlpha = 0.95;
    ctx.beginPath(); ctx.arc(x, y, 26 * s, -Math.PI / 2, -Math.PI / 2 + TAU * easeOut(k * 1.4)); ctx.stroke();
    const p = clamp((k - 0.35) / 0.5, 0, 1);
    if (p > 0) {
      const pts = [[-11, 1], [-3, 9], [13, -8]];
      ctx.beginPath(); ctx.moveTo(x + pts[0][0] * s, y + pts[0][1] * s);
      const l1 = Math.min(1, p * 2), l2 = clamp(p * 2 - 1, 0, 1);
      ctx.lineTo(x + (pts[0][0] + (pts[1][0] - pts[0][0]) * l1) * s, y + (pts[0][1] + (pts[1][1] - pts[0][1]) * l1) * s);
      if (l2 > 0) ctx.lineTo(x + (pts[1][0] + (pts[2][0] - pts[1][0]) * l2) * s, y + (pts[1][1] + (pts[2][1] - pts[1][1]) * l2) * s);
      ctx.stroke();
    }
    ctx.restore();
  }

  OMNI_FACES.push({
    id: 'wifi-qr', group: 'Setup', title: 'Wi-Fi from your phone',
    summary: 'First boot: a real, scannable ESP BLE Provisioning QR, then link \u2192 join \u2192 time set. Tap to advance.',
    notes: `
<p><b>What it is.</b> The first screen on first boot (or whenever no saved network answers). The watch has no clock chip,
so until Wi-Fi + NTP succeed it cannot tell the time; this screen's only job is to get there in one scan.</p>
<p><b>The QR is real.</b> It encodes Espressif's standard provisioning payload
<code>${QR_PAYLOAD.replace(/</g, '&lt;')}</code> \u2014 byte mode, ECC&nbsp;L, version&nbsp;4 (33\u00d733), mask chosen by the
standard penalty rules, drawn by a ~150-line encoder in this file. Scan it with <b>ESP BLE Provisioning</b>
(Android/iOS) to see the app accept it. Firmware side: <code>network_provisioning</code> with the BLE scheme, security&nbsp;1, the PoP
printed per device (derive it from the MAC or a random value in NVS; the name suffix is the last MAC bytes).</p>
<ul>
<li><b>Size.</b> 7&nbsp;px modules + 3-module quiet zone = a 273&nbsp;px plate (\u224826&nbsp;mm across on the 1.75" panel, \u22480.67&nbsp;mm per module).
Corners sit at r\u2248193, inside the circle with room for a title and a button. ECC&nbsp;L is deliberate: an emissive screen has no
dirt or creases to correct, and the lower level keeps us at version&nbsp;4 with bigger modules \u2014 bigger modules matter more for a phone
camera at arm's length than redundancy.</li>
<li><b>Contrast vs. AMOLED.</b> Dark-on-light is what every scanner expects, so the plate is warm white \u2014 the one big bright area in the
whole UI. Mitigations: the plate shows only while unprovisioned, the screen dims to 40&nbsp;% after 60&nbsp;s and sleeps after 3&nbsp;min
(lift to bring it back), and firmware should orbit the plate by \u00b11&nbsp;px every 30&nbsp;s.</li>
<li><b>Omni lenses as status.</b> The red lens at 11 o'clock blinks while the watch is "not fixed" (no time), as the Omni's red
lens does; the green lens at 1 o'clock lights solid when time is set. A four-tick track between them shows the step.</li>
<li><b>States</b> (tap the screen to advance in this mockup): waiting \u2192 <i>phone linked</i> (BLE session up, choose a network in the app)
\u2192 <i>joining "Home-Fibre"</i> (auto-advances here after ~3&nbsp;s) \u2192 <i>time set \u2713</i> with the synced clock. Tap again to restart.</li>
<li><b>Type it instead</b> (46&nbsp;px pill) opens the rim keyboard for the password; "Join" there comes back here at the joining step.
A real build also needs a network picker before the keyboard (scan list on the rim, strongest first) \u2014 not drawn this round.</li>
<li><b>Failure</b> (not drawn): wrong password \u2192 red lens fast-blinks, the ring retracts to step 2 with "Wrong password \u2014 retry on phone",
and the app reports the reason (<code>WIFI_PROV_STA_AUTH_ERROR</code>).</li>
</ul>
<p><b>Feasibility.</b> Trivial in LVGL (<code>lv_qrcode</code> exists, or blit our matrix). BLE provisioning + Wi-Fi + NTP fit the S3
comfortably; free the BLE stack after provisioning to reclaim ~100&nbsp;KB RAM.</p>`,
    init() { if (!qrCode) qrCode = QR.encode(QR_PAYLOAD, 'L'); },
    wake() { S.qr.wakeT = now(); },
    frame(ctx, t, env) {
      const T = now(), st = S.qr, k = (T - st.stepT) / 420;
      if (st.step === 2 && T - st.stepT > 3200) setStep(3);
      blit(ctx, thinBezel());

      // Omni lenses + step ticks
      const ra = 202, aR = -Math.PI / 2 - 0.52, aG = -Math.PI / 2 + 0.52;
      const blink = st.step < 3 ? 0.25 + 0.75 * smooth(Math.sin(T / 380) * 0.5 + 0.5) : 0;
      lens(ctx, CX + Math.cos(aR) * ra, CY + Math.sin(aR) * ra, 7, 'red', st.step < 3 ? blink : 0.0);
      lens(ctx, CX + Math.cos(aG) * ra, CY + Math.sin(aG) * ra, 7, 'green', st.step === 3 ? smooth(k) : 0);
      for (let i = 0; i < 4; i++) {
        const a = -Math.PI / 2 + (i - 1.5) * 0.19, x = CX + Math.cos(a) * ra, y = CY + Math.sin(a) * ra;
        const done = i < st.step || st.step === 3, cur = i === st.step && st.step < 3;
        ctx.beginPath(); ctx.arc(x, y, cur ? 4 : 3, 0, TAU);
        ctx.fillStyle = done ? C.brass : cur ? `rgba(240,213,149,${0.5 + 0.5 * Math.sin(T / 200)})` : '#2b261f'; ctx.fill();
      }

      // the QR plate shrinks away when the phone links; comes back on restart
      const qrK = st.step === 0 ? (st.prevStep !== 0 ? easeOut(k) : 1) : (st.prevStep === 0 && st.step === 1 ? 1 - easeOut(k) : 0);
      if (qrK > 0.001) {
        drawQRPlate(ctx, 0.55 + 0.45 * qrK, qrK);
        const a = qrK;
        ctx.globalAlpha = a;
        text(ctx, 'Scan to set up', CX, 76, `700 26px ${FD}`, '#eadcbc');
        text(ctx, 'ESP BLE Provisioning app \u00b7 ' + PROV.name, CX, 386, `400 11px ${FM}`, C.dim);
        key(ctx, typeBtn, { pressed: S.qr.pressBtn });
        // little keyboard glyph
        ctx.save(); ctx.strokeStyle = C.brass; ctx.lineWidth = 1.4;
        rrect(ctx, typeBtn.x + 18, typeBtn.y + 15, 22, 15, 3); ctx.stroke();
        ctx.fillStyle = C.brass;
        for (let r = 0; r < 2; r++) for (let c = 0; c < 4; c++) ctx.fillRect(typeBtn.x + 21 + c * 4.6, typeBtn.y + 18 + r * 4, 2.2, 2);
        ctx.fillRect(typeBtn.x + 23, typeBtn.y + 26, 12, 1.6);
        ctx.restore();
        text(ctx, 'Type it instead', typeBtn.x + 104, typeBtn.y + 28, `500 14px ${FS}`, C.ink);
        ctx.globalAlpha = 1;
      }
      if (st.step === 0) return;

      // progress ring for steps 1\u20133
      const inK = easeOut((T - st.stepT) / 500);
      const appear = st.prevStep === 0 ? smooth((T - st.stepT - 150) / 450) : 1;
      ctx.save(); ctx.globalAlpha = appear;
      ctx.strokeStyle = '#231e17'; ctx.lineWidth = 3; ctx.beginPath(); ctx.arc(CX, CY, 158, 0, TAU); ctx.stroke();
      const from = (st.step - 1) / 3, to = st.step / 3, p = from + (to - from) * inK;
      const a0 = -Math.PI / 2, a1 = a0 + TAU * p;
      const pg = ctx.createLinearGradient(CX - 158, CY - 158, CX + 158, CY + 158);
      pg.addColorStop(0, '#f0d595'); pg.addColorStop(1, '#8d6c34');
      ctx.strokeStyle = st.step === 3 ? C.green : pg; ctx.lineWidth = 3; ctx.lineCap = 'round';
      ctx.beginPath(); ctx.arc(CX, CY, 158, a0, Math.max(a0 + 0.01, a1)); ctx.stroke();
      for (let i = 0; i <= 3; i++) {          // station marks
        const a = a0 + TAU * i / 3 * (i === 3 ? 0.999 : 1);
        ctx.fillStyle = i <= st.step ? (st.step === 3 ? C.green : C.brassHi) : '#3a3229';
        ctx.beginPath(); ctx.arc(CX + Math.cos(a) * 158, CY + Math.sin(a) * 158, i === 0 ? 0 : 4, 0, TAU); ctx.fill();
      }
      if (st.step === 2) {                    // working comet chasing round the track
        const ca = (T / 900) % 1 * TAU;
        for (let i = 0; i < 18; i++) {
          ctx.strokeStyle = `rgba(240,213,149,${(1 - i / 18) * 0.5})`; ctx.lineWidth = 3;
          ctx.beginPath(); ctx.arc(CX, CY, 158, ca - (i + 1) * 0.03, ca - i * 0.03); ctx.stroke();
        }
      }
      // centre content
      const ck = smooth((T - st.stepT - (st.prevStep === 0 ? 250 : 60)) / 380);
      ctx.globalAlpha = appear * ck;
      const dy = (1 - ck) * 8;
      if (st.step === 1) {
        phoneGlyph(ctx, CX - 30, 166 + dy, 1, C.brassHi);
        ctx.save(); ctx.setLineDash([3, 5]); ctx.lineDashOffset = -T / 60; ctx.strokeStyle = C.brass; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.moveTo(CX - 12, 166 + dy); ctx.lineTo(CX + 18, 166 + dy); ctx.stroke(); ctx.restore();
        ctx.save(); ctx.strokeStyle = C.brassHi; ctx.lineWidth = 2.2; ctx.beginPath(); ctx.arc(CX + 36, 166 + dy, 15, 0, TAU); ctx.stroke(); ctx.restore();
        text(ctx, 'Phone linked', CX, 238 + dy, `700 30px ${FD}`, '#eadcbc');
        text(ctx, 'Now choose your network', CX, 266 + dy, `400 14px ${FS}`, C.dim);
        text(ctx, 'in the app on your phone', CX, 285 + dy, `400 14px ${FS}`, C.dim);
        text(ctx, 'BLE \u00b7 ' + PROV.name, CX, 318 + dy, `400 11px ${FM}`, C.faint);
      } else if (st.step === 2) {
        wifiGlyph(ctx, CX, 160 + dy, 1, C.brassHi, T);
        text(ctx, 'Joining', CX, 238 + dy, `700 30px ${FD}`, '#eadcbc');
        text(ctx, '\u201c' + S.ssid + '\u201d', CX, 266 + dy, `500 16px ${FS}`, C.ink);
        text(ctx, st.via === 'typed' ? 'with the password you typed' : 'credentials received from phone', CX, 290 + dy, `400 13px ${FS}`, C.dim);
        text(ctx, 'WPA2 \u00b7 ch 6 \u00b7 \u221258 dBm', CX, 318 + dy, `400 11px ${FM}`, C.faint);
      } else {
        checkGlyph(ctx, CX, 136, 1, clamp((T - st.stepT) / 900, 0, 1));
        const d = env.now();
        const hh = String(d.getHours()).padStart(2, '0'), mm = String(d.getMinutes()).padStart(2, '0');
        text(ctx, hh + ':' + mm, CX, 245 + dy, `700 70px ${FD}`, '#f1e4c4');
        text(ctx, d.toLocaleDateString('en-GB', { weekday: 'long', day: 'numeric', month: 'long' }), CX, 274 + dy, `400 14px ${FS}`, C.dim);
        text(ctx, 'time set \u00b7 pool.ntp.org \u00b7 ' + env.location.name, CX, 300 + dy, `400 11px ${FM}`, C.faint);
      }
      tracked(ctx, st.step === 3 ? 'TAP TO CONTINUE' : `STEP ${st.step + 1} OF 4`, CX, 370, `600 10px ${FS}`, st.step === 3 ? C.brass : C.faint, 2.4);
      ctx.restore();
    },
    pointer(type, x, y, env) {
      const st = S.qr;
      if (type === 'down') { st.downX = x; st.downY = y; st.pressBtn = st.step === 0 && inRect(typeBtn, x, y); return; }
      if (type === 'move') { if (st.pressBtn && !inRect(typeBtn, x, y)) st.pressBtn = false; return; }
      if (type !== 'up') return;
      const tap = Math.hypot(x - st.downX, y - st.downY) < 14;
      if (st.pressBtn && inRect(typeBtn, x, y)) { st.pressBtn = false; env.goto('rim-keyboard'); return; }
      st.pressBtn = false;
      if (!tap) return;
      if (st.step === 3) { st.via = 'phone'; setStep(0); } else setStep(st.step + 1);
    },
  });

  /* ------------------------------------------------------------------ 2. rim-keyboard */
  const KB = {
    sets: { abc: 'abcdefghijklmnopqrstuvwxyz', num: '0123456789-_.@!?#$%&*+=/:;', sym: '"\'(),<>[\\]^`{|}~' },
    R: 196, r0: 171, r1: 222, loupeR: 112,
    btn: {
      back: { x: 158, y: 86, w: 150, h: 44 },
      field: { x: 101, y: 144, w: 214, h: 52 },
      eye: { x: 321, y: 148, w: 44, h: 44 },
      shift: { x: 116, y: 212, w: 54, h: 48 },
      mode: { x: 176, y: 212, w: 54, h: 48 },
      space: { x: 236, y: 212, w: 54, h: 48 },
      del: { x: 296, y: 212, w: 54, h: 48 },
      done: { x: 161, y: 274, w: 144, h: 50 },
    },
  };
  function kbChars() {
    const k = S.kb, s = KB.sets[k.set];
    return k.set === 'abc' && k.shift ? s.toUpperCase() : s;
  }
  const kbStep = () => TAU / kbChars().length;
  function kbIndexAt(x, y) {
    const n = kbChars().length; let u = angle(x, y) + Math.PI / 2; u = ((u % TAU) + TAU) % TAU;
    return Math.round(u / (TAU / n)) % n;
  }
  const kbAngle = i => -Math.PI / 2 + i * kbStep();
  function kbType(ch) {
    const k = S.kb;
    if (k.text.length >= 63) { k.shakeT = now(); return; }   // WPA2 passphrase max 63
    k.text += ch; k.lastT = now();
    if (k.set === 'abc' && k.shift === 1) k.shift = 0;
    k.flashT = now(); k.flashCh = ch;
  }
  function kbButton(name, env) {
    const k = S.kb;
    if (name === 'back') { setStep(0); env.goto('wifi-qr'); }
    else if (name === 'eye') k.reveal = !k.reveal;
    else if (name === 'shift') {
      if (k.set === 'abc') {
        const T = now();
        if (k.shift === 0) { k.shift = 1; k.lastShiftT = T; }
        else if (k.shift === 1 && T - k.lastShiftT < 400) k.shift = 2;
        else k.shift = 0;
      } else k.set = k.set === 'num' ? 'sym' : 'num';
      k.ringT = now();
    } else if (name === 'mode') { k.set = k.set === 'abc' ? 'num' : 'abc'; k.shift = 0; k.ringT = now(); }
    else if (name === 'space') kbType(' ');
    else if (name === 'del') { k.text = k.text.slice(0, -1); k.lastT = 0; }
    else if (name === 'done') {
      if (!k.text.length) { k.shakeT = now(); return; }
      S.qr.via = 'typed'; S.qr.prevStep = 1; S.qr.step = 2; S.qr.stepT = now();
      env.goto('wifi-qr');
    }
  }
  function kbRing() {
    return cached('kbRing', g => {
      brassBand(g, KB.r1, 233, { seed: 11 });
      brassBand(g, KB.r0, KB.r1, { seed: 5, dark: 0.62, speck: 1400 });
      g.strokeStyle = 'rgba(201,163,91,.35)'; g.lineWidth = 1;
      g.beginPath(); g.arc(CX, CY, KB.r0 - 3.5, 0, TAU); g.stroke();
    });
  }
  function eyeIcon(ctx, x, y, open, col) {
    ctx.save(); ctx.strokeStyle = col; ctx.lineWidth = 1.8; ctx.lineCap = 'round';
    ctx.beginPath(); ctx.moveTo(x - 11, y); ctx.quadraticCurveTo(x, y - 10, x + 11, y); ctx.quadraticCurveTo(x, y + 10, x - 11, y); ctx.stroke();
    ctx.beginPath(); ctx.arc(x, y, 3.6, 0, TAU); ctx.stroke();
    if (!open) { ctx.beginPath(); ctx.moveTo(x - 10, y + 9); ctx.lineTo(x + 10, y - 9); ctx.stroke(); }
    ctx.restore();
  }
  function shiftIcon(ctx, x, y, state, col) {
    ctx.save(); ctx.strokeStyle = col; ctx.fillStyle = col; ctx.lineWidth = 1.8; ctx.lineJoin = 'round';
    ctx.beginPath(); ctx.moveTo(x, y - 10); ctx.lineTo(x + 9, y); ctx.lineTo(x + 4, y); ctx.lineTo(x + 4, y + 7);
    ctx.lineTo(x - 4, y + 7); ctx.lineTo(x - 4, y); ctx.lineTo(x - 9, y); ctx.closePath();
    if (state) ctx.fill(); else ctx.stroke();
    if (state === 2) ctx.fillRect(x - 5, y + 10, 10, 2);
    ctx.restore();
  }
  function delIcon(ctx, x, y, col) {
    ctx.save(); ctx.strokeStyle = col; ctx.lineWidth = 1.8; ctx.lineJoin = 'round'; ctx.lineCap = 'round';
    ctx.beginPath(); ctx.moveTo(x - 12, y); ctx.lineTo(x - 5, y - 8); ctx.lineTo(x + 11, y - 8); ctx.lineTo(x + 11, y + 8); ctx.lineTo(x - 5, y + 8); ctx.closePath(); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(x - 1, y - 4); ctx.lineTo(x + 7, y + 4); ctx.moveTo(x + 7, y - 4); ctx.lineTo(x - 1, y + 4); ctx.stroke();
    ctx.restore();
  }

  OMNI_FACES.push({
    id: 'rim-keyboard', group: 'Setup', title: 'Rim keyboard',
    summary: 'Fallback password entry: one ring of characters; tap, or drag round the rim with a loupe and release.',
    notes: `
<p><b>One ring, four sets.</b> A single ring of up to 26 characters at r&nbsp;=&nbsp;196 gives each one a 47&nbsp;px arc \u2014 over the
44&nbsp;px minimum \u2014 while two rings would squeeze the inner ring under 40&nbsp;px <i>and</i> eat the centre that has to hold the
field and the big keys. Sets: <b>abc</b>, <b>ABC</b> (\u21e7 once = one capital, double-tap = caps lock, iOS habits),
<b>123</b> (digits + the 16 symbols most often found in passwords: <code>- _ . @ ! ? # $ % &amp; * + = / : ;</code>) and
<b>#+=</b> (the other 16: quotes, brackets, <code>\\ ^ \` | ~ , &lt; &gt;</code>). Together they cover all 95 printable ASCII
characters, i.e. every legal WPA2 passphrase (8\u201363 chars). Space is its own key. In 123/#+= the \u21e7 key becomes the 123\u21c4#+= switch.</p>
<p><b>One gesture, two styles.</b> Every touch on the ring does the same thing: the character under the finger lights up
and a <b>loupe</b> appears 85&nbsp;px inward along the same radius (where the finger isn't), showing it big with its neighbours;
<b>release commits</b>. So a quick tap types the key you hit, and if you missed you simply keep your finger down and
<b>slide round the rim like a dial</b> until the loupe shows the right one. Neighbours on the ring swell slightly (fish-eye) so you
can see where you are. To abandon, slide inward past the inner edge: the loupe shows \u2715 and release types nothing. Each character change
should click the speaker softly (a detent) in firmware.</p>
<p><b>Centre.</b> Network name (also the back target), the field \u2014 password shows the last character for 0.9&nbsp;s then a dot,
the eye toggles reveal \u2014 then four 54\u00d748 keys (\u21e7, 123, space, delete; hold delete to repeat) and a 144\u00d750 <b>Join</b>.
Plex Mono on the ring and in the field so <code>l I 1 | O 0</code> are distinguishable. Join with an empty field shakes it.</p>
<p><b>Future swipe mode</b> layers on without moving a key: a stroke that starts in the centre and touches the ring
records the ring characters it dwells on or turns at, then returns; dictionary decoding (swipe is for words, never passwords)
runs on the angle sequence. Because the ring order is fixed and alphabetical, a later "frequency" ordering
(e.g. ETAOIN spread to maximise angular distance between common bigrams) is a layout swap, not a redesign.</p>
<p><b>Feasibility.</b> LVGL: the ring is a pre-rendered bitmap with glyph labels; the loupe is a small redraw region
(~100\u00d7100). Angle-to-index is one atan2 per touch event. Open: should the ring rotate so the last-used set's
first letter sits under the thumb? (Probably not \u2014 fixed positions build muscle memory.)</p>`,
    wake() { S.kb.active = null; },
    frame(ctx, t, env) {
      const k = S.kb, T = now(), chars = kbChars(), n = chars.length, step = TAU / n, b = KB.btn;
      // delete auto-repeat
      if (k.press === 'del' && T - k.pressT > 450 && T - k.repT > 85) { k.text = k.text.slice(0, -1); k.repT = T; }

      blit(ctx, kbRing());
      const act = k.active, ringIn = smooth((T - (k.ringT || 0)) / 260);
      // active segment highlight + red index notch
      if (act && !act.cancel) {
        const a = kbAngle(act.idx);
        ctx.save();
        ctx.beginPath(); ctx.arc(CX, CY, KB.r1 - 1, a - step / 2, a + step / 2); ctx.arc(CX, CY, KB.r0 + 1, a + step / 2, a - step / 2, true); ctx.closePath();
        const hg = ctx.createRadialGradient(CX, CY, KB.r0, CX, CY, KB.r1);
        hg.addColorStop(0, '#a07b38'); hg.addColorStop(0.5, '#e8cb88'); hg.addColorStop(1, '#a07b38');
        ctx.fillStyle = hg; ctx.fill();
        ctx.fillStyle = C.red; ctx.beginPath();
        ctx.moveTo(CX + Math.cos(a) * 224, CY + Math.sin(a) * 224);
        ctx.lineTo(CX + Math.cos(a - 0.035) * 233, CY + Math.sin(a - 0.035) * 233);
        ctx.lineTo(CX + Math.cos(a + 0.035) * 233, CY + Math.sin(a + 0.035) * 233); ctx.closePath(); ctx.fill();
        ctx.restore();
      }
      // separators + characters (fish-eye near the finger)
      ctx.save();
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      for (let i = 0; i < n; i++) {
        const a = kbAngle(i), sa = a + step / 2;
        ctx.strokeStyle = 'rgba(201,163,91,.28)'; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(CX + Math.cos(sa) * (KB.r0 + 3), CY + Math.sin(sa) * (KB.r0 + 3)); ctx.lineTo(CX + Math.cos(sa) * (KB.r0 + 9), CY + Math.sin(sa) * (KB.r0 + 9)); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(CX + Math.cos(sa) * (KB.r1 - 3), CY + Math.sin(sa) * (KB.r1 - 3)); ctx.lineTo(CX + Math.cos(sa) * (KB.r1 - 9), CY + Math.sin(sa) * (KB.r1 - 9)); ctx.stroke();
        let s = 1, col = C.cream;
        if (act && !act.cancel) {
          let d = Math.abs(i - act.idx); d = Math.min(d, n - d);
          s = 1 + 0.32 * Math.exp(-d * d / 1.2);
          if (d === 0) col = '#1b1206';
        }
        const r = KB.R + (1 - ringIn) * 10;
        ctx.globalAlpha = ringIn;
        ctx.font = `500 ${Math.round(21 * s)}px ${FM}`;
        const x = CX + Math.cos(a) * r, y = CY + Math.sin(a) * r;
        if (col === C.cream) { ctx.fillStyle = 'rgba(0,0,0,.6)'; ctx.fillText(chars[i], x, y + 1.2); }
        ctx.fillStyle = col; ctx.fillText(chars[i], x, y);
      }
      ctx.restore();

      // centre: network / back
      const bb = b.back;
      key(ctx, bb, { pressed: k.press === 'back', radius: 22 });
      text(ctx, '\u2039', bb.x + 22, bb.y + 29, `400 22px ${FS}`, C.brass);
      text(ctx, S.ssid, bb.x + bb.w / 2 + 8, bb.y + 27, `500 14px ${FS}`, C.ink);
      tracked(ctx, 'PASSWORD', CX, 76, `600 9px ${FS}`, C.faint, 2.6);

      // field
      const sh = T - k.shakeT < 380 ? Math.sin((T - k.shakeT) / 22) * 7 * (1 - (T - k.shakeT) / 380) : 0;
      const f = b.field;
      ctx.save(); ctx.translate(sh, 0);
      rrect(ctx, f.x, f.y, f.w, f.h, 12); ctx.fillStyle = '#080706'; ctx.fill();
      ctx.strokeStyle = sh ? C.red : 'rgba(201,163,91,.55)'; ctx.lineWidth = 1.2; ctx.stroke();
      const len = k.text.length;
      let shown;
      if (k.reveal) shown = k.text;
      else shown = '\u2022'.repeat(Math.max(0, len - 1)) + (len ? (T - k.lastT < 900 ? k.text[len - 1] : '\u2022') : '');
      const maxC = 14;
      const clipped = shown.length > maxC;
      if (clipped) shown = shown.slice(-maxC + 1);
      ctx.font = `400 21px ${FM}`;
      const tw = ctx.measureText(shown).width;
      const tx = clipped ? f.x + f.w - 18 - tw : CX - tw / 2 + 0;
      if (!len) text(ctx, 'password', f.x + f.w / 2, f.y + 32, `italic 400 15px ${FS}`, C.faint);
      else {
        ctx.save(); rrect(ctx, f.x + 4, f.y, f.w - 8, f.h, 10); ctx.clip();
        text(ctx, shown, tx, f.y + 34, `400 21px ${FM}`, C.ink, 'left');
        if (clipped) { const fg = ctx.createLinearGradient(f.x, 0, f.x + 34, 0); fg.addColorStop(0, '#080706'); fg.addColorStop(1, 'rgba(8,7,6,0)'); ctx.fillStyle = fg; ctx.fillRect(f.x, f.y, 34, f.h); }
        ctx.restore();
      }
      const caretX = len ? tx + tw + 2 : f.x + f.w / 2 + 38;
      if (Math.floor(T / 530) % 2 === 0 || T - k.lastT < 600) { ctx.fillStyle = C.brass; ctx.fillRect(caretX, f.y + 15, 1.6, 23); }
      ctx.restore();
      key(ctx, b.eye, { pressed: k.press === 'eye', on: k.reveal });
      eyeIcon(ctx, b.eye.x + 22, b.eye.y + 22, k.reveal, k.reveal ? C.brassHi : C.dim);

      // keys
      const sk = b.shift;
      key(ctx, sk, { pressed: k.press === 'shift', on: k.set === 'abc' && k.shift > 0, radius: 14 });
      if (k.set === 'abc') shiftIcon(ctx, sk.x + 27, sk.y + 23, k.shift, k.shift ? C.brassHi : C.ink);
      else text(ctx, k.set === 'num' ? '#+=' : '123', sk.x + 27, sk.y + 29, `500 14px ${FM}`, C.ink);
      key(ctx, b.mode, { pressed: k.press === 'mode', radius: 14 });
      text(ctx, k.set === 'abc' ? '123' : 'abc', b.mode.x + 27, b.mode.y + 29, `500 14px ${FM}`, C.ink);
      key(ctx, b.space, { pressed: k.press === 'space', radius: 14 });
      ctx.save(); ctx.strokeStyle = C.ink; ctx.lineWidth = 1.6; ctx.beginPath();
      ctx.moveTo(b.space.x + 16, b.space.y + 24); ctx.lineTo(b.space.x + 16, b.space.y + 30); ctx.lineTo(b.space.x + 38, b.space.y + 30); ctx.lineTo(b.space.x + 38, b.space.y + 24); ctx.stroke(); ctx.restore();
      key(ctx, b.del, { pressed: k.press === 'del', radius: 14 });
      delIcon(ctx, b.del.x + 27, b.del.y + 24, C.ink);
      key(ctx, b.done, { pressed: k.press === 'done', primary: true });
      text(ctx, 'Join', b.done.x + b.done.w / 2, b.done.y + 32, `700 22px ${FD}`, '#1f1606');

      // set + count hint
      const setName = k.set === 'abc' ? (k.shift === 2 ? 'ABC LOCK' : k.shift ? 'ABC' : 'abc') : k.set === 'num' ? '123' : '#+=';
      text(ctx, len ? `${len} character${len === 1 ? '' : 's'} \u00b7 ${setName}` : `tap or drag the rim \u00b7 ${setName}`, CX, 348, `400 11px ${FM}`, C.faint);

      // loupe
      if (act) {
        const a = act.cancel ? act.ang : kbAngle(act.idx), lk = easeOut((T - act.t0) / 140);
        const lx = CX + Math.cos(a) * KB.loupeR, ly = CY + Math.sin(a) * KB.loupeR, LR = 40 * (0.6 + 0.4 * lk);
        ctx.save();
        ctx.globalAlpha = lk;
        // pointer tail toward the finger
        ctx.fillStyle = '#8d6c34'; ctx.beginPath();
        const ta = a, px = CX + Math.cos(ta) * (KB.loupeR + LR + 14), py = CY + Math.sin(ta) * (KB.loupeR + LR + 14);
        ctx.moveTo(px, py);
        ctx.lineTo(lx + Math.cos(ta + 0.5) * (LR + 2), ly + Math.sin(ta + 0.5) * (LR + 2));
        ctx.lineTo(lx + Math.cos(ta - 0.5) * (LR + 2), ly + Math.sin(ta - 0.5) * (LR + 2)); ctx.closePath(); ctx.fill();
        ctx.shadowColor = 'rgba(0,0,0,.8)'; ctx.shadowBlur = 18;
        const bz = ctx.createLinearGradient(lx - LR, ly - LR, lx + LR, ly + LR);
        bz.addColorStop(0, '#f0d595'); bz.addColorStop(0.45, '#a7833f'); bz.addColorStop(1, '#3f2d14');
        ctx.fillStyle = bz; ctx.beginPath(); ctx.arc(lx, ly, LR + 6, 0, TAU); ctx.fill();
        ctx.shadowBlur = 0;
        const gl = ctx.createRadialGradient(lx - LR * 0.3, ly - LR * 0.4, 2, lx, ly, LR);
        gl.addColorStop(0, '#2a2418'); gl.addColorStop(1, '#050403');
        ctx.fillStyle = gl; ctx.beginPath(); ctx.arc(lx, ly, LR, 0, TAU); ctx.fill();
        ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        if (act.cancel) {
          ctx.font = `400 30px ${FS}`; ctx.fillStyle = C.red; ctx.fillText('\u2715', lx, ly - 4);
          ctx.font = `500 9px ${FS}`; ctx.fillStyle = C.dim; ctx.fillText('CANCEL', lx, ly + 22);
        } else {
          const pc = chars[(act.idx - 1 + n) % n], nc = chars[(act.idx + 1) % n];
          ctx.font = `400 14px ${FM}`; ctx.fillStyle = 'rgba(243,230,198,.38)';
          ctx.fillText(pc, lx - 27, ly + 2); ctx.fillText(nc, lx + 27, ly + 2);
          ctx.font = `500 42px ${FM}`; ctx.fillStyle = '#fff3d6';
          ctx.fillText(chars[act.idx] === ' ' ? '\u2423' : chars[act.idx], lx, ly + 1);
        }
        // glass glint
        ctx.fillStyle = 'rgba(255,255,255,.07)'; ctx.beginPath(); ctx.ellipse(lx - LR * 0.25, ly - LR * 0.45, LR * 0.6, LR * 0.28, -0.3, 0, TAU); ctx.fill();
        ctx.restore();
      }
      // just-typed flash: the character flies from the ring into the field
      if (k.flashT && T - k.flashT < 260 && k.flashA != null) {
        const p = easeOut((T - k.flashT) / 260);
        const sx = CX + Math.cos(k.flashA) * KB.loupeR, sy = CY + Math.sin(k.flashA) * KB.loupeR;
        ctx.globalAlpha = 1 - p;
        text(ctx, k.reveal ? k.flashCh : k.flashCh, sx + (CX - sx) * p, sy + (b.field.y + 30 - sy) * p, `500 ${Math.round(40 - 18 * p)}px ${FM}`, '#fff3d6', 'center', 'middle');
        ctx.globalAlpha = 1;
      }
    },
    pointer(type, x, y, env) {
      const k = S.kb, r = dist(x, y);
      if (type === 'down') {
        k.downX = x; k.downY = y;
        for (const [name, bx] of Object.entries(KB.btn)) {
          if (name === 'field') continue;
          if (inRect(bx, x, y)) { k.press = name; k.pressT = now(); k.repT = now(); return; }
        }
        if (r >= 150) k.active = { idx: kbIndexAt(x, y), t0: now(), cancel: false, ang: angle(x, y) };
        return;
      }
      if (type === 'move') {
        if (k.press) { if (!inRect(KB.btn[k.press], x, y)) k.press = null; return; }
        if (k.active) {
          k.active.ang = angle(x, y);
          k.active.cancel = r < KB.r0 - 40;
          if (!k.active.cancel) k.active.idx = kbIndexAt(x, y);
        }
        return;
      }
      if (type === 'up') {
        if (k.press) {
          const p = k.press; k.press = null;
          if (inRect(KB.btn[p], x, y) && !(p === 'del' && now() - k.pressT > 450)) kbButton(p, env);
          return;
        }
        if (k.active) {
          const a = k.active; k.active = null;
          if (!a.cancel) { const ch = kbChars()[a.idx]; kbType(ch); k.flashA = kbAngle(a.idx); }
        }
      }
    },
  });

  /* ------------------------------------------------------------------ thumbnails (nav-map, settings) */
  const PLANNED = [
    { id: 'omni', title: 'Omni', group: 'Omni', glyph: 'omni' },
    { id: 'live-globe', title: 'Live globe', group: 'Faces', glyph: 'globe' },
    { id: 'spin-globe', title: 'Spinning globe', group: 'Faces', glyph: 'spin' },
    { id: 'globe-hands', title: 'Globe + hands', group: 'Faces', glyph: 'hands' },
    { id: 'digital', title: 'Digital', group: 'Faces', glyph: 'digital' },
    { id: 'settings', title: 'Settings', group: 'Setup', glyph: 'settings' },
    { id: 'wifi-qr', title: 'Wi-Fi setup', group: 'Setup', glyph: 'wifi' },
  ];
  function glyphFor(f) {
    const s = (f.id + ' ' + f.title).toLowerCase();
    if (/omni/.test(s)) return 'omni';
    if (/wifi|wi-fi|qr/.test(s)) return 'wifi';
    if (/setting/.test(s)) return 'settings';
    if (/keyboard/.test(s)) return 'keys';
    if (/hand|analog/.test(s)) return 'hands';
    if (/spin/.test(s)) return 'spin';
    if (/globe|earth|world|map|night/.test(s)) return 'globe';
    if (/digit/.test(s)) return 'digital';
    return 'initial';
  }
  function miniGlobe(ctx, x, y, r, env, lonOff) {
    ctx.save(); ctx.beginPath(); ctx.arc(x, y, r, 0, TAU); ctx.clip();
    const img = env && env.assets && env.assets.earthDay;
    if (img) {
      const lon = (env.location ? env.location.lon : -83) + (lonOff || 0);
      const span = img.width * 0.3, sc = (r * 2) / span, cym = img.height * 0.36;
      const cxm = ((((lon + 180) / 360) % 1) + 1) % 1 * img.width;
      const dx = x - cxm * sc, dy = y - cym * sc;          // map drawn twice side by side so it wraps
      ctx.drawImage(img, dx, dy, img.width * sc, img.height * sc);
      ctx.drawImage(img, dx + (cxm < img.width / 2 ? -1 : 1) * img.width * sc, dy, img.width * sc, img.height * sc);
    } else {
      const g = ctx.createLinearGradient(x - r, y - r, x + r, y + r); g.addColorStop(0, '#2f6fa8'); g.addColorStop(1, '#123050');
      ctx.fillStyle = g; ctx.fillRect(x - r, y - r, r * 2, r * 2);
    }
    const sh = ctx.createRadialGradient(x - r * 0.35, y - r * 0.4, r * 0.1, x, y, r);
    sh.addColorStop(0, 'rgba(255,255,255,.18)'); sh.addColorStop(0.6, 'rgba(0,0,0,0)'); sh.addColorStop(1, 'rgba(0,0,0,.75)');
    ctx.fillStyle = sh; ctx.fillRect(x - r, y - r, r * 2, r * 2);
    ctx.restore();
  }
  function drawThumb(ctx, item, x, y, r, env, hi) {
    ctx.save();
    ctx.fillStyle = '#060504'; ctx.beginPath(); ctx.arc(x, y, r, 0, TAU); ctx.fill();
    const g = item.glyph, T = now();
    if (g === 'omni') {
      miniGlobe(ctx, x, y, r * 0.5, env);
      ctx.strokeStyle = '#b8924c'; ctx.lineWidth = r * 0.22; ctx.beginPath(); ctx.arc(x, y, r * 0.72, 0, TAU); ctx.stroke();
      ctx.strokeStyle = 'rgba(0,0,0,.5)'; ctx.lineWidth = 0.8;
      for (let i = 0; i < 24; i++) { const a = i / 24 * TAU; ctx.beginPath(); ctx.moveTo(x + Math.cos(a) * r * 0.62, y + Math.sin(a) * r * 0.62); ctx.lineTo(x + Math.cos(a) * r * 0.8, y + Math.sin(a) * r * 0.8); ctx.stroke(); }
      ctx.fillStyle = C.red; ctx.beginPath(); ctx.moveTo(x - r * 0.12, y + r * 0.62); ctx.lineTo(x, y + r * 0.86); ctx.lineTo(x + r * 0.12, y + r * 0.62); ctx.fill();
    } else if (g === 'globe' || g === 'spin' || g === 'hands') {
      miniGlobe(ctx, x, y, r * 0.78, env, g === 'spin' ? (T / 30) % 360 : 0);
      if (g === 'spin') {
        ctx.strokeStyle = 'rgba(240,213,149,.7)'; ctx.lineWidth = 1.5; ctx.lineCap = 'round';
        ctx.beginPath(); ctx.arc(x, y, r * 0.9, -2.6, -1.6); ctx.stroke();
        ctx.beginPath(); ctx.arc(x, y, r * 0.9, 0.55, 1.55); ctx.stroke();
      }
      if (g === 'hands') {
        ctx.strokeStyle = '#f3e6c6'; ctx.lineCap = 'round';
        ctx.lineWidth = 2.4; ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + r * 0.32, y - r * 0.28); ctx.stroke();
        ctx.lineWidth = 1.6; ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x - r * 0.1, y - r * 0.66); ctx.stroke();
        ctx.fillStyle = C.brass; ctx.beginPath(); ctx.arc(x, y, 2.4, 0, TAU); ctx.fill();
      }
    } else if (g === 'digital') {
      const d = env.now();
      text(ctx, String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0'), x, y + r * 0.16, `500 ${Math.round(r * 0.5)}px ${FM}`, C.cream);
      ctx.fillStyle = C.brass; ctx.fillRect(x - r * 0.3, y + r * 0.32, r * 0.6, 1.2);
    } else if (g === 'settings') {
      ctx.fillStyle = C.brass; ctx.beginPath();
      for (let i = 0; i < 16; i++) { const a = i / 16 * TAU, rr2 = i % 2 ? r * 0.42 : r * 0.56; ctx.lineTo(x + Math.cos(a) * rr2, y + Math.sin(a) * rr2); }
      ctx.closePath(); ctx.fill(); ctx.fillStyle = '#060504'; ctx.beginPath(); ctx.arc(x, y, r * 0.2, 0, TAU); ctx.fill();
    } else if (g === 'wifi') {
      ctx.fillStyle = '#f6f3ec'; ctx.fillRect(x - r * 0.5, y - r * 0.5, r, r);
      ctx.fillStyle = '#000'; const u = r / 9;
      [[-0.5, -0.5], [0.5 - 3 * u / r, -0.5], [-0.5, 0.5 - 3 * u / r]].forEach(([dx, dy]) => {
        ctx.fillRect(x + dx * r + u * 0.5, y + dy * r + u * 0.5, u * 2.5, u * 2.5); ctx.fillStyle = '#f6f3ec';
        ctx.fillRect(x + dx * r + u, y + dy * r + u, u * 1.5, u * 1.5); ctx.fillStyle = '#000';
        ctx.fillRect(x + dx * r + u * 1.35, y + dy * r + u * 1.35, u * 0.8, u * 0.8);
      });
      const rnd = mulberry(4);
      for (let i = 0; i < 14; i++) ctx.fillRect(x + (rnd() * 0.5) * r - u * 0.1, y + (rnd() * 0.5) * r - u * 0.1, u * 0.9, u * 0.9);
    } else if (g === 'keys') {
      ctx.fillStyle = C.cream; ctx.font = `500 ${Math.round(r * 0.22)}px ${FM}`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      'abcdefghij'.split('').forEach((ch, i) => { const a = -Math.PI / 2 + i / 10 * TAU; ctx.fillText(ch, x + Math.cos(a) * r * 0.66, y + Math.sin(a) * r * 0.66); });
    } else {
      text(ctx, (item.title || '?').slice(0, 2), x, y + r * 0.18, `700 ${Math.round(r * 0.6)}px ${FD}`, C.brass);
    }
    ctx.lineWidth = hi ? 2.5 : 1.5; ctx.strokeStyle = hi ? C.brassHi : 'rgba(201,163,91,.65)';
    ctx.beginPath(); ctx.arc(x, y, r, 0, TAU); ctx.stroke();
    ctx.restore();
  }

  /* ------------------------------------------------------------------ 3. nav-map */
  const NAV_SKIP = new Set(['nav-map', 'rim-keyboard', 'netlist', 'faces-edit']);   // tools, not faces: off the ring
  const NAV = { items: [], rot: 0, target: 0, vel: 0, drag: null, R: 182, tr: 27, open: { x: 173, y: 238, w: 120, h: 44 }, flashT: -1e9, msg: '' };
  function navSel() {   // index of the item under the 12 o'clock window
    const n = NAV.items.length, step = TAU / n;
    return ((Math.round(-NAV.rot / step) % n) + n) % n;
  }
  function navBezel() {
    return cached('navBezel', g => {
      brassBand(g, 146, 233, { seed: 21, dark: 0.35 });
      g.strokeStyle = 'rgba(0,0,0,.6)'; g.lineWidth = 1.2;
      g.beginPath(); g.arc(CX, CY, 222, 0, TAU); g.stroke();
      g.strokeStyle = 'rgba(255,232,180,.18)'; g.beginPath(); g.arc(CX, CY, 220.8, 0, TAU); g.stroke();
    });
  }
  /* the magnifier window at 12 \u2014 the Omni's date window \u2014 with the red V index under it (the picker's) */
  function magWindow(ctx) {
    ctx.save();
    const wx = CX - 41, wy = 6, ww = 82, wh = 78;
    rrect(ctx, wx - 6, wy - 6, ww + 12, wh + 12, 14); rrect(ctx, wx, wy, ww, wh, 10, true);
    const wg = ctx.createLinearGradient(wx, wy, wx + ww, wy + wh);
    wg.addColorStop(0, '#f2d797'); wg.addColorStop(0.5, '#a07b38'); wg.addColorStop(1, '#4a3518');
    ctx.fillStyle = wg; ctx.fill('evenodd');
    ctx.strokeStyle = 'rgba(0,0,0,.7)'; ctx.lineWidth = 1; rrect(ctx, wx, wy, ww, wh, 10); ctx.stroke();
    const gl = ctx.createLinearGradient(wx, wy, wx, wy + wh);
    gl.addColorStop(0, 'rgba(255,255,255,.14)'); gl.addColorStop(0.4, 'rgba(255,255,255,0)');
    ctx.fillStyle = gl; rrect(ctx, wx, wy, ww, wh, 10); ctx.fill();
    // red V index under the window
    ctx.fillStyle = C.red; ctx.beginPath(); ctx.moveTo(CX - 7, wy + wh + 8); ctx.lineTo(CX + 7, wy + wh + 8); ctx.lineTo(CX, wy + wh + 17); ctx.closePath(); ctx.fill();
    ctx.restore();
  }
  /* the picker's dark centre disc inside the bezel */
  function centreDisc(ctx) {
    const cg = ctx.createRadialGradient(CX, CY, 60, CX, CY, 146);
    cg.addColorStop(0, '#000'); cg.addColorStop(1, '#0c0a07');
    ctx.fillStyle = cg; ctx.beginPath(); ctx.arc(CX, CY, 145, 0, TAU); ctx.fill();
  }

  const NAV_NOTES = `
<p><b>The model in one line:</b> faces live on a ring; you <b>turn the bezel</b> to choose one, like setting the Omni's rings,
and the face sitting under the magnifier window at 12 o'clock is the one you get. The screen on the left is that selector \u2014 drag
round the brass band (it has inertia and detents), tap a thumbnail to bring it round, tap <b>Open</b> to go there. The list is
built live from every face loaded in this review page.</p>
<svg viewBox="0 0 600 300" width="100%" role="img" aria-label="Navigation map" style="max-width:600px;font-family:var(--f-body);font-size:11px">
 <defs><marker id="nvA" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0L8 4L0 8z" fill="var(--brass)"/></marker></defs>
 <g fill="none" stroke="var(--rule)">
  <rect x="200" y="100" width="160" height="80" rx="40" stroke="var(--brass)"/>
  <rect x="200" y="10" width="160" height="44" rx="10"/><rect x="200" y="236" width="160" height="44" rx="10"/>
  <rect x="10" y="118" width="140" height="44" rx="10"/><rect x="410" y="104" width="140" height="72" rx="10"/>
  <rect x="410" y="214" width="140" height="66" rx="10"/><rect x="420" y="10" width="140" height="44" rx="10"/>
 </g>
 <g fill="var(--ink)" text-anchor="middle">
  <text x="280" y="134" font-weight="600">Current face</text><text x="280" y="152" fill="var(--ink-dim)">(ring of faces)</text>
  <text x="280" y="36">Status sheet</text><text x="280" y="262">Settings</text>
  <text x="80" y="144">Dark (sleep)</text><text x="480" y="128">Face selector</text><text x="480" y="146" fill="var(--ink-dim)">bezel + window</text>
  <text x="480" y="242">Setup</text><text x="480" y="260" fill="var(--ink-dim)">QR \u00b7 rim keyboard</text>
  <text x="490" y="36">First boot</text>
 </g>
 <g stroke="var(--brass)" fill="none" marker-end="url(#nvA)" stroke-width="1.3">
  <path d="M280 100V58"/><path d="M290 54V96" stroke-dasharray="3 3"/>
  <path d="M280 180V232"/><path d="M290 236V184" stroke-dasharray="3 3"/>
  <path d="M200 132H154"/><path d="M150 148H196"/>
  <path d="M360 130H406"/><path d="M406 150H364" stroke-dasharray="3 3"/>
  <path d="M360 256H406"/><path d="M560 32H584V247H556"/>
 </g>
 <g fill="var(--ink-dim)" text-anchor="middle" font-size="10">
  <text x="244" y="80">swipe down</text><text x="330" y="80">swipe up / BOOT</text>
  <text x="246" y="210">long-press</text><text x="330" y="210">BOOT</text>
  <text x="176" y="126">PWR \u00b7 timeout</text><text x="176" y="164">lift \u00b7 PWR</text>
  <text x="384" y="124">rim drag</text><text x="384" y="166">release</text>
  <text x="384" y="250">Wi-Fi row</text><text x="597" y="140" transform="rotate(-90 597 140)">no network</text>
 </g>
</svg>
<ul>
<li><b>Next / previous face:</b> horizontal swipe across the centre (quick, one step). <b>Turn the bezel:</b> touch within ~40&nbsp;px of the edge
and slide along it \u2014 the face shrinks to the centre, the ring of faces appears and follows the finger with detents (and a soft speaker tick);
release to land on the face in the window. Faces that use the rim themselves (keyboard, settings) suspend this.</li>
<li><b>Settings:</b> long-press (600&nbsp;ms) anywhere on a face. <b>Status sheet</b> (battery %, Wi-Fi, "time synced 2&nbsp;h ago"): swipe down from the top.</li>
<li><b>BOOT</b> (GPIO0, the "activator"): short = <i>back</i> in menus, <i>home to the default face</i> on faces; double = replay the reveal; long (1.5&nbsp;s) = settings.
(BOOT only means download mode if held during reset \u2014 harmless in use.)</li>
<li><b>PWR</b> (AXP2101 PWRON): short = "close the lid" (screen off now) / wake; ~2&nbsp;s = power menu (restart, off); the AXP2101 hard-off at ~6&nbsp;s stays as the escape hatch.</li>
<li><b>Lift-to-wake</b> (QMI8658: a rise + rotation to face-up within ~0.6&nbsp;s): <i>Omni</i> \u2192 full reveal (iris or flip) then globe spin-up;
<i>globe faces</i> \u2192 short 300&nbsp;ms iris, globe already in place; <i>digital</i> \u2192 instant. Re-lifted within 10&nbsp;s of sleeping \u2192
no reveal at all (no one wants the show twice in a row). Setup screens resume exactly where they were.</li>
<li><b>Sleep:</b> 15&nbsp;s after the last touch by default (settable 5&nbsp;s\u20131&nbsp;min); 5&nbsp;s if it's put face-down or goes vertical and still (pocket/chain);
setup screens 3&nbsp;min and kept awake while the phone is connected; keyboard 60&nbsp;s. Fade, don't cut.</li>
</ul>
<p><b>Why a bezel?</b> It's the Omni gesture \u2014 rings you turn until the marks line up \u2014 and on a round screen the rim is the one
place a finger doesn't hide what you're looking at. Swipes stay for speed; the bezel gives an overview and a direct jump.
Open question: with only 5\u20136 faces, is the selector worth it, or is swipe + long-press "choose default" enough? I'd ship both \u2014 the
bezel costs one gesture and gives the device its character.</p>`;

  OMNI_FACES.push({
    id: 'nav-map', group: 'Setup', title: 'Navigation: turn the bezel',
    summary: 'Proposed navigation model; the canvas is a working face selector \u2014 drag the bezel, faces land in the window.',
    notes: NAV_NOTES,
    init(env) {
      const live = (window.OMNI_FACES || []).filter(f => !NAV_SKIP.has(f.id));
      const hasOthers = live.some(f => f.group !== 'Setup');
      NAV.items = hasOthers ? live.map(f => ({ id: f.id, title: f.title, group: f.group, glyph: glyphFor(f), live: true }))
        : PLANNED.map(p => Object.assign({}, p, { live: live.some(f => f.id === p.id) }));
    },
    wake() { NAV.openT = now(); },
    frame(ctx, t, env) {
      const T = now(), items = NAV.items, n = items.length, step = TAU / n;
      if (!NAV.drag) {                                      // spring toward detent
        const d = NAV.target - NAV.rot; NAV.rot += d * 0.18; if (Math.abs(d) < 0.0005) NAV.rot = NAV.target;
      }
      blit(ctx, navBezel());
      // rotating engraved scale on the bezel
      ctx.save();
      for (let i = 0; i < n * 8; i++) {
        const a = -Math.PI / 2 + NAV.rot + i * step / 8, major = i % 8 === 0, mid = i % 4 === 0;
        ctx.strokeStyle = major ? 'rgba(30,18,4,.85)' : 'rgba(30,18,4,.55)'; ctx.lineWidth = major ? 1.6 : 1;
        const r0 = 216, r1 = major ? 226 : mid ? 223 : 220;
        ctx.beginPath(); ctx.moveTo(CX + Math.cos(a) * r0, CY + Math.sin(a) * r0); ctx.lineTo(CX + Math.cos(a) * r1, CY + Math.sin(a) * r1); ctx.stroke();
      }
      ctx.restore();
      const sel = navSel();
      // thumbnails
      for (let i = 0; i < n; i++) {
        const a = -Math.PI / 2 + NAV.rot + i * step;
        const near = Math.max(0, 1 - Math.abs(wrap(a + Math.PI / 2)) / (step * 0.8));
        const r = NAV.tr * (1 + 0.18 * smooth(near));
        drawThumb(ctx, items[i], CX + Math.cos(a) * NAV.R, CY + Math.sin(a) * NAV.R, r, env, i === sel && !NAV.drag);
        if (!items[i].live) {     // planned but not in this build: hollow tag
          ctx.fillStyle = 'rgba(0,0,0,.45)'; ctx.beginPath(); ctx.arc(CX + Math.cos(a) * NAV.R, CY + Math.sin(a) * NAV.R, r, 0, TAU); ctx.fill();
        }
      }
      magWindow(ctx);

      // centre
      ctx.save();
      centreDisc(ctx);
      const it = items[sel];
      const chT = NAV.selT && NAV.lastSel === sel ? smooth((T - NAV.selT) / 220) : 1;
      if (NAV.lastSel !== sel) { NAV.lastSel = sel; NAV.selT = T; }
      ctx.globalAlpha = chT;
      tracked(ctx, (it.group || '').toUpperCase(), CX, 150, `600 10px ${FS}`, C.brass, 3);
      text(ctx, it.title, CX, 194, `700 ${it.title.length > 18 ? 22 : it.title.length > 13 ? 26 : 32}px ${FD}`, '#eadcbc');
      ctx.globalAlpha = 1;
      const ob = NAV.open;
      key(ctx, ob, { pressed: NAV.pressOpen, primary: it.live });
      text(ctx, it.live ? 'Open' : 'Planned', CX, ob.y + 29, `${it.live ? 700 : 500} ${it.live ? 20 : 15}px ${it.live ? FD : FS}`, it.live ? '#1f1606' : C.dim);
      const lines = [['\u21bb', 'turn the bezel \u00b7 choose'], ['\u21c6', 'swipe centre \u00b7 next / prev'], ['\u25c9', 'hold \u00b7 settings']];
      lines.forEach(([ic, s], i) => {
        text(ctx, ic, CX - 86, 314 + i * 18, `500 12px ${FS}`, C.brass, 'center');
        text(ctx, s, CX - 74, 314 + i * 18, `400 11.5px ${FS}`, C.dim, 'left');
      });
      ctx.restore();
    },
    pointer(type, x, y, env) {
      const r = dist(x, y), n = NAV.items.length, step = TAU / n;
      if (type === 'down') {
        NAV.downX = x; NAV.downY = y; NAV.downT = now();
        if (inRect(NAV.open, x, y)) { NAV.pressOpen = true; return; }
        if (r > 146) NAV.drag = { a: angle(x, y), lastA: angle(x, y), lastT: now(), moved: 0 };
        return;
      }
      if (type === 'move') {
        if (NAV.pressOpen && !inRect(NAV.open, x, y)) NAV.pressOpen = false;
        if (NAV.drag) {
          const a = angle(x, y), d = wrap(a - NAV.drag.lastA), T = now();
          NAV.rot += d; NAV.drag.moved += Math.abs(d);
          NAV.vel = 0.7 * NAV.vel + 0.3 * (d / Math.max(1, T - NAV.drag.lastT));
          NAV.drag.lastA = a; NAV.drag.lastT = T;
        }
        return;
      }
      if (type === 'up') {
        if (NAV.pressOpen) {
          NAV.pressOpen = false;
          if (inRect(NAV.open, x, y)) { const it = NAV.items[navSel()]; if (it.live) env.goto(it.id); }
          return;
        }
        if (NAV.drag) {
          const moved = NAV.drag.moved; NAV.drag = null;
          if (moved < 0.04) {                       // a tap on a thumbnail brings it round to the window
            let best = -1, bd = 1e9;
            for (let i = 0; i < n; i++) {
              const a = -Math.PI / 2 + NAV.rot + i * step, d = Math.hypot(x - (CX + Math.cos(a) * NAV.R), y - (CY + Math.sin(a) * NAV.R));
              if (d < bd) { bd = d; best = i; }
            }
            if (bd < NAV.tr + 14) {
              const cur = NAV.rot, want = -best * step, delta = wrap(want - cur);
              NAV.target = cur + delta; NAV.rot = cur; return;
            }
          }
          const proj = NAV.rot + clamp(NAV.vel * 160, -step * 2.5, step * 2.5);
          NAV.target = Math.round(proj / step) * step; NAV.vel = 0;
        }
      }
    },
  });

  /* ------------------------------------------------------------------ 4. settings */
  const SET_ROWS = [
    { key: 'brightness', label: 'Brightness', type: 'level' },
    { key: 'sleep', label: 'Sleep after', opts: ['5 s', '10 s', '15 s', '30 s', '1 min'], short: ['5S', '10S', '15S', '30S', '1M'] },
    { key: 'face', label: 'Default face', opts: ['Omni', 'Live globe', 'Spin globe', 'Globe + hands', 'Digital'], short: ['OMNI', 'LIVE', 'SPIN', 'HANDS', 'DIGIT'] },
    { key: 'reveal', label: 'Reveal', opts: ['Brass iris', 'Lid flip', 'Instant'], short: ['IRIS', 'FLIP', 'NONE'] },
    { key: 'sound', label: 'Sound', opts: ['On', 'Off'], short: ['ON', 'OFF'] },
    { key: 'tz', label: 'Time zone', opts: ['Auto', 'New York', 'London', 'Tokyo', 'Sydney'], short: ['AUTO', 'NYC', 'LON', 'TYO', 'SYD'] },
    { key: 'wifi', label: 'Wi-Fi', type: 'link', to: 'wifi-qr' },
    { key: 'about', label: 'About', type: 'info' },
  ];
  const SETV = { s: 0, target: 0, drag: null, rowH: 54, rim: null, flashT: -1e9 };
  const ENUM_SPAN = 2.0;   // radians of top arc used by option labels
  function optAngle(i, n) { return -Math.PI / 2 + (n === 1 ? 0 : (i / (n - 1) - 0.5) * ENUM_SPAN); }
  const LEVEL_A0 = Math.PI * 0.75, LEVEL_SWEEP = Math.PI * 1.5;     // 7:30 \u2192 4:30 through 12
  function rowValue(row) {
    const st = S.settings;
    if (row.type === 'level') return Math.round(st.brightness * 100) + ' %';
    if (row.type === 'link') return S.ssid + ' \u203a';
    if (row.type === 'info') return 'v0.1 \u00b7 82 %';
    return row.opts[st[row.key]];
  }
  function setRimAt(row, x, y) {
    const st = S.settings, a = angle(x, y);
    if (row.type === 'level') {
      let u = a - LEVEL_A0; u = ((u % TAU) + TAU) % TAU;
      if (u > LEVEL_SWEEP) u = u > LEVEL_SWEEP + (TAU - LEVEL_SWEEP) / 2 ? 0 : LEVEL_SWEEP;
      st.brightness = clamp(Math.round((0.05 + 0.95 * u / LEVEL_SWEEP) * 20) / 20, 0.05, 1);
    } else if (row.opts) {
      let best = 0, bd = 1e9;
      row.opts.forEach((_, i) => { const d = Math.abs(wrap(a - optAngle(i, row.opts.length))); if (d < bd) { bd = d; best = i; } });
      if (st[row.key] !== best) { st[row.key] = best; SETV.flashT = now(); }
    }
  }
  /* the level scale on the rim: dark track, brass fill to v (0..1; null = track only), engraved ticks.
     Leaves lineCap round on ctx (the callers' end glyphs rely on it). Returns the fill's end angle. */
  function levelScale(ctx, v) {
    ctx.lineCap = 'round';
    ctx.strokeStyle = 'rgba(0,0,0,.6)'; ctx.lineWidth = 10;
    ctx.beginPath(); ctx.arc(CX, CY, 196, LEVEL_A0, LEVEL_A0 + LEVEL_SWEEP); ctx.stroke();
    const ae = LEVEL_A0 + LEVEL_SWEEP * (v == null ? 0 : v);
    if (v != null) {
      const lg = ctx.createLinearGradient(CX - 196, CY + 100, CX + 196, CY - 100);
      lg.addColorStop(0, '#6d5024'); lg.addColorStop(1, '#f0d595');
      ctx.strokeStyle = lg; ctx.lineWidth = 6;
      ctx.beginPath(); ctx.arc(CX, CY, 196, LEVEL_A0, Math.max(LEVEL_A0 + 0.001, ae)); ctx.stroke();
    }
    for (let i = 0; i <= 20; i++) {
      const a = LEVEL_A0 + LEVEL_SWEEP * i / 20;
      ctx.strokeStyle = 'rgba(201,163,91,.45)'; ctx.lineWidth = i % 5 ? 1 : 1.6;
      ctx.beginPath(); ctx.moveTo(CX + Math.cos(a) * 176, CY + Math.sin(a) * 176); ctx.lineTo(CX + Math.cos(a) * (i % 5 ? 181 : 185), CY + Math.sin(a) * (i % 5 ? 181 : 185)); ctx.stroke();
    }
    return ae;
  }
  /* the Settings drum: rows on a cylinder behind the brass window. D = { s, rowH }; win(i, y) draws the windowed
     row (alpha already set), dim(i, y, sc) a row above or below. */
  const SETWIN = { x: 76, y: CY - 28, w: 314, h: 56 };
  function drumGlass(ctx) {
    const { x: wx, y: wy, w: ww, h: wh } = SETWIN;
    ctx.save();
    rrect(ctx, wx, wy, ww, wh, 12); ctx.fillStyle = '#0d0b08'; ctx.fill();
    ctx.restore();
  }
  function drumRows(ctx, D, n, win, dim) {
    ctx.save();
    ctx.beginPath(); ctx.arc(CX, CY, 168, 0, TAU); ctx.clip();
    for (let i = 0; i < n; i++) {
      const d = i - D.s; if (Math.abs(d) > 2.6) continue;
      const y = CY + Math.sin(clamp(d / 3.2, -1, 1) * Math.PI / 2) * D.rowH * 2.6;
      const inWin = Math.max(0, 1 - Math.abs(d));
      const sc = 1 - Math.min(1, Math.abs(d)) * 0.22 - Math.max(0, Math.abs(d) - 1) * 0.1;
      ctx.globalAlpha = clamp(1 - Math.abs(d) * 0.33, 0, 1);
      if (inWin > 0.5) {
        ctx.globalAlpha = smooth((inWin - 0.5) * 2);
        win(i, y);
      } else dim(i, y, sc);
    }
    ctx.restore();
  }
  function drumFrame(ctx) {
    const { x: wx, y: wy, w: ww, h: wh } = SETWIN;
    ctx.save();
    rrect(ctx, wx - 4, wy - 4, ww + 8, wh + 8, 15); rrect(ctx, wx, wy, ww, wh, 12, true);
    const fg = ctx.createLinearGradient(wx, wy, wx + ww * 0.3, wy + wh * 1.6);
    fg.addColorStop(0, '#f2d797'); fg.addColorStop(0.5, '#a07b38'); fg.addColorStop(1, '#4a3518');
    ctx.fillStyle = fg; ctx.fill('evenodd');
    const gl = ctx.createLinearGradient(0, wy, 0, wy + wh);
    gl.addColorStop(0, 'rgba(255,255,255,.08)'); gl.addColorStop(0.45, 'rgba(255,255,255,0)');
    ctx.fillStyle = gl; rrect(ctx, wx, wy, ww, wh, 12); ctx.fill();
    ctx.restore();
  }
  function drumPips(ctx, n, cur) {
    for (let i = 0; i < n; i++) {
      ctx.fillStyle = i === cur ? C.brass : '#3a3229';
      ctx.beginPath(); ctx.arc(CX - (n - 1) * 5 + i * 10, 384, i === cur ? 3 : 2, 0, TAU); ctx.fill();
    }
  }
  /* drum touch: spring toward the target row, drag with a fling, tap the window or a row above / below */
  function drumSpring(D) {
    if (!D.drag) { const d = D.target - D.s; D.s += d * 0.2; if (Math.abs(d) < 0.001) D.s = D.target; }
  }
  function drumDown(D, y) { D.drag = { y0: y, s0: D.s, lastY: y, lastT: now(), v: 0, moved: 0 }; }
  function drumMove(D, y, n) {
    if (!D.drag) return;
    const T = now(), dy = y - D.drag.lastY;
    D.drag.moved = Math.max(D.drag.moved, Math.abs(y - D.drag.y0));
    D.s = clamp(D.drag.s0 - (y - D.drag.y0) / D.rowH, -0.4, n - 0.6);
    D.drag.v = 0.7 * D.drag.v + 0.3 * (-dy / D.rowH) / Math.max(1, T - D.drag.lastT);
    D.drag.lastY = y; D.drag.lastT = T;
  }
  function drumUp(D, y, n, onWindow) {   // onWindow(): a tap on the windowed row
    const dr = D.drag; D.drag = null;
    if (!dr) return;
    if (dr.moved < 8) {
      const off = (y - CY) / D.rowH;
      if (Math.abs(off) < 0.55) onWindow();
      else D.target = clamp(Math.round(D.s + Math.sign(off) * Math.max(1, Math.round(Math.abs(off) * 0.8))), 0, n - 1);
      return;
    }
    D.target = clamp(Math.round(D.s + clamp(dr.v * 180, -3, 3)), 0, n - 1);
  }
  function settingsBezel() {
    return cached('setBezel', g => {
      brassBand(g, 222, 233, { seed: 9 });
      brassBand(g, 170, 222, { seed: 13, dark: 0.72, speck: 1200 });
    });
  }

  OMNI_FACES.push({
    id: 'settings', group: 'Setup', title: 'Settings',
    summary: 'Drum list under a magnifier window; turn the rim to set the value of the row in the window.',
    notes: `
<p><b>Two controls, never ambiguous:</b> the <b>centre picks the row</b>, the <b>rim sets its value</b>. Drag the centre up/down
(or tap a row above/below) and the list rolls like a drum until a row sits in the brass window \u2014 the same window motif as the
Omni's date window and the face selector. The rim then becomes that row's dial: a continuous 270\u00b0 scale for brightness,
engraved detents for choices (the red index marks the current one). Tapping the windowed row also steps a choice, so one-handed use
in a pocket-grip works without the rim.</p>
<ul>
<li><b>Rows:</b> Brightness (5\u2013100&nbsp;%; this mockup actually dims the screen), Sleep after (5&nbsp;s / 10 / 15 / 30 / 1&nbsp;min),
Default face, Reveal (brass iris / lid flip / instant), Sound, Time zone (Auto from the provisioning phone or IP geolocation,
or a city), Wi-Fi (opens setup), About (firmware, battery).</li>
<li><b>Exit:</b> BOOT = back to the face; changes apply immediately (no Save button \u2014 undo by turning back).</li>
<li><b>Text</b> stays inside r\u2248200; rim labels at r=196 in 11&nbsp;px Plex Mono caps; window text 18&nbsp;px.</li>
</ul>
<p><b>Feasibility.</b> LVGL roller (<code>lv_roller</code>) restyled + an arc (<code>lv_arc</code>) on the rim; all values in NVS.
Open: time-zone list needs a picker with search for the whole tz database \u2014 that's the one place the rim keyboard would be reused.</p>`,
    frame(ctx, t, env) {
      const T = now(), rows = SET_ROWS, st = S.settings;
      drumSpring(SETV);
      const curI = clamp(Math.round(SETV.s), 0, rows.length - 1), row = rows[curI];
      blit(ctx, settingsBezel());

      // rim dial for the current row
      ctx.save();
      if (row.type === 'level') {
        const v = (st.brightness - 0.05) / 0.95, ae = levelScale(ctx, v);
        // knob
        const kx = CX + Math.cos(ae) * 196, ky = CY + Math.sin(ae) * 196;
        const kg = ctx.createRadialGradient(kx - 4, ky - 4, 1, kx, ky, 13);
        kg.addColorStop(0, '#fff0c8'); kg.addColorStop(0.5, '#c9a35b'); kg.addColorStop(1, '#5a4220');
        ctx.fillStyle = kg; ctx.beginPath(); ctx.arc(kx, ky, 13, 0, TAU); ctx.fill();
        ctx.fillStyle = C.red; ctx.beginPath(); ctx.arc(kx, ky, 2.6, 0, TAU); ctx.fill();
        // sun glyphs at the ends
        [[LEVEL_A0 - 0.17, 4], [LEVEL_A0 + LEVEL_SWEEP + 0.17, 7]].forEach(([a, s]) => {
          const x = CX + Math.cos(a) * 196, y = CY + Math.sin(a) * 196;
          ctx.strokeStyle = C.brass; ctx.lineWidth = 1.3; ctx.beginPath(); ctx.arc(x, y, s * 0.55, 0, TAU); ctx.stroke();
          for (let i = 0; i < 8; i++) { const b = i / 8 * TAU; ctx.beginPath(); ctx.moveTo(x + Math.cos(b) * s * 0.85, y + Math.sin(b) * s * 0.85); ctx.lineTo(x + Math.cos(b) * s * 1.25, y + Math.sin(b) * s * 1.25); ctx.stroke(); }
        });
      } else if (row.opts) {
        const n = row.opts.length, cur = st[row.key];
        row.short.forEach((s, i) => {
          const a = optAngle(i, n), on = i === cur;
          ctx.strokeStyle = on ? C.brassHi : 'rgba(201,163,91,.4)'; ctx.lineWidth = on ? 2 : 1;
          ctx.beginPath(); ctx.moveTo(CX + Math.cos(a) * 172, CY + Math.sin(a) * 172); ctx.lineTo(CX + Math.cos(a) * 180, CY + Math.sin(a) * 180); ctx.stroke();
          arcText(ctx, s, 196, a, `${on ? 500 : 400} ${on ? 13 : 11}px ${FM}`, on ? C.brassHi : 'rgba(236,230,220,.55)', 1.2);
          if (on) {
            ctx.fillStyle = C.red; ctx.beginPath();
            ctx.moveTo(CX + Math.cos(a) * 224, CY + Math.sin(a) * 224);
            ctx.lineTo(CX + Math.cos(a - 0.03) * 233, CY + Math.sin(a - 0.03) * 233);
            ctx.lineTo(CX + Math.cos(a + 0.03) * 233, CY + Math.sin(a + 0.03) * 233); ctx.closePath(); ctx.fill();
          }
        });
        arcText(ctx, 'TURN THE RIM', 196, Math.PI / 2, `500 9px ${FS}`, 'rgba(163,154,140,.6)', 2.5);
      } else {
        arcText(ctx, row.type === 'link' ? 'TAP TO OPEN' : 'OMNI \u00b7 ESP32-S3 \u00b7 466 PX', 196, Math.PI / 2, `500 9px ${FS}`, 'rgba(163,154,140,.6)', 2.5);
      }
      ctx.restore();

      // header
      tracked(ctx, 'SETTINGS', CX, 92, `700 15px ${FD}`, C.brass, 3.5);

      // the window, the drum rows, the window frame on top
      const { x: wx, w: ww } = SETWIN;
      drumGlass(ctx);
      drumRows(ctx, SETV, rows.length, (i, y) => {
        text(ctx, rows[i].label, wx + 20, y + 6, `500 17px ${FS}`, C.ink, 'left');
        const flash = T - SETV.flashT < 220 && i === curI;
        text(ctx, rowValue(rows[i]), wx + ww - 18, y + 6, `500 17px ${FS}`, flash ? '#fff3d6' : C.brassHi, 'right');
      }, (i, y, sc) => {
        text(ctx, rows[i].label, CX, y + 5, `400 ${Math.round(16 * sc)}px ${FS}`, C.dim);
      });
      drumFrame(ctx);
      // position pips
      drumPips(ctx, rows.length, curI);
      text(ctx, 'BOOT \u2039 back', CX, 405, `400 10px ${FM}`, C.faint);
      // brightness preview: dim everything (the device would set panel brightness)
      if (st.brightness < 1) { ctx.fillStyle = `rgba(0,0,0,${(1 - st.brightness) * 0.75})`; ctx.fillRect(0, 0, W, W); }
    },
    pointer(type, x, y, env) {
      const r = dist(x, y), rows = SET_ROWS;
      const curI = clamp(Math.round(SETV.s), 0, rows.length - 1);
      if (type === 'down') {
        SETV.downX = x; SETV.downY = y;
        if (r >= 168) { SETV.rim = true; setRimAt(rows[curI], x, y); return; }
        drumDown(SETV, y);
        return;
      }
      if (type === 'move') {
        if (SETV.rim) { setRimAt(rows[curI], x, y); return; }
        drumMove(SETV, y, rows.length);
        return;
      }
      if (type === 'up') {
        if (SETV.rim) { SETV.rim = false; return; }
        drumUp(SETV, y, rows.length, () => {   // tap the windowed row
          const row = rows[curI];
          if (row.type === 'link') env.goto(row.to);
          else if (row.opts) { S.settings[row.key] = (S.settings[row.key] + 1) % row.opts.length; SETV.flashT = now(); }
        });
      }
    },
  });

  /* ------------------------------------------------------------------ 5. netlist */
  const NET_FOUND = [   // a scan, strongest first, de-duplicated by name; hidden (empty) names already left out
    { ssid: 'Riverside Commons Residents 5GHz', rssi: -44, auth: 'wpa2' },
    { ssid: 'Office-Secure', rssi: -49, auth: 'eap' },              // enterprise: listed, dim, inert
    { ssid: 'Home-Fibre', rssi: -58, auth: 'wpa2', saved: true },
  ];
  const NET_FOUND_LONG = [
    { ssid: 'Home-Fibre', rssi: -52, auth: 'wpa2', saved: true },
    { ssid: 'Riverside Commons Residents 5GHz', rssi: -63, auth: 'wpa3' },
    { ssid: 'Library-Free', rssi: -79, auth: 'open' },
  ];
  const NET = {
    state: 'listening', found: NET_FOUND, scanT: 0, scans: 0, hint: '', hintT: -1e9, shakeT: -1e9,
    btn: { scan: { x: 158, y: 280, w: 150, h: 44 }, phone: { x: 158, y: 332, w: 150, h: 44 } }, press: null,
  };
  const NETV = { s: 0, target: 0, drag: null, rowH: 54 };
  const NET_SCAN_MS = 2000;
  const NET_UNSUPPORTED = { eap: 'Enterprise', wep: 'WEP', wapi: 'WAPI' };
  function netRows() {
    if (NET.state !== 'list') return [];
    return NET.found.map(n => Object.assign({ kind: NET_UNSUPPORTED[n.auth] ? 'bad' : 'net' }, n))
      .concat([{ kind: 'other', label: 'Other network\u2026' }, { kind: 'scan', label: 'Scan again' }]);
  }
  const netLevel = rssi => clamp((rssi + 90) / 60, 0, 1);              // \u221290 dBm \u2192 empty, \u221230 dBm \u2192 full
  const netBars = rssi => (rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1);
  function netScan() { NET.state = 'listening'; NET.scanT = now(); NET.press = null; }
  function ellipsize(ctx, s, font, max) {   // cut with an ellipsis to fit max px
    ctx.font = font; if (ctx.measureText(s).width <= max) return s;
    let lo = 0, hi = s.length;
    while (lo < hi) { const m = (lo + hi + 1) >> 1; if (ctx.measureText(s.slice(0, m) + '\u2026').width <= max) lo = m; else hi = m - 1; }
    return s.slice(0, lo).replace(/\s+$/, '') + '\u2026';
  }
  function barsGlyph(ctx, x, y, lit, s, on, off) {   // four signal bars; x left, y baseline
    ctx.save();
    for (let i = 0; i < 4; i++) {
      const h = (4.5 + i * 3) * s;
      ctx.fillStyle = i < lit ? on : off;
      rrect(ctx, x + i * 5 * s, y - h, 3.2 * s, h, 0.9 * s); ctx.fill();
    }
    ctx.restore();
  }
  function lockGlyph(ctx, x, y, col) {   // x centre, y baseline
    ctx.save(); ctx.strokeStyle = col; ctx.fillStyle = col; ctx.lineWidth = 1.6;
    ctx.beginPath(); ctx.moveTo(x - 3.4, y - 7); ctx.lineTo(x - 3.4, y - 9.6); ctx.arc(x, y - 9.6, 3.4, Math.PI, 0); ctx.lineTo(x + 3.4, y - 7); ctx.stroke();
    rrect(ctx, x - 5.5, y - 7.5, 11, 8.5, 1.6); ctx.fill();
    ctx.fillStyle = '#0d0b08'; ctx.fillRect(x - 0.75, y - 5, 1.5, 3.2);
    ctx.restore();
  }
  function savedTag(ctx, xr, y) {   // the brass "saved" tag; xr right edge, y centre; returns its left edge
    ctx.font = `600 10.5px ${FS}`;
    const w = Math.ceil(ctx.measureText('saved').width) + 14, h = 18, x = xr - w;
    ctx.save();
    rrect(ctx, x, y - h / 2, w, h, h / 2);
    const g = ctx.createLinearGradient(0, y - h / 2, 0, y + h / 2);
    g.addColorStop(0, '#e3c582'); g.addColorStop(1, '#a07b38');
    ctx.fillStyle = g; ctx.fill();
    ctx.strokeStyle = 'rgba(255,240,200,.45)'; ctx.lineWidth = 1; ctx.stroke();
    ctx.restore();
    text(ctx, 'saved', x + w / 2, y + 3.8, `600 10.5px ${FS}`, '#1f1606');
    return x;
  }
  function rescanGlyph(ctx, x, y, col) {
    ctx.save(); ctx.strokeStyle = col; ctx.fillStyle = col; ctx.lineWidth = 1.8; ctx.lineCap = 'round';
    ctx.beginPath(); ctx.arc(x, y, 7, -Math.PI * 0.35, Math.PI * 1.35); ctx.stroke();
    const a = -Math.PI * 0.35, ex = x + Math.cos(a) * 7, ey = y + Math.sin(a) * 7;
    ctx.beginPath(); ctx.moveTo(ex + 4.5, ey - 1.5); ctx.lineTo(ex - 1.2, ey - 4.2); ctx.lineTo(ex + 0.6, ey + 2.6); ctx.closePath(); ctx.fill();
    ctx.restore();
  }
  function rimNotch(ctx, a) {   // the red index at the rim's edge (the current value)
    ctx.fillStyle = C.red; ctx.beginPath();
    ctx.moveTo(CX + Math.cos(a) * 224, CY + Math.sin(a) * 224);
    ctx.lineTo(CX + Math.cos(a - 0.03) * 233, CY + Math.sin(a - 0.03) * 233);
    ctx.lineTo(CX + Math.cos(a + 0.03) * 233, CY + Math.sin(a + 0.03) * 233); ctx.closePath(); ctx.fill();
  }
  const rimHint = (ctx, s) => arcText(ctx, s, 196, Math.PI / 2, `500 9px ${FS}`, 'rgba(163,154,140,.6)', 2.5);

  OMNI_FACES.push({
    id: 'netlist', group: 'Setup', title: 'Choose network',
    summary: 'The Wi-Fi list for the keyboard path: the Settings drum; the rim shows the windowed network\u2019s signal.',
    notes: `
<p><b>The Settings drum, reused.</b> The centre picks a network and the brass window shows it; there is nothing to set,
so the rim only <i>shows</i>: the windowed network's signal as an arc on the level scale (\u221290 to \u221230&nbsp;dBm, red index at
the reading). The window row is the name (Plex Sans&nbsp;17, cut with an ellipsis), a lock for secured networks, four bars,
and the brass <b>saved</b> tag on the saved network. Rows above and below are dim, as in Settings.</p>
<ul>
<li><b>Order:</b> strongest first, one row per name, hidden names left out, at most 16, then <b>Other network\u2026</b> and
<b>Scan again</b>.</li>
<li><b>Can't join</b> (enterprise, WEP, WAPI): listed dim with <i>not supported</i>, inert, so a network never seems to vanish.</li>
<li><b>Tap the window:</b> secured \u2192 the keyboard (password); open \u2192 "Join an open network?"; saved \u2192 "Saved \u00b7 Join now" /
"Change password"; Other network\u2026 \u2192 the keyboard (name, then password). Sheets are not drawn here.</li>
<li><b>Listening\u2026</b> sits in the window for the ~2&nbsp;s scan while a brass tick sweeps the rim. <b>No networks found</b>
offers Scan again and Use your phone. In this mockup Scan again alternates between the two outcomes.</li>
</ul>`,
    init() { NET.found = NET_FOUND; NET.scans = 0; netScan(); NETV.s = NETV.target = 0; },
    frame(ctx, t, env) {
      const T = now();
      if (NET.state === 'listening' && T - NET.scanT > NET_SCAN_MS) {
        NET.state = NET.scans++ % 2 ? 'empty' : 'list';
        const rows = netRows(), sv = rows.findIndex(r => r.saved);   // the window opens on the saved network
        NETV.s = NETV.target = sv >= 0 ? sv : 0;
      }
      drumSpring(NETV);
      const rows = netRows(), n = rows.length, curI = clamp(Math.round(NETV.s), 0, Math.max(0, n - 1)), row = rows[curI];
      blit(ctx, settingsBezel());
      const { x: wx, y: wy, w: ww, h: wh } = SETWIN;

      // the rim: information only
      ctx.save();
      if (row && (row.kind === 'net' || row.kind === 'bad')) {
        const ae = levelScale(ctx, netLevel(row.rssi));
        rimNotch(ctx, ae);
        [[LEVEL_A0 - 0.15, 1], [LEVEL_A0 + LEVEL_SWEEP + 0.15, 4]].forEach(([a, lit]) => {
          const x = CX + Math.cos(a) * 196, y = CY + Math.sin(a) * 196;
          barsGlyph(ctx, x - 7.5, y + 5.5, lit, 0.78, C.brass, 'rgba(201,163,91,.28)');
        });
        const db = String(row.rssi).replace('-', '\u2212') + ' dBm';
        rimHint(ctx, row.kind === 'bad' ? db + ' \u00b7 CAN\u2019T JOIN' : db + (row.saved ? ' \u00b7 TAP FOR OPTIONS' : ' \u00b7 TAP TO JOIN'));
      } else {
        levelScale(ctx, null);
        if (NET.state === 'listening') {   // the brass tick sweeps the scale while the scan runs
          const p = ((T - NET.scanT) % 1400) / 1400, a = LEVEL_A0 + LEVEL_SWEEP * easeOut(p) * 0.999 + 0.0005;
          for (let i = 0; i < 26; i++) {
            const a1 = a - i * 0.033, a0 = a1 - 0.033; if (a0 < LEVEL_A0) break;
            ctx.strokeStyle = `rgba(240,213,149,${0.9 * (1 - i / 26)})`; ctx.lineWidth = 5; ctx.lineCap = 'butt';
            ctx.beginPath(); ctx.arc(CX, CY, 196, a0, a1); ctx.stroke();
          }
          ctx.lineCap = 'round'; ctx.strokeStyle = C.brassHi; ctx.lineWidth = 3;
          ctx.shadowColor = 'rgba(240,213,149,.8)'; ctx.shadowBlur = 8;
          ctx.beginPath(); ctx.moveTo(CX + Math.cos(a) * 185, CY + Math.sin(a) * 185); ctx.lineTo(CX + Math.cos(a) * 207, CY + Math.sin(a) * 207); ctx.stroke();
          ctx.shadowBlur = 0;
          rimHint(ctx, 'LISTENING ON EVERY CHANNEL');
        } else if (NET.state === 'empty') rimHint(ctx, 'NOTHING IN RANGE');
        else if (row) rimHint(ctx, row.kind === 'other' ? 'TAP TO TYPE A NAME' : 'TAP TO LISTEN AGAIN');
      }
      ctx.restore();

      tracked(ctx, 'CHOOSE NETWORK', CX, 92, `700 13px ${FD}`, C.brass, 2.6);

      // the window, the rows, the frame
      drumGlass(ctx);
      if (NET.state === 'list') {
        const sh = T - NET.shakeT < 380 ? Math.sin((T - NET.shakeT) / 22) * 6 * (1 - (T - NET.shakeT) / 380) : 0;
        drumRows(ctx, NETV, n, (i, y) => {
          const r = rows[i], x0 = wx + 20, xr = wx + ww - 18, base = y + 6;
          if (r.kind === 'net') {
            barsGlyph(ctx, xr - 18.2, base, netBars(r.rssi), 1, C.brassHi, '#3a3229');
            let right = xr - 18.2 - 11;
            if (r.auth !== 'open') { lockGlyph(ctx, right - 5.5, base, C.dim); right -= 11 + 9; }
            if (r.saved) right = savedTag(ctx, right, y) - 10;
            const f = `500 17px ${FS}`;
            text(ctx, ellipsize(ctx, r.ssid, f, Math.min(250, right - x0)), x0, base, f, C.ink, 'left');
          } else if (r.kind === 'bad') {
            ctx.save(); ctx.translate(sh, 0);
            text(ctx, 'not supported', xr, base - 1, `italic 400 12px ${FS}`, C.faint, 'right');
            ctx.font = `italic 400 12px ${FS}`;
            const room = xr - ctx.measureText('not supported').width - 12 - x0, f = `500 17px ${FS}`;
            text(ctx, ellipsize(ctx, r.ssid, f, Math.min(250, room)), x0, base, f, C.faint, 'left');
            ctx.restore();
          } else {
            text(ctx, r.label, x0, base, `500 17px ${FS}`, C.ink, 'left');
            if (r.kind === 'other') text(ctx, '\u203a', xr, base, `500 20px ${FS}`, C.brassHi, 'right');
            else rescanGlyph(ctx, xr - 8, y, C.brassHi);
          }
        }, (i, y, sc) => {
          const r = rows[i], fs = Math.round(16 * sc), f = `400 ${fs}px ${FS}`;
          if (r.kind === 'other' || r.kind === 'scan') { text(ctx, r.label, CX, y + 5, f, C.dim); return; }
          const tag = r.kind === 'bad' ? ' \u00b7 not supported' : r.saved ? ' \u00b7 saved' : '';
          const tf = `${r.kind === 'bad' ? 'italic ' : ''}400 ${Math.round(fs * 0.8)}px ${FS}`;
          ctx.font = tf; const tw = tag ? ctx.measureText(tag).width : 0;
          const name = ellipsize(ctx, r.ssid, f, 250 - tw), nw = ctx.measureText(name).width, x = CX - (nw + tw) / 2;
          text(ctx, name, x, y + 5, f, r.kind === 'bad' ? C.faint : C.dim, 'left');
          if (tag) text(ctx, tag, x + nw, y + 5, tf, r.kind === 'bad' ? C.faint : C.brass, 'left');
        });
      } else {
        ctx.save();
        wifiGlyph(ctx, CX, 142, 0.9, NET.state === 'listening' ? C.brassHi : '#5b5246', NET.state === 'listening' ? T - NET.scanT : null);
        if (NET.state === 'empty') {   // engraved slash: nothing heard
          ctx.strokeStyle = '#5b5246'; ctx.lineWidth = 2.2; ctx.lineCap = 'round';
          ctx.beginPath(); ctx.moveTo(CX - 20, 166); ctx.lineTo(CX + 20, 122); ctx.stroke();
        }
        ctx.restore();
        text(ctx, NET.state === 'listening' ? 'Listening\u2026' : 'No networks found', CX, wy + wh / 2 + 6, `500 17px ${FS}`, C.ink);
      }
      drumFrame(ctx);

      if (NET.state === 'empty') {
        const b = NET.btn;
        key(ctx, b.scan, { primary: true, pressed: NET.press === 'scan' });
        rescanGlyph(ctx, b.scan.x + 25, b.scan.y + 22, '#1f1606');
        text(ctx, 'Scan again', b.scan.x + b.scan.w / 2 + 12, b.scan.y + 28, `700 18px ${FD}`, '#1f1606');
        key(ctx, b.phone, { pressed: NET.press === 'phone' });
        phoneGlyph(ctx, b.phone.x + 21, b.phone.y + 22, 0.4, C.brass);
        text(ctx, 'Use your phone', b.phone.x + b.phone.w / 2 + 10, b.phone.y + 27, `500 14px ${FS}`, C.ink);
      } else if (NET.state === 'list' && n <= 9) drumPips(ctx, n, curI);
      if (NET.state !== 'empty') {
        const hint = T - NET.hintT < 1800 ? NET.hint : 'BOOT \u2039 back';
        text(ctx, hint, CX, 405, `400 10px ${FM}`, C.faint);
      }
    },
    pointer(type, x, y, env) {
      const rows = netRows(), n = rows.length;
      if (NET.state === 'empty') {
        if (type === 'down') { NET.press = Object.keys(NET.btn).find(k => inRect(NET.btn[k], x, y)) || null; return; }
        if (type === 'move') { if (NET.press && !inRect(NET.btn[NET.press], x, y)) NET.press = null; return; }
        if (type === 'up' && NET.press) {
          const p = NET.press; NET.press = null;
          if (p === 'scan') netScan(); else env.goto('wifi-qr');
        }
        return;
      }
      if (NET.state !== 'list') return;
      if (type === 'down') { if (dist(x, y) < 168) drumDown(NETV, y); return; }
      if (type === 'move') { drumMove(NETV, y, n); return; }
      if (type === 'up') drumUp(NETV, y, n, () => {
        const r = rows[clamp(Math.round(NETV.s), 0, n - 1)];
        if (r.kind === 'bad') { NET.shakeT = now(); return; }            // inert: it only says so
        if (r.kind === 'scan') { netScan(); return; }
        if (r.kind === 'net' && r.saved) { NET.hint = 'sheet: Join now \u00b7 Change password'; NET.hintT = now(); return; }
        if (r.kind === 'net' && r.auth === 'open') { NET.hint = 'sheet: Join an open network?'; NET.hintT = now(); return; }
        if (r.kind === 'net') S.ssid = r.ssid;
        env.goto('rim-keyboard');
      });
    },
  });

  /* ------------------------------------------------------------------ 6. faces-edit */
  const FE_FACES = [   // theme order. The first eight are the Omni's faces; eight stand-ins fill the 16-face maximum.
    { id: 'omni', title: 'Omni dial', group: 'Omni' },
    { id: 'globe-live', title: 'Real-time globe', group: 'Faces' },
    { id: 'globe-analog', title: 'Globe analog', group: 'Faces' },
    { id: 'digital', title: 'Digital', group: 'Faces' },
    { id: 'world', title: 'World time', group: 'Faces' },
    { id: 'almanac', title: 'Almanac', group: 'Faces' },
    { id: 'timer', title: 'Timer & stopwatch', group: 'Faces' },
    { id: 'globe-spin', title: 'Spinning globe', group: 'Faces' },
    { id: 'moon', title: 'Moon phase', group: 'Faces' },
    { id: 'sun', title: 'Sun & daylight', group: 'Faces' },
    { id: 'calendar', title: 'Calendar', group: 'Faces' },
    { id: 'chrono', title: 'Chronograph', group: 'Faces' },
    { id: 'big', title: 'Big numerals', group: 'Faces' },
    { id: 'quiet', title: 'Quiet dial', group: 'Faces' },
    { id: 'seconds', title: 'Seconds sweep', group: 'Faces' },
    { id: 'lunar', title: 'Lunar tides', group: 'Faces' },
  ].map(f => Object.assign(f, { glyph: glyphFor(f) }));
  const FE_BY = Object.fromEntries(FE_FACES.map(f => [f.id, f]));
  const FE_DEFAULT = FE_FACES.map(f => f.id);   // the theme's default carousel: every face, in theme order
  const FE_GAP = 0.75, FE_R = 182, FE_TR = 27, FE_HOLD = 1000;
  const FED = {
    shown: FE_DEFAULT.slice(), hidden: [], rot: 0, target: 0, vel: 0, drag: null, disp: {}, focus: 'omni', selT: 0, lastSel: null,
    btn: {
      earlier: { x: 161, y: 174, w: 64, h: 48 }, later: { x: 241, y: 174, w: 64, h: 48 },
      swipe: { x: 161, y: 232, w: 144, h: 48 }, restore: { x: 158, y: 296, w: 150, h: 44 },
    },
    press: null, holdT: 0, doneT: -1e9,
  };
  const feOrder = () => FED.shown.concat(FED.hidden);
  const feSlots = () => FED.shown.length + FED.hidden.length + (FED.hidden.length ? FE_GAP : 0);
  const feStep = () => TAU / feSlots();
  function feSlot(id) { const i = FED.shown.indexOf(id); return i >= 0 ? i : FED.shown.length + FE_GAP + FED.hidden.indexOf(id); }
  const feAngle = (slot, rot) => -Math.PI / 2 + rot + slot * feStep();
  function feNearest(rot) {   // the face nearest the window for a ring angle
    let best = null, bd = 1e9;
    for (const id of feOrder()) { const d = Math.abs(wrap(feAngle(feSlot(id), rot) + Math.PI / 2)); if (d < bd) { bd = d; best = id; } }
    return best;
  }
  function feFocus(id, instant) {
    FED.focus = id;
    FED.target = FED.rot + wrap(-feSlot(id) * feStep() - FED.rot);
    if (instant) { FED.rot = FED.target; feOrder().forEach(k => { FED.disp[k] = feSlot(k); }); }
  }
  const feSame = () => FED.shown.length === FE_DEFAULT.length && FED.shown.every((id, i) => id === FE_DEFAULT[i]);
  function feCan(name) {
    const i = FED.shown.indexOf(FED.focus);
    if (name === 'earlier') return i > 0;
    if (name === 'later') return i >= 0 && i < FED.shown.length - 1;
    if (name === 'swipe') return !(i >= 0 && FED.shown.length === 1);   // the last shown face cannot be hidden
    if (name === 'restore') return !feSame();
    return false;
  }
  function feAct(name) {
    const id = FED.focus, sh = FED.shown, i = sh.indexOf(id);
    if (name === 'earlier' || name === 'later') {
      const j = i + (name === 'earlier' ? -1 : 1); [sh[i], sh[j]] = [sh[j], sh[i]];
    } else if (name === 'swipe') {
      if (i >= 0) { sh.splice(i, 1); FED.hidden.push(id); FED.hidden.sort((a, b) => FE_DEFAULT.indexOf(a) - FE_DEFAULT.indexOf(b)); }
      else { FED.hidden.splice(FED.hidden.indexOf(id), 1); sh.push(id); }
    } else if (name === 'restore') { FED.shown = FE_DEFAULT.slice(); FED.hidden = []; FED.doneT = now(); }
    feFocus(id);
  }
  function pillPath(ctx, x, y, w, h) {   // a pill outline that starts at its top centre, clockwise
    const r = h / 2;
    ctx.beginPath(); ctx.moveTo(x + w / 2, y); ctx.lineTo(x + w - r, y); ctx.arc(x + w - r, y + r, r, -Math.PI / 2, Math.PI / 2);
    ctx.lineTo(x + r, y + h); ctx.arc(x + r, y + r, r, Math.PI / 2, Math.PI * 1.5); ctx.closePath();
    return 2 * (w - 2 * r) + TAU * r;
  }
  function triGlyph(ctx, x, y, dir, col) {   // \u25c2 / \u25b8
    ctx.fillStyle = col; ctx.beginPath();
    ctx.moveTo(x + dir * 5, y); ctx.lineTo(x - dir * 4, y - 5.5); ctx.lineTo(x - dir * 4, y + 5.5); ctx.closePath(); ctx.fill();
  }

  OMNI_FACES.push({
    id: 'faces-edit', group: 'Setup', title: 'Faces: swipe order',
    summary: 'The carousel editor from Settings \u2192 Faces: the picker\u2019s ring of every face; order and show or hide in the centre.',
    notes: `
<p><b>The picker's grammar.</b> The ring holds <b>every face the theme has</b> at the picker's radius (r&nbsp;182) and thumbnail
size; turn it to bring a face under the window. Faces in the swipe carousel come first, in carousel order, each with a small brass
order numeral. Faces not in it follow after a gap, at 40&nbsp;% with no numeral. This frame shows the 16-face maximum: the Omni's
eight faces plus eight stand-ins.</p>
<ul>
<li><b>\u25c2 Earlier / Later \u25b8</b> (64\u00d748) move the windowed face one place in the carousel; the ring re-flows at once so the
move is visible, and the window stays on the face.</li>
<li><b>In swipe order</b> (144\u00d748, on/off) puts the face in the carousel (at its end) or takes it out. Hidden means only "not in the
swipe carousel": the picker still lists it. The last shown face cannot be hidden, so its toggle is dim.</li>
<li><b>Restore default</b>: hold 1&nbsp;s; a brass ring fills round the key, and lifting early cancels.</li>
<li>Every edit saves the whole order at once (debounced, as today); there is no Save.</li>
</ul>`,
    init() { if (!FED.inited) { FED.inited = true; feFocus(FED.focus, true); } },
    frame(ctx, t, env) {
      const T = now(), order = feOrder(), step = feStep(), b = FED.btn;
      if (!FED.drag) { const d = FED.target - FED.rot; FED.rot += d * 0.18; if (Math.abs(d) < 0.0005) FED.rot = FED.target; }
      order.forEach(id => { const s = feSlot(id), d = FED.disp[id] == null ? s : FED.disp[id]; FED.disp[id] = Math.abs(s - d) < 0.002 ? s : d + (s - d) * 0.2; });
      if (FED.press === 'restore' && T - FED.holdT >= FE_HOLD) { FED.press = null; feAct('restore'); }
      blit(ctx, navBezel());

      // engraved scale, turning with the ring: a major mark at every face, minors between
      ctx.save();
      const total = feSlots();
      for (let k = 0; k < Math.round(total * 4); k++) {
        const a = feAngle(k / 4, FED.rot), major = k % 4 === 0 && k / 4 < FED.shown.length;
        ctx.strokeStyle = major ? 'rgba(30,18,4,.85)' : 'rgba(30,18,4,.55)'; ctx.lineWidth = major ? 1.6 : 1;
        const r1 = major ? 226 : k % 2 === 0 ? 223 : 220;
        ctx.beginPath(); ctx.moveTo(CX + Math.cos(a) * 216, CY + Math.sin(a) * 216); ctx.lineTo(CX + Math.cos(a) * r1, CY + Math.sin(a) * r1); ctx.stroke();
      }
      FED.hidden.forEach(id => {
        const a = feAngle(feSlot(id), FED.rot);
        ctx.strokeStyle = 'rgba(30,18,4,.85)'; ctx.lineWidth = 1.6;
        ctx.beginPath(); ctx.moveTo(CX + Math.cos(a) * 216, CY + Math.sin(a) * 216); ctx.lineTo(CX + Math.cos(a) * 226, CY + Math.sin(a) * 226); ctx.stroke();
      });
      if (FED.hidden.length) {   // the gap: an engraved divider between the carousel and the rest
        const a = feAngle(FED.shown.length - 1 + (1 + FE_GAP) / 2, FED.rot);
        ctx.strokeStyle = 'rgba(30,18,4,.8)'; ctx.lineWidth = 1.4;
        [-0.035, 0.035].forEach(o => { ctx.beginPath(); ctx.moveTo(CX + Math.cos(a + o) * 160, CY + Math.sin(a + o) * 160); ctx.lineTo(CX + Math.cos(a + o) * 204, CY + Math.sin(a + o) * 204); ctx.stroke(); });
        ctx.strokeStyle = 'rgba(255,232,180,.16)';
        [-0.035, 0.035].forEach(o => { ctx.beginPath(); ctx.moveTo(CX + Math.cos(a + o + 0.005) * 160, CY + Math.sin(a + o + 0.005) * 160); ctx.lineTo(CX + Math.cos(a + o + 0.005) * 204, CY + Math.sin(a + o + 0.005) * 204); ctx.stroke(); });
      }
      ctx.restore();

      const sel = FED.drag ? feNearest(FED.rot) : FED.focus;
      for (const id of order) {
        const a = feAngle(FED.disp[id], FED.rot), shown = FED.shown.indexOf(id);
        const near = Math.max(0, 1 - Math.abs(wrap(a + Math.PI / 2)) / (step * 0.8));
        const r = FE_TR * (1 + 0.18 * smooth(near)), x = CX + Math.cos(a) * FE_R, y = CY + Math.sin(a) * FE_R;
        drawThumb(ctx, FE_BY[id], x, y, r, env, id === sel && !FED.drag);
        if (shown < 0) {   // not in the carousel: 40 %, no numeral (lifted to full in the window, so it can be seen)
          ctx.fillStyle = `rgba(0,0,0,${0.6 * (1 - smooth(near))})`; ctx.beginPath(); ctx.arc(x, y, r + 1.5, 0, TAU); ctx.fill();
        } else if (!(id === sel && !FED.drag)) {   // the windowed face's place is spelled out in the centre
          const bx = CX + Math.cos(a) * (FE_R - r - 1), by = CY + Math.sin(a) * (FE_R - r - 1);
          ctx.save();
          ctx.fillStyle = '#120e08'; ctx.beginPath(); ctx.arc(bx, by, 8.5, 0, TAU); ctx.fill();
          ctx.strokeStyle = 'rgba(201,163,91,.85)'; ctx.lineWidth = 1; ctx.stroke();
          text(ctx, String(shown + 1), bx, by + 0.5, `600 ${shown >= 9 ? 9 : 10}px ${FS}`, C.brassHi, 'center', 'middle');
          ctx.restore();
        }
      }
      magWindow(ctx);

      // centre
      ctx.save();
      centreDisc(ctx);
      const f = FE_BY[sel], inC = FED.shown.indexOf(sel);
      if (FED.lastSel !== sel) { FED.lastSel = sel; FED.selT = T; }
      ctx.globalAlpha = smooth((T - FED.selT) / 220);
      tracked(ctx, f.group.toUpperCase() + (inC >= 0 ? ` \u00b7 ${inC + 1} OF ${FED.shown.length}` : ' \u00b7 HIDDEN'), CX, 124, `600 10px ${FS}`, C.brass, 2.4);
      text(ctx, f.title, CX, 160, `700 ${f.title.length > 18 ? 22 : f.title.length > 13 ? 26 : 32}px ${FD}`, '#eadcbc');
      ctx.globalAlpha = 1;

      [['earlier', -1], ['later', 1]].forEach(([k, dir]) => {
        const bb = b[k], on = feCan(k) && !FED.drag;
        ctx.globalAlpha = on ? 1 : 0.35;
        key(ctx, bb, { pressed: FED.press === k, radius: 14 });
        triGlyph(ctx, bb.x + bb.w / 2, bb.y + 17, dir, on ? C.brassHi : C.dim);
        text(ctx, k === 'earlier' ? 'Earlier' : 'Later', bb.x + bb.w / 2, bb.y + 38, `500 12px ${FS}`, C.ink);
      });
      const sw = b.swipe, swOn = inC >= 0, swCan = feCan('swipe');
      ctx.globalAlpha = swCan ? 1 : 0.35;
      key(ctx, sw, { pressed: FED.press === 'swipe', on: swOn, radius: 14 });
      text(ctx, 'In swipe order', sw.x + 13, sw.y + 29, `500 12.5px ${FS}`, C.ink, 'left');
      const tx = sw.x + sw.w - 12 - 28, ty = sw.y + 17;
      rrect(ctx, tx, ty, 28, 14, 7);
      if (swOn) { const g = ctx.createLinearGradient(0, ty, 0, ty + 16); g.addColorStop(0, '#e3c582'); g.addColorStop(1, '#a07b38'); ctx.fillStyle = g; ctx.fill(); }
      else { ctx.fillStyle = '#080706'; ctx.fill(); ctx.strokeStyle = 'rgba(201,163,91,.45)'; ctx.lineWidth = 1; ctx.stroke(); }
      ctx.fillStyle = swOn ? '#fff3d6' : '#5b5246';
      ctx.beginPath(); ctx.arc(swOn ? tx + 21 : tx + 7, ty + 7, 4.8, 0, TAU); ctx.fill();

      const rb = b.restore, rCan = feCan('restore');
      ctx.globalAlpha = rCan ? 1 : 0.35;
      key(ctx, rb, { pressed: FED.press === 'restore', radius: 22 });
      text(ctx, 'Restore default', CX, rb.y + 21, `500 13px ${FS}`, C.ink);
      tracked(ctx, 'HOLD 1 S', CX, rb.y + 34, `600 8px ${FS}`, C.faint, 1.8);
      // the hold ring: a faint track round the key; brass fills it over 1 s
      const P = pillPath(ctx, rb.x - 4, rb.y - 4, rb.w + 8, rb.h + 8);
      ctx.strokeStyle = 'rgba(201,163,91,.16)'; ctx.lineWidth = 2; ctx.stroke();
      const hp = FED.press === 'restore' ? clamp((T - FED.holdT) / FE_HOLD, 0, 1) : T - FED.doneT < 500 ? 1 - (T - FED.doneT) / 500 : 0;
      if (hp > 0) {
        pillPath(ctx, rb.x - 4, rb.y - 4, rb.w + 8, rb.h + 8);
        ctx.setLineDash([P * hp, P * 2]); ctx.strokeStyle = C.brassHi; ctx.lineWidth = 2.6; ctx.lineCap = 'round';
        ctx.shadowColor = 'rgba(240,213,149,.6)'; ctx.shadowBlur = 6; ctx.stroke();
        ctx.setLineDash([]); ctx.shadowBlur = 0;
      }
      ctx.restore();
    },
    pointer(type, x, y, env) {
      const step = feStep();
      if (type === 'down') {
        FED.downX = x; FED.downY = y;
        const hit = Object.keys(FED.btn).find(k => inRect(FED.btn[k], x, y));
        if (hit) { if (feCan(hit)) { FED.press = hit; FED.holdT = now(); } return; }
        if (dist(x, y) > 146) FED.drag = { lastA: angle(x, y), lastT: now(), moved: 0 };
        return;
      }
      if (type === 'move') {
        if (FED.press && !inRect(FED.btn[FED.press], x, y)) FED.press = null;
        if (FED.drag) {
          const a = angle(x, y), d = wrap(a - FED.drag.lastA), T = now();
          FED.rot += d; FED.drag.moved += Math.abs(d);
          FED.vel = 0.7 * FED.vel + 0.3 * (d / Math.max(1, T - FED.drag.lastT));
          FED.drag.lastA = a; FED.drag.lastT = T;
        }
        return;
      }
      if (type !== 'up') return;
      if (FED.press) {
        const p = FED.press; FED.press = null;
        if (p !== 'restore' && inRect(FED.btn[p], x, y)) feAct(p);   // restore acts only on a full hold
        return;
      }
      if (FED.drag) {
        const moved = FED.drag.moved; FED.drag = null;
        if (moved < 0.04) {   // a tap on a thumbnail brings it round
          let best = null, bd = 1e9;
          for (const id of feOrder()) {
            const a = feAngle(feSlot(id), FED.rot), d = Math.hypot(x - (CX + Math.cos(a) * FE_R), y - (CY + Math.sin(a) * FE_R));
            if (d < bd) { bd = d; best = id; }
          }
          if (bd < FE_TR + 14) { feFocus(best); return; }
        }
        const proj = FED.rot + clamp(FED.vel * 160, -step * 2.5, step * 2.5);
        FED.vel = 0; feFocus(feNearest(proj));
      }
    },
  });

  /* review hooks: put the two screens into the states the exported shots show */
  window.OMNI_SETUP_DEMO = {
    fed: FED, net: NET,
    netShot(state, idx) {
      NET.found = state === 'long' ? NET_FOUND_LONG : NET_FOUND;
      NET.state = state === 'long' ? 'list' : state; NET.scanT = now(); NET.press = null;
      NETV.s = NETV.target = idx || 0; NETV.drag = null;
    },
    facesShot(focus) {
      FED.shown = ['omni', 'globe-live', 'globe-analog', 'digital', 'world', 'almanac', 'globe-spin', 'moon', 'sun', 'calendar', 'chrono'];
      FED.hidden = ['timer', 'big', 'quiet', 'seconds', 'lunar'];
      FED.press = null; FED.drag = null; FED.lastSel = focus; FED.selT = -1e9;
      feFocus(focus, true);
    },
  };
})();
