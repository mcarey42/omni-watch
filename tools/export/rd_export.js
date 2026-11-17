/* rd_export.js: the Mining Ship layer export, page side (rd wave 1 Task 7; spec section 12.1). tools/rd_export.mjs opens
   tools/export/rd_export.html, which loads this first, and calls window.rdExport(job).

   Before any mockup code exists, this file:
     - seeds Math.random (mulberry32(1), as dw's export does);
     - wraps document.createElement so every canvas a mockup makes is tagged with its creating function (the first stack
       frame outside the mockups' canvas helpers);
     - wraps fillText and strokeText: any drawn text matching the list (tools/export/rd_terms.mjs's RD_FORBIDDEN, whose
       source rd_export.mjs hands the page as window.RD_FORBIDDEN_SRC before it loads) throws, and the export fails
       (spec section 13.2). Each call's text is tested, and so is all the text drawn on its canvas so far, joined, so a
       term drawn one glyph per call is caught at its last glyph.

   The exposure shim (spec section 18: the builder's choice). The mockups keep their layer builders private inside one
   closure each. loadMockup() fetches a mockup's source (read in place, never copied or edited), and runs it with one
   statement added just before its closure's final `})();`: it publishes the named functions on window.RD_EXPOSE[file]
   (each `typeof name === 'undefined' ? undefined : name`, so a name the file lacks is simply absent). Nothing else of
   the file changes, and nothing is written anywhere.

   window.rdExport(job) returns each row as PNG data URLs with its record:
     atlas    one image per glyph, the glyph drawn in white on transparent (its alpha is the A8 plane), with its origin
              (ox, oy: where the image's top-left sits from the pen and the baseline) and its own advance (adv, the
              font's): draw stencil is the reveal mockup's own stencil(ch, size) (faces-reveal.js; its painted recipe
              and per-glyph seed), its 2x canvas drawn at 1x as drawStencil draws it, the baseline moved onto a whole
              pixel; stroke an outline (strokeText, row.line px, row.join); fill a plain fill (row.weight). "advance":
              "time" also returns the mockup's timeMetrics (the digit and colon advances). row.extra draws more glyphs
              another way, each under its code in row.extra[i].as. tools/rd_pack.py cuts each image to its ink and packs.
     atlas2   the same per glyph, twice: the glow plane (fillText at row.glow.a with shadowBlur row.glow.blur, both in
              white: the mockups' glow pass) and the core plane (fillText at full alpha), one size and origin;
     a8 / rgb565 / rgb565a8 / a8_strip
              row.call: the exposed builder RD_EXPOSE[call.script][call.fn](g, ...call.args) on a w x h canvas
              (call.tile: drawn at the nine offsets of a w x h period, so the texture tiles); or row.fn: the canvas a
              mockup face (row.mockup) makes from fn while driven, as dw's bin rows (exactly one, of w x h x scale);
              or row.build (rd wave 1 Task 10): the canvas an exposed builder returns, RD_EXPOSE[build.script]
              [build.fn](...build.args), drawn at build.size px square as the mockup draws it (drawImage at W), then
              cut to row.crop [x, y, w, h] (default the whole); an rgb565 row is that crop on black; an a8 row's alpha
              is worked out from it by row.a8 (built() below: a ramp index, a tint's coverage over a base, or the
              delta of the builder at build.delta_args), in white, so tools/rd_pack.py's A8 plane is that number;
              a row.fn canvas with row.a8 (Task 11) is cut to row.crop and worked out by the same rule (not delta;
              a ramp's a8.erase lists screen rects left at index 0, Task 12; a8.over, Task 13: a mark layer's bytes
              over a base canvas of the same drive, through a8.runs' colours and alphas; row.pick, the pick-th
              canvas of a builder that makes several),
              and an rgb565 row.fn canvas with row.crop (Task 12) is that crop on black; row.disc (Task 15) leaves
              every pixel whose centre lies outside a disc black (rgb565) or at index 0 (a ramp's a8); build.before
              (wave 2 Task 3) cuts the builder's canvas as it stood before its first stroke in one colour, and
              row.polar an A8 polar strip of the builder's canvas (polarBuilt() below); build.dest (wave 2 Task 5)
              draws the builder's canvas into a rect [x, y, w, h] of the square instead of over all of it;
              row.sprites (wave 2 Task 7) replays a builder's sprites drawn onto the face's frame over the row's
              canvas (withSprites() below), and a8.runs without a8.over lays its runs over black;
              row.wisp (wave 2 Task 12, kind a8_rows) draws Curry Clock's steam loop from the blob the tagged canvas
              holds (wispStrip() below);
     thumb    the face's settled frame at pinned.thumb_t_ms, clipped to the circle and scaled to 68 px. */
(function () {
  'use strict';

  function mulberry32(a) {
    return function () {
      a |= 0; a = (a + 0x6D2B79F5) | 0;
      let t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  Math.random = mulberry32(1);

  const W = 466, CX = 233, CY = 233, TAU = Math.PI * 2;
  function assert(ok, msg) { if (!ok) throw new Error('rd export: ' + msg); }

  /* ---- the drawn-text guard (spec section 13.2): tools/export/rd_terms.mjs's list, handed over by rd_export.mjs */
  let forbid = null;
  function list() {
    if (!forbid) {
      assert(typeof window.RD_FORBIDDEN_SRC === 'string' && window.RD_FORBIDDEN_SRC.length >= 15,
        'no RD_FORBIDDEN_SRC: tools/rd_export.mjs hands the list over before the page loads');
      forbid = new RegExp(window.RD_FORBIDDEN_SRC, 'i');
    }
    return forbid;
  }
  const P2 = CanvasRenderingContext2D.prototype;
  const realFillText = P2.fillText, realStrokeText = P2.strokeText, realDrawImage = P2.drawImage, realFillRect = P2.fillRect;
  let drawnTexts = 0;
  const perCanvas = new WeakMap();
  const KEEP = 256;
  function guard(t, ctx) {
    drawnTexts++;
    const s = String(t);
    if (list().test(s)) throw new Error('rd export: drawn text matches a forbidden term (spec section 13.2)');
    if (ctx && ctx.canvas) {
      const joined = ((perCanvas.get(ctx.canvas) || '') + s).slice(-KEEP);
      perCanvas.set(ctx.canvas, joined);
      if (list().test(joined))
        throw new Error('rd export: the text drawn on one canvas, joined, matches a forbidden term (spec section 13.2)');
    }
  }
  P2.fillText = function (t, ...rest) { guard(t, this); return realFillText.call(this, t, ...rest); };
  P2.strokeText = function (t, ...rest) { guard(t, this); return realStrokeText.call(this, t, ...rest); };

  /* ---- canvas tags (dw's export's rule): the first frame outside the mockups' canvas helpers */
  Error.stackTraceLimit = 64;
  const HELPERS = new Set(['off', 'mk', 'layer', 'canvas', 'offscreen']);
  const registry = [];
  const realCreate = Document.prototype.createElement;
  function callerOf(stack) {
    for (const line of stack.split('\n').slice(2)) {
      const m = line.match(/^\s*at (?:new )?([^\s(]+) \(/);
      const name = m ? m[1].split('.').pop() : '<anonymous>';
      if (!HELPERS.has(name)) return name;
    }
    return '<none>';
  }
  Document.prototype.createElement = function (tag, opts) {
    const el = realCreate.call(this, tag, opts);
    if (String(tag).toLowerCase() === 'canvas') { el.rdFn = callerOf(new Error().stack); registry.push(el); }
    return el;
  };
  function mkc(w, h) { const c = realCreate.call(document, 'canvas'); c.width = w; c.height = h || w; return c; }
  function onBlack(src, w, h) {
    const c = mkc(w, h), g = c.getContext('2d');
    realFillRect.call(Object.assign(g, { fillStyle: '#000' }), 0, 0, w, h);
    realDrawImage.call(g, src, 0, 0, w, h);
    return c;
  }

  /* ---- the exposure shim */
  window.RD_EXPOSE = window.RD_EXPOSE || {};
  async function loadMockup(url, key, names) {
    const res = await fetch(url);
    assert(res.ok, `${url}: ${res.status}`);
    const src = await res.text();
    const m = src.match(/\}\)\(\);?\s*$/);
    assert(m, `${url}: does not end in one closure's \`})();\` (the shim's anchor)`);
    for (const n of names) assert(/^[A-Za-z_$][\w$]*$/.test(n), `${key}: ${n} is not a name`);
    const pub = names.map(n => `${n}: typeof ${n} === 'undefined' ? undefined : ${n}`).join(', ');
    const shim = `\n;(window.RD_EXPOSE[${JSON.stringify(key)}] = { ${pub} });\n`;
    const el = realCreate.call(document, 'script');
    el.textContent = src.slice(0, m.index) + shim + src.slice(m.index) + `\n//# sourceURL=${url}\n`;
    document.head.appendChild(el);
    assert(window.RD_EXPOSE[key], `${url}: the shim did not run (a syntax error in the file?)`);
    return window.RD_EXPOSE[key];
  }

  /* ---- driving a face (dw's export's drive) */
  const realPerf = performance.now.bind(performance);
  function faceEnv(pinned) {
    return { W, cx: CX, cy: CY, R: W / 2, assets: {}, reducedMotion: false, timeValid: true, timeStale: false,
      awake: true, now: () => new Date(pinned), location: { name: 'Columbus, OH', lat: 39.96, lon: -83.0 },
      clock: { state: 'ok' }, goto() {} };
  }
  /* wave 2 Task 7: every drawImage onto the face's frame during the drive's last time is recorded (draws: the image,
     its arguments, the context's alpha and transform), so a row can replay a builder's sprites (row.sprites) */
  function drive(id, times, pinned) {
    const f = window.OMNI_FACES.find(x => x.id === id);
    assert(f, 'no mockup face ' + id);
    const start = registry.length;
    let perf = 0;
    performance.now = () => perf;
    let full = null;
    const draws = [], hooked = P2.drawImage;
    P2.drawImage = function (img, ...a) {
      if (full && this.canvas === full)
        draws.push({ img, a, alpha: this.globalAlpha, m: this.getTransform(), op: this.globalCompositeOperation });
      return hooked.call(this, img, ...a);
    };
    try {
      Math.random = mulberry32(1);
      const env = faceEnv(pinned);
      if (f.init) f.init(env);
      if (f.wake) f.wake(env);
      for (const t of times) {
        perf = t;
        draws.length = 0;
        full = mkc(W);
        const g = full.getContext('2d');
        g.fillStyle = '#000'; realFillRect.call(g, 0, 0, W, W);
        g.save(); f.frame(g, t, env); g.restore();
      }
      return { full, made: registry.slice(start), draws: draws.slice() };
    } finally {
      performance.now = realPerf;
      P2.drawImage = hooked;
    }
  }
  /* wave 2 Task 7: row.sprites, a builder's name: src (a canvas of the frame's size) with every drawImage of a canvas
     that builder made, onto the face's frame at the drive's last time, replayed over it in order, as the face drew them
     (Nav Scope's twelve stencilled numerals, paintSprite()'s, over its build() canvas: the dial is one layer). Only
     plain draws replay: the identity transform, source-over, alpha 1; any other refuses */
  function withSprites(row, src, d) {
    const c = mkc(src.width, src.height), g = c.getContext('2d');
    realDrawImage.call(g, src, 0, 0);
    let n = 0;
    for (const q of d.draws) {
      if (q.img.rdFn !== row.sprites) continue;
      assert(q.m.isIdentity && q.op === 'source-over' && q.alpha === 1,
        `${row.name}: a ${row.sprites}() sprite drawn with a transform, a compositing mode or an alpha`);
      realDrawImage.call(g, q.img, ...q.a);
      n++;
    }
    assert(n > 0, `${row.name}: ${row.mockup} drew no ${row.sprites}() sprite at its last time`);
    return c;
  }

  /* ---- atlases */
  const FAMILY = { 'Saira Stencil One': 'stencil', 'VT323': 'crt', 'Chakra Petch': 'util' };
  function fontOf(row) {
    assert(FAMILY[row.font], `${row.name}: font ${row.font} is not one of the theme's three families`);
    const f = `${row.weight || 400} ${row.size}px "${row.font}"`;
    assert(document.fonts.check(f), `${row.name}: font ${f} is not loaded (tools/fonts.sh)`);
    return f;
  }
  const png = c => c.toDataURL('image/png');
  /* our own drawing of one glyph: a canvas with room for the ink and pad px about it, the pen at (px, py) */
  function ownGlyph(font, ch, pad, paint) {
    const m = mkc(4).getContext('2d');
    m.font = font;
    const t = m.measureText(ch);
    const l = Math.ceil(t.actualBoundingBoxLeft), r = Math.ceil(t.actualBoundingBoxRight);
    const a = Math.ceil(t.actualBoundingBoxAscent), d = Math.ceil(t.actualBoundingBoxDescent);
    const w = Math.max(1, l + r) + 2 * pad, h = Math.max(1, a + d) + 2 * pad, px = pad + l, py = pad + a;
    const out = [];
    for (const p of paint) {
      const c = mkc(w, h), g = c.getContext('2d');
      g.font = font; g.textAlign = 'left'; g.textBaseline = 'alphabetic';
      p(g, px, py);
      out.push(c);
    }
    return { canvases: out, ox: -px, oy: -py, adv: t.width };
  }
  function stencilGlyph(X, row, ch) {
    assert(X && typeof X.stencil === 'function', `${row.name}: the reveal mockup's stencil() is not exposed`);
    const s = X.stencil(ch, row.size, '#ffffff', 0);
    const base = Math.ceil(s.ay), f = base - s.ay;
    const c = mkc(s.w, s.h + 1), g = c.getContext('2d');
    realDrawImage.call(g, s.c, 0, f, s.w, s.h);   /* drawStencil's drawImage, the baseline on a whole pixel */
    return { canvases: [c], ox: -s.ax, oy: -base, adv: s.tw };
  }
  function glyphOf(X, row, part, ch, font) {
    const draw = part.draw || row.draw;
    if (draw === 'stencil') return stencilGlyph(X, row, ch);
    if (draw === 'stroke') {
      const lw = part.line || row.line, join = part.join || row.join || 'round';
      assert(lw > 0, `${row.name}: a stroke needs line`);
      return ownGlyph(font, ch, Math.ceil(lw * 6) + 2, [(g, x, y) => {
        g.strokeStyle = '#fff'; g.lineWidth = lw; g.lineJoin = join; g.strokeText(ch, x, y);
      }]);
    }
    if (draw === 'fill') return ownGlyph(font, ch, 2, [(g, x, y) => { g.fillStyle = '#fff'; g.fillText(ch, x, y); }]);
    if (draw === 'phosphor') {
      const gl = row.glow;
      assert(gl && gl.blur > 0 && gl.a > 0, `${row.name}: phosphor needs glow {blur, a}`);
      return ownGlyph(font, ch, Math.ceil(gl.blur * 2) + 2, [
        (g, x, y) => {
          g.globalAlpha = gl.a; g.shadowColor = '#fff'; g.shadowBlur = gl.blur; g.fillStyle = '#fff'; g.fillText(ch, x, y);
        },
        (g, x, y) => { g.fillStyle = '#fff'; g.fillText(ch, x, y); },
      ]);
    }
    throw new Error(`rd export: ${row.name}: draw ${draw}`);
  }
  function drawAtlas(X, row) {
    const font = fontOf(row);
    const parts = [{ glyphs: row.glyphs, as: row.glyphs }].concat(row.extra || []);
    let glyphs = '';
    const cells = [];
    for (const part of parts) {
      const as = part.as || part.glyphs;
      assert(as.length === part.glyphs.length, `${row.name}: ${part.glyphs.length} glyphs drawn as ${as.length} codes`);
      guard(part.glyphs, null);
      [...part.glyphs].forEach((ch, i) => {
        const gph = glyphOf(X, row, part, ch, font);
        const rec = { ch: as[i], ox: gph.ox, oy: gph.oy, adv: gph.adv, w: gph.canvases[0].width, h: gph.canvases[0].height };
        if (row.kind === 'atlas2') { assert(gph.canvases.length === 2, `${row.name}: atlas2 draws two planes`); rec.glow = png(gph.canvases[0]); rec.core = png(gph.canvases[1]); }
        else rec.png = png(gph.canvases[0]);
        cells.push(rec);
        glyphs += as[i];
      });
    }
    assert(new Set(glyphs).size === glyphs.length, `${row.name}: a glyph code twice`);
    guard(glyphs, null);
    const out = { glyphs, cells };
    if (row.advance === 'time') {
      assert(X && typeof X.timeMetrics === 'function', `${row.name}: the reveal mockup's timeMetrics() is not exposed`);
      const tm = X.timeMetrics(row.size);
      out.digit_adv = tm.adv; out.colon_adv = tm.col;
    }
    return out;
  }

  /* ---- row.build (rd wave 1 Task 10): a builder's own canvas, drawn at build.size square and cut to row.crop */
  const hexRgb = h => { assert(/^#[0-9a-fA-F]{6}$/.test(h), `bad colour ${h}`); const n = parseInt(h.slice(1), 16); return [n >> 16 & 255, n >> 8 & 255, n & 255]; };
  /* wave 2 Task 3: build.before {stroke: '#rrggbb'}, the builder's canvas as it stood just before its first stroke in
   * that colour (Bay Mouth's bayBase(): its stars, corona and photosphere before the door frame's lip, the lip and its
   * rivets being the face's own procedural drawing). The builder must draw (not return its cache): the rows run before
   * any drive of the face, and a cached build refuses here */
  function snapshotBefore(row, before, call) {
    const want = String(before.stroke).toLowerCase(), realStroke = P2.stroke;
    assert(/^#[0-9a-f]{6}$/.test(want), `${row.name}: build.before.stroke is a #rrggbb colour`);
    let snap = null;
    P2.stroke = function (...a) {
      if (!snap && String(this.strokeStyle).toLowerCase() === want) {
        snap = mkc(this.canvas.width, this.canvas.height);
        realDrawImage.call(snap.getContext('2d'), this.canvas, 0, 0);
      }
      return realStroke.apply(this, a);
    };
    try { call(); } finally { P2.stroke = realStroke; }
    assert(snap, `${row.name}: the builder drew no ${want} stroke (did it return a cached canvas?)`);
    return snap;
  }
  function buildPixels(X, row, args) {
    const b = row.build, fn = X[b.script] && X[b.script][b.fn];
    assert(typeof fn === 'function', `${row.name}: ${b.script}'s ${b.fn}() is not exposed`);
    let src = null;
    if (b.before) src = snapshotBefore(row, b.before, () => fn(...args));
    else src = fn(...args);
    assert(src && src.width > 0, `${row.name}: ${b.fn}() returned no canvas`);
    const size = b.size || W, c = mkc(size, size), g = c.getContext('2d');
    realFillRect.call(Object.assign(g, { fillStyle: '#000' }), 0, 0, size, size);
    /* the mockup's drawImage(img, 0, 0, W, W); wave 2 Task 5: build.dest [x, y, w, h], drawn into that rect instead
       (a builder whose canvas is not the screen's square: Drift Plate's hull tile at 466 x 186, its WASH ME at
       130 x 34, each as the mockup draws it) */
    const dst = b.dest || [0, 0, size, size];
    assert(dst.length === 4 && dst.every(Number.isFinite), `${row.name}: build.dest is [x, y, w, h]`);
    realDrawImage.call(g, src, dst[0], dst[1], dst[2], dst[3]);
    return g.getImageData(0, 0, size, size).data;
  }
  /* The ramp's 256 colours from its stops [[index, '#rrggbb'], ...] (0 first, 255 last), each channel between two stops
   * (c0 (i1 - i) + c1 (i - i0) + floor((i1 - i0) / 2)) / (i1 - i0), floored: all terms are >= 0, so C's integer
   * division gives the same bytes (rd_core/release.c builds the same table from the same stops). */
  function rampOf(stops) {
    assert(Array.isArray(stops) && stops.length >= 2 && stops[0][0] === 0 && stops[stops.length - 1][0] === 255, 'a ramp runs 0 to 255');
    const out = [];
    for (let k = 0; k + 1 < stops.length; k++) {
      const [i0, h0] = stops[k], [i1, h1] = stops[k + 1], c0 = hexRgb(h0), c1 = hexRgb(h1);
      assert(i1 > i0, 'ramp stops rise');
      for (let i = i0; i < i1 || (k + 2 === stops.length && i === i1); i++)
        out.push(c0.map((v, j) => Math.floor((v * (i1 - i) + c1[j] * (i - i0) + Math.floor((i1 - i0) / 2)) / (i1 - i0))));
    }
    assert(out.length === 256, 'a ramp of 256');
    return out;
  }
  /* the under-layer's colour at a pixel centre inside its rounded rect (fill, then the radial glow, then the sheen, as
   * the builder lays them), or null outside it */
  function underAt(u, px, py) {
    const [x, y, w, h, r] = u.rrect;
    if (px < x || px > x + w || py < y || py > y + h) return null;
    const cx = px < x + r ? x + r : px > x + w - r ? x + w - r : px, cy = py < y + r ? y + r : py > y + h - r ? y + h - r : py;
    if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > r * r) return null;
    let c = hexRgb(u.fill);
    const over = (rgb, a) => { c = c.map((v, j) => v * (1 - a) + rgb[j] * a); };
    if (u.radial) {
      const q = u.radial, d = Math.hypot(px - q.cx, py - q.cy);
      over(hexRgb(q.rgb), q.a * (1 - Math.min(1, Math.max(0, (d - q.r0) / (q.r1 - q.r0)))));
    }
    if (u.sheen) {
      const s = u.sheen, vx = s.x1 - s.x0, vy = s.y1 - s.y0, t = ((px - s.x0) * vx + (py - s.y0) * vy) / (vx * vx + vy * vy);
      over([255, 255, 255], s.a * Math.max(0, 1 - Math.max(0, t) / s.stop));
    }
    return c;
  }
  /* a's share of the way from b to t, onto 0..255: 255 (d . v) / (v . v), d = p - b, v = t - b */
  function coverage(p, b, t) {
    let dv = 0, vv = 0;
    for (let j = 0; j < 3; j++) { const v = t[j] - b[j]; dv += (p[j] - b[j]) * v; vv += v * v; }
    return vv ? Math.min(255, Math.max(0, Math.round(255 * dv / vv))) : 0;
  }
  function built(X, row) {
    const b = row.build;
    if (row.polar) return polarBuilt(X, row);
    return cut(X, row, buildPixels(X, row, b.args || []), b.size || W);
  }
  /* wave 2 Task 3: row.polar {cx, cy, r0, r1, n, ss}, an A8 polar strip of the builder's own canvas (read at its own
   * scale, with its alpha): column j (0 at 12 o'clock, clockwise) and row i (0 at r0) cover the angles [j, j + 1) of
   * n and the radii [r0 + i, r0 + i + 1), sampled ss x ss within the cell (nearest canvas pixel); the cell's colour is
   * their alpha-weighted mean and its alpha their mean. Each byte is that alpha times the colour's share of the way from
   * a8.base to a8.tint (coverage()), 0..255: Bay Mouth's hazard ring, whose layer holds the mockup's .72 in its alpha,
   * as the tint's coverage over the dark pass the face draws first. n need not be a power of two */
  function polarBuilt(X, row) {
    const b = row.build, q = row.polar, A = row.a8, fn = X[b.script] && X[b.script][b.fn];
    assert(typeof fn === 'function', `${row.name}: ${b.script}'s ${b.fn}() is not exposed`);
    assert(row.kind === 'a8' && A && A.base && A.tint, `${row.name}: a polar row is a8 with a8.base and a8.tint`);
    const src = fn(...(b.args || []));
    assert(src && src.width > 0 && src.width === src.height, `${row.name}: ${b.fn}() returned no square canvas`);
    const S = src.width / W, n = q.n, nr = Math.ceil(q.r1 - q.r0), ss = q.ss || 4;
    assert(Number.isInteger(S) && S >= 1, `${row.name}: the canvas is ${src.width} px, not a whole multiple of ${W}`);
    assert(row.w === n && row.h === nr, `${row.name}: the strip is ${n} x ${nr}, the row says ${row.w} x ${row.h}`);
    const D = src.getContext('2d').getImageData(0, 0, src.width, src.height).data, base = hexRgb(A.base), tint = hexRgb(A.tint);
    const c = mkc(n, nr), g = c.getContext('2d'), out = g.createImageData(n, nr);
    for (let i = 0; i < nr; i++) for (let j = 0; j < n; j++) {
      let pr = 0, pg = 0, pb = 0, pa = 0;
      for (let v = 0; v < ss; v++) for (let u = 0; u < ss; u++) {
        const ang = (j + (u + .5) / ss) * TAU / n, rad = q.r0 + i + (v + .5) / ss;
        const x = Math.floor((q.cx + rad * Math.sin(ang)) * S), y = Math.floor((q.cy - rad * Math.cos(ang)) * S);
        if (x < 0 || y < 0 || x >= src.width || y >= src.height) continue;
        const k = 4 * (y * src.width + x), a = D[k + 3] / 255;
        pr += D[k] * a; pg += D[k + 1] * a; pb += D[k + 2] * a; pa += a;
      }
      const alpha = pa / (ss * ss), o = 4 * (i * n + j);
      out.data[o] = out.data[o + 1] = out.data[o + 2] = 255;
      out.data[o + 3] = pa > 0 ? Math.min(255, Math.round(alpha * coverage([pr / pa, pg / pa, pb / pa], base, tint))) : 0;
    }
    g.putImageData(out, 0, 0);
    return { w: n, h: nr, png: png(c) };
  }
  /* wave 2 Task 12: row.wisp, Curry Clock's steam as a pre-rendered per-wisp loop (rd/faces.h's RD_CU_WISP_*): our own
   * drawing of the cited recipe (faces-quarters.js:425-439 steam(): puff k of a wisp at life (phi + k / 30) mod 1, at
   * y = cy + 30 - 220 life, x = cx + sin(7 life + theta) (6 + 22 life), half-size 7 + 20 life, alpha .085 sin(pi life),
   * the blob drawn over its square, added), one wisp about its own axis, for each theta step t pi / n_theta and phi step
   * p / (30 n_phi). src is the blob (glowSprite('255,248,236', 16), the canvas the face makes while driven: the
   * same bytes as the host oracle's glow plane, rd_cu_steam_oracle.c); each pixel's light S sums, for every puff, its
   * alpha x the blob's bilinear texel as the face's wave 1 steam sampled it (an integer phase, edge-clamped), floored to its 8.4 step at 255 as that steam's sums
   * floored it; then each frame goes to half resolution (row.wisp.scale 2) by least squares for the blit's bilinear x 2
   * (ls back-projection passes from the 2 x 2 mean), x q rounded: an A8 strip, frame (t, p)'s rows at (t n_phi + p) x
   * its rows, in white (tools/rd_pack.py packs it as ARW1, each row's extent and its bytes) */
  function wispStrip(row, src) {
    const P = row.wisp, [BW, BH] = P.box, S2 = P.scale, HW = BW / S2, HH = Math.ceil(BH / S2), NT = P.n_theta, NP = P.n_phi;
    assert(Number.isInteger(HW) && S2 === 2, `${row.name}: the box ${BW} x ${BH} at x${S2}`);
    assert(row.w === HW && row.h === NT * NP * HH, `${row.name}: the strip is ${HW} x ${NT * NP * HH}, the row says ${row.w} x ${row.h}`);
    const B = src.width;
    assert(B === 32 && src.height === 32, `${row.name}: the blob is ${src.width} x ${src.height}, not 32 x 32`);
    const BD = src.getContext('2d').getImageData(0, 0, B, B).data, blob = new Uint8Array(B * B);
    for (let i = 0; i < B * B; i++) blob[i] = BD[4 * i + 3];
    const RISE = 220, BASE_Y = 30, PUFFS = 30, cx = BW / 2, cy = W / 2 - P.top;   /* the wisp's axis; the box's rows */
    const X = new Float64Array(BW * BH), U = new Float64Array(BW * BH), h = new Float64Array(HW * HH);
    const c = mkc(HW, row.h), g = c.getContext('2d'), out = g.createImageData(HW, row.h);
    const clamp = (v) => v < 0 ? 0 : v > B - 1 ? B - 1 : v;
    const box = (F, j, i) => { let s = 0; for (let dy = 0; dy < 2; dy++) for (let dx = 0; dx < 2; dx++) { const y = 2 * j + dy; if (y < BH) s += F[y * BW + 2 * i + dx]; } return s / 4; };
    const up = () => {
      for (let y = 0; y < BH; y++) for (let x = 0; x < BW; x++) {
        const j = y & 1 ? (y - 1) / 2 : y / 2 - 1, i = x & 1 ? (x - 1) / 2 : x / 2 - 1, wy = y & 1 ? .25 : .75, wx = x & 1 ? .25 : .75;
        let v = 0;
        for (let a = 0; a < 2; a++) for (let b = 0; b < 2; b++) {
          const jj = j + a, ii = i + b;
          if (jj >= 0 && jj < HH && ii >= 0 && ii < HW) v += (a ? wy : 1 - wy) * (b ? wx : 1 - wx) * h[jj * HW + ii];
        }
        U[y * BW + x] = v;
      }
    };
    let top = 0;
    for (let t = 0; t < NT; t++) for (let p = 0; p < NP; p++) {
      const theta = t * Math.PI / NT, phi = p / (PUFFS * NP);
      X.fill(0);
      for (let k = 0; k < PUFFS; k++) {
        let life = phi + k / PUFFS; life -= Math.floor(life);
        const a = .085 * Math.sin(life * Math.PI);
        if (!(a > 0)) continue;
        const aq = Math.floor(a * 4096 + .5);
        const y = cy + BASE_Y - life * RISE, x = cx + Math.sin(life * 7 + theta) * (6 + life * 22), s = 7 + life * 20;
        const x0 = x - s, y0 = y - s, inv = B / (2 * s);
        for (let py = 0; py < BH; py++) {
          const vy = (py + .5 - y0) * inv - .5;
          if (!(vy > -1 && vy < B)) continue;
          const vq = Math.trunc((vy + 1) * 256) - 256, fy = vq & 255, j0 = clamp(vq >> 8), j1 = clamp((vq >> 8) + 1);
          for (let px = 0; px < BW; px++) {
            const ux = (px + .5 - x0) * inv - .5;
            if (!(ux > -1 && ux < B)) continue;
            const uq = Math.trunc((ux + 1) * 256) - 256, fx = uq & 255, i0 = clamp(uq >> 8), i1 = clamp((uq >> 8) + 1);
            const tp = blob[j0 * B + i0] * (256 - fx) + blob[j0 * B + i1] * fx, bt = blob[j1 * B + i0] * (256 - fx) + blob[j1 * B + i1] * fx;
            const bv = Math.floor((tp * (256 - fy) + bt * fy) / 65536);
            X[py * BW + px] += Math.floor(255 * bv * aq / 65536) / 16 * 256 / 255;
          }
        }
      }
      for (let j = 0; j < HH; j++) for (let i = 0; i < HW; i++) h[j * HW + i] = box(X, j, i);
      for (let it = 0; it < P.ls; it++) {
        up();
        for (let j = 0; j < HH; j++) for (let i = 0; i < HW; i++) {
          let e = 0;
          for (let dy = 0; dy < 2; dy++) for (let dx = 0; dx < 2; dx++) { const y = 2 * j + dy; if (y < BH) e += X[y * BW + 2 * i + dx] - U[y * BW + 2 * i + dx]; }
          h[j * HW + i] = Math.max(0, h[j * HW + i] + e / 4);
        }
      }
      for (let j = 0; j < HH; j++) for (let i = 0; i < HW; i++) {
        const q = Math.floor(h[j * HW + i] * P.q + .5), o = 4 * (((t * NP + p) * HH + j) * HW + i);
        assert(q <= 255, `${row.name}: frame (${t}, ${p}) row ${j} col ${i}: ${q} past 255 (q ${P.q})`);
        top = Math.max(top, q);
        out.data[o] = out.data[o + 1] = out.data[o + 2] = 255;
        out.data[o + 3] = q;
      }
    }
    assert(top > 128, `${row.name}: the brightest byte is ${top}`);
    g.putImageData(out, 0, 0);
    return { w: HW, h: row.h, png: png(c) };
  }
  /* row.crop of P (size x size, RGBA on black) through row.kind and row.a8: a build row's (Task 10), or a tagged
   * canvas's with crop and a8 (Task 11: Ship Terminal's housing, a ramp index; the delta rule needs a build) */
  function cut(X, row, P, size, base) {
    const b = row.build, [cx0, cy0, cw, ch] = row.crop || [0, 0, size, size];
    assert(cw === row.w && ch === row.h, `${row.name}: the crop is ${cw}x${ch}, the row says ${row.w}x${row.h}`);
    assert(cx0 >= 0 && cy0 >= 0 && cx0 + cw <= size && cy0 + ch <= size, `${row.name}: the crop leaves the canvas`);
    const c = mkc(cw, ch), g = c.getContext('2d'), out = g.createImageData(cw, ch);
    const at = (D, x, y) => { const i = 4 * ((cy0 + y) * size + cx0 + x); return [D[i], D[i + 1], D[i + 2]]; };
    const put = (x, y, rgb, a) => { const i = 4 * (y * cw + x); out.data[i] = rgb[0]; out.data[i + 1] = rgb[1]; out.data[i + 2] = rgb[2]; out.data[i + 3] = a; };
    const A = row.a8;
    /* Task 15: row.disc [cx, cy, r] (canvas coordinates): a pixel whose centre lies farther than r from (cx, cy) is
       black (rgb565) or index 0 (a ramp's a8), so what the builder drew outside its disc (Curry Clock's numerals and
       ticks beside the tray, the lid card's specks past its rim) stays out of the layer */
    const off = (x, y) => !!row.disc && (cx0 + x + .5 - row.disc[0]) ** 2 + (cy0 + y + .5 - row.disc[1]) ** 2 > row.disc[2] ** 2;
    if (row.kind === 'rgb565') {
      for (let y = 0; y < ch; y++) for (let x = 0; x < cw; x++) put(x, y, off(x, y) ? [0, 0, 0] : at(P, x, y), 255);
    } else {
      assert(row.kind === 'a8' && A, `${row.name}: a build row is rgb565, or a8 with its a8 rule`);
      const WH = [255, 255, 255];
      if (A.ramp) {
        const R = rampOf(A.ramp);
        /* Task 12: A.erase, screen rects [x, y, w, h] left at index 0 (glyphs the face draws itself, as polygons) */
        const erased = (x, y) => (A.erase || []).some(([ex, ey, ew, eh]) => x >= ex && x < ex + ew && y >= ey && y < ey + eh);
        for (let y = 0; y < ch; y++) for (let x = 0; x < cw; x++) {
          if (erased(cx0 + x, cy0 + y) || off(x, y)) { put(x, y, WH, 0); continue; }
          const p = (A.under && underAt(A.under, cx0 + x + .5, cy0 + y + .5)) || at(P, x, y);
          let best = 0, bd = Infinity;
          for (let i = 0; i < 256; i++) {
            const d = (p[0] - R[i][0]) ** 2 + (p[1] - R[i][1]) ** 2 + (p[2] - R[i][2]) ** 2;
            if (d < bd) { bd = d; best = i; }
          }
          put(x, y, WH, best);
        }
      } else if (A.over || A.runs) {
        /* Task 13: A.over, a mark layer over a base (Hull Plate's numerals, ticks and rivet ring over its lit plate):
           base is the base's pixels (on black, the same size); each pixel's byte is the one whose (colour, alpha) of
           A.runs, [[i0, i1, '#rrggbb'], ...] (byte i in [i0, i1] is the colour at alpha (i - i0 + 1) / (i1 - i0 + 1)),
           laid over the base pixel comes nearest the drawn pixel; 0 (the base alone) where the two are equal, and in
           A.erase's screen rects (what the face draws from another layer) */
        assert(base && base.length === P.length, `${row.name}: the over rule needs its base canvas`);
        const C = [];
        for (const [i0, i1, h] of A.runs) {
          assert(Number.isInteger(i0) && Number.isInteger(i1) && i0 >= 1 && i1 >= i0 && i1 <= 255, `${row.name}: a run ${i0}..${i1}`);
          for (let i = i0; i <= i1; i++) { assert(!C[i], `${row.name}: byte ${i} in two runs`); C[i] = [hexRgb(h), (i - i0 + 1) / (i1 - i0 + 1)]; }
        }
        const erased = (x, y) => (A.erase || []).some(([ex, ey, ew, eh]) => x >= ex && x < ex + ew && y >= ey && y < ey + eh);
        for (let y = 0; y < ch; y++) for (let x = 0; x < cw; x++) {
          const p = at(P, x, y), q = at(base, x, y);
          if (erased(cx0 + x, cy0 + y) || (p[0] === q[0] && p[1] === q[1] && p[2] === q[2])) { put(x, y, WH, 0); continue; }
          let best = 0, bd = (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2;
          for (let i = 1; i < 256; i++) {
            if (!C[i]) continue;
            const [c, a] = C[i];
            let d = 0;
            for (let j = 0; j < 3; j++) { const v = q[j] * (1 - a) + c[j] * a - p[j]; d += v * v; }
            if (d < bd) { bd = d; best = i; }
          }
          put(x, y, WH, best);
        }
      } else if (A.delta) {
        assert(b, `${row.name}: the delta rule needs a build row`);
        const Q = buildPixels(X, row, b.delta_args || []), T0 = hexRgb(A.tint);
        for (let y = 0; y < ch; y++) for (let x = 0; x < cw; x++) {
          const px = cx0 + x + .5, py = cy0 + y + .5;
          const disc = (A.discs || []).find(d => (px - d.cx) ** 2 + (py - d.cy) ** 2 <= d.r * d.r);
          put(x, y, WH, coverage(at(P, x, y), at(Q, x, y), disc ? hexRgb(disc.rgb) : T0));
        }
      } else {
        assert(A.tint && A.under, `${row.name}: an a8 build row names ramp, delta or tint with under`);
        const T0 = hexRgb(A.tint);
        for (let y = 0; y < ch; y++) for (let x = 0; x < cw; x++) {
          const u = underAt(A.under, cx0 + x + .5, cy0 + y + .5);
          put(x, y, WH, u ? coverage(at(P, x, y), u, T0) : 0);
        }
      }
    }
    g.putImageData(out, 0, 0);
    return { w: cw, h: ch, png: png(c) };
  }

  async function fontsReady() {
    const want = ['132px "Saira Stencil One"', '28px "VT323"', '600 13px "Chakra Petch"', '500 20px "Cormorant SC"',
      '700 20px "Cormorant SC"', '400 20px "IBM Plex Sans"', '500 20px "IBM Plex Sans"', '600 20px "IBM Plex Sans"',
      '400 20px "IBM Plex Mono"', '500 20px "IBM Plex Mono"'];
    await Promise.all(want.map(f => document.fonts.load(f)));
    for (const f of want) assert(document.fonts.check(f), f + ' is not loaded (tools/fonts.sh)');
    await new Promise(r => setTimeout(r, 0));   // the mockups' font loaders reset their caches on this load: let them run
  }

  /* job: { rows, pinned: { utc, thumb_t_ms }, mockups: [{ url, key, expose }] } */
  window.rdExport = async function (job) {
    await fontsReady();
    for (const m of job.mockups) await loadMockup(m.url, m.key, m.expose || []);
    await new Promise(r => setTimeout(r, 0));
    await fontsReady();
    const X = window.RD_EXPOSE;
    const pinned = Date.parse(job.pinned.utc), T = job.pinned.thumb_t_ms;
    assert(!isNaN(pinned), 'bad pinned date');
    const out = [], byFace = {};
    const add = (row, rec) => out.push(Object.assign({ name: row.name, kind: row.kind }, rec));
    for (const row of job.rows) {
      if (row.crc32) continue;                                 // committed bytes, not exported
      Math.random = mulberry32(1);
      if (row.kind === 'atlas' || row.kind === 'atlas2') {
        add(row, drawAtlas(X['faces-reveal.js'], row));
      } else if (row.kind === 'thumb') {
        const d = drive(row.mockup, [T], pinned);
        const g = d.full.getContext('2d');
        g.globalCompositeOperation = 'destination-in';        // the panel is round (the mockups' index.html tick())
        g.beginPath(); g.arc(CX, CY, W / 2, 0, TAU); g.fill();
        const c = mkc(row.w, row.h), t = c.getContext('2d');
        t.imageSmoothingEnabled = true; t.imageSmoothingQuality = 'high';
        realDrawImage.call(t, d.full, 0, 0, row.w, row.h);
        add(row, { w: c.width, h: c.height, png: png(c) });
      } else if (row.build) {                                   // an exposed builder's own canvas (Task 10)
        add(row, built(X, row));
      } else if (row.call) {                                    // an exposed builder, called on a w x h canvas
        const k = row.call, fn = X[k.script] && X[k.script][k.fn];
        assert(typeof fn === 'function', `${row.name}: ${k.script}'s ${k.fn}() is not exposed`);
        const c = mkc(row.w, row.h), g = c.getContext('2d');
        const offs = k.tile ? [-1, 0, 1] : [0];
        for (const oy of offs) for (const ox of offs) {
          const args = k.args.slice();
          args[0] += ox * row.w; args[1] += oy * row.h;
          g.save(); fn(g, ...args); g.restore();
        }
        add(row, { w: c.width, h: c.height, png: png(row.kind === 'rgb565' ? onBlack(c, row.w, row.h) : c) });
      } else {                                                  // a tagged canvas, as dw's bin rows
        assert(row.fn && row.mockup, `${row.name}: names neither call nor fn and mockup`);
        const s = row.scale || 1;
        const d = drive(row.mockup, row.times || [0, 200, 500, 1000, T], pinned);
        byFace[row.mockup] = (byFace[row.mockup] || []).concat(d.made);
        /* this drive's own canvases first (Task 12: a face whose init drops its cache builds it again on every drive,
           so two rows cut from one builder each see their own), else every drive's so far (a cache kept across
           drives) */
        const own = [...new Set(d.made.filter(c => c.rdFn === row.fn))];
        const tagged = own.length ? own : [...new Set(byFace[row.mockup].filter(c => c.rdFn === row.fn))];
        /* Task 13: row.pick, the pick-th (from 0, in the order made) of a builder that makes several (Hull Plate's
           plateTexture(): its plate, then the plate lit by the work lamp) */
        const pick = row.pick === undefined ? 0 : row.pick;
        assert(row.pick === undefined ? tagged.length === 1 : Number.isInteger(pick) && pick >= 0 && pick < tagged.length,
          `${row.name}: ${row.fn}() made ${tagged.length} canvases while ${row.mockup} ran` + (row.pick === undefined ? '' : `; pick ${row.pick}`));
        const src = row.sprites ? withSprites(row, tagged[pick], d) : tagged[pick];
        if (row.wisp) { add(row, wispStrip(row, src)); continue; }   /* wave 2 Task 12 */
        if (row.a8 || (row.crop && row.kind === 'rgb565')) {   // Task 11: a crop through its a8 rule; Task 12: rgb565
          assert(src.width === src.height, `${row.name}: ${row.fn}() made a ${src.width}x${src.height} canvas, not square`);
          const px = c => onBlack(c, c.width, c.height).getContext('2d').getImageData(0, 0, c.width, c.height).data;
          let base = null;
          if (row.a8 && row.a8.over) {   /* Task 13: the base, another canvas of the same drive (fn, pick) */
            const o = row.a8.over, made = [...new Set(d.made.filter(c => c.rdFn === o.fn))], k = o.pick || 0;
            assert(k < made.length, `${row.name}: its base ${o.fn}() made ${made.length} canvases; pick ${k}`);
            assert(made[k].width === src.width && made[k].height === src.height, `${row.name}: its base is not ${src.width}x${src.height}`);
            base = px(made[k]);
          } else if (row.a8 && row.a8.runs) {   /* wave 2 Task 7: runs over black (no base canvas) */
            base = new Uint8ClampedArray(src.width * src.height * 4);
          }
          add(row, cut(X, row, px(src), src.width, base));
          continue;
        }
        if (row.kind === 'a8_strip') { add(row, { w: src.width, h: src.height, png: png(src) }); continue; }
        assert(src.width === row.w * s && src.height === row.h * s,
          `${row.name}: ${row.fn}() made a ${src.width}x${src.height} canvas; the row says ${row.w}x${row.h} at x${s}`);
        const c = row.kind === 'rgb565' ? onBlack(src, row.w, row.h) : mkc(row.w, row.h);
        if (row.kind !== 'rgb565') realDrawImage.call(c.getContext('2d'), src, 0, 0, row.w, row.h);
        add(row, { w: c.width, h: c.height, png: png(c) });
      }
    }
    return { layers: out, checks: { drawnTexts } };
  };

  /* --selftest (tools/rd_export.mjs): the guard on the list's own corpus, glyph by glyph too, and the shim. */
  window.rdSelftest = async function (corpus) {
    const res = [];
    for (const { text, hit } of corpus) for (const k of ['fillText', 'strokeText']) {
      let threw = false;
      try { mkc(64).getContext('2d')[k](text, 0, 10); } catch (e) { threw = /forbidden/.test(e.message); }
      res.push({ what: `${k} of corpus sample ${JSON.stringify(text.length)} chars ${hit ? 'throws' : 'passes'}`, ok: threw === hit });
    }
    for (const { text, hit } of corpus.filter(c => c.hit).slice(0, 4)) {
      const c = mkc(64).getContext('2d');
      let at = -1;
      [...text].forEach((ch, i) => { if (at < 0) try { c.fillText(ch, i * 8, 10); } catch (e) { if (/forbidden/.test(e.message)) at = i; } });
      res.push({ what: `a sample drawn one glyph per call (${text.length} glyphs) is caught by its end`, ok: hit && at >= 0 });
    }
    const src = '(function () {\n  function hidden(n) { return n * 6; }\n  const k = 7;\n  window.OMNI_FACES = window.OMNI_FACES || [];\n})();\n';
    const url = URL.createObjectURL(new Blob([src], { type: 'text/javascript' }));
    const x = await loadMockup(url, 'selftest.js', ['hidden', 'k', 'absent']);
    res.push({ what: 'the shim exposes a closure-private function and constant', ok: x.hidden(7) === 42 && x.k === 7 && x.absent === undefined });
    let refused = false;
    try { await loadMockup(URL.createObjectURL(new Blob(['var y = 1;\n'], { type: 'text/javascript' })), 'bad.js', []); }
    catch (e) { refused = /anchor/.test(e.message); }
    res.push({ what: 'a file that is not one closure is refused', ok: refused });
    function mk(w) { const c = document.createElement('canvas'); c.width = w; c.height = w; return c; }
    function buildThing() { return mk(8); }
    res.push({ what: 'a canvas made through mk is tagged with its builder', ok: buildThing().rdFn === 'buildThing' });
    /* wave 2 Task 7: row.sprites replays a builder's plain draws (only its) over the row's canvas, and refuses an alpha */
    function paintThing() { return mk(4); }
    const spr = paintThing(), sg = spr.getContext('2d');
    realFillRect.call(Object.assign(sg, { fillStyle: '#fff' }), 0, 0, 4, 4);
    const other = mk(4), og = other.getContext('2d');
    realFillRect.call(Object.assign(og, { fillStyle: '#fff' }), 0, 0, 4, 4);
    const plain = (img, x, y, alpha) => ({ img, a: [x, y], alpha, m: new DOMMatrix(), op: 'source-over' });
    const row = { name: 'selftest', sprites: 'paintThing', mockup: 'selftest' };
    const rep = withSprites(row, mk(16), { draws: [plain(spr, 5, 6, 1), plain(other, 10, 10, 1)] }).getContext('2d');
    const at = (x, y) => rep.getImageData(x, y, 1, 1).data[3];
    let alphaRefused = false;
    try { withSprites(row, mk(16), { draws: [plain(spr, 0, 0, 0.5)] }); } catch (e) { alphaRefused = /sprite drawn/.test(e.message); }
    res.push({ what: "row.sprites replays its builder's draws alone, and refuses one drawn at an alpha",
      ok: at(6, 7) === 255 && at(11, 11) === 0 && at(1, 1) === 0 && alphaRefused });
    return res;
  };
})();
