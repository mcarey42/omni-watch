/* faces-omni.js — the Omni mode (group 'Omni').
   Entries: omni-iris (brass iris reveal), omni-flip (hinged lid reveal), omni-dial (the living dial).
   One shared engine: pre-rendered brass layers (offscreen canvases at 2x), a LUT-projected globe,
   rotating date rings, annular hands, magnifier, red/green lenses and the "time travel" sequence. */
(function () {
  'use strict';
  window.OMNI_FACES = window.OMNI_FACES || [];

  const W = 466, C = 233, S = 2, TAU = Math.PI * 2, D2R = Math.PI / 180;
  const FD = '"Cormorant SC", "Cormorant Garamond", Georgia, serif';
  const FB = '"UnifrakturMaguntia", "UnifrakturCook", "Old English Text MT", "Cormorant SC", Georgia, serif';

  // ---------- geometry (1x screen px) ----------
  const GR = 112;                         // globe radius -> 224 px globe
  const TILT = 22 * D2R;                  // globe tilt (view latitude)
  const RINGS = [
    { key: 'month',   r0: 126, r1: 143, n: 12,  font: '700 11px ',  rt: 134.6 },
    { key: 'day',     r0: 143, r1: 160, n: 32,  font: '700 10.5px ', rt: 151.6 },
    { key: 'year',    r0: 160, r1: 178, n: 100, font: '700 8.6px ', rt: 169.2 },
    { key: 'century', r0: 178, r1: 197, n: 42,  font: '700 9.4px ', rt: 190.2 },
  ];
  const VD = [11.5, 12.5, 14, 16.5];      // half-gap of the red V marks per ring (px)
  const MAG = { x0: 207, x1: 259, y0: 18, y1: 122, cy: C - 162.5, k: 1.4 };
  const LENS_R = 215, LENS_A = 30 * D2R;  // red at -30deg (11 o'clock), green at +30deg (1 o'clock)
  const DRIFT = 2.2;                      // deg/s idle drift of the globe

  const clamp = (x, a, b) => x < a ? a : x > b ? b : x;
  const lerp = (a, b, t) => a + (b - a) * t;
  const ph = (t, a, b) => clamp((t - a) / (b - a), 0, 1);
  const E = {
    inOutCubic: s => s < .5 ? 4 * s * s * s : 1 - Math.pow(-2 * s + 2, 3) / 2,
    inOutQuart: s => s < .5 ? 8 * s * s * s * s : 1 - Math.pow(-2 * s + 2, 4) / 2,
    outCubic: s => 1 - Math.pow(1 - s, 3),
    inCubic: s => s * s * s,
    outBack: s => { const c1 = 1.5, c3 = c1 + 1; return 1 + c3 * Math.pow(s - 1, 3) + c1 * Math.pow(s - 1, 2); },
  };
  const px = (r, a) => C + r * Math.sin(a), py = (r, a) => C - r * Math.cos(a);
  function mk(w, h) { const c = document.createElement('canvas'); c.width = w; c.height = h || w; return c; }
  function rng(seed) { return function () { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
  function annulus(g, r0, r1) { g.beginPath(); g.arc(C, C, r1, 0, TAU); g.arc(C, C, r0, 0, TAU, true); g.fill(); }
  function ring(g, r, col, w) { g.strokeStyle = col; g.lineWidth = w; g.beginPath(); g.arc(C, C, r, 0, TAU); g.stroke(); }
  function rrect(g, x, y, w, h, r) { g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); }

  // tarnished brass ramp, 0..1
  const BR = [[0, [8, 6, 3]], [.22, [38, 28, 13]], [.45, [80, 62, 31]], [.65, [128, 101, 54]], [.83, [178, 146, 90]], [1, [230, 208, 152]]];
  function brassArr(l) {
    l = clamp(l, 0, 1);
    for (let i = 1; i < BR.length; i++) if (l <= BR[i][0]) {
      const t = (l - BR[i - 1][0]) / (BR[i][0] - BR[i - 1][0]), a = BR[i - 1][1], b = BR[i][1];
      return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
    }
    return BR[BR.length - 1][1];
  }
  function brass(l, a) { const c = brassArr(l); return `rgba(${c[0] | 0},${c[1] | 0},${c[2] | 0},${a == null ? 1 : a})`; }
  // machined-ring lighting: two anisotropic lobes (key light at 10:30 and its opposite), fixed in world space
  function conicBrass(g, phase, lo, hi, cx, cy) {
    const gr = g.createConicGradient(-Math.PI / 2, cx == null ? C : cx, cy == null ? C : cy);
    for (let i = 0; i <= 48; i++) {
      const a = i / 48 * TAU, la = a + 45 * D2R - phase;
      const v = Math.pow(0.5 + 0.5 * Math.cos(la), 4), w = Math.pow(0.5 + 0.5 * Math.cos(la - Math.PI), 6);
      gr.addColorStop(i / 48, brass(lo + (hi - lo) * clamp(v * 0.9 + w * 0.45 + 0.06 * Math.cos(3 * la) + 0.1, 0, 1)));
    }
    return gr;
  }

  // ---------- fonts ----------
  let fontGen = 0, fontsAsked = false;
  function askFonts() {
    if (fontsAsked) return; fontsAsked = true;
    const bump = () => { fontGen++; };
    try {
      const l = document.createElement('link'); l.rel = 'stylesheet';
      l.href = 'https://fonts.googleapis.com/css2?family=UnifrakturMaguntia&display=swap';
      l.onload = () => { document.fonts && document.fonts.load('40px "UnifrakturMaguntia"').then(bump, bump); };
      document.head.appendChild(l);
    } catch (e) { /* offline: blackletter falls back */ }
    if (document.fonts) {
      Promise.all([document.fonts.load('700 20px "Cormorant SC"'), document.fonts.load('500 20px "Cormorant SC"')]).then(bump, bump);
      document.fonts.ready.then(bump, bump);
    }
  }

  // ---------- shared caches ----------
  const K = { gen: -1 };
  function caches(env) {
    askFonts();
    if (K.gen !== fontGen) {
      K.gen = fontGen;
      K.base = buildBase(); K.top = buildTop(); K.over = buildOver(); K.glass = buildMagGlass();
      K.eng = RINGS.map((r, i) => buildEngrave(i));
      K.lens = { red: buildLens('red'), green: buildLens('green') };
      K.dome = buildDome();
      K.lid = null;            // rebuilt lazily (blackletter font may arrive later)
      K.compKey = '';
    }
    if (!K.comp) K.comp = mk(W * S);
    if (!K.globe) initGlobe(env);
    return K;
  }

  // ---------- static dial base: case rim, bezel, ring floors ----------
  function buildBase() {
    const c = mk(W * S), g = c.getContext('2d'); g.scale(S, S);
    const R = rng(11);
    // case rim
    g.fillStyle = conicBrass(g, 0.3, 0.16, 0.6); annulus(g, 225, 233);
    ring(g, 225.4, 'rgba(0,0,0,.85)', 1); ring(g, 226.6, 'rgba(255,226,170,.16)', .7); ring(g, 232.4, 'rgba(0,0,0,.6)', 1.2);
    // bezel floor: near-black, faintly brushed
    const bg = g.createRadialGradient(C, C, 197, C, C, 225);
    bg.addColorStop(0, '#0f0c07'); bg.addColorStop(.5, '#0a0805'); bg.addColorStop(1, '#050403');
    g.fillStyle = bg; annulus(g, 197, 225);
    for (let r = 199; r < 225; r += .6) ring(g, r, `rgba(170,130,70,${(0.01 + R() * 0.03).toFixed(3)})`, .35);
    // minute track (skip the lens and magnifier furniture)
    for (let m = 0; m < 60; m++) {
      const a = m * 6 * D2R, ad = Math.abs(((m * 6 + 180) % 360) - 180);
      if (ad < 9 || Math.abs(ad - 30) < 12) continue;
      const big = m % 5 === 0;
      g.strokeStyle = big ? 'rgba(196,160,96,.85)' : 'rgba(150,120,70,.55)'; g.lineWidth = big ? 1.6 : .8;
      g.beginPath(); g.moveTo(px(201.5, a), py(201.5, a)); g.lineTo(px(big ? 210 : 206.5, a), py(big ? 210 : 206.5, a)); g.stroke();
    }
    // hour studs (ball rivets) at 2..10 o'clock; 11/1 are the lenses, 12 the magnifier bridge
    for (let h = 2; h <= 10; h++) {
      const a = h * 30 * D2R, x = px(218, a), y = py(218, a), r = (h % 3 === 0) ? 4.6 : 3.4;
      g.fillStyle = 'rgba(0,0,0,.7)'; g.beginPath(); g.arc(x + .8, y + 1.3, r + 1, 0, TAU); g.fill();
      const sg = g.createRadialGradient(x - r * .4, y - r * .45, r * .1, x, y, r);
      sg.addColorStop(0, brass(.95)); sg.addColorStop(.35, brass(.66)); sg.addColorStop(1, brass(.2));
      g.fillStyle = sg; g.beginPath(); g.arc(x, y, r, 0, TAU); g.fill();
    }
    // inner bezel lip
    g.fillStyle = conicBrass(g, 1.2, .2, .7); annulus(g, 196, 199.5);
    ring(g, 199.3, 'rgba(0,0,0,.8)', .8); ring(g, 196.4, 'rgba(255,230,170,.3)', .6);
    // ring floors (stepped, interlocking; inner rings sit higher)
    RINGS.forEach((rg, i) => {
      g.fillStyle = conicBrass(g, i * 0.5 + 0.2, 0.17, 0.62); annulus(g, rg.r0, rg.r1);
      // turned-finish hairlines
      for (let r = rg.r0 + .5; r < rg.r1; r += .55) ring(g, r, R() < .5 ? `rgba(0,0,0,${(R() * .07).toFixed(3)})` : `rgba(255,230,180,${(R() * .05).toFixed(3)})`, .3);
      const sh = g.createRadialGradient(C, C, rg.r0, C, C, rg.r0 + 5);
      sh.addColorStop(0, 'rgba(0,0,0,.6)'); sh.addColorStop(1, 'rgba(0,0,0,0)');
      g.fillStyle = sh; annulus(g, rg.r0, rg.r0 + 5);
      ring(g, rg.r1 - .6, 'rgba(255,226,160,.30)', .8);
      ring(g, rg.r0 + .35, 'rgba(0,0,0,.95)', .9);
    });
    return c;
  }

  // ---------- engraved ring faces (rotate) ----------
  function engr(g, s, x, y, col) {
    g.fillStyle = 'rgba(255,228,170,.24)'; g.fillText(s, x, y + .75);
    g.fillStyle = col; g.fillText(s, x, y);
  }
  const MONTHS = ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUNE', 'JULY', 'AUG', 'SEPT', 'OCT', 'NOV', 'DEC'];
  function buildEngrave(ri) {
    const rg = RINGS[ri], c = mk(W * S), g = c.getContext('2d');
    g.scale(S, S); g.translate(C, C);
    g.textAlign = 'center'; g.textBaseline = 'middle';
    const n = rg.n, ink = 'rgba(14,9,3,.92)', red = 'rgba(150,26,14,.95)';
    for (let k = 0; k < n; k++) {
      const a = k / n * TAU;
      g.save(); g.rotate(a);
      g.font = rg.font + FD;
      if (rg.key === 'month') engr(g, MONTHS[k], 0, -rg.rt, ink);
      else if (rg.key === 'day') engr(g, String(k), 0, -rg.rt, ink);
      else if (rg.key === 'year') engr(g, String(k).padStart(2, '0'), 0, -rg.rt, ink);
      else {
        if (k === 41) { // ornament slot
          g.fillStyle = ink; g.beginPath(); g.moveTo(0, -192); g.lineTo(3, -187.5); g.lineTo(0, -183); g.lineTo(-3, -187.5); g.closePath(); g.fill();
        } else {
          const bc = k < 20, num = bc ? 19 - k : k - 20;
          engr(g, String(num), 0, -rg.rt, bc ? red : ink);
          g.font = '700 5.4px ' + FD; engr(g, bc ? 'BC' : 'AD', 0, -182.2, bc ? red : ink);
        }
      }
      // slot dividers
      g.rotate(Math.PI / n);
      g.strokeStyle = 'rgba(10,7,3,.75)'; g.lineWidth = rg.key === 'year' ? .5 : .7;
      g.beginPath(); g.moveTo(0, -(rg.r0 + 1.2)); g.lineTo(0, -(rg.r0 + (rg.key === 'year' ? 2.6 : 3.8))); g.stroke();
      g.beginPath(); g.moveTo(0, -(rg.r1 - 1.2)); g.lineTo(0, -(rg.r1 - (rg.key === 'year' ? 2.6 : 3.8))); g.stroke();
      g.restore();
    }
    return c;
  }

  // ---------- lock rings over the hand roots (static) ----------
  function buildTop() {
    const c = mk(W * S), g = c.getContext('2d'); g.scale(S, S);
    // drop shadow of the raised lock ring onto the month ring
    const sh = g.createRadialGradient(C, C, 125, C, C, 132);
    sh.addColorStop(0, 'rgba(0,0,0,.55)'); sh.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = sh; annulus(g, 125, 132);
    // seat
    g.fillStyle = '#060402'; annulus(g, 110, 115);
    // knurled lock ring
    g.fillStyle = conicBrass(g, .9, .16, .66); annulus(g, 114.5, 120.5);
    g.save(); g.translate(C, C);
    for (let i = 0; i < 180; i++) {
      g.rotate(TAU / 180);
      g.strokeStyle = 'rgba(0,0,0,.45)'; g.lineWidth = .6; g.beginPath(); g.moveTo(0, -115); g.lineTo(0, -120); g.stroke();
      g.strokeStyle = 'rgba(255,230,170,.12)'; g.lineWidth = .4; g.beginPath(); g.moveTo(.8, -115); g.lineTo(.8, -120); g.stroke();
    }
    g.restore();
    // outer lock ring
    g.fillStyle = conicBrass(g, 2.1, .24, .78); annulus(g, 120.5, 126.2);
    ring(g, 120.6, 'rgba(0,0,0,.85)', .8); ring(g, 125.7, 'rgba(255,230,170,.35)', .7); ring(g, 126.3, 'rgba(0,0,0,.9)', .7);
    ring(g, 115, 'rgba(255,230,170,.18)', .5);
    // red index chevron: the point of the "V"
    g.fillStyle = 'rgba(176,32,18,1)';
    g.beginPath(); g.moveTo(C - 4.2, C - 125.4); g.lineTo(C + 4.2, C - 125.4); g.lineTo(C, C - 121); g.closePath(); g.fill();
    g.strokeStyle = 'rgba(0,0,0,.6)'; g.lineWidth = .5; g.stroke();
    return c;
  }

  // ---------- bezel furniture: magnifier frame + bridge, lens housings ----------
  function buildOver() {
    const c = mk(W * S), g = c.getContext('2d'); g.scale(S, S);
    // bridge strap from case rim to magnifier
    g.save();
    g.shadowColor = 'rgba(0,0,0,.8)'; g.shadowBlur = 6; g.shadowOffsetY = 3;
    const bx0 = C - 16, bx1 = C + 16;
    const lg = g.createLinearGradient(bx0, 0, bx1, 0);
    lg.addColorStop(0, brass(.25)); lg.addColorStop(.3, brass(.62)); lg.addColorStop(.55, brass(.42)); lg.addColorStop(1, brass(.18));
    g.fillStyle = lg; g.fillRect(bx0, -2, 32, MAG.y0 + 4);
    // magnifier frame
    const fg = g.createLinearGradient(MAG.x0 - 4, 0, MAG.x1 + 4, 0);
    fg.addColorStop(0, brass(.22)); fg.addColorStop(.15, brass(.66)); fg.addColorStop(.5, brass(.4)); fg.addColorStop(.85, brass(.55)); fg.addColorStop(1, brass(.16));
    g.fillStyle = fg; rrect(g, MAG.x0 - 4, MAG.y0 - 4, MAG.x1 - MAG.x0 + 8, MAG.y1 - MAG.y0 + 8, 5); g.fill();
    g.restore();
    // frame bevels + screws on the bridge
    g.strokeStyle = 'rgba(255,230,170,.35)'; g.lineWidth = .7; rrect(g, MAG.x0 - 3.6, MAG.y0 - 3.6, MAG.x1 - MAG.x0 + 7.2, MAG.y1 - MAG.y0 + 7.2, 4.6); g.stroke();
    for (const sx of [C - 9, C + 9]) {
      const sy = 9.5, r = 2.6;
      const sg = g.createRadialGradient(sx - 1, sy - 1, .2, sx, sy, r);
      sg.addColorStop(0, brass(.9)); sg.addColorStop(1, brass(.25));
      g.fillStyle = sg; g.beginPath(); g.arc(sx, sy, r, 0, TAU); g.fill();
      g.strokeStyle = 'rgba(0,0,0,.7)'; g.lineWidth = .7; g.beginPath(); g.moveTo(sx - 1.8, sy + .9); g.lineTo(sx + 1.8, sy - .9); g.stroke();
    }
    // lens housings
    for (const sgn of [-1, 1]) {
      const a = sgn * LENS_A;
      g.save(); g.translate(px(LENS_R, a), py(LENS_R, a)); g.rotate(a);
      g.shadowColor = 'rgba(0,0,0,.8)'; g.shadowBlur = 5; g.shadowOffsetY = 2;
      const hg = g.createLinearGradient(-22, -15, 22, 15);
      hg.addColorStop(0, brass(.62)); hg.addColorStop(.5, brass(.32)); hg.addColorStop(1, brass(.14));
      g.fillStyle = hg; rrect(g, -22, -15, 44, 30, 4); g.fill();
      g.shadowColor = 'transparent';
      g.strokeStyle = 'rgba(255,230,170,.3)'; g.lineWidth = .6; rrect(g, -21.5, -14.5, 43, 29, 3.6); g.stroke();
      g.fillStyle = '#030201'; rrect(g, -18.5, -11.5, 37, 23, 2.5); g.fill();
      g.restore();
    }
    return c;
  }

  function buildMagGlass() {
    const w = MAG.x1 - MAG.x0, h = MAG.y1 - MAG.y0, c = mk(w * S, h * S), g = c.getContext('2d'); g.scale(S, S);
    // edge refraction darkening
    let lg = g.createLinearGradient(0, 0, w, 0);
    lg.addColorStop(0, 'rgba(0,0,0,.55)'); lg.addColorStop(.12, 'rgba(0,0,0,0)'); lg.addColorStop(.88, 'rgba(0,0,0,0)'); lg.addColorStop(1, 'rgba(0,0,0,.6)');
    g.fillStyle = lg; g.fillRect(0, 0, w, h);
    lg = g.createLinearGradient(0, 0, 0, h);
    lg.addColorStop(0, 'rgba(0,0,0,.45)'); lg.addColorStop(.08, 'rgba(0,0,0,0)'); lg.addColorStop(.92, 'rgba(0,0,0,0)'); lg.addColorStop(1, 'rgba(0,0,0,.5)');
    g.fillStyle = lg; g.fillRect(0, 0, w, h);
    // specular streaks
    lg = g.createLinearGradient(0, 0, w, 0);
    lg.addColorStop(.14, 'rgba(255,248,230,0)'); lg.addColorStop(.2, 'rgba(255,248,230,.22)'); lg.addColorStop(.27, 'rgba(255,248,230,0)');
    lg.addColorStop(.8, 'rgba(255,248,230,0)'); lg.addColorStop(.84, 'rgba(255,248,230,.08)'); lg.addColorStop(.87, 'rgba(255,248,230,0)');
    g.fillStyle = lg; g.fillRect(0, 0, w, h);
    const tg = g.createLinearGradient(0, 0, 0, 14);
    tg.addColorStop(0, 'rgba(255,248,230,.18)'); tg.addColorStop(1, 'rgba(255,248,230,0)');
    g.fillStyle = tg; g.fillRect(2, 1, w - 4, 14);
    g.strokeStyle = 'rgba(255,250,235,.25)'; g.lineWidth = .6; rrect(g, .6, .6, w - 1.2, h - 1.2, 3); g.stroke();
    return c;
  }

  // ---------- lenses ----------
  function buildLens(kind) {
    const red = kind === 'red', w = 37, h = 23;
    const make = (lit) => {
      const c = mk((w + 2) * S, (h + 2) * S), g = c.getContext('2d'); g.scale(S, S); g.translate(1, 1);
      rrect(g, 0, 0, w, h, 2.5); g.save(); g.clip();
      const rg = g.createRadialGradient(w / 2, h / 2, 1, w / 2, h / 2, w * .62);
      if (lit) {
        if (red) { rg.addColorStop(0, '#ffd2b8'); rg.addColorStop(.18, '#ff5a33'); rg.addColorStop(.6, '#d01d0c'); rg.addColorStop(1, '#5a0602'); }
        else { rg.addColorStop(0, '#e6ffd8'); rg.addColorStop(.18, '#6dff6a'); rg.addColorStop(.6, '#19c43a'); rg.addColorStop(1, '#03401a'); }
      } else {
        if (red) { rg.addColorStop(0, '#3a0b07'); rg.addColorStop(1, '#130302'); }
        else { rg.addColorStop(0, '#0b2a13'); rg.addColorStop(1, '#030d05'); }
      }
      g.fillStyle = rg; g.fillRect(0, 0, w, h);
      // faceted plastic: diagonal facets
      g.globalAlpha = lit ? .18 : .5;
      for (let i = -3; i < 8; i++) {
        g.fillStyle = i % 2 ? 'rgba(255,255,255,.08)' : 'rgba(0,0,0,.18)';
        g.beginPath(); g.moveTo(i * 6, 0); g.lineTo(i * 6 + 6, 0); g.lineTo(i * 6 - 4, h); g.lineTo(i * 6 - 10, h); g.closePath(); g.fill();
      }
      g.globalAlpha = 1;
      const sg = g.createLinearGradient(0, 0, 0, h);
      sg.addColorStop(0, 'rgba(255,255,255,.28)'); sg.addColorStop(.3, 'rgba(255,255,255,0)');
      g.fillStyle = sg; g.fillRect(0, 0, w, h);
      g.restore();
      g.strokeStyle = 'rgba(0,0,0,.7)'; g.lineWidth = 1; rrect(g, .5, .5, w - 1, h - 1, 2.2); g.stroke();
      return c;
    };
    const bloom = mk(140 * S), bg = bloom.getContext('2d'); bg.scale(S, S);
    const gg = bg.createRadialGradient(70, 70, 4, 70, 70, 70);
    if (red) { gg.addColorStop(0, 'rgba(255,90,50,.75)'); gg.addColorStop(.3, 'rgba(230,40,20,.25)'); gg.addColorStop(1, 'rgba(200,20,10,0)'); }
    else { gg.addColorStop(0, 'rgba(110,255,120,.7)'); gg.addColorStop(.3, 'rgba(40,220,80,.22)'); gg.addColorStop(1, 'rgba(20,180,60,0)'); }
    bg.fillStyle = gg; bg.fillRect(0, 0, 140, 140);
    return { off: make(false), on: make(true), bloom, w, h };
  }
  function drawLens(ctx, kind, level) {
    const L = K.lens[kind], a = (kind === 'red' ? -1 : 1) * LENS_A, x = px(LENS_R, a), y = py(LENS_R, a);
    ctx.save(); ctx.translate(x, y); ctx.rotate(a);
    ctx.drawImage(L.off, -L.w / 2 - 1, -L.h / 2 - 1, L.w + 2, L.h + 2);
    if (level > 0.01) {
      ctx.globalAlpha = clamp(level, 0, 1); ctx.drawImage(L.on, -L.w / 2 - 1, -L.h / 2 - 1, L.w + 2, L.h + 2);
      ctx.globalCompositeOperation = 'lighter'; ctx.globalAlpha = clamp(level, 0, 1) * .55;
      ctx.drawImage(L.bloom, -70, -70, 140, 140);
    }
    ctx.restore();
  }

  // ---------- globe: LUT orthographic projection of the equirectangular map ----------
  function initGlobe(env) {
    const D = GR * 2 * S;
    K.globe = mk(D); K.gctx = K.globe.getContext('2d');
    K.gimg = K.gctx.createImageData(D, D); K.gout = new Uint32Array(K.gimg.data.buffer);
    const img = env.assets && env.assets.earthDay;
    let TW = 512, TH = 256, tex;
    if (img && img.width) {
      TW = img.width; TH = img.height;
      const tc = mk(TW, TH), tg = tc.getContext('2d'); tg.drawImage(img, 0, 0);
      const d = tg.getImageData(0, 0, TW, TH).data;
      // grade toward the prop's painted "map insert": warmer land, deeper ocean, a touch of contrast
      for (let i = 0; i < d.length; i += 4) {
        let r = d[i], gg = d[i + 1], b = d[i + 2];
        const ocean = b > r + 8 && b > gg;
        if (ocean) { r = r * .5 + 4; gg = gg * .78 + 10; b = b * .82 + 12; }
        else { r = r * 1.08 + 10; gg = gg * 1.0 + 4; b = b * .82; }
        d[i] = clamp((r - 128) * 1.08 + 128, 0, 255); d[i + 1] = clamp((gg - 128) * 1.08 + 128, 0, 255); d[i + 2] = clamp((b - 128) * 1.08 + 128, 0, 255);
      }
      tex = new Uint32Array(d.buffer.slice(0));
    } else {
      tex = new Uint32Array(TW * TH).fill(0xff6a3a1a);  // fallback: blue-ish blank (ABGR)
    }
    K.tex = tex; K.TW = TW; K.TH = TH;
    const N = D * D, lutI = new Int32Array(N).fill(-1), lutU = new Float32Array(N), lutR = new Int32Array(N), lutS = new Uint16Array(N), lutA = new Uint8Array(N);
    const rad = D / 2, ct = Math.cos(TILT), st = Math.sin(TILT);
    const Lx = -.42, Ly = .52, Lz = .74;  // light from upper-left-front
    let m = 0;
    const idx = [];
    for (let j = 0; j < D; j++) for (let i = 0; i < D; i++) {
      const x = (i + .5 - rad) / rad, y = (j + .5 - rad) / rad, rr = x * x + y * y, dist = Math.sqrt(rr) * rad;
      if (dist > rad + .5) continue;
      const k = j * D + i, rrc = Math.min(rr, .9999), z = Math.sqrt(1 - rrc), yu = -y;
      const lat = Math.asin(clamp(yu * ct + z * st, -1, 1)), zz = z * ct - yu * st;
      const lon = Math.atan2(x, zz);
      lutU[k] = lon / TAU * TW;
      lutR[k] = clamp(Math.floor((.5 - lat / Math.PI) * TH), 0, TH - 1) * TW;
      const dif = Math.max(0, x * Lx + yu * Ly + z * Lz);
      const shade = (.32 + .78 * dif) * (.62 + .38 * Math.pow(z, .5));
      lutS[k] = Math.round(clamp(shade, 0, 1.15) * 256);
      lutA[k] = Math.round(clamp(rad - dist + .5, 0, 1) * 255);
      idx.push(k); m++;
    }
    K.gidx = Int32Array.from(idx); K.lutU = lutU; K.lutR = lutR; K.lutS = lutS; K.lutA = lutA;
  }
  function renderGlobe(lonC, blurPx, dim) {
    const TW = K.TW, tex = K.tex, out = K.gout, idx = K.gidx, U = K.lutU, Rw = K.lutR, Sh = K.lutS, A = K.lutA;
    let shift = ((lonC + 180) / 360) * TW; shift = ((shift % TW) + TW) % TW;
    const b = blurPx > 1.5 ? Math.min(blurPx, 60) : 0;
    const dm = dim == null ? 256 : Math.round(dim * 256);
    for (let q = 0, n = idx.length; q < n; q++) {
      const k = idx[q];
      let u = U[k] + shift; if (u < 0) u += TW; else if (u >= TW) u -= TW;
      const row = Rw[k];
      let r, g, bb;
      const c0 = tex[row + (u | 0)];
      if (b) {   // 6-tap box blur along the spin direction
        r = c0 & 255; g = (c0 >> 8) & 255; bb = (c0 >> 16) & 255;
        for (let j = 1; j < 6; j++) {
          let uj = u + b * j / 5; if (uj >= TW) uj -= TW;
          const cj = tex[row + (uj | 0)]; r += cj & 255; g += (cj >> 8) & 255; bb += (cj >> 16) & 255;
        }
        r = r / 6 | 0; g = g / 6 | 0; bb = bb / 6 | 0;
      } else { r = c0 & 255; g = (c0 >> 8) & 255; bb = (c0 >> 16) & 255; }
      const s = (Sh[k] * dm) >> 8;
      r = (r * s) >> 8; g = (g * s) >> 8; bb = (bb * s) >> 8;
      if (r > 255) r = 255; if (g > 255) g = 255; if (bb > 255) bb = 255;
      out[k] = (A[k] << 24) | (bb << 16) | (g << 8) | r;
    }
    K.gctx.putImageData(K.gimg, 0, 0);
  }
  function buildDome() {
    const D = GR * 2, c = mk(D * S), g = c.getContext('2d'); g.scale(S, S);
    const r = GR;
    g.translate(r, r);
    g.save(); g.beginPath(); g.arc(0, 0, r, 0, TAU); g.clip();
    // seat shadow + rim refraction
    let rg = g.createRadialGradient(0, 0, r * .78, 0, 0, r);
    rg.addColorStop(0, 'rgba(0,0,0,0)'); rg.addColorStop(.8, 'rgba(0,0,0,.25)'); rg.addColorStop(1, 'rgba(0,0,0,.75)');
    g.fillStyle = rg; g.fillRect(-r, -r, 2 * r, 2 * r);
    // dome specular: soft window reflection, upper-left
    g.save(); g.translate(-r * .36, -r * .42); g.rotate(-.62); g.scale(1, .55);
    rg = g.createRadialGradient(0, 0, 0, 0, 0, r * .46);
    rg.addColorStop(0, 'rgba(255,250,236,.42)'); rg.addColorStop(.45, 'rgba(255,250,236,.14)'); rg.addColorStop(1, 'rgba(255,250,236,0)');
    g.fillStyle = rg; g.beginPath(); g.arc(0, 0, r * .46, 0, TAU); g.fill();
    g.restore();
    rg = g.createRadialGradient(-r * .42, -r * .46, 0, -r * .42, -r * .46, r * .09);
    rg.addColorStop(0, 'rgba(255,255,250,.9)'); rg.addColorStop(1, 'rgba(255,255,250,0)');
    g.fillStyle = rg; g.beginPath(); g.arc(-r * .42, -r * .46, r * .09, 0, TAU); g.fill();
    // lower-right caustic arc
    g.strokeStyle = 'rgba(255,236,200,.16)'; g.lineWidth = 3;
    g.beginPath(); g.arc(0, 0, r - 6, .05 * Math.PI, .48 * Math.PI); g.stroke();
    g.strokeStyle = 'rgba(255,250,240,.10)'; g.lineWidth = 1.2;
    g.beginPath(); g.arc(0, 0, r - 2.5, 1.05 * Math.PI, 1.42 * Math.PI); g.stroke();
    // prop reticle etched on the glass
    g.strokeStyle = 'rgba(12,10,8,.36)'; g.lineWidth = .8;
    g.beginPath(); g.moveTo(-r, 0); g.lineTo(-11, 0); g.moveTo(11, 0); g.lineTo(r, 0); g.moveTo(0, -r); g.lineTo(0, -11); g.moveTo(0, 11); g.lineTo(0, r); g.stroke();
    g.beginPath(); g.arc(0, 0, 11, 0, TAU); g.stroke();
    g.strokeStyle = 'rgba(255,240,220,.10)'; g.lineWidth = .5;
    g.beginPath(); g.moveTo(-r, .9); g.lineTo(-11, .9); g.moveTo(11, .9); g.lineTo(r, .9); g.stroke();
    g.restore();
    return c;
  }
  function project(lat, lon, lonC) {
    const la = lat * D2R, dl = (lon - lonC) * D2R;
    const x = Math.cos(la) * Math.sin(dl), y = Math.sin(la), z = Math.cos(la) * Math.cos(dl);
    const ct = Math.cos(TILT), st = Math.sin(TILT);
    const yv = y * ct - z * st, zv = y * st + z * ct;
    return { x: C + x * GR, y: C - yv * GR, z: zv };
  }

  // ---------- hands: annular, blackened steel, pinned beneath the dome ----------
  function hand(g, a, r0, r1, w, hour) {
    g.save(); g.translate(C, C); g.rotate(a);
    const tipL = hour ? 16 : 12;
    const shape = () => {
      g.beginPath();
      if (hour) { g.moveTo(-w * .32, -r0); g.lineTo(-w / 2, -(r1 - tipL)); g.lineTo(0, -r1); g.lineTo(w / 2, -(r1 - tipL)); g.lineTo(w * .32, -r0); }
      else { g.moveTo(-w / 2, -r0); g.lineTo(-w * .38, -(r1 - tipL)); g.lineTo(0, -r1); g.lineTo(w * .38, -(r1 - tipL)); g.lineTo(w / 2, -r0); }
      g.closePath();
    };
    // shadow (screen-space offset)
    const sx = 1.4, sy = 2.6, ca = Math.cos(-a), sa = Math.sin(-a);
    g.save(); g.translate(sx * ca - sy * sa, sx * sa + sy * ca);
    g.fillStyle = 'rgba(0,0,0,.55)'; shape(); g.fill(); g.restore();
    // two facets
    shape(); g.save(); g.clip();
    const lit = 0.5 + 0.5 * Math.cos(a + 45 * D2R);
    g.fillStyle = `rgb(${34 + lit * 30 | 0},${30 + lit * 26 | 0},${26 + lit * 20 | 0})`; g.fillRect(-w, -r1 - 2, w, r1);
    g.fillStyle = `rgb(${14 + (1 - lit) * 16 | 0},${12 + (1 - lit) * 14 | 0},${10 + (1 - lit) * 10 | 0})`; g.fillRect(0, -r1 - 2, w, r1);
    // red enamel tip
    g.fillStyle = '#b42a17';
    if (hour) { g.beginPath(); g.moveTo(0, -r1); g.lineTo(-w * .3, -(r1 - 9)); g.lineTo(0, -(r1 - 22)); g.lineTo(w * .3, -(r1 - 9)); g.closePath(); g.fill(); }
    else { g.fillRect(-w, -r1 - 2, 2 * w, 11); }
    g.restore();
    // polished bevel edges
    g.strokeStyle = 'rgba(214,180,118,.85)'; g.lineWidth = .7; shape(); g.stroke();
    g.strokeStyle = 'rgba(255,236,190,.35)'; g.lineWidth = .5;
    g.beginPath(); g.moveTo(0, -r0); g.lineTo(0, -r1 + 1); g.stroke();
    g.restore();
  }

  // ---------- composite of rings + marks + hands (cached when still) ----------
  function drawRot(g, img, rot) { g.save(); g.translate(C, C); g.rotate(rot); g.drawImage(img, -C, -C, W, W); g.restore(); }
  function renderComp(m) {
    const key = m.vals.map(v => v.toFixed(3)).join(',') + '|' + m.hA.toFixed(3) + ',' + m.mA.toFixed(3) + '|' + m.markOld.toFixed(2) + m.markNew.toFixed(2) + '|' + m.selOld.join() + m.selNew.join() + (m.ringBlur ? 'b' : '');
    if (key === K.compKey) return;
    K.compKey = key;
    const g = K.comp.getContext('2d');
    g.setTransform(1, 0, 0, 1, 0, 0); g.clearRect(0, 0, W * S, W * S);
    g.drawImage(K.base, 0, 0);
    g.setTransform(S, 0, 0, S, 0, 0);
    RINGS.forEach((rg, i) => {
      const rot = -m.vals[i] / rg.n * TAU;
      const dv = m.ringBlur ? m.ringBlur[i] / rg.n * TAU : 0;
      if (Math.abs(dv) > .01) {
        g.globalAlpha = .5; drawRot(g, K.eng[i], rot + dv * .66);
        g.globalAlpha = .7; drawRot(g, K.eng[i], rot + dv * .33);
        g.globalAlpha = .8; drawRot(g, K.eng[i], rot); g.globalAlpha = 1;
      } else drawRot(g, K.eng[i], rot);
      // red "V" marks: a pair per ring flanking the set value
      const rc = (rg.r0 + rg.r1) / 2;
      for (const [sel, al] of [[m.selOld[i], m.markOld], [m.selNew[i], m.markNew]]) {
        if (al < .02) continue;
        const a = (sel - m.vals[i]) / rg.n * TAU, da = VD[i] / rc;
        g.globalAlpha = al;
        for (const sgn of [-1, 1]) {
          g.save(); g.translate(C, C); g.rotate(a + sgn * da);
          g.fillStyle = 'rgba(255,200,170,.25)'; g.fillRect(-1.1, -(rg.r1 - 2.6) + .7, 2.2, rg.r1 - rg.r0 - 5);
          g.fillStyle = '#b8261a'; g.fillRect(-1.1, -(rg.r1 - 2.6), 2.2, rg.r1 - rg.r0 - 5);
          g.restore();
        }
        g.globalAlpha = 1;
      }
    });
    hand(g, m.hA, 118, 178, 11, true);
    hand(g, m.mA, 118, 214, 5.2, false);
  }

  // ---------- the full dial ----------
  function drawDial(ctx, m) {
    renderComp(m);
    ctx.drawImage(K.comp, 0, 0, W, W);
    ctx.drawImage(K.top, 0, 0, W, W);
    renderGlobe(m.lon, m.globeBlur, m.globeDim);
    ctx.drawImage(K.globe, C - GR, C - GR, 2 * GR, 2 * GR);
    if (m.marker) {
      const p = project(m.marker.lat, m.marker.lon, m.lon);
      if (p.z > .15) {
        const al = clamp((p.z - .15) * 3, 0, 1) * m.marker.a;
        ctx.save(); ctx.globalAlpha = al;
        ctx.strokeStyle = 'rgba(255,70,40,.95)'; ctx.lineWidth = 1.4;
        ctx.beginPath(); ctx.arc(p.x, p.y, 5 + (m.marker.pulse || 0) * 4, 0, TAU); ctx.stroke();
        ctx.fillStyle = '#ff4a2a'; ctx.beginPath(); ctx.arc(p.x, p.y, 1.8, 0, TAU); ctx.fill();
        ctx.restore();
      }
    }
    ctx.drawImage(K.dome, C - GR, C - GR, 2 * GR, 2 * GR);
    ctx.drawImage(K.over, 0, 0, W, W);
    // magnifier
    const w = MAG.x1 - MAG.x0, h = MAG.y1 - MAG.y0, k = MAG.k;
    const sx = C + (MAG.x0 - C) / k, sy = MAG.cy + (MAG.y0 - MAG.cy) / k;
    ctx.save(); rrect(ctx, MAG.x0, MAG.y0, w, h, 3); ctx.clip();
    ctx.fillStyle = '#000'; ctx.fillRect(MAG.x0, MAG.y0, w, h);
    ctx.drawImage(K.comp, sx * S, sy * S, w / k * S, h / k * S, MAG.x0, MAG.y0, w, h);
    ctx.globalCompositeOperation = 'lighter'; ctx.fillStyle = 'rgba(40,32,18,.35)'; ctx.fillRect(MAG.x0, MAG.y0, w, h);
    ctx.globalCompositeOperation = 'source-over';
    ctx.drawImage(K.glass, MAG.x0, MAG.y0, w, h);
    ctx.restore();
    drawLens(ctx, 'red', m.red); drawLens(ctx, 'green', m.green);
    if (m.caption && m.caption.a > .01) {
      ctx.save(); ctx.globalAlpha = m.caption.a;
      ctx.font = '700 13px ' + FD; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      const tw = ctx.measureText(m.caption.text).width;
      ctx.fillStyle = 'rgba(0,0,0,.55)'; rrect(ctx, C - tw / 2 - 10, C + 66, tw + 20, 20, 10); ctx.fill();
      ctx.fillStyle = '#e9d7ae'; ctx.fillText(m.caption.text, C, C + 76.5);
      ctx.restore();
    }
    if (m.loupe > .01) drawLoupe(ctx, m.loupe);
    if (m.flash > .005) {
      ctx.save(); ctx.globalCompositeOperation = 'lighter';
      const fg = ctx.createRadialGradient(C, C, 0, C, C, 260);
      fg.addColorStop(0, `rgba(255,250,235,${Math.min(1, m.flash * 1.1)})`); fg.addColorStop(.45, `rgba(255,220,170,${m.flash * .8})`); fg.addColorStop(1, `rgba(255,160,90,${m.flash * .4})`);
      ctx.fillStyle = fg; ctx.fillRect(0, 0, W, W); ctx.restore();
    }
  }

  // tap-the-magnifier loupe: the date column, big enough to actually read
  function drawLoupe(ctx, a) {
    const e = E.outCubic(a), k = 2.35, lw = 74, lh = 176;
    const cx = C, cy = lerp(MAG.cy, C + 6, e), w = lerp(MAG.x1 - MAG.x0, lw, e), h = lerp(MAG.y1 - MAG.y0, lh, e);
    const x0 = cx - w / 2, y0 = cy - h / 2;
    const srcC = C - 162, sw = w / k, sh = h / k;
    ctx.save();
    ctx.globalAlpha = clamp(a * 2, 0, 1);
    ctx.shadowColor = 'rgba(0,0,0,.9)'; ctx.shadowBlur = 18; ctx.shadowOffsetY = 6;
    const fg = ctx.createLinearGradient(x0, 0, x0 + w, 0);
    fg.addColorStop(0, brass(.2)); fg.addColorStop(.2, brass(.6)); fg.addColorStop(.6, brass(.36)); fg.addColorStop(1, brass(.14));
    ctx.fillStyle = fg; rrect(ctx, x0 - 5, y0 - 5, w + 10, h + 10, 8); ctx.fill();
    ctx.shadowColor = 'transparent';
    rrect(ctx, x0, y0, w, h, 5); ctx.clip();
    ctx.fillStyle = '#000'; ctx.fillRect(x0, y0, w, h);
    ctx.drawImage(K.comp, (C - sw / 2) * S, (srcC - sh / 2) * S, sw * S, sh * S, x0, y0, w, h);
    ctx.globalCompositeOperation = 'lighter'; ctx.fillStyle = 'rgba(46,36,20,.4)'; ctx.fillRect(x0, y0, w, h);
    ctx.globalCompositeOperation = 'source-over';
    ctx.drawImage(K.glass, x0, y0, w, h);
    ctx.restore();
  }

  // ---------- date <-> ring values ----------
  function dateVals(Y, M, D) {  // Y: astronomical-ish, negative = BC (no year zero)
    let cent, yy;
    if (Y > 0) { cent = 20 + Math.floor(Y / 100); yy = Y % 100; }
    else { const B = -Y; cent = 19 - Math.floor(B / 100); yy = B % 100; }
    return [M - 1, D, yy, clamp(cent, 0, 40)];
  }
  const nowVals = env => { const d = env.now(); return dateVals(d.getFullYear(), d.getMonth() + 1, d.getDate()); };
  function handAngles(env) {
    const d = env.now(), mins = d.getMinutes() + d.getSeconds() / 60, hrs = (d.getHours() % 12) + mins / 60;
    return [hrs / 12 * TAU, Math.round(mins / 60 * TAU * 720) / 720];  // minute hand quantised to 0.5deg
  }
  const DEST = [
    { name: 'Philadelphia', Y: 1776, M: 7, D: 4, lat: 39.95, lon: -75.17 },
    { name: 'Menlo Park', Y: 1879, M: 10, D: 22, lat: 40.56, lon: -74.34 },
    { name: 'Rome', Y: -44, M: 3, D: 15, lat: 41.89, lon: 12.49 },
    { name: 'Kitty Hawk', Y: 1903, M: 12, D: 17, lat: 36.02, lon: -75.67 },
    { name: 'Hastings', Y: 1066, M: 10, D: 14, lat: 50.91, lon: .49 },
    { name: 'Alexandria', Y: -48, M: 8, D: 9, lat: 31.2, lon: 29.92 },
    { name: 'North Atlantic', Y: 1912, M: 4, D: 14, lat: 41.73, lon: -49.95 },
  ];
  function wrap180(d) { d = ((d + 180) % 360 + 360) % 360 - 180; return d; }

  // spin-up profile: from rest, fast, long flywheel tail; returns remaining offset (deg)
  function spinOff(t, t0, dur, Kdeg) {
    if (t <= t0) return Kdeg;
    const s = clamp((t - t0) / dur, 0, 1);
    return Kdeg * Math.pow(1 - Math.pow(s, 1.55), 3);
  }

  // ---------- the living dial state machine (shared by all three entries) ----------
  function makeDial(opts) {
    const st = {};
    function reset(env) {
      st.lastT = 0; st.travel = null; st.away = null; st.awayT = 0; st.loupeT = -1e9; st.prevLon = null; st.prevVals = null;
      st.destIx = 0; st.spinT0 = opts.spinT0; st.noSpin = false; st.homeLon = env.location ? env.location.lon : -83;
    }
    function model(t, env, rm) {
      const home = nowVals(env), [hA0, mA0] = handAngles(env);
      const m = { vals: home.slice(), hA: hA0, mA: mA0, markOld: 1, markNew: 0, selOld: home, selNew: home, red: 0, green: 0, flash: 0, globeBlur: 0, loupe: 0, ringBlur: null };
      const spinK = rm ? 0 : opts.spinK, spinDur = opts.spinDur;
      // base orientation with spin-up and drift
      const drift = rm ? 0 : DRIFT;
      let lon;
      const baseLon = st.away ? st.away.lon : st.homeLon;
      const driftT = st.away ? st.awayT : st.spinT0;
      lon = baseLon + spinOff(t, st.spinT0, spinDur, (st.away || st.noSpin) ? 0 : spinK) - drift * Math.max(0, t - driftT) / 1000;
      if (st.away) { const v = dateVals(st.away.Y, st.away.M, st.away.D); m.vals = v.slice(); m.selOld = v; m.selNew = v; }
      // wake: rings click into today's date
      if (opts.ringSettle && !st.away) {
        const s = ph(t, opts.ringSettle[0], opts.ringSettle[1]);
        if (s < 1) m.vals = m.vals.map((v, i) => v - [1.4, -2.2, 3.5, -1.2][i] * (1 - E.outBack(s)) * (rm ? 0 : 1));
      }
      // lights at rest: green solid once the globe settles (home or arrived)
      m.green = st.away ? 1 : ph(t, opts.greenAt, opts.greenAt + 350);
      m.marker = { lat: st.away ? st.away.lat : (env.location ? env.location.lat : 40), lon: baseLon, a: .9, pulse: st.away ? .5 + .5 * Math.sin(t / 300) : 0 };
      // travel overrides
      const tr = st.travel;
      if (tr) {
        const tau = t - tr.t0;
        const fromV = tr.fromVals, toV = tr.toVals;
        const turns = [2, -3, 3, -1];
        const vals = [], blur = [];
        for (let i = 0; i < 4; i++) {
          const a0 = 380 + i * 80, a1 = (rm ? 900 : 1900) + i * 140;
          const s = ph(tau, a0, a1), e = E.inOutQuart(s);
          const total = (toV[i] - fromV[i]) + (rm ? 0 : turns[i] * RINGS[i].n);
          let v = fromV[i] + total * e;
          if (tau < a0 && !rm) v += Math.sin(tau / 22 + i) * .08 * ph(tau, 0, 200);         // unlock jitter
          if (tau > a1 && !rm) v += .22 * Math.sin((tau - a1) / 55) * Math.exp(-(tau - a1) / 110) * Math.sign(total || 1); // detent click
          vals.push(v);
          const s2 = ph(tau - 16, a0, a1); blur.push(total * (e - E.inOutQuart(s2)));
        }
        m.vals = vals;
        m.ringBlur = rm ? null : blur.map((b, i) => -clamp(b, -RINGS[i].n / 6, RINGS[i].n / 6));
        m.selOld = tr.fromVals; m.selNew = toV;
        m.markOld = 1 - ph(tau, 300, 650); m.markNew = ph(tau, 1750, 2500);
        // hands rewind a few turns and come back to the true time
        const hs = E.inOutCubic(ph(tau, 380, 2250));
        const sw = rm ? 0 : 4 * hs * (1 - hs);   // 0 -> 1 -> 0: rewind and come back
        m.mA = mA0 - 1.5 * TAU * sw;
        m.hA = hA0 - .25 * TAU * sw;
        // globe: hard spin to the destination
        const g0 = 250, g1 = rm ? 1200 : 2650;
        const lonAt = (tt) => {
          const s = ph(tt, g0, g1), e = E.inOutCubic(s);
          let d = wrap180(tr.toLon - tr.fromLon); if (d > 0) d -= 360;   // always spin eastward
          const total = d - (rm ? 0 : 3 * 360);
          const lonT = tr.fromLon + total * e;
          return tt < g0 ? tr.fromLon - DRIFT * tt / 1000 : lonT;
        };
        lon = tau > g1 ? tr.toLon - (rm ? 0 : DRIFT) * (tau - g1) / 1000 : lonAt(tau);
        const lonPrev = tau - 16 > g1 ? lon : lonAt(tau - 16);
        m.globeBlur = rm ? 0 : Math.abs(wrap180(lon - lonPrev)) / 360 * K.TW * S * .5;
        // lights: red blinks while "unstable", flash, then green
        const blink = (Math.floor(tau / 140) % 2) ? 1 : .15;
        m.red = tau < 2230 ? blink * (tau < 200 ? ph(tau, 0, 60) : 1) : Math.max(0, 1 - (tau - 2230) / 150);
        m.green = tau < 2700 ? Math.max(0, 1 - tau / 200) : ph(tau, 2850, 3150);
        m.flash = rm ? 0 : (tau < 2040 ? 0 : tau < 2230 ? Math.pow((tau - 2040) / 190, 2) : Math.exp(-(tau - 2230) / 210));
        m.flash *= .92;
        m.marker = { lat: tr.dest.lat, lon: tr.toLon, a: ph(tau, 2700, 3000), pulse: .5 + .5 * Math.sin(t / 300) };
        if (tau > 3300) { // land
          st.travel = null;
          st.awayT = t - (tau - g1);
          if (tr.dest.home) { st.away = null; st.noSpin = true; st.spinT0 = st.awayT; st.homeLon = tr.toLon; }
          else st.away = { ...tr.dest, lon: tr.toLon };
        }
        const capA = tr.dest.home ? 0 : ph(tau, 3000, 3300);
        if (capA > 0) m.caption = { text: tr.dest.name.toUpperCase(), a: capA };
      } else if (st.away) {
        m.caption = { text: st.away.name.toUpperCase(), a: 1 - ph(t - st.awayT, 9000, 9600) };
        if (t - st.awayT > 12000) startTravel(t, env, true);   // the Omni won't let you stay: auto-return
      }
      m.lon = lon;
      // loupe
      const lt = t - st.loupeT;
      m.loupe = lt < 0 ? 0 : lt < 300 ? lt / 300 : lt < 3600 ? 1 : Math.max(0, 1 - (lt - 3600) / 300);
      return m;
    }
    function currentLon(t, env) { return st.lastLon == null ? st.homeLon : st.lastLon; }
    function startTravel(t, env, toHome) {
      const fromVals = st.lastVals || nowVals(env);
      let dest;
      if (toHome) {
        dest = { home: true, name: env.location ? env.location.name : 'Home', lat: env.location ? env.location.lat : 40 };
        st.travel = { t0: t, fromVals, toVals: nowVals(env), fromLon: currentLon(t, env), toLon: env.location ? env.location.lon : -83, dest };
      } else {
        dest = DEST[st.destIx % DEST.length]; st.destIx++;
        st.travel = { t0: t, fromVals, toVals: dateVals(dest.Y, dest.M, dest.D), fromLon: currentLon(t, env), toLon: dest.lon, dest };
      }
      st.away = null;
    }
    return {
      st, reset,
      frame(ctx, t, env) {
        caches(env);
        st.lastT = t;
        const m = model(t, env, env.reducedMotion);
        st.lastLon = m.lon; st.lastVals = m.vals.slice();
        drawDial(ctx, m);
        return m;
      },
      tap(x, y, env, revealDone) {
        const t = st.lastT;
        const hit = (a) => Math.hypot(x - px(LENS_R, a), y - py(LENS_R, a)) < 38;
        if (st.travel) return;
        if (hit(-LENS_A)) { if (!revealDone) return; startTravel(t, env, !!st.away); return; }
        if (hit(LENS_A) && st.away) { startTravel(t, env, true); return; }
        if (x > MAG.x0 - 12 && x < MAG.x1 + 12 && y > MAG.y0 - 10 && y < MAG.y1 + 8) { st.loupeT = (t - st.loupeT < 3600) ? t - 3600 : t; return; }
        if (t - st.loupeT < 3900 && Math.abs(x - C) < 50 && Math.abs(y - C) < 100) { st.loupeT = t - 3600; }
      },
    };
  }

  // ---------- iris ----------
  const IRIS_N = 9, IRIS_R = 199;
  let bladeTex = null;
  function getBladeTex() {
    if (bladeTex) return bladeTex;
    const c = mk(256, 512), g = c.getContext('2d'), R = rng(5);
    for (let x = 0; x < 256; x += .7) {
      g.fillStyle = R() < .5 ? `rgba(255,232,180,${(R() * .07).toFixed(3)})` : `rgba(0,0,0,${(R() * .12).toFixed(3)})`;
      g.fillRect(x, 0, .6, 512);
    }
    return (bladeTex = c);
  }
  function drawIris(ctx, ap, phi) {
    // ap: aperture inradius (px), phi: blade rotation (rad)
    if (ap >= IRIS_R + 4) return;
    const N = IRIS_N, half = Math.PI / N, vr = ap / Math.cos(half);
    const alpha = i => i * TAU / N + phi;
    const V = i => { const a = alpha(i) - half; return [C + vr * Math.cos(a), C + vr * Math.sin(a)]; };
    const curve = (i, from) => {        // edge i continued as a gently curving ray from vertex `from`
      const a = alpha(i), t = [Math.cos(a + Math.PI / 2), Math.sin(a + Math.PI / 2)], n = [Math.cos(a), Math.sin(a)];
      const pts = [];
      for (let k = 0; k <= 8; k++) { const s = k * 60; pts.push([from[0] + t[0] * s + n[0] * s * s / 900, from[1] + t[1] * s + n[1] * s * s / 900]); }
      return pts;
    };
    const region = i => {
      const v0 = V(i), v1 = V(i + 1), rayPrev = curve(i - 1, v0), ray = curve(i, v1);
      const p = [v0, v1].concat(ray.slice(1));
      for (let k = rayPrev.length - 1; k >= 1; k--) p.push(rayPrev[k]);
      return p;
    };
    const path = (p) => { ctx.beginPath(); ctx.moveTo(p[0][0], p[0][1]); for (let k = 1; k < p.length; k++) ctx.lineTo(p[k][0], p[k][1]); ctx.closePath(); };
    ctx.save();
    ctx.beginPath(); ctx.arc(C, C, IRIS_R, 0, TAU); ctx.clip();
    // blades' shadow onto the revealed dial
    if (ap > 1) {
      ctx.save();
      ctx.beginPath(); for (let i = 0; i < N; i++) { const v = V(i); i ? ctx.lineTo(v[0], v[1]) : ctx.moveTo(v[0], v[1]); } ctx.closePath();
      ctx.strokeStyle = 'rgba(0,0,0,.55)'; ctx.lineWidth = 26; ctx.stroke();
      ctx.strokeStyle = 'rgba(0,0,0,.5)'; ctx.lineWidth = 10; ctx.stroke();
      ctx.restore();
    }
    const tex = getBladeTex();
    const lightA = -135 * D2R;
    for (let i = 0; i < N; i++) {
      const a = alpha(i), reg = region(i);
      const lum = .3 + .17 * Math.cos(a - lightA);
      path(reg);
      const n = [Math.cos(a), Math.sin(a)], v0 = V(i);
      const lg = ctx.createLinearGradient(v0[0], v0[1], v0[0] + n[0] * 230, v0[1] + n[1] * 230);
      lg.addColorStop(0, brass(lum + .12)); lg.addColorStop(.25, brass(lum)); lg.addColorStop(1, brass(lum - .14));
      ctx.fillStyle = lg; ctx.fill();
      // brushed grain, blade-local
      ctx.save(); path(reg); ctx.clip();
      ctx.translate(v0[0], v0[1]); ctx.rotate(a);
      ctx.drawImage(tex, 0, -40, 260, 520);
      ctx.restore();
    }
    // overlap shadows + polished edges
    for (let i = 0; i < N; i++) {
      const v1 = V(i + 1), ray = curve(i, v1), seg = [V(i), v1].concat(ray.slice(1));
      ctx.save(); path(region(i + 1)); ctx.clip();
      ctx.beginPath(); ctx.moveTo(ray[0][0], ray[0][1]); for (const p of ray) ctx.lineTo(p[0], p[1]);
      ctx.strokeStyle = 'rgba(0,0,0,.35)'; ctx.lineWidth = 16; ctx.stroke();
      ctx.strokeStyle = 'rgba(0,0,0,.45)'; ctx.lineWidth = 6; ctx.stroke();
      ctx.restore();
      ctx.beginPath(); ctx.moveTo(seg[0][0], seg[0][1]); for (const p of seg) ctx.lineTo(p[0], p[1]);
      const lit = .5 + .5 * Math.cos(alpha(i) - lightA);
      ctx.strokeStyle = `rgba(255,228,170,${(.25 + .45 * lit).toFixed(3)})`; ctx.lineWidth = 1.1; ctx.stroke();
    }
    // closed-centre glint
    if (ap < 30) {
      const gl = ctx.createRadialGradient(C - 8, C - 10, 0, C, C, 40);
      gl.addColorStop(0, `rgba(255,236,190,${(.22 * (1 - ap / 30)).toFixed(3)})`); gl.addColorStop(1, 'rgba(255,236,190,0)');
      ctx.fillStyle = gl; ctx.fillRect(C - 40, C - 40, 80, 80);
    }
    ctx.restore();
  }

  // ---------- lid ----------
  function vPath(g) {
    g.beginPath();
    g.moveTo(4, 16); g.bezierCurveTo(12, 8, 26, 5, 42, 7); g.lineTo(47, 8);
    g.lineTo(57, 70); g.lineTo(73, 25); g.bezierCurveTo(74, 16, 80, 10, 90, 6); g.lineTo(97, 6);
    g.bezierCurveTo(89, 12, 85, 18, 83, 25); g.lineTo(59, 95); g.lineTo(51, 95);
    g.lineTo(27, 22); g.bezierCurveTo(25, 17, 17, 15, 4, 16); g.closePath();
  }
  function buildLid() {
    const c = mk(W * S), g = c.getContext('2d'); g.scale(S, S);
    const R = rng(23), LR = 226;
    g.save(); g.beginPath(); g.arc(C, C, LR, 0, TAU); g.clip();
    // dome body
    let rg = g.createRadialGradient(C - 80, C - 95, 10, C, C, LR * 1.08);
    rg.addColorStop(0, brass(.8)); rg.addColorStop(.3, brass(.6)); rg.addColorStop(.7, brass(.4)); rg.addColorStop(1, brass(.16));
    g.fillStyle = rg; g.fillRect(0, 0, W, W);
    // broad reflection band (polished dome)
    g.save(); g.translate(C + 40, C + 70); g.rotate(-.5); g.scale(1, .35);
    rg = g.createRadialGradient(0, 0, 0, 0, 0, 170);
    rg.addColorStop(0, 'rgba(255,226,160,.22)'); rg.addColorStop(1, 'rgba(255,226,160,0)');
    g.fillStyle = rg; g.beginPath(); g.arc(0, 0, 170, 0, TAU); g.fill(); g.restore();
    // tarnish blotches
    for (let i = 0; i < 26; i++) {
      const x = R() * W, y = R() * W, r = 20 + R() * 60;
      rg = g.createRadialGradient(x, y, 0, x, y, r);
      rg.addColorStop(0, `rgba(30,24,8,${(.06 + R() * .1).toFixed(3)})`); rg.addColorStop(1, 'rgba(30,24,8,0)');
      g.fillStyle = rg; g.fillRect(x - r, y - r, 2 * r, 2 * r);
    }
    // hammered dimples
    for (let i = 0; i < 1100; i++) {
      const a = R() * TAU, rr = Math.sqrt(R()) * (LR - 8), x = C + rr * Math.cos(a), y = C + rr * Math.sin(a), r = 3 + R() * 6;
      g.fillStyle = 'rgba(0,0,0,.028)'; g.beginPath(); g.arc(x + r * .25, y + r * .3, r, 0, TAU); g.fill();
      g.fillStyle = 'rgba(255,232,180,.026)'; g.beginPath(); g.arc(x - r * .25, y - r * .3, r * .8, 0, TAU); g.fill();
    }
    // rim
    g.fillStyle = conicBrass(g, .4, .2, .62); annulus(g, 218, LR);
    ring(g, 218, 'rgba(0,0,0,.6)', 1.2); ring(g, 219.2, 'rgba(255,230,170,.25)', .6); ring(g, LR - .6, 'rgba(0,0,0,.8)', 1.2);
    // diamonds across the top edge, point to point
    const nD = 12, span = 132 * D2R, w = span / nD;
    for (let k = 0; k < nD; k++) {
      const ac = -span / 2 + w * (k + .5), ri = 199, ro = 214, rm = (ri + ro) / 2;
      const P = [[rm, ac - w / 2], [ro, ac], [rm, ac + w / 2], [ri, ac]];
      const pathD = (dx, dy) => { g.beginPath(); P.forEach(([r, a], j) => { const X = px(r, a) + dx, Y = py(r, a) + dy; j ? g.lineTo(X, Y) : g.moveTo(X, Y); }); g.closePath(); };
      pathD(0, 0); g.fillStyle = 'rgba(20,14,5,.28)'; g.fill();
      pathD(0, .8); g.strokeStyle = 'rgba(255,230,170,.22)'; g.lineWidth = 1; g.stroke();
      pathD(0, 0); g.strokeStyle = 'rgba(16,10,3,.85)'; g.lineWidth = 1.3; g.stroke();
    }
    // blackletter motto along the lower edge, tops toward the centre
    const text = 'Time Waits For No Man';
    g.font = '46px ' + FB; g.textAlign = 'center'; g.textBaseline = 'alphabetic';
    const ws = [...text].map(ch => g.measureText(ch).width), total = ws.reduce((a, b) => a + b, 0);
    const rb = 213, targetSpan = 150 * D2R, spacing = (targetSpan * rb - total) / (text.length - 1);
    let acc = 0;
    [...text].forEach((ch, j) => {
      const s = acc + ws[j] / 2; acc += ws[j] + spacing;
      const a = Math.PI + targetSpan / 2 - s / rb;     // left side first, sweeping through 6 o'clock
      g.save(); g.translate(px(rb, a), py(rb, a)); g.rotate(a - Math.PI);
      g.fillStyle = 'rgba(255,232,176,.28)'; g.fillText(ch, 0, 1);
      g.fillStyle = 'rgba(18,11,3,.9)'; g.fillText(ch, 0, 0);
      g.restore();
    });
    // the hammered V (raised, stippled)
    g.save(); g.translate(C - 78, C - 86); g.scale(1.56, 1.56);
    g.save(); g.translate(2, 3); g.filter = 'blur(3px)'; vPath(g); g.fillStyle = 'rgba(0,0,0,.55)'; g.fill(); g.restore();
    vPath(g); g.save(); g.clip();
    g.fillStyle = brass(.42); g.fillRect(0, 0, 100, 100);
    for (let i = 0; i < 2600; i++) {
      const x = R() * 100, y = R() * 100;
      g.fillStyle = R() < .5 ? 'rgba(0,0,0,.35)' : 'rgba(255,226,160,.25)';
      g.fillRect(x, y, .55, .55);
    }
    g.lineWidth = 1.6;
    g.save(); g.translate(1.1, 1.1); vPath(g); g.strokeStyle = 'rgba(255,232,180,.55)'; g.stroke(); g.restore();
    g.save(); g.translate(-1.1, -1.1); vPath(g); g.strokeStyle = 'rgba(0,0,0,.6)'; g.stroke(); g.restore();
    g.restore();
    vPath(g); g.strokeStyle = 'rgba(20,12,4,.6)'; g.lineWidth = .5; g.stroke();
    g.restore();
    g.restore();
    // hinge knuckle at 12
    const kg = g.createLinearGradient(0, 0, 0, 12);
    kg.addColorStop(0, brass(.2)); kg.addColorStop(.45, brass(.7)); kg.addColorStop(1, brass(.15));
    g.fillStyle = kg; rrect(g, C - 34, -2, 68, 12, 4); g.fill();
    g.strokeStyle = 'rgba(0,0,0,.6)'; g.lineWidth = .8; g.beginPath(); g.moveTo(C - 11, 0); g.lineTo(C - 11, 10); g.moveTo(C + 11, 0); g.lineTo(C + 11, 10); g.stroke();
    // derived masks for strip shading
    const dark = mk(W * S), dg = dark.getContext('2d'); dg.drawImage(c, 0, 0); dg.globalCompositeOperation = 'source-in'; dg.fillStyle = '#000'; dg.fillRect(0, 0, W * S, W * S);
    const spec = mk(W * S), sg = spec.getContext('2d'); sg.drawImage(c, 0, 0); sg.globalCompositeOperation = 'source-in';
    const hg = sg.createLinearGradient(0, 0, W * S, 0);
    hg.addColorStop(0, 'rgba(255,236,196,.15)'); hg.addColorStop(.3, 'rgba(255,240,205,.8)'); hg.addColorStop(.42, 'rgba(255,246,222,1)'); hg.addColorStop(.7, 'rgba(255,236,196,.55)'); hg.addColorStop(1, 'rgba(255,236,196,.1)');
    sg.fillStyle = hg; sg.fillRect(0, 0, W * S, W * S);
    return { img: c, dark, spec };
  }
  function drawLid(ctx, theta) {
    if (!K.lid) K.lid = buildLid();
    const L = K.lid, Hh = -6, F = 800, Hd = 22, step = 3;
    const ct = Math.cos(theta), stt = Math.sin(theta);
    const proj = (y) => {
      const d = y - Hh, yr = clamp((y - C) / 226, -1, 1), hr = Hd * (1 - yr * yr);
      const Yw = Hh + d * ct - hr * stt, Z = d * stt + (hr - Hd * .6) * ct, s = F / (F - Z);
      return [C + (Yw - C) * s, s];
    };
    let [yPrev, sPrev] = proj(0);
    for (let y = 0; y < W; y += step) {
      const [yN, sN] = proj(y + step);
      const h = yN - yPrev;
      if (h > .05 && yPrev < W + 40 && yN > -40) {
        const sc = (sPrev + sN) / 2, dw = W * sc, dx = C - dw / 2;
        ctx.drawImage(L.img, 0, y * S, W * S, step * S, dx, yPrev, dw, h + .6);
        const yr = clamp((y + step / 2 - C) / 226, -1, 1), beta = Math.asin(yr) * .55;
        const n = theta + beta;
        const darkA = clamp(1 - Math.cos(n + .5) / Math.cos(beta + .5), 0, .6);
        if (darkA > .01) { ctx.globalAlpha = darkA; ctx.drawImage(L.dark, 0, y * S, W * S, step * S, dx, yPrev, dw, h + .6); }
        const spec = (Math.exp(-Math.pow((n - .38) / .14, 2)) * .5 + Math.exp(-Math.pow((n - 1.05) / .22, 2)) * .22) * clamp(theta * 6, 0, 1);
        if (spec > .01) { ctx.globalCompositeOperation = 'lighter'; ctx.globalAlpha = spec; ctx.drawImage(L.spec, 0, y * S, W * S, step * S, dx, yPrev, dw, h + .6); ctx.globalCompositeOperation = 'source-over'; }
        ctx.globalAlpha = 1;
      }
      yPrev = yN; sPrev = sN;
    }
  }

  // ---------- entries ----------
  function fadeIn(ctx, t, a, b) {
    const k = ph(t, a, b);
    if (k < 1) { ctx.fillStyle = `rgba(0,0,0,${(1 - E.outCubic(k)).toFixed(3)})`; ctx.fillRect(0, 0, W, W); }
  }

  // shared notes fragments
  const FEAS = `<p><b>Device feasibility.</b> Everything brass is a pre-rendered bitmap: case/bezel/ring floors (one 466² layer), the lock rings, the bezel furniture, four ring-engraving layers (transparent, ~70–90&nbsp;KB each as A8 + palette), lens on/off sprites, the dome highlight (A8). The globe is live: a 224&nbsp;px orthographic sphere from a 512×256 (or 1024×512) equirectangular map through a per-pixel lookup table (lon/lat index + shade, ~150&nbsp;KB in PSRAM), redrawn only inside its circle — ~40k pixels, comfortably 40–60&nbsp;fps on the S3. At rest the rings and hands are re-composited once a minute, so the idle cost is the globe region only. The time-travel whirl rotates four ring layers every frame; on LVGL that is a full-screen redraw (~20–25&nbsp;fps) — acceptable because motion blur/flash hide it, or pre-render 8 blurred ring frames. (In this browser mockup: ~1–2&nbsp;ms/frame at rest and during reveals; ~18&nbsp;ms on frames where the four ring layers recomposite — the wake click-in and the whirl — the same hotspot the device will have.)</p>`;

  const DIAL_NOTES = `<p><b>The Omni dial as a working watch face.</b> The four engraved rings (month · day · year · century, BC in red) turn so that <b>today</b> sits under the 12 o'clock magnifier, which genuinely magnifies the brass beneath it (1.4×). Each ring carries a pair of red marks at the set value; aligned, they draw the red <b>V</b> that funnels into the red chevron on the lock ring — the prop's "V" easter egg, here meaning "date is set".</p>
<p><b>Time, the Omni way.</b> The Omni never told time, so the hands are <i>annular</i>: two blackened-steel blades with polished bevels and red-enamel tips that emerge from under the dome's lock ring, as if pivoted beneath the globe. The globe stays unobstructed; the hour reads against the bezel's ball rivets (2–10), the two lenses (11 and 1) and the magnifier bridge (12); the minute tip runs on a fine track. Dark hands on mid-brass were the most legible combination I tried; brass-on-brass disappeared.</p>
<p><b>Interactions.</b> <b>Tap the red lens</b> (11 o'clock) to time-travel: red blinks, rings unlock and whirl (different turns and directions), the hands rewind, the globe spins hard eastward, a warm flash, then the rings click home with a detent bounce, the V re-forms on the new date, the globe lands on the place with a red pip, and the green lens lights. Destinations cycle (Philadelphia 1776, Menlo Park 1879, Rome 44&nbsp;BC, Kitty Hawk…). <b>Tap green</b> (or wait ~12&nbsp;s) to come home to the real date. <b>Tap the magnifier</b> to swing out a 2.35× loupe that makes the date column big enough to read on a 1.75" panel; tap again to dismiss. Reduced motion: no spin, short whirl, no flash.</p>
<p><b>AMOLED.</b> Bezel and background are near-black; brass is kept in the dark half of its ramp; only the lenses and dome glint are bright. Rings move daily and the globe drifts, which helps against burn-in; a 1–2&nbsp;px orbit shift per hour would finish the job.</p>
<p><b>Open questions.</b> The ring numerals are near the limit of legibility at 1:1 (year ring ≈ 8.6&nbsp;px) — on the real panel they need checking; the loupe is the fallback. Should the hands hide entirely during travel? Should the century ring stop at 19 like the prop (which had no provision beyond 1999) — I added 20&nbsp;AD so it can show today.</p>`;

  function makeRevealEntry(kind) {
    const isIris = kind === 'iris';
    const dial = makeDial({ spinT0: isIris ? 620 : 760, spinK: 990, spinDur: 3600, greenAt: isIris ? 3900 : 4000 });
    const REVEAL_END = isIris ? 1450 : 1500;
    return {
      dial,
      wake(env) { dial.reset(env); },
      frame(ctx, t, env) {
        if (dial.st.lastT === undefined) dial.reset(env);
        const rm = env.reducedMotion;
        const tt = rm ? t * 2.2 : t;
        dial.frame(ctx, t, env);
        if (isIris) {
          const anticip = E.outCubic(ph(tt, 260, 430)) - E.inOutCubic(ph(tt, 430, 560));
          const s = E.inOutCubic(ph(tt, 480, 1420));
          const ap = s * (IRIS_R + 8);
          const phi = -6 * D2R * anticip + 62 * D2R * s;
          // dial lit from behind as the aperture widens
          if (s < 1) { ctx.fillStyle = `rgba(0,0,0,${(.55 * (1 - s)).toFixed(3)})`; ctx.beginPath(); ctx.arc(C, C, IRIS_R, 0, TAU); ctx.fill(); }
          drawIris(ctx, ap, phi);
        } else {
          const pop = 4 * D2R * E.outCubic(ph(tt, 280, 380)) - 1.5 * D2R * E.inOutCubic(ph(tt, 380, 540));
          const s = ph(tt, 580, 1640);
          const theta = pop + 108 * D2R * E.inOutCubic(s);
          if (theta < 90 * D2R) {
            const open = ph(theta, 2 * D2R, 80 * D2R);
            ctx.fillStyle = `rgba(0,0,0,${(.92 * (1 - E.outCubic(open))).toFixed(3)})`; ctx.fillRect(0, 0, W, W);
            // shadow cast just below the leading edge
            drawLid(ctx, theta);
          }
        }
        fadeIn(ctx, t, 0, 300);
      },
      pointer(type, x, y, env) { if (type === 'up') dial.tap(x, y, env, dial.st.lastT > REVEAL_END + 400); },
    };
  }


  // ---------- the glance (always-on, wrist down): outline only, < 5 % of the pixels lit, no globe (spec §13) ----------
  // The same 9-minute orbit as the firmware (omni_orbit_offset), against burn-in.
  const GL_ORBIT = [[0, 0], [3, 1], [4, -2], [2, -4], [-1, -3], [-3, -1], [-4, 2], [-2, 4], [1, 3]];
  const GL_MONTHS = ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUNE', 'JULY', 'AUG', 'SEPT', 'OCT', 'NOV', 'DEC'];
  // hand(): the annular hand polygons (118 -> r1) as the glance outlines them
  function handPath(g, r0, r1, w, hour) {
    const tipL = hour ? 16 : 12;
    g.beginPath();
    if (hour) { g.moveTo(-w * .32, -r0); g.lineTo(-w / 2, -(r1 - tipL)); g.lineTo(0, -r1); g.lineTo(w / 2, -(r1 - tipL)); g.lineTo(w * .32, -r0); }
    else { g.moveTo(-w / 2, -r0); g.lineTo(-w * .38, -(r1 - tipL)); g.lineTo(0, -r1); g.lineTo(w * .38, -(r1 - tipL)); g.lineTo(w / 2, -r0); }
    g.closePath();
  }
  function omniGlance(ctx, t, env) {
    const d = env.now(), set = d.getFullYear() >= 2025;
    const o = GL_ORBIT[((Math.floor(d.getTime() / 60000) % 9) + 9) % 9];
    const [hA, mA] = set ? handAngles(env) : [0, 0];
    ctx.save(); ctx.translate(o[0], o[1]); ctx.lineWidth = 1;
    // dial outline: the rings' edges and the bezel's hour dots
    ctx.strokeStyle = 'rgba(201,163,91,.30)';
    for (const r of [126, 197, 225]) { ctx.beginPath(); ctx.arc(C, C, r, 0, TAU); ctx.stroke(); }
    ctx.fillStyle = 'rgba(201,163,91,.36)';
    for (let i = 0; i < 12; i++) {
      const a = i / 12 * TAU;
      ctx.beginPath(); ctx.arc(px(218, a), py(218, a), i % 3 === 0 && i ? 3.2 : 2.2, 0, TAU); ctx.fill();
    }
    // annular hands
    ctx.strokeStyle = 'rgba(236,230,220,.38)'; ctx.lineWidth = 1.5; ctx.lineJoin = 'round';
    for (const [a, r1, w, hour] of [[hA, 178, 11, true], [mA, 214, 5.2, false]]) {
      ctx.save(); ctx.translate(C, C); ctx.rotate(a); handPath(ctx, 118, r1, w, hour); ctx.stroke(); ctx.restore();
    }
    // the magnifier's frame and its date column
    ctx.strokeStyle = 'rgba(201,163,91,.34)'; ctx.lineWidth = 1;
    rrect(ctx, MAG.x0, MAG.y0, MAG.x1 - MAG.x0, MAG.y1 - MAG.y0, 3); ctx.stroke();
    if (set) {
      const v = dateVals(d.getFullYear(), d.getMonth() + 1, d.getDate()), bc = v[3] < 20;
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      const row = (txt, y, px, rgba) => { ctx.font = '700 ' + px + 'px ' + FD; ctx.fillStyle = rgba; ctx.fillText(txt, C, y); };
      const ink = 'rgba(236,230,220,.36)', red = 'rgba(216,68,58,.40)';
      row((bc ? 19 - v[3] : v[3] - 20) + (bc ? ' BC' : ' AD'), 32, 13, bc ? red : ink);
      row(String(v[2]).padStart(2, '0'), 61, 13, ink);
      row(String(v[1]), 86, 15, ink);
      row(GL_MONTHS[v[0]], 110, 15, ink);
    }
    ctx.restore();
  }

  const iris = makeRevealEntry('iris');
  OMNI_FACES.push({
    id: 'omni-iris', group: 'Omni', title: 'Brass iris reveal',
    summary: 'Nine brass blades snap open on lift; the globe spins up from rest and settles to a drift.',
    notes: `<p><b>What it is.</b> On lift, the screen wakes to a closed nine-blade brass iris sitting inside the Omni's bezel. A small anticipation twist (the blades "unlatch"), then the aperture opens over ~0.95&nbsp;s with an ease-in-out; you see the globe start to turn through the opening, then the spin-up runs: from rest to ~700°/s in under a second, then a long flywheel tail into a slow 2°/s drift (≈3.6&nbsp;s, 2¾ turns), landing on the home location. The green lens lights when the globe settles. After that it is the full <code>omni-dial</code> (tap red to travel, tap the magnifier for the loupe).</p>
<p><b>Craft.</b> Blades are drawn as a true iris partition: each blade's visible region is bounded by its own straight aperture edge, a gently curving overlap edge and its neighbour's — so the pinwheel is correct at every aperture, including fully closed. Per-blade lighting follows a fixed key light, with brushed grain, contact shadows on the blade beneath and a polished edge; the blades throw a shadow ring onto the dial as they retract under the bezel lip.</p>
${FEAS}
<p><b>Iris cost on device.</b> Blades are flat-shaded polygons + one rotated grain sprite each; as a full-screen effect it runs ~25–30&nbsp;fps with a custom scan-line polygon fill (the 9-fold symmetry means one blade's span table, rotated). It is ~1&nbsp;s, so a few dropped frames are tolerable. Alternative: 12 pre-rendered frames RLE-compressed (mostly flat colour) ≈ 1–1.5&nbsp;MB flash.</p>
<p><b>Recommendation, iris vs flip:</b> see the flip notes — in short, <b>flip is my pick for the Omni mode</b>; the iris is the better generic "lift to wake" for the rest of the family because it's quicker to read and loops gracefully when the watch is lifted repeatedly.</p>`,
    init(env) { caches(env); },
    wake: iris.wake, frame: iris.frame, pointer: iris.pointer, sleepFrame: omniGlance,
  });

  const flip = makeRevealEntry('flip');
  OMNI_FACES.push({
    id: 'omni-flip', group: 'Omni', title: 'Lid flip reveal',
    summary: 'The hammered-V lid pops its latch and swings up toward you; then the globe spins up.',
    notes: `<p><b>What it is.</b> The screen wakes on the closed lid: domed, hand-hammered tarnished brass, the raised stippled <b>V</b>, the point-to-point diamond band across the top and <i>Time Waits For No Man</i> in blackletter round the lower edge (UnifrakturMaguntia, fetched by this file; falls back to Cormorant). The latch pops (lid jumps 4° and settles back 1.5° — a 260&nbsp;ms "click"), then the lid swings up on its 12 o'clock hinge <i>toward</i> you and leaves over the top of the screen. The dial underneath brightens as light gets in. Then the same globe spin-up as the iris, the green lens lighting on settle.</p>
<p><b>The 2.5D fake.</b> The lid bitmap is drawn as ~155 horizontal 3&nbsp;px strips; each strip is projected with a real hinge rotation + dome height + perspective (focal 800&nbsp;px), so it foreshortens <i>and</i> looms larger as the leading edge comes toward you. Each strip also gets a diffuse darkening from its rotated dome normal and a specular term — the glint is a highlight band that travels up the dome as it turns, like a real lid catching the room light. A dark lip shows the lid's thickness along the leading edge.</p>
${FEAS}
<p><b>Flip cost on device.</b> Cheaper than the iris: it is a row-wise scaled blit of one pre-rendered lid bitmap (each output row = one source row with an x-scale and a shade/spec multiplier) — a tight custom draw callback, no polygon fill, ~30+&nbsp;fps even full-screen. The lid bitmap (466², RGB565) is ~430&nbsp;KB in flash, and doubles as the art for a "closed" state.</p>
<p><b>Recommendation: flip.</b> It is the Omni — "flipping it open" is the gesture, the lid with the V and the motto is the most recognisable thing about the prop, and the closed lid gives the mode a natural resting/"put away" image. It's also cheaper to render on the S3. The iris is more watch-like and quicker (≈1&nbsp;s vs 1.5&nbsp;s), so I'd keep it as the family's generic wake transition and as an option here. Weakness to watch: the fake perspective is convincing in motion, less so if you freeze a frame at ~60°; on device the exit should be ≤ 0.6&nbsp;s.</p>`,
    init(env) { caches(env); },
    wake: flip.wake, frame: flip.frame, pointer: flip.pointer, sleepFrame: omniGlance,
  });

  const dial = makeDial({ spinT0: 150, spinK: 540, spinDur: 2600, greenAt: 2300, ringSettle: [80, 1100] });
  OMNI_FACES.push({
    id: 'omni-dial', group: 'Omni', title: 'Omni dial',
    summary: "Today's date on the rings under the magnifier, annular hands, red lens = time travel.",
    notes: DIAL_NOTES + FEAS,
    init(env) { caches(env); },
    wake(env) { dial.reset(env); },
    frame(ctx, t, env) {
      if (dial.st.lastT === undefined) dial.reset(env);
      dial.frame(ctx, t, env);
      fadeIn(ctx, t, 0, 380);
    },
    pointer(type, x, y, env) { if (type === 'up') dial.tap(x, y, env, true); },
    sleepFrame: omniGlance,
  });
  window.OMNI_OMNI = { W, C, S, RINGS, VD, MAG, LENS_R, LENS_A, GR, TILT, buildBase, buildEngrave, buildTop, buildOver, buildMagGlass, buildLens, buildDome, buildLid, getBladeTex, drawIris, hand, dateVals, spinOff, DEST, makeDial, caches };
})();
