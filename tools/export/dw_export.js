/* dw_export.js: the dw layer export, page side (dw wave 1 Task 9, WD11). tools/dw_export.mjs loads this first,
   then dw_generic.js, then the designers' mockups (read in place, never copied), and calls window.dwExport(job).

   Before any mockup code exists, this file:
     - seeds Math.random (mulberry32(1), as export.js does);
     - wraps document.createElement so every canvas a mockup makes is tagged with its creating function: the first
       stack frame outside the mockups' canvas helpers (off, mk, layer, canvas, offscreen);
     - wraps fillText and strokeText: any drawn text matching the trademark pattern throws, and the export fails (spec
       section 14). The pattern is not here (public release Task 6): tools/dw_export.mjs hands it over as
       window.DW_FORBIDDEN_SRC before the page loads (tools/export/dw_terms.mjs's, or only the neutral mark in a public
       tree), and this file refuses to draw or export without it.

   window.dwExport(job) drives each row's face (init, wake, then frame at each of the row's times) at the pinned date
   (performance.now frozen per call), and returns each row as a PNG data URL with its record:
     bin      the face's canvas tagged row.fn (exactly one, or row.nth of exactly row.of when one builder makes
              several (wave 2 Task 7); a tagged canvas of another size than w x h x scale fails),
              drawn into a w x h canvas as the face draws it (over black for RGB565); row.rect [x, y, w, h] takes that
              rect of it alone (Task 14: #7's collar over the column, row.origin its place on the screen);
     strip    the tagged canvas as it is (tools/dw_pack.py unwraps it to ORS1); row.luma (wave 2 Task 12) makes an
              opaque one-colour canvas an alpha (its level over the tint, times a radial vignette) for an A8 strip;
     atlas    drawn here from the row's font, size, weight, color, glow and glyphs (cited in the row's src); row.under
              (wave 2 Task 11) draws what a mockup lays under a fill instead (a blurred fill, then a stroke), no fill;
              row.bloom (wave 2 Task 12) the fill's shadow alone (shadowBlur row.bloom), no fill;
     generic  dw_generic.js's drawing (window.DW_GENERIC[row.fn]) at row.scale, drawn into w x h over black;
     crop     rects of the face's frames at pinned.thumb_t_ms (or row.t), each at its own local time (crops[i].local
              on the pinned date), side by side in one w x h strip (Task 13: #4's lit roundels, #5's lit pane);
     radial   a radial gradient from the row's cited values (r0, r1, alpha a0 to a1), alpha only (#5's lamp glow);
     layers   (Task 14) the face's tagged canvases row.layers[i].fn composed over black in order, each but within its
              exclude rect if it names one (#7's base: the glass, and the collar everywhere but over the column);
     drawn    (Task 14) our own drawing of a cited piece of a face (DRAWN[row.fn], below; the mockup draws it straight
              onto its frame, so there is no canvas to find), on transparent at row.scale (#7's stack);
     thumb    the face's settled frame at pinned.thumb_t_ms, clipped to the circle and scaled to 68 px; row.swap draws
              a generic row's image in place of a tagged canvas (the #5 thumbnail shows the generic front, B10; #3's
              its kept leaves from the generic doors, a source rect scaled onto the generic image, wave 2 Task 11), and
              must take at least once in the drive.
   It also checks the #5 geometry (A15/D6): whenever geometry.b5.face is driven, the mockup must draw every pane, the
   lens and the window surrounds exactly where the table says, the rectangles both art sets put their sprites on. */
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
  function assert(ok, msg) { if (!ok) throw new Error('dw export: ' + msg); }

  /* ---- the drawn-text guard (spec section 14): the pattern tools/dw_export.mjs hands over (public release Task 6) */
  const MARK = 'zzneutral' + 'markzz';                /* tools/export/terms.mjs's NEUTRAL_MARK, in two halves */
  const SRC = window.DW_FORBIDDEN_SRC;
  const FORBID = typeof SRC === 'string' && SRC.length >= MARK.length ? new RegExp(SRC, 'i') : null;
  function list() {
    assert(FORBID, 'no DW_FORBIDDEN_SRC: tools/dw_export.mjs hands the list over before the page loads');
    return FORBID;
  }
  const P2 = CanvasRenderingContext2D.prototype;
  const realFillText = P2.fillText, realStrokeText = P2.strokeText, realDrawImage = P2.drawImage, realFillRect = P2.fillRect;
  /* Each call's text is tested, and so is all the text drawn on its canvas so far, joined with nothing between: the
     mockups draw some labels one glyph per call (faces-classic.js tape(), cells()), and the word must not slip
     through glyph by glyph (review M1). */
  let drawnTexts = 0;
  const perCanvas = new WeakMap();
  const KEEP = 256;                                    // the joined text kept per canvas (longer than any word)
  function guard(t, ctx) {
    drawnTexts++;
    const s = String(t);
    if (list().test(s)) throw new Error('dw export: drawn text matches the trademark pattern (spec section 14)');
    if (ctx && ctx.canvas) {
      const joined = ((perCanvas.get(ctx.canvas) || '') + s).slice(-KEEP);
      perCanvas.set(ctx.canvas, joined);
      if (list().test(joined))
        throw new Error('dw export: the text drawn on one canvas, joined, matches the trademark pattern (spec section 14)');
    }
  }
  P2.fillText = function (t, ...rest) { guard(t, this); return realFillText.call(this, t, ...rest); };
  P2.strokeText = function (t, ...rest) { guard(t, this); return realStrokeText.call(this, t, ...rest); };

  /* ---- canvas tags */
  Error.stackTraceLimit = 64;
  const HELPERS = new Set(['off', 'mk', 'layer', 'canvas', 'offscreen']);
  const registry = [];
  const realCreate = Document.prototype.createElement;
  function callerOf(stack) {
    for (const line of stack.split('\n').slice(2)) {   /* past "Error" and the wrapper's own frame */
      const m = line.match(/^\s*at (?:new )?([^\s(]+) \(/);
      const name = m ? m[1].split('.').pop() : '<anonymous>';
      if (!HELPERS.has(name)) return name;
    }
    return '<none>';
  }
  Document.prototype.createElement = function (tag, opts) {
    const el = realCreate.call(this, tag, opts);
    if (String(tag).toLowerCase() === 'canvas') { el.dwFn = callerOf(new Error().stack); registry.push(el); }
    return el;
  };
  /* Our own canvases: untagged, never in the registry. */
  function mkc(w, h) { const c = realCreate.call(document, 'canvas'); c.width = w; c.height = h || w; return c; }
  function onBlack(src, w, h) {
    const c = mkc(w, h), g = c.getContext('2d');
    realFillRect.call(Object.assign(g, { fillStyle: '#000' }), 0, 0, w, h);
    realDrawImage.call(g, src, 0, 0, w, h);
    return c;
  }

  /* ---- driving a face */
  const realPerf = performance.now.bind(performance);
  function faceEnv(pinned) {
    return { W, cx: CX, cy: CY, R: W / 2, assets: {}, reducedMotion: false, timeValid: true, awake: true,
      now: () => new Date(pinned), location: { name: 'Columbus, OH', lat: 39.96, lon: -83.0 }, clock: { state: 'ok' },
      goto() {} };
  }
  /* init, wake, frame at each time; returns the last frame (466, over black) and the canvases made meanwhile. */
  function drive(id, times, pinned, hooks) {
    const f = window.OMNI_FACES.find(x => x.id === id);
    assert(f, 'no mockup face ' + id);
    const start = registry.length, rects = [];
    let perf = 0;
    performance.now = () => perf;
    P2.fillRect = function (x, y, w, h) { rects.push([x, y, w, h]); return realFillRect.call(this, x, y, w, h); };
    let swapped = 0;
    if (hooks && hooks.swap) {
      P2.drawImage = function (img, ...rest) {
        const s = img && img.dwFn && hooks.swap[img.dwFn];
        if (!s) return realDrawImage.call(this, img, ...rest);
        swapped++;
        /* a source rect (the nine-argument form: drawLeaf's columns, faces-reveal.js:170) is in the tagged canvas's
           pixels: scale it onto the generic image's (wave 2 Task 11: #3's kept leaves from the generic doors) */
        if (rest.length === 8) {
          const kx = s.width / img.width, ky = s.height / img.height;
          rest = [rest[0] * kx, rest[1] * ky, rest[2] * kx, rest[3] * ky, ...rest.slice(4)];
        }
        return realDrawImage.call(this, s, ...rest);
      };
    }
    try {
      Math.random = mulberry32(1);
      const env = faceEnv(pinned);
      if (f.init) f.init(env);
      if (f.wake) f.wake(env);
      let full = null;
      for (const t of times) {
        perf = t;
        full = mkc(W);
        const g = full.getContext('2d');
        g.fillStyle = '#000'; realFillRect.call(g, 0, 0, W, W);
        g.save(); f.frame(g, t, env); g.restore();
      }
      return { full, made: registry.slice(start), rects, swapped };
    } finally {
      performance.now = realPerf; P2.fillRect = realFillRect; P2.drawImage = realDrawImage;
    }
  }

  /* A15/D6: the mockup draws the panes, the lens and the window surrounds on the table's rectangles. */
  function checkB5(geo, d, rectsBy) {
    /* the rects of every drive of this face so far: a mockup caches its layers (boxFrame), so a later drive finds the
       window surrounds drawn by an earlier one (Task 13: #5's pane crop drives it before its thumbnail) */
    const all = rectsBy[geo.face] = (rectsBy[geo.face] || []).concat(d.rects);
    const has = r => all.some(q => q.every((v, i) => Math.abs(v - r[i]) < 1e-9));
    for (const p of geo.panes) assert(has(p), `#5 pane ${p} is not where the mockup draws a pane`);
    assert(has(geo.lens), `#5 lens ${geo.lens} is not where the mockup draws the lens`);
    for (const [x, y, w, h] of geo.windows) assert(has([x - 5, y - 5, w + 10, h + 10]), `#5 window ${[x, y, w, h]} is not boxFrame's`);
    return geo.panes.length;
  }

  /* Task 13: geometry.r4.at[i] is roundel i + 1's lit box (geometry.r4.cell square), centred on the hour position the
     mockup draws it at (faces-classic.js ANA.R about the centre, hour h at h / 12 of a turn from 12 o'clock) */
  function checkCrop(key, geom, i, rect) {
    const g = geom[key];
    if (key !== 'r4' || !g || !g.at[i]) return false;
    const a = (i + 1) / 12 * TAU - Math.PI / 2, x = CX + Math.cos(a) * g.r, y = CY + Math.sin(a) * g.r, h = g.cell / 2;
    return rect[0] === g.at[i][0] && rect[1] === g.at[i][1] && rect[2] === g.cell && rect[3] === g.cell &&
      g.at[i][0] === Math.round(x) - h && g.at[i][1] === Math.round(y) - h;
  }

  /* ---- atlases: our own drawing from the row's cited values */
  function drawAtlas(row) {
    const font = `${row.weight || 400} ${row.size}px "${row.font}"`;
    assert(document.fonts.check(font), `${row.asset}: font ${font} is not loaded`);
    guard(row.glyphs, null);
    const pad = row.pad !== undefined ? row.pad : Math.ceil((row.glow || 0) * 2) + 1;
    const m = mkc(4), mg = m.getContext('2d');
    mg.font = font;
    const met = [...row.glyphs].map(ch => {
      const t = mg.measureText(ch);
      return { ch, l: Math.ceil(t.actualBoundingBoxLeft), r: Math.ceil(t.actualBoundingBoxRight),
        a: Math.ceil(t.actualBoundingBoxAscent), d: Math.ceil(t.actualBoundingBoxDescent), adv: Math.round(t.width) };
    });
    const hMax = Math.max(...met.map(g => Math.max(0, g.a + g.d) + 2 * pad));
    /* cells left to right, wrapped onto a new line of cells (1 px apart) before a line would pass row.maxw (Task 13:
       the #5 atlases stay within 200 px) */
    const maxw = row.maxw || 4096;
    let x = 0, y = 0, wide = 0;
    const cells = met.map(g => {
      const w = Math.max(0, g.l + g.r) + 2 * pad;
      if (x > 0 && x + w > maxw) { x = 0; y += hMax + 1; }
      const c = { x, y, w, h: hMax, g };
      x += w + 1; wide = Math.max(wide, x - 1);
      return c;
    });
    assert(wide <= maxw, `${row.asset}: atlas ${wide} px wide (at most ${maxw})`);
    /* an even width: the packed file (12 + 3 w h bytes) is then an even length, so the next file stays even (C6) */
    const c = mkc(Math.max(2, (wide + 1) & ~1), y + hMax), g = c.getContext('2d');
    g.font = font; g.textAlign = 'left'; g.textBaseline = 'alphabetic'; g.fillStyle = row.color;
    const top = Math.max(...met.map(q => q.a));
    for (const cell of cells) {
      const px = cell.x + pad + cell.g.l, py = cell.y + pad + top;
      if (row.gradient) {   /* Task 14: a vertical gradient fixed to the baseline (stops [dy, colour], dy from it) */
        const gr = g.createLinearGradient(0, py + row.gradient[0][0], 0, py + row.gradient[row.gradient.length - 1][0]);
        const d0 = row.gradient[0][0], span = row.gradient[row.gradient.length - 1][0] - d0;
        for (const [dy, col] of row.gradient) gr.addColorStop((dy - d0) / span, col);
        g.fillStyle = gr;
      }
      if (row.glow) {
        g.shadowColor = row.glowColor || row.color; g.shadowBlur = row.glow;
        g.fillText(cell.g.ch, px, py);
        g.shadowBlur = 0; g.shadowColor = 'transparent';
      }
      if (row.bloom) {   /* wave 2 Task 12: a fill's shadow alone (shadowBlur row.bloom in row.color), no fill: the
                            glyph drawn far off the canvas with its shadow offset back onto its cell */
        const OFF = 4096;
        g.shadowColor = row.color; g.shadowBlur = row.bloom; g.shadowOffsetX = OFF;
        g.fillText(cell.g.ch, px - OFF, py);
        g.shadowBlur = 0; g.shadowOffsetX = 0; g.shadowColor = 'transparent';
        continue;
      }
      if (row.under) {   /* wave 2 Task 11: what a mockup draws under its fill, alone (no fill): a blurred fill in
                            under.blurColor (filter blur(under.blur) in this atlas's own pixels), then a stroke
                            under.stroke px wide in under.strokeColor; the face draws the fill from its own atlas */
        g.save(); g.filter = `blur(${row.under.blur}px)`; g.fillStyle = row.under.blurColor;
        g.fillText(cell.g.ch, px, py); g.restore();
        g.lineWidth = row.under.stroke; g.strokeStyle = row.under.strokeColor; g.lineJoin = 'miter';
        g.strokeText(cell.g.ch, px, py);
        continue;
      }
      g.fillText(cell.g.ch, px, py);
    }
    return { c, glyphs: row.glyphs, line_h: hMax,
      cells: cells.map(q => ({ x: q.x, y: q.y, w: q.w, h: q.h, ox: -(pad + q.g.l), oy: -(pad + top), adv: q.g.adv })) };
  }

  /* ---- a strip row's luma (wave 2 Task 12): an opaque one-colour canvas (the mockup puts row.luma.tint x v in each
     pixel, alpha 255) made an A8 source: white at alpha v (its blue over the tint's), times the radial vignette the
     face lays over it later, row.luma.vignette [cx, cy, r0, r1, a1] (createRadialGradient from r0 at alpha 0 to r1 at a1
     in black: each pixel x (1 - a1 clamp((r - r0) / (r1 - r0), 0, 1)) at its centre). Radial, so it turns with the ring
     and bakes into the strip's rows (R147 PR8); dw_annulus_draw_a8 tints it back. */
  function lumaAlpha(row, c) {
    const s = c.width / W, g = c.getContext('2d'), img = g.getImageData(0, 0, c.width, c.height), d = img.data;
    const tb = row.luma.tint[2], [vx, vy, r0, r1, a1] = row.luma.vignette;
    for (let y = 0; y < c.height; y++) for (let x = 0; x < c.width; x++) {
      const i = (y * c.width + x) * 4;
      if (d[i + 3] === 0) continue;
      assert(d[i + 3] === 255, `${row.asset}: luma wants an opaque canvas`);
      const r = Math.hypot((x + 0.5) / s - vx, (y + 0.5) / s - vy);
      const v = Math.min(1, d[i + 2] / tb) * (1 - a1 * Math.min(1, Math.max(0, (r - r0) / (r1 - r0))));
      d[i] = d[i + 1] = d[i + 2] = 255; d[i + 3] = Math.round(255 * v);
    }
    g.putImageData(img, 0, 0);
  }

  /* ---- drawn rows (Task 14): our own transcription of a cited piece the mockup draws straight onto its frame */
  const DRAWN = {
    /* faces-reveal.js:887 doorwayOver(): #3's smoke backplate, an ellipse 118 x 56 about (233, 312) filled black at .82
       under blur(16px) on the mockup's 2x layer (8 screen px: a canvas filter's lengths are the canvas's own pixels, not
       the transform's), at row.scale 2 as there; drawn into the row's box, its top-left at row.origin on the screen
       (wave 2 Task 11) */
    plate(g, row) {
      const [ox, oy] = row.origin;
      g.save(); g.filter = 'blur(16px)'; g.fillStyle = 'rgba(0,0,0,.82)';
      g.beginPath(); g.ellipse(233 - ox, 312 - oy, 118, 56, 0, 0, TAU); g.fill(); g.restore();
    },
    /* faces-classic.js:729-743 classic-rotor frame(): the perspex plates, the rods and the chrome spheres at coreL 1
       (the brightest the stack gets; rotor.c blends the sprite at the plates' level), the stack's base line (baseY) at
       row.base and its axis at the middle; the core's light (724-728) is not in it: rotor.c draws it, since it alone
       goes dark when the clock is unset */
    rotorStack(g, row) {
      const cx = row.w / 2, baseY = row.base, coreL = 1, P = { cream: '242,234,216' };
      for (let k = 0; k < 12; k++) {
        const y = baseY - 8 - k * 13, w = 40 - k * 2.2 + (k % 3 === 0 ? 4 : 0);
        g.strokeStyle = `rgba(${P.cream},${0.26 + 0.40 * coreL})`; g.lineWidth = 1;
        g.beginPath(); g.ellipse(cx, y, w, 3.2, 0, 0, TAU); g.stroke();
        g.strokeStyle = `rgba(255,255,255,${0.35 + 0.55 * coreL})`; g.lineWidth = 1.4;
        g.beginPath(); g.ellipse(cx, y, w, 3.2, 0, 0.15 * Math.PI, 0.85 * Math.PI); g.stroke();
      }
      g.strokeStyle = `rgba(${P.cream},0.30)`; g.lineWidth = 1;
      for (const dx of [-30, -14, 14, 30]) { g.beginPath(); g.moveTo(cx + dx, baseY - 4); g.lineTo(cx + dx, baseY - 150 + Math.abs(dx) * 0.8); g.stroke(); }
      for (const dx of [-22, 22]) {
        const sg = g.createRadialGradient(cx + dx - 2, baseY - 2, 0, cx + dx, baseY, 6);
        sg.addColorStop(0, '#fff'); sg.addColorStop(0.5, '#8a8f88'); sg.addColorStop(1, '#1a1c19');
        g.fillStyle = sg; g.beginPath(); g.arc(cx + dx, baseY, 6, 0, TAU); g.fill();
      }
    },
    /* faces-classic.js:110-143 roundelSprite(row.r, colour, row.lit) for each of row.colors, stacked down in row.cell px
       cells (wave 2 Task 10, WD27, R147 PR16: #6's unlit well and its six lit sprites, one per hour colour). The mockup
       makes its sprite 2 (r + ceil(.45 r)) px square (51 for r 17.5) about its middle; here each is centred in its
       row.cell square (52), so the sprite is whole pixels about (26, 26); its drawing is the mockup's, line for line, at
       the one level the device blends (wall.c draws the lit sprite over the unlit at the cell's level) */
    roundels(g, row) {
      const r = row.r, L = Math.round(Math.max(0, Math.min(1, row.lit)) * 24) / 24, pad = Math.ceil(r * 0.45);
      const S = row.cell, o = S / 2, cream = '#F2EAD8';
      assert(2 * (r + pad) <= S, `${row.asset}: a ${2 * (r + pad)} px sprite does not fit its ${S} px cell`);
      assert(row.h === S * row.colors.length && row.w === S, `${row.asset}: not ${row.colors.length} cells of ${S} px`);
      const rgba = (hex, a) => { const n = parseInt(hex.slice(1), 16); return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`; };
      row.colors.forEach((color, k) => {
        g.save(); g.translate(0, k * S);
        g.beginPath(); g.rect(0, 0, S, S); g.clip();
        if (L > 0) {
          const sp = g.createRadialGradient(o, o, r * 0.9, o, o, r + pad);
          sp.addColorStop(0, rgba(color, 0.22 * L)); sp.addColorStop(1, rgba(color, 0));
          g.fillStyle = sp; g.beginPath(); g.arc(o, o, r + pad, 0, TAU); g.fill();
        }
        const fx = o - r * 0.18, fy = o - r * 0.22;
        const fg = g.createRadialGradient(fx, fy, r * 0.05, o, o, r);
        if (L > 0) {
          fg.addColorStop(0, rgba(color, 0.18 + 0.80 * L)); fg.addColorStop(0.7, rgba(color, 0.12 + 0.55 * L)); fg.addColorStop(1, rgba(color, 0.08 + 0.32 * L));
        } else { fg.addColorStop(0, '#13120f'); fg.addColorStop(1, '#090807'); }
        g.fillStyle = fg; g.beginPath(); g.arc(o, o, r, 0, TAU); g.fill();
        g.save(); g.beginPath(); g.arc(o, o, r, 0, TAU); g.clip();
        const sh = g.createRadialGradient(o + r * 0.22, o + r * 0.28, r * 0.72, o + r * 0.22, o + r * 0.28, r * 1.18);
        sh.addColorStop(0, 'rgba(0,0,0,0)'); sh.addColorStop(1, `rgba(0,0,0,${L > 0 ? 0.55 : 0.6})`);
        g.fillStyle = sh; g.fillRect(0, 0, S, S); g.restore();
        g.lineWidth = Math.max(1, r * 0.05);
        g.strokeStyle = L > 0 ? `rgba(255,255,255,${0.10 + 0.32 * L})` : rgba(cream, 0.15);
        g.beginPath(); g.arc(o, o, r * 0.80, 0, TAU); g.stroke();
        g.strokeStyle = 'rgba(0,0,0,0.35)'; g.lineWidth = 1; g.beginPath(); g.arc(o + 0.6, o + 0.9, r * 0.80 - 1.5, 0, TAU); g.stroke();
        g.lineWidth = Math.max(1.2, r * 0.07);
        const rim = g.createLinearGradient(o - r, o - r, o + r, o + r);
        rim.addColorStop(0, `rgba(242,234,216,${0.07 + 0.08 * L})`); rim.addColorStop(1, `rgba(242,234,216,${0.32 + 0.22 * L})`);
        g.strokeStyle = rim; g.beginPath(); g.arc(o, o, r + g.lineWidth / 2, 0, TAU); g.stroke();
        g.restore();
      });
    },
  };

  async function fontsReady() {
    const want = ['500 20px "Cormorant SC"', '700 20px "Cormorant SC"', '400 20px "IBM Plex Sans"',
      '500 20px "IBM Plex Sans"', '600 20px "IBM Plex Sans"', '400 20px "IBM Plex Mono"', '500 20px "IBM Plex Mono"'];
    await Promise.all(want.map(f => document.fonts.load(f)));
    for (const f of want) assert(document.fonts.check(f), f + ' is not loaded (tools/fonts.sh)');
    await new Promise(r => setTimeout(r, 0));   // the mockups' askFonts() reset their caches on this load: let it run
  }

  /* job: { rows, geometry, pinned: { utc, thumb_t_ms } } */
  window.dwExport = async function (job) {
    list();
    await fontsReady();
    const alias = job.alias || {}, fid = id => alias[id] || id;   /* table face id -> mockup face id (DW_FACE_ALIAS) */
    const pinned = Date.parse(job.pinned.utc), T = job.pinned.thumb_t_ms;
    assert(!isNaN(pinned), 'bad pinned date');
    const geo = (job.geometry || {}).b5;
    const out = [], made = {}, checks = { b5: 0 }, byFace = {}, rectsBy = {};
    const add = (row, c, extra) => {
      made[row.asset] = c;
      out.push(Object.assign({ asset: row.asset, file: row.file, cf: row.cf, out: row.out, w: c.width, h: c.height,
        png: c.toDataURL('image/png') }, extra || {}));
    };
    for (const row of job.rows) {
      if (row.crc32) continue;                          // committed bytes, not exported
      const s = row.scale || 1;
      if (row.out === 'generic') {
        const fn = window.DW_GENERIC && window.DW_GENERIC[row.fn];
        assert(fn, `${row.asset}: dw_generic.js has no ${row.fn}()`);
        const c = mkc(row.w * s, row.h * s), g = c.getContext('2d');
        g.scale(s, s);
        fn(g, job.geometry || {});
        add(row, onBlack(c, row.w, row.h));
      } else if (row.out === 'atlas') {
        const a = drawAtlas(row);
        add(row, a.c, { cells: a.cells, glyphs: a.glyphs, line_h: a.line_h });
      } else if (row.out === 'crop') {                 // rects of the face's settled frames, side by side
        const c = mkc(row.w, row.h), g = c.getContext('2d');
        let x = 0;
        row.crops.forEach((cr, i) => {
          const [hh, mm, ss] = cr.local.split(':').map(Number), at = new Date(pinned);
          at.setHours(hh, mm, ss, 0);                   // the page's zone is pinned.zone (tools/dw_export.mjs)
          const d = drive(fid(row.face), [row.t || T], at.getTime(), {});
          byFace[row.face] = (byFace[row.face] || []).concat(d.made);
          if (geo && row.face === geo.face) checks.b5 += checkB5(geo, d, rectsBy);
          const [rx, ry, rw, rh] = cr.rect;
          if (row.check) assert(checkCrop(row.check, job.geometry || {}, i, cr.rect), `${row.asset}: crop ${i} ${cr.rect} is not geometry.${row.check}'s`);
          assert(rh === row.h && x + rw <= row.w, `${row.asset}: crop ${i} does not fit the ${row.w}x${row.h} strip`);
          realDrawImage.call(g, d.full, rx, ry, rw, rh, x, 0, rw, rh);
          x += rw;
        });
        assert(x === row.w, `${row.asset}: the crops fill ${x} of ${row.w} px`);
        add(row, c);
      } else if (row.out === 'radial') {               // our own drawing of a cited radial gradient (alpha only)
        const c = mkc(row.w, row.h), g = c.getContext('2d'), ox = row.w / 2, oy = row.h / 2;
        const gr = g.createRadialGradient(ox, oy, row.r0, ox, oy, row.r1);
        gr.addColorStop(0, `rgba(255,255,255,${row.a0})`); gr.addColorStop(1, `rgba(255,255,255,${row.a1})`);
        g.fillStyle = gr; g.beginPath(); g.arc(ox, oy, row.r1, 0, TAU); g.fill();
        add(row, c);
      } else if (row.out === 'drawn') {
        const fn = DRAWN[row.fn];
        assert(fn, `${row.asset}: dw_export.js draws no ${row.fn}()`);
        const c = mkc(row.w * s, row.h * s), g = c.getContext('2d');
        g.scale(s, s);
        fn(g, row);
        const o = mkc(row.w, row.h);
        realDrawImage.call(o.getContext('2d'), c, 0, 0, row.w, row.h);
        add(row, row.cf === 'RGB565' ? onBlack(o, row.w, row.h) : o);
      } else if (row.out === 'layers') {           // tagged canvases composed over black, each but its exclude rect
        const d = drive(fid(row.face), row.times || [0, 200, 500, 1000, T], pinned, {});
        byFace[row.face] = (byFace[row.face] || []).concat(d.made);
        const c = mkc(row.w, row.h), g = c.getContext('2d');
        realFillRect.call(Object.assign(g, { fillStyle: '#000' }), 0, 0, row.w, row.h);
        for (const L of row.layers) {
          const tagged = [...new Set(byFace[row.face].filter(q => q.dwFn === L.fn))];
          assert(tagged.length === 1, `${row.asset}: ${L.fn}() made ${tagged.length} canvases while ${row.face} ran`);
          assert(tagged[0].width === row.w * s && tagged[0].height === row.h * s, `${row.asset}: ${L.fn}() is not ${row.w}x${row.h} at x${s}`);
          g.save();
          if (L.exclude) {
            const [ex, ey, ew, eh] = L.exclude;
            g.beginPath(); g.rect(0, 0, row.w, row.h); g.rect(ex, ey, ew, eh); g.clip('evenodd');
          }
          realDrawImage.call(g, tagged[0], 0, 0, row.w, row.h);
          g.restore();
        }
        add(row, c);
      } else if (row.out === 'thumb') {
        const swap = {};
        for (const [fn, asset] of Object.entries(row.swap || {})) {
          assert(made[asset], `${row.asset}: swap ${fn} -> ${asset}, which is not drawn before it`);
          swap[fn] = made[asset];
        }
        const d = drive(fid(row.face), [T], pinned, { swap });
        /* a swap must take: a mockup that drew a cached layer made from the tagged canvas before this drive (#3's
           kept leaves, faces-reveal.js drawStaticLeaves) would show the designed art; every earlier row of the face
           must stop before anything caches it (wave 2 Task 11) */
        assert(!row.swap || d.swapped > 0, `${row.asset}: no swap of ${Object.keys(row.swap || {})} took while ${row.face} ran`);
        byFace[row.face] = (byFace[row.face] || []).concat(d.made);
        if (geo && row.face === geo.face) checks.b5 += checkB5(geo, d, rectsBy);
        const g = d.full.getContext('2d');
        g.globalCompositeOperation = 'destination-in';   // the panel is round (index.html tick())
        g.beginPath(); g.arc(CX, CY, W / 2, 0, TAU); g.fill();
        const c = mkc(row.w, row.h), t = c.getContext('2d');
        t.imageSmoothingEnabled = true; t.imageSmoothingQuality = 'high';
        realDrawImage.call(t, d.full, 0, 0, row.w, row.h);
        add(row, c);
      } else {                                          // bin, strip: a tagged canvas
        const d = drive(fid(row.face), row.times || [0, 200, 500, 1000, T], pinned, {});
        if (geo && row.face === geo.face) checks.b5 += checkB5(geo, d, rectsBy);
        /* every canvas this face has made in this export: a mockup caches its layers (faces-reveal.js K), so a
           second row of one face finds its canvas made by an earlier row's drive (Task 11: #1 has four layers) */
        byFace[row.face] = (byFace[row.face] || []).concat(d.made);
        const tagged = [...new Set(byFace[row.face].filter(c => c.dwFn === row.fn))];
        assert(tagged.length, `${row.asset}: driving ${row.face} made no canvas from ${row.fn}()`);
        for (const c of tagged)
          assert(row.rect || (c.width === row.w * s && c.height === row.h * s),
            `${row.asset}: ${row.fn}() made a ${c.width}x${c.height} canvas; the row says ${row.w}x${row.h} at x${s}`);
        /* row.nth of row.of (wave 2 Task 7): one builder makes several canvases (faces-modern.js fwBuild: the lid, its
           back and the dial, in that order); the row takes the nth made, and the builder must have made exactly of */
        assert((row.nth === undefined) === (row.of === undefined), `${row.asset}: nth and of go together (one without the other)`);
        const want = row.nth === undefined ? 1 : row.of;
        assert(row.nth === undefined || (Number.isInteger(row.nth) && row.nth >= 0 && row.nth < row.of),
          `${row.asset}: nth ${row.nth} is not one of ${row.of}`);
        assert(tagged.length === want, `${row.asset}: ${row.fn}() made ${tagged.length} canvases while ${row.face} ran (the row says ${want})`);
        let src = tagged[row.nth || 0];
        if (row.rect) {                                   // one rect of it (Task 14: #7's collar over the column)
          const [rx, ry, rw, rh] = row.rect;
          assert(rw === row.w && rh === row.h, `${row.asset}: rect ${row.rect} is not ${row.w}x${row.h}`);
          assert(!row.origin || (row.origin[0] === rx && row.origin[1] === ry), `${row.asset}: origin is not the rect's`);
          const c = mkc(rw * s, rh * s);
          realDrawImage.call(c.getContext('2d'), src, rx * s, ry * s, rw * s, rh * s, 0, 0, rw * s, rh * s);
          src = c;
        }
        if (row.out === 'strip') {
          const c = mkc(src.width, src.height);
          realDrawImage.call(c.getContext('2d'), src, 0, 0);
          if (row.luma) lumaAlpha(row, c);
          add(row, c);
        }
        else if (row.cf === 'A8') { const c = mkc(row.w, row.h); realDrawImage.call(c.getContext('2d'), src, 0, 0, row.w, row.h); add(row, c); }
        else if (row.cf === 'RGB565') add(row, onBlack(src, row.w, row.h));
        else { const c = mkc(row.w, row.h); realDrawImage.call(c.getContext('2d'), src, 0, 0, row.w, row.h); add(row, c); }
      }
    }
    checks.drawnTexts = drawnTexts;
    return { layers: out, checks };
  };

  /* --selftest (tools/dw_export.mjs): the guard and the tags, on a test page with no mockups. */
  window.dwSelftest = function () {
    const res = [];
    list();
    const words = window.DW_SELFTEST_WORDS;
    res.push({ what: 'the self-test words include the neutral mark', ok: Array.isArray(words) && words.includes(MARK) });
    for (const w of Array.isArray(words) ? words : []) for (const k of ['fillText', 'strokeText']) {
      let threw = false;
      try { mkc(64).getContext('2d')[k](w, 0, 10); } catch (e) { threw = /trademark/.test(e.message); }
      res.push({ what: w === MARK ? `${k} of the neutral mark` : `${k} of a forbidden word (${w.length} chars)`, ok: threw });
    }
    for (const w of ['call box', 'Time Traveller', '10:09', 'abbcd']) {
      let threw = false;
      try { mkc(64).getContext('2d').fillText(w, 0, 10); } catch (e) { threw = true; }
      res.push({ what: `fillText ${JSON.stringify(w)} passes`, ok: !threw });
    }
    /* M1: a forbidden word drawn one glyph per call, on one canvas, is caught at its last glyph */
    const glyphs = (k, w) => {
      const c = mkc(64).getContext('2d');
      let threwAt = -1;
      [...w].forEach((ch, i) => { if (threwAt < 0) try { c[k](ch, i * 8, 10); } catch (e) { if (/trademark/.test(e.message)) threwAt = i; } });
      return threwAt;
    };
    for (const k of ['fillText', 'strokeText'])
      res.push({ what: `${k} of the neutral mark one glyph per call (${MARK.length} glyphs) caught at its last glyph`,
        ok: glyphs(k, MARK) === MARK.length - 1 });
    for (const w of (Array.isArray(words) ? words : []).filter(x => x !== MARK))
      res.push({ what: `fillText of a forbidden word one glyph per call (${w.length} glyphs) caught by its end`, ok: glyphs('fillText', w) >= 0 });
    {
      const a = mkc(64).getContext('2d'), b = mkc(64).getContext('2d');
      let threw = false;
      try { a.fillText('zzneutral', 0, 10); b.fillText('markzz', 0, 10); } catch (e) { threw = true; }
      res.push({ what: 'halves of a word on two different canvases are not joined', ok: !threw });
    }
    function mk(w) { const c = document.createElement('canvas'); c.width = w; c.height = w; return c; }
    function layer(fn) { const c = mk(8); fn(c.getContext('2d')); return c; }
    function buildThing() { return layer(() => {}); }
    function direct() { return document.createElement('canvas'); }
    res.push({ what: 'a canvas made through mk and layer is tagged with its builder', ok: buildThing().dwFn === 'buildThing' });
    res.push({ what: 'a canvas made directly is tagged with its maker', ok: direct().dwFn === 'direct' });
    return res;
  };
})();
