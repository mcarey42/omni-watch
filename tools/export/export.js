/* export.js: draws the device's pre-rendered layers with the mockup's own code (design/mockups/faces-practical.js,
   through window.OMNI_PRACTICAL, and faces-omni.js, through window.OMNI_OMNI), plus the Omni and Stage 5 reference frames, and hands them to tools/export_layers.mjs as PNG data URLs plus their geometry.
   Line numbers below refer to faces-practical.js. Screen coordinates are the mockup's 466 px canvas. */
(function () {
  'use strict';

  /* Determinism: brassRing brushes its hairlines with Math.random. Seeded before the mockup code is even loaded. */
  function mulberry32(a) {
    return function () {
      a |= 0; a = (a + 0x6D2B79F5) | 0;
      let t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  Math.random = mulberry32(1);

  const TAU = Math.PI * 2, R2D = 180 / Math.PI;
  const W = 466, CX = 233, CY = 233;
  const env = { W, cx: CX, cy: CY, assets: {} };
  const FONT = '700 22px "Cormorant SC"';
  const BLACKLETTER = '46px "UnifrakturMaguntia"';   // the lid's motto (buildLid); tools/export/fonts, never Google Fonts

  function canvas(w, h) { const c = document.createElement('canvas'); c.width = w; c.height = h || w; return c; }
  function crop(src, x, y, w, h) { const c = canvas(w, h); c.getContext('2d').drawImage(src, -x, -y); return c; }
  /* Tight bounding box of alpha > 0, or null when the canvas is empty. */
  function alphaBox(c) {
    const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    let x0 = c.width, y0 = c.height, x1 = -1, y1 = -1;
    for (let y = 0; y < c.height; y++) for (let x = 0; x < c.width; x++) {
      if (d[(y * c.width + x) * 4 + 3] === 0) continue;
      if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
    }
    return x1 < 0 ? null : { x0, y0, x1, y1 };
  }
  function assert(ok, msg) { if (!ok) throw new Error('export: ' + msg); }

  /* A context whose shadow setters are rewritten: drawHand's shadow pass (645-646) then draws either no shadow
     (mode 'none') or only the shadow, shifted onto the canvas while the hand itself lands off it (mode 'only').
     The shadow is the browser's own: rgba(0,0,0,.85), shadowBlur 9 (a Gaussian of sigma 4.5), offset dropped. */
  const SHIFT = 4096;
  function shadowCtx(g, mode) {
    const P = CanvasRenderingContext2D.prototype, prop = n => Object.getOwnPropertyDescriptor(P, n);
    const col = prop('shadowColor'), ox = prop('shadowOffsetX'), oy = prop('shadowOffsetY');
    Object.defineProperty(g, 'shadowColor', {
      get() { return col.get.call(g); },
      set(v) { col.set.call(g, mode === 'none' ? 'transparent' : v); },
    });
    Object.defineProperty(g, 'shadowOffsetX', { get() { return ox.get.call(g); }, set(v) { ox.set.call(g, v ? SHIFT : 0); } });
    Object.defineProperty(g, 'shadowOffsetY', { get() { return oy.get.call(g); }, set() { oy.set.call(g, 0); } });
    return g;
  }

  /* drawHand at 12 o'clock with the pivot at (px, py) of a scratch canvas. */
  function handCanvas(spec, mode) {
    const P = window.OMNI_PRACTICAL, c = canvas(120, 300), g = shadowCtx(c.getContext('2d'), mode);
    g.translate(60 - (mode === 'only' ? SHIFT : 0), 240);
    P.drawHand(g, 0, spec);
    return { c, px: 60, py: 240 };
  }

  window.omniExport = async function () {
    const P = window.OMNI_PRACTICAL;
    assert(P, 'faces-practical.js did not publish window.OMNI_PRACTICAL');
    await document.fonts.load(FONT);
    assert(document.fonts.check(FONT), 'Cormorant SC 700 22px is not loaded');

    /* D3: the spin globe's north pole and the meridian's top pin sit at the same screen angle. The globe is
       spinGlobe()'s (492): lat0 14, roll 23.44. The pins are buildMeridian's r 6 discs (505-511): their centres are
       read back through the canvas transform while the meridian is drawn (it is cached, so this is its first draw). */
    {
      const G = new P.Globe(316, { lat0: 14, roll: 23.44, texW: 1024 });
      const p = G.project(90, 0, 0), poleAng = Math.atan2(p[1], p[0]) * R2D;
      const proto = CanvasRenderingContext2D.prototype, arc = proto.arc, pins = [];
      proto.arc = function (x, y, r, ...rest) {
        if (r === 6) { const m = this.getTransform(); pins.push([m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f]); }
        return arc.call(this, x, y, r, ...rest);
      };
      try { P.buildMeridian(env); } finally { proto.arc = arc; }
      assert(pins.length === 2, 'expected 2 meridian pins, saw ' + pins.length);
      const top = pins[0][1] < pins[1][1] ? pins[0] : pins[1];
      const pinAng = Math.atan2(top[1] - 222, top[0] - CX) * R2D;
      const diff = Math.abs(((pinAng - poleAng) + 540) % 360 - 180);
      assert(diff <= 0.5, `spin pole at ${poleAng.toFixed(2)} deg but meridian pin at ${pinAng.toFixed(2)} deg (D3)`);
      window.omniExportD3 = { poleAng, pinAng, diff };
    }

    const out = [];
    const add = (name, cf, c, face, extra) => out.push(Object.assign({ name, cf, w: c.width, h: c.height, face,
      png: c.toDataURL('image/png') }, extra || {}));

    /* ---- globe-live: gy 212, R 131 (frame() 378-458); the 286 box is centred on the globe */
    {
      const gy = 212, R = 131, S = 286, ox = CX - S / 2, oy = gy - S / 2;
      // hour ring and 24 ticks (395-401)
      let c = canvas(W), ctx = c.getContext('2d');
      ctx.strokeStyle = 'rgba(201,163,91,0.28)'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(CX, gy, R + 9, 0, TAU); ctx.stroke();
      for (let i = 0; i < 24; i++) {
        const a = i / 24 * TAU, r0 = R + (i % 6 ? 6.5 : 4), r1 = R + 9;
        ctx.strokeStyle = i % 6 ? 'rgba(201,163,91,0.25)' : 'rgba(201,163,91,0.55)';
        ctx.beginPath(); ctx.moveTo(CX + Math.cos(a) * r0, gy + Math.sin(a) * r0); ctx.lineTo(CX + Math.cos(a) * r1, gy + Math.sin(a) * r1); ctx.stroke();
      }
      add('live_ring', 'ARGB8888', crop(c, ox, oy, S, S), 'globe-live', { origin: [ox, oy] });
      // fine reticle (406-407), then the dome (408)
      c = canvas(W); ctx = c.getContext('2d');
      ctx.strokeStyle = 'rgba(236,210,154,0.10)'; ctx.lineWidth = 0.8;
      ctx.beginPath(); ctx.moveTo(CX - R * 0.96, gy); ctx.lineTo(CX + R * 0.96, gy); ctx.moveTo(CX, gy - R * 0.96); ctx.lineTo(CX, gy + R * 0.96); ctx.stroke();
      ctx.drawImage(P.dome(R), CX - R - 2, gy - R - 2);
      add('live_over', 'ARGB8888', crop(c, ox, oy, S, S), 'globe-live', { origin: [ox, oy] });
      // sun bead (412-415), centred in 24 x 24
      c = canvas(24); ctx = c.getContext('2d');
      { const bx = 12, by = 12, gl = ctx.createRadialGradient(bx, by, 0, bx, by, 12);
        gl.addColorStop(0, 'rgba(255,214,130,0.9)'); gl.addColorStop(1, 'rgba(255,214,130,0)');
        ctx.fillStyle = gl; ctx.beginPath(); ctx.arc(bx, by, 12, 0, TAU); ctx.fill();
        ctx.fillStyle = '#ffe2a6'; ctx.beginPath(); ctx.arc(bx, by, 3.2, 0, TAU); ctx.fill(); }
      add('sun_bead', 'RGB565A8', c, 'globe-live', { pivot: [12, 12] });
      // sun glow on the disc (418-420), centred in 52 x 52
      c = canvas(52); ctx = c.getContext('2d');
      { const g2 = ctx.createRadialGradient(26, 26, 0, 26, 26, 26);
        g2.addColorStop(0, 'rgba(255,240,200,0.45)'); g2.addColorStop(1, 'rgba(255,240,200,0)');
        ctx.fillStyle = g2; ctx.beginPath(); ctx.arc(26, 26, 26, 0, TAU); ctx.fill(); }
      add('sun_glow', 'RGB565A8', c, 'globe-live', { pivot: [26, 26] });
    }

    /* ---- globe-spin: cy 222, R 158 (frame() 553-581): dome (567) then the meridian (568), in the 350 box */
    {
      const cy = 222, R = 158, S = 350, ox = 58, oy = 47;
      assert(ox + S / 2 === CX && oy + S / 2 === cy, 'spin box not centred on the globe');
      const c = canvas(W), ctx = c.getContext('2d');
      ctx.drawImage(P.dome(R), CX - R - 2, cy - R - 2);
      ctx.drawImage(P.buildMeridian(env), 0, 0);
      const b = alphaBox(P.buildMeridian(env));
      assert(b.x0 >= ox && b.y0 >= oy && b.x1 < ox + S && b.y1 < oy + S, 'meridian overflows the spin box');
      add('spin_over', 'ARGB8888', crop(c, ox, oy, S, S), 'globe-spin', { origin: [ox, oy] });
    }

    /* ---- globe-analog: R 150 (frame() 687-727) */
    {
      const R = 150, S = 316, ox = CX - S / 2, oy = CY - S / 2;
      // the dial over the screen's black (696)
      let c = canvas(W), ctx = c.getContext('2d');
      ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, W);
      ctx.drawImage(P.buildDial(env), 0, 0);
      add('ana_dial', 'RGB565', c, 'globe-analog', { origin: [0, 0] });
      // dome (695) then the dial (696) over the globe box: the bezel and the dial's inner edge
      c = canvas(W); ctx = c.getContext('2d');
      ctx.drawImage(P.dome(R), CX - R - 2, CY - R - 2);
      ctx.drawImage(P.buildDial(env), 0, 0);
      add('ana_over', 'ARGB8888', crop(c, ox, oy, S, S), 'globe-analog', { origin: [ox, oy] });
      // hands (709-710), 12 o'clock up; the shadow is a separate image drawn at +3,+4 at run time
      const PAD = 12;
      for (const [name, spec] of [['ana_hour', { len: 112, tail: 18, w: 7.5, at: 0.36, lume: true }],
                                  ['ana_min', { len: 192, tail: 22, w: 6, at: 0.3, lume: true }]]) {
        const h = handCanvas(spec, 'none'), b = alphaBox(h.c);
        assert(b && b.x0 >= PAD && b.y0 >= PAD && b.x1 < h.c.width - PAD && b.y1 < h.c.height - PAD, name + ' clipped');
        const x = b.x0 - PAD, y = b.y0 - PAD, w = b.x1 - b.x0 + 1 + 2 * PAD, hh = b.y1 - b.y0 + 1 + 2 * PAD;
        const pivot = [h.px - x, h.py - y];
        add(name, 'RGB565A8', crop(h.c, x, y, w, hh), 'globe-analog', { pivot });
        const s = handCanvas(spec, 'only');
        add(name + '_sh', 'RGB565A8', crop(s.c, x, y, w, hh), 'globe-analog', { pivot, offset: [3, 4] });
      }
      // centre cap (721-725), centred in 18 x 18
      c = canvas(18); ctx = c.getContext('2d'); ctx.translate(9, 9);
      const cg = ctx.createRadialGradient(-2, -2, 0, 0, 0, 8);
      cg.addColorStop(0, '#fff1c8'); cg.addColorStop(0.5, '#c9a35b'); cg.addColorStop(1, '#4a3818');
      ctx.fillStyle = cg; ctx.beginPath(); ctx.arc(0, 0, 7.5, 0, TAU); ctx.fill();
      ctx.strokeStyle = 'rgba(0,0,0,.7)'; ctx.lineWidth = 1; ctx.stroke();
      ctx.fillStyle = '#2a1e0a'; ctx.beginPath(); ctx.arc(0, 0, 1.6, 0, TAU); ctx.fill();
      add('ana_cap', 'RGB565A8', c, 'globe-analog', { pivot: [9, 9] });
    }

    /* ---- picker thumbnails (Task 12, D10): each face's own frame() at t = 6000 ms, 466 px, clipped to the panel's
       circle and downscaled to 68 x 68. env.now() is fixed (2026-10-22 14:10:40 UTC; tools/export_layers.mjs pins the
       time zone to America/New_York, so 10:10 on the clock faces) and Columbus is home. performance.now() is frozen too,
       so the faces' own animation clocks (spin, timer) read the same on every run. */
    const earth = {};
    {
      for (const [key, src] of [['earthDay', 'earth-day-1024.jpg'], ['earthNight', 'earth-night-1024.jpg']])
        earth[key] = await new Promise((res, rej) => {
          const i = new Image(); i.onload = () => res(i); i.onerror = () => rej(new Error('export: cannot load ' + src));
          i.src = '/design/mockups/assets/' + src;
        });
      const NOW = Date.UTC(2026, 9, 22, 14, 10, 40), T = 6000, TH = 68;
      let perf = 0;
      const realPerf = performance.now.bind(performance);
      performance.now = () => perf;
      const tenv = Object.assign({}, env, { assets: earth, reducedMotion: false, awake: true, R: W / 2,
        now: () => new Date(NOW), location: { name: 'Columbus, OH', lat: 39.96, lon: -83.0 }, goto() {} });
      try {
        for (const id of ['omni-dial', 'globe-live', 'globe-analog', 'digital', 'world', 'almanac', 'timer', 'globe-spin']) {
          const f = window.OMNI_FACES.find(x => x.id === id);
          assert(f, 'no mockup face ' + id);
          Math.random = mulberry32(1);
          perf = 0;
          if (f.init) f.init(tenv);
          if (f.wake) f.wake(tenv);
          perf = T;
          const full = canvas(W), g = full.getContext('2d');
          g.fillStyle = '#000'; g.fillRect(0, 0, W, W);
          g.save(); f.frame(g, T, tenv); g.restore();
          g.globalCompositeOperation = 'destination-in';           // the panel is round (index.html tick())
          g.beginPath(); g.arc(CX, CY, W / 2, 0, TAU); g.fill();
          const c = canvas(TH), t = c.getContext('2d');
          t.imageSmoothingEnabled = true; t.imageSmoothingQuality = 'high';
          t.drawImage(full, 0, 0, TH, TH);
          add('thumb_' + id.replace(/-/g, '_'), 'RGB565A8', c, id);
        }
      } finally { performance.now = realPerf; }
    }

    /* ---- Omni ring engravings (Stage 3 Task 2): buildEngrave(k) (faces-omni.js 152-182) at 2x, 932 x 932, as raw
       unpremultiplied RGBA for tools/ringpack (getImageData is unpremultiplied). Not partition assets themselves:
       ringpack turns the four into the omni_strip<L>.bin strips. buildEngrave needs no caches() (no network font). */
    const raw = [];
    {
      const O = window.OMNI_OMNI;
      assert(O, 'faces-omni.js did not publish window.OMNI_OMNI');
      for (let k = 0; k < O.RINGS.length; k++) {
        const c = O.buildEngrave(k);
        assert(c.width === W * O.S && c.height === W * O.S, 'engraving ' + k + ' is ' + c.width + 'x' + c.height);
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
        let s = '';
        for (let i = 0; i < d.length; i += 0x8000) s += String.fromCharCode.apply(null, d.subarray(i, i + 0x8000));
        raw.push({ name: 'engr_' + k, w: c.width, h: c.height, rgba: btoa(s) });
      }
      /* Stage 3 Task 10 (R69/C6): the RLE subset of the iris: 8 drawIris frames (faces-omni.js 736) over transparent at
         466 px, at the middles of 8 equal steps of the opening (480-1420 ms; the timeline of makeRevealEntry 949-952),
         for ringpack --iris to pack and size. Bench input only: never partition assets. Only on request
         (OMNI_EXPORT_IRIS=1, tools/export_layers.mjs: window.omniExportIris): RLE is retired (R99), and the 8 frames
         are ~7 MB through the page. */
      const D2R = Math.PI / 180, ph = (t, a, b) => Math.min(1, Math.max(0, (t - a) / (b - a)));
      const outCubic = x => 1 - Math.pow(1 - x, 3), inOutCubic = x => x < .5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2;
      for (let i = 0; window.omniExportIris && i < 8; i++) {
        const t = 480 + (1420 - 480) * (i + 0.5) / 8;
        const anticip = outCubic(ph(t, 260, 430)) - inOutCubic(ph(t, 430, 560)), sv = inOutCubic(ph(t, 480, 1420));
        const c = canvas(W), g = c.getContext('2d');
        O.drawIris(g, sv * (199 + 8), -6 * D2R * anticip + 62 * D2R * sv);
        const d = g.getImageData(0, 0, W, W).data;
        let s = '';
        for (let j = 0; j < d.length; j += 0x8000) s += String.fromCharCode.apply(null, d.subarray(j, j + 0x8000));
        raw.push({ name: 'iris_' + i, w: W, h: W, rgba: btoa(s) });
      }
    }

    /* ---- Omni dial layers (Stage 3 Task 4): the mockup's own builders (faces-omni.js, through window.OMNI_OMNI), each
       a 2x canvas drawn at 1x the way drawDial (501-546) draws it: drawImage onto the 466 screen, default smoothing.
       Line numbers below refer to faces-omni.js. */
    {
      const O = window.OMNI_OMNI, GR = O.GR, MAG = O.MAG;
      await document.fonts.load(BLACKLETTER);
      assert(document.fonts.check(BLACKLETTER), 'UnifrakturMaguntia 46px is not loaded (tools/export/fonts)');
      const screen = (src, x, y, w, h) => { const c = canvas(W); c.getContext('2d').drawImage(src, x, y, w, h); return c; };
      const boxed = (name, cf, c, face, extra) => {
        const b = alphaBox(c);
        assert(b, name + ' is empty');
        add(name, cf, crop(c, b.x0, b.y0, b.x1 - b.x0 + 1, b.y1 - b.y0 + 1), face, Object.assign({ origin: [b.x0, b.y0] }, extra || {}));
      };
      const fixed = (name, c, x, y, w, h, face) => {
        const b = alphaBox(c);
        assert(b && b.x0 >= x && b.y0 >= y && b.x1 < x + w && b.y1 < y + h, name + ' overflows its ' + w + 'x' + h + ' box');
        add(name, 'ARGB8888', crop(c, x, y, w, h), face, { origin: [x, y] });
      };
      // the static base over black (buildBase 102-149; drawn first by renderComp 471)
      {
        const c = canvas(W), g = c.getContext('2d');
        g.fillStyle = '#000'; g.fillRect(0, 0, W, W);
        g.drawImage(O.buildBase(), 0, 0, W, W);
        add('omni_base', 'RGB565', c, 'omni-dial', { origin: [0, 0] });
      }
      boxed('omni_top', 'ARGB8888', screen(O.buildTop(), 0, 0, W, W), 'omni-dial');      // lock rings (504)
      boxed('omni_over', 'ARGB8888', screen(O.buildOver(), 0, 0, W, W), 'omni-dial');    // bezel furniture (519)
      fixed('omni_dome', screen(O.buildDome(), CX - GR, CY - GR, 2 * GR, 2 * GR), CX - GR, CY - GR, 2 * GR, 2 * GR, 'omni-dial');
      fixed('omni_glass', screen(O.buildMagGlass(), MAG.x0, MAG.y0, MAG.x1 - MAG.x0, MAG.y1 - MAG.y0),
        MAG.x0, MAG.y0, MAG.x1 - MAG.x0, MAG.y1 - MAG.y0, 'omni-dial');
      // lenses (buildLens 274-308) placed as drawLens (309-320) places them: translate to the lens centre, rotate a
      const BLOOM_TINT = {   // the bloom gradient's stops (302-306): [offset, r, g, b, alpha]; drawn 'lighter' at level x .55
        red: [[0, 255, 90, 50, 0.75], [0.3, 230, 40, 20, 0.25], [1, 200, 20, 10, 0]],
        green: [[0, 110, 255, 120, 0.7], [0.3, 40, 220, 80, 0.22], [1, 20, 180, 60, 0]],
      };
      for (const kind of ['red', 'green']) {
        const L = O.buildLens(kind), a = (kind === 'red' ? -1 : 1) * O.LENS_A;
        const x = CX + O.LENS_R * Math.sin(a), y = CY - O.LENS_R * Math.cos(a);
        for (const state of ['off', 'on']) {
          const c = canvas(W), g = c.getContext('2d');
          g.translate(x, y); g.rotate(a);
          g.drawImage(L[state], -L.w / 2 - 1, -L.h / 2 - 1, L.w + 2, L.h + 2);
          boxed(`omni_lens_${kind}_${state}`, 'ARGB8888', c, 'omni-dial', { centre: [+x.toFixed(3), +y.toFixed(3)], angle: Math.round(a / Math.PI * 180) });
        }
        // the bloom: a centred radial, so unrotated; its alpha only (A8), tinted at draw (D12)
        const c = canvas(140);
        c.getContext('2d').drawImage(L.bloom, 0, 0, 140, 140);
        add(`omni_bloom_${kind}`, 'A8', c, 'omni-dial', { origin: [Math.round(x - 70), Math.round(y - 70)], pivot: [70, 70],
          centre: [+x.toFixed(3), +y.toFixed(3)], tint: BLOOM_TINT[kind], level: 0.55 });
      }
      // the lid (buildLid 810-892) with the blackletter motto; dark and spec are its alpha, derived at run time
      add('omni_lid', 'RGB565A8', screen(O.buildLid().img, 0, 0, W, W), 'omni-flip', { origin: [0, 0] });
      // the iris blade grain (getBladeTex 727-735) as one channel: its luminance over mid grey, 128 = no grain
      {
        const t = O.getBladeTex(), c = canvas(t.width, t.height), g = c.getContext('2d');
        assert(t.width === 256 && t.height === 512, 'blade texture is ' + t.width + 'x' + t.height);
        g.fillStyle = 'rgb(128,128,128)'; g.fillRect(0, 0, t.width, t.height);
        g.drawImage(t, 0, 0);
        const im = g.getImageData(0, 0, t.width, t.height), d = im.data;
        for (let i = 0; i < d.length; i += 4) {
          d[i + 3] = Math.round(0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2]);
          d[i] = d[i + 1] = d[i + 2] = 255;
        }
        g.putImageData(im, 0, 0);
        add('omni_blade', 'A8', c, 'omni-iris', { format: 'A8: the grain luminance over rgb(128,128,128); 128 = none, above lighter, below darker' });
      }
    }

    /* ---- Reference frames (design/shots/<prefix><name>.png): each entry's own frame(ctx, t, env) at 466 px over
       black, clipped round as index.html's tick() does, home Columbus, reducedMotion false. Each frame starts from a
       fresh wake. The Omni's (prefix omni-ref-) have env.now() fixed at Thursday 2026-10-22 10:10:40 EDT (14:10:40 UTC);
       Stage 5's (omni-ref-s5-, below) at Sunday 2026-11-01 10:00:00 EST (15:00:00 UTC). performance.now() is frozen
       for the whole block, as in the thumbnails: 0 at init and wake, then each step's `at` (a frame step: its t), so
       the faces' own animation clocks (the timer's) read the same on every run. A step is a frame { t }, a tap
       { tap: [x, y] } (pointer 'up' only) or one pointer event { down | move | up: [x, y] }. */
    const refs = [];
    let perf = 0;
    const realPerf = performance.now.bind(performance);
    performance.now = () => perf;
    try {
      const NOW = Date.UTC(2026, 9, 22, 14, 10, 40);
      const refEnv = now => Object.assign({}, env, { assets: earth, reducedMotion: false, awake: true, R: W / 2,
        now: () => new Date(now), location: { name: 'Columbus, OH', lat: 39.96, lon: -83.0 }, goto() {} });
      const renv = refEnv(NOW);
      const O = window.OMNI_OMNI, D2R = Math.PI / 180;
      const lerp01 = (t, a, b) => Math.min(1, Math.max(0, (t - a) / (b - a)));
      const outCubic = s => 1 - Math.pow(1 - s, 3), inOutCubic = s => s < .5 ? 4 * s * s * s : 1 - Math.pow(-2 * s + 2, 3) / 2;
      // the flip's lid angle at t (makeRevealEntry, 957-959)
      const flipTheta = t => 4 * D2R * outCubic(lerp01(t, 280, 380)) - 1.5 * D2R * inOutCubic(lerp01(t, 380, 540)) +
        108 * D2R * inOutCubic(lerp01(t, 580, 1640));
      let lo = 580, hi = 1640;
      for (let i = 0; i < 60; i++) { const mid = (lo + hi) / 2; if (flipTheta(mid) < 60 * D2R) lo = mid; else hi = mid; }
      const t60 = (lo + hi) / 2;
      const red = [CX + O.LENS_R * Math.sin(-O.LENS_A), CY - O.LENS_R * Math.cos(-O.LENS_A)];
      const POINTER = ['down', 'move', 'up'];
      const isFrame = s => s.t !== undefined;
      const shot = (name, id, steps, opt) => {
        const f = window.OMNI_FACES.find(x => x.id === id), e = (opt && opt.env) || renv;
        assert(f, 'no mockup face ' + id);
        perf = 0;
        if (f.init) f.init(e);
        if (f.wake) f.wake(e);
        let c;
        for (const s of steps) {
          if (s.at !== undefined) perf = s.at;
          if (s.tap) { f.pointer('up', s.tap[0], s.tap[1], e); continue; }
          const type = POINTER.find(k => s[k]);
          if (type) { f.pointer(type, s[type][0], s[type][1], e); continue; }
          assert(isFrame(s), name + ': a step is a frame, a tap or a pointer event');
          if (s.at === undefined) perf = s.t;
          c = canvas(W); const g = c.getContext('2d');
          g.fillStyle = '#000'; g.fillRect(0, 0, W, W);
          g.save(); f.frame(g, s.t, e); g.restore();
          g.globalCompositeOperation = 'destination-in';
          g.beginPath(); g.arc(CX, CY, W / 2, 0, TAU); g.fill();
        }
        refs.push({ prefix: (opt && opt.prefix) || 'omni-ref-', name, id, t: steps.filter(isFrame).map(s => +s.t.toFixed(3)),
          png: c.toDataURL('image/png') });
      };
      shot('dial-rest', 'omni-dial', [{ t: 6000 }]);
      shot('dial-settle', 'omni-dial', [{ t: 400 }]);
      shot('flip-latch', 'omni-flip', [{ t: 330 }]);
      shot('flip-60', 'omni-flip', [{ t: t60 }]);
      shot('iris-half', 'omni-iris', [{ t: 950 }]);
      for (const [name, dt] of [['travel-1000', 1000], ['travel-2230', 2230], ['arrived', 4000]])
        shot(name, 'omni-dial', [{ t: 6000 }, { tap: red }, { t: 6000 + dt }]);
      window.omniExportRefs = { t60, flipTheta60: flipTheta(t60) / D2R, red };

      /* ---- Stage 5 (world, almanac, timer). Everything above is drawn; only now are the IBM Plex faces loaded (the
         firmware's fonts are cut from these TTFs, tools/fonts.sh), so the layers and frames below use them while every
         existing layer, thumbnail and reference frame keeps its bytes. faces-practical.js FS is Plex Sans (500-700:
         the variable font) and FM Plex Mono (500 only in these faces). */
      for (const [family, file, weight] of [['IBM Plex Sans', 'IBMPlexSans-VF.ttf', '100 700'],
                                            ['IBM Plex Mono', 'IBMPlexMono-Medium.ttf', '500']]) {
        const ff = new FontFace(family, `url("/tools/fonts/${file}")`, { weight });
        await ff.load();
        document.fonts.add(ff);
      }
      for (const spec of ['500 21px "IBM Plex Sans"', '700 14px "IBM Plex Sans"', '500 26px "IBM Plex Mono"'])
        assert(document.fonts.check(spec), spec + ' is not loaded (tools/fonts)');
      /* the world face (faces-practical.js world): the frame (city band, brass 24 h ring, hour numerals) over black,
         drawn afresh with Plex (the thumbnail cached one without it), and the graticule (equator and tropics) over
         transparent black in the 338 map box at (64, 64), centred on (169, 169), radius unit WM = 166 */
      {
        assert(P.WM === 166, 'world map radius ' + P.WM);
        Math.random = mulberry32(1);   // brassRing's hairlines
        P.resetWorldFrame();
        let c = canvas(W), g = c.getContext('2d');
        g.fillStyle = '#000'; g.fillRect(0, 0, W, W);
        g.drawImage(P.buildWorldFrame(env), 0, 0);
        add('wt_frame', 'RGB565', c, 'world', { origin: [0, 0] });
        const S = 338, o = CX - S / 2;
        c = canvas(S); g = c.getContext('2d');
        P.worldGraticule(g, S / 2, S / 2);
        const b = alphaBox(c);
        assert(b && b.x0 > 0 && b.y0 > 0 && b.x1 < S - 1 && b.y1 < S - 1, 'graticule overflows its 338 box');
        add('wt_over', 'ARGB8888', c, 'world', { origin: [o, o] });
      }
      /* the three Stage 5 reference frames: frame() at t = 5 s; the timer in COUNT with 25:00 left. The mockup's timer
         starts its countdown on the wind's release (R135 moves that start to a centre tap on the device: the same
         COUNT frame), so the wind is its steps: down on the rim at 12 o'clock (r 195), a move per minute clockwise to
         the 25-minute mark (150 deg), up there; all at perf 5000, the frame's own time, so 25:00 exactly is left. */
      {
        const s5 = { prefix: 'omni-ref-s5-', env: refEnv(Date.UTC(2026, 10, 1, 15, 0, 0)) };
        const T5 = 5000, RW = 195, rim = m => [CX + RW * Math.sin(m / 60 * TAU), CY - RW * Math.cos(m / 60 * TAU)];
        const wind = [{ down: rim(0), at: T5 }];
        for (let m = 1; m <= 25; m++) wind.push({ move: rim(m) });
        wind.push({ up: rim(25) });
        for (const id of ['world', 'almanac', 'timer']) {
          Math.random = mulberry32(1);
          shot(id, id, (id === 'timer' ? wind : []).concat([{ t: T5 }]), s5);
        }
      }
    } finally { performance.now = realPerf; }

    for (const a of out) assert(/^[a-z0-9_]+$/.test(a.name), 'bad asset name ' + a.name);   // R48/C8
    return { layers: out, raw, refs, d3: window.omniExportD3, refInfo: window.omniExportRefs };
  };
})();
