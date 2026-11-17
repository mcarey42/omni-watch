/* dw_generic.js: the dw theme's generic art, our own drawing (dw wave 1 Task 9, WD20, R136 item 12). These are
   the files the private art overlay may replace, so the public build carries original time-machine art and no
   call-box sign, lettering or silhouette (R132):
     doors(g, geo)  the doors: two tall leaves in dark green, hinged at the screen's edges (x 0 and 466) and meeting at
                    the seam (x 233), the designed set's frame (faces-reveal.js:58-118). Circular glazing and brass
                    bands in place of panels and windows; warm light at the seam. No sign box, no lettering, no
                    lantern, no roof, no call-box outline.
     doors_c(g)     the Classic doors (wave 2 Task 4, WD26): the same frame and hinge columns, in a lighter dark green
                    (20, 72, 44), with vertical grain, hinge shading and a lintel shadow. Laid out unlike doors(): one
                    large porthole high on each leaf and two small ones low, between brass bands; on the left leaf a
                    blank cream enamel plate where the designed set has its notice (96, 199, 98, 58), with no
                    lettering. Warm light at the seam. No sign box, lettering,
                    lantern, roof, panelled windows or call-box outline.
     front(g, geo)  #5's front: a lit time sign over a pair of glazed doors, their leaves filled with doors()' dark
                    green gradient, with no roof and no call-box outline. The window surrounds sit on geo.b5's pane
                    rectangles and a lamp body under geo.b5's lens, because the shared dw_b5_pane and dw_b5_lamp
                    sprites are drawn there in both art sets (D6).
   The colours are dark green with brass (public release C14; the private overlay keeps its own art). Each draws in
   the 466 px screen space on a context already scaled by its row's scale (dw_export.js), and draws only with fills,
   strokes and gradients: no text. Deterministic: our own seeded generator, no Math.random, so
   tools/dw_assets.sh --check can render it again and byte-compare the committed files (G1). */
(function () {
  'use strict';
  const W = 466, C = 233, TAU = Math.PI * 2;

  function rng(seed) {   /* mulberry32, our own seed per drawing */
    return function () {
      seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
      let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  const rgba = (c, a) => `rgba(${c[0]},${c[1]},${c[2]},${a})`;
  const GREEN = [12, 46, 30], GREEN_HI = [24, 78, 52], GREEN_LO = [5, 22, 14], SPECK = [150, 220, 170], BRASS = ['#5a4220', '#c9a35b', '#f1d79a', '#8a6a34', '#3a2a12'];

  function brassGrad(g, x0, y0, x1, y1) {
    const gr = g.createLinearGradient(x0, y0, x1, y1);
    BRASS.forEach((c, i) => gr.addColorStop(i / (BRASS.length - 1), c));
    return gr;
  }
  /* A horizontal brass band with rivets, x0..x1 at y (h tall). */
  function band(g, x0, x1, y, h) {
    g.fillStyle = 'rgba(0,0,0,.55)'; g.fillRect(x0, y + 1.5, x1 - x0, h);
    g.fillStyle = brassGrad(g, 0, y, 0, y + h); g.fillRect(x0, y, x1 - x0, h);
    g.fillStyle = 'rgba(255,240,200,.35)'; g.fillRect(x0, y, x1 - x0, .8);
    for (let x = x0 + 9; x < x1 - 4; x += 22) {
      g.fillStyle = '#2c200e'; g.beginPath(); g.arc(x + .4, y + h / 2 + .5, 1.6, 0, TAU); g.fill();
      g.fillStyle = '#f6e2ae'; g.beginPath(); g.arc(x, y + h / 2, 1.2, 0, TAU); g.fill();
    }
  }
  /* A round window: a brass ring, warm glass behind four glazing bars and an inner ring. lit 0..1. */
  function porthole(g, x, y, r, lit, R) {
    g.fillStyle = 'rgba(0,0,0,.6)'; g.beginPath(); g.arc(x + 1.5, y + 2, r + 6, 0, TAU); g.fill();
    g.fillStyle = brassGrad(g, x - r, y - r, x + r, y + r); g.beginPath(); g.arc(x, y, r + 5, 0, TAU); g.fill();
    g.fillStyle = '#1a1206'; g.beginPath(); g.arc(x, y, r + 1, 0, TAU); g.fill();
    const gl = g.createRadialGradient(x - r * .15, y - r * .2, 1, x, y, r);
    gl.addColorStop(0, rgba([255, 244, 214], lit)); gl.addColorStop(.55, rgba([236, 196, 128], .92 * lit));
    gl.addColorStop(1, rgba([150, 98, 44], .85 * lit));
    g.fillStyle = '#060504'; g.beginPath(); g.arc(x, y, r, 0, TAU); g.fill();
    g.fillStyle = gl; g.beginPath(); g.arc(x, y, r, 0, TAU); g.fill();
    for (let i = 0; i < 40; i++) {   /* seeds in the glass */
      const a = R() * TAU, d = Math.sqrt(R()) * r * .92;
      g.fillStyle = `rgba(255,255,255,${(R() * .14).toFixed(3)})`; g.fillRect(x + Math.cos(a) * d, y + Math.sin(a) * d, 1, 1);
    }
    g.strokeStyle = 'rgba(40,28,10,.9)'; g.lineWidth = 3;
    g.beginPath(); g.arc(x, y, r * .48, 0, TAU); g.stroke();
    for (let k = 0; k < 4; k++) {
      const a = k * TAU / 4 + TAU / 8;
      g.beginPath(); g.moveTo(x + Math.cos(a) * r * .48, y + Math.sin(a) * r * .48); g.lineTo(x + Math.cos(a) * r, y + Math.sin(a) * r); g.stroke();
    }
    g.strokeStyle = 'rgba(255,236,190,.45)'; g.lineWidth = 1;
    g.beginPath(); g.arc(x, y, r + 4.2, Math.PI * 1.05, Math.PI * 1.7); g.stroke();
  }
  /* A brass ring pull on its plate. */
  function pull(g, x, y) {
    g.fillStyle = 'rgba(0,0,0,.5)'; g.beginPath(); g.arc(x + 1.2, y + 1.6, 7.5, 0, TAU); g.fill();
    g.fillStyle = brassGrad(g, x - 7, y - 7, x + 7, y + 7); g.beginPath(); g.arc(x, y, 7, 0, TAU); g.fill();
    g.strokeStyle = '#f3dca2'; g.lineWidth = 2.2; g.beginPath(); g.arc(x, y + 9, 7, Math.PI * 1.15, Math.PI * 1.85, true); g.stroke();
  }

  /* The Modern leaves' vertical gradient, over the full 466 px height. */
  function leafGrad(g) {
    const lg = g.createLinearGradient(0, 0, 0, W);
    lg.addColorStop(0, rgba(GREEN_HI, 1)); lg.addColorStop(.5, rgba(GREEN, 1)); lg.addColorStop(1, rgba(GREEN_LO, 1));
    return lg;
  }

  function doors(g) {
    const R = rng(20261101);
    let lg;
    g.fillStyle = leafGrad(g); g.fillRect(0, 0, W, W);
    for (let i = 0; i < 260; i++) {   /* fine vertical grain */
      const x = R() * W, y = R() * W * .5 - 40, h = W * (.3 + R() * .9);
      g.fillStyle = R() < .6 ? `rgba(0,0,0,${(.03 + R() * .06).toFixed(3)})` : rgba(SPECK, (.015 + R() * .03).toFixed(3));
      g.fillRect(x, y, .5 + R() * 1.2, h);
    }
    /* hinge shading at the screen's edges, a lintel shadow at the top */
    lg = g.createLinearGradient(0, 0, 56, 0); lg.addColorStop(0, 'rgba(0,0,0,.6)'); lg.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = lg; g.fillRect(0, 0, 56, W);
    lg = g.createLinearGradient(W, 0, W - 56, 0); lg.addColorStop(0, 'rgba(0,0,0,.6)'); lg.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = lg; g.fillRect(W - 56, 0, 56, W);
    lg = g.createLinearGradient(0, 0, 0, 44); lg.addColorStop(0, 'rgba(0,0,0,.7)'); lg.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = lg; g.fillRect(0, 0, W, 44);

    for (const cx of [116.5, 349.5]) {   /* each leaf: glazing and bands, mirrored about the seam */
      const x0 = cx - 92, x1 = cx + 92;
      band(g, x0, x1, 82, 7);
      porthole(g, cx, 158, 52, 1, R);
      band(g, x0, x1, 232, 9);
      porthole(g, cx - 40, 300, 21, .55, R);
      porthole(g, cx + 40, 300, 21, .55, R);
      band(g, x0, x1, 352, 7);
      porthole(g, cx, 408, 26, .4, R);
      /* light from the glass falling on the wood */
      g.globalCompositeOperation = 'lighter';
      const rg = g.createRadialGradient(cx, 158, 40, cx, 158, 165);
      rg.addColorStop(0, 'rgba(255,190,110,.12)'); rg.addColorStop(1, 'rgba(255,190,110,0)');
      g.fillStyle = rg; g.fillRect(0, 0, W, W);
      g.globalCompositeOperation = 'source-over';
    }
    pull(g, 216, 262); pull(g, 250, 262);
    /* the seam, and warm light through it */
    g.fillStyle = 'rgba(0,0,0,.9)'; g.fillRect(231.6, 0, 2.8, W);
    g.globalCompositeOperation = 'lighter';
    lg = g.createLinearGradient(C - 10, 0, C + 10, 0);
    lg.addColorStop(0, 'rgba(255,190,110,0)'); lg.addColorStop(.42, 'rgba(255,200,130,.22)'); lg.addColorStop(.5, 'rgba(255,236,200,.75)');
    lg.addColorStop(.58, 'rgba(255,200,130,.22)'); lg.addColorStop(1, 'rgba(255,190,110,0)');
    g.fillStyle = lg; g.fillRect(C - 10, 30, 20, W - 60);
    g.globalCompositeOperation = 'source-over';
    /* vignette toward the round edge */
    const vg = g.createRadialGradient(C, C * .9, 120, C, C, 250);
    vg.addColorStop(0, 'rgba(0,0,0,0)'); vg.addColorStop(1, 'rgba(0,0,0,.55)');
    g.fillStyle = vg; g.fillRect(0, 0, W, W);
  }

  /* A blank enamel plate: cream, a dark rim and four screws. No lettering. */
  function plate(g, x, y, w, h) {
    g.fillStyle = 'rgba(0,0,0,.5)'; g.fillRect(x + 2, y + 2.5, w, h);
    const pg = g.createLinearGradient(x, y, x + w * .4, y + h);
    pg.addColorStop(0, '#e4dcc3'); pg.addColorStop(1, '#c9c0a4');
    g.fillStyle = pg; g.fillRect(x, y, w, h);
    g.strokeStyle = 'rgba(40,36,28,.7)'; g.lineWidth = 1.2; g.strokeRect(x + 3.6, y + 3.6, w - 7.2, h - 7.2);
    g.fillStyle = 'rgba(255,255,245,.35)'; g.fillRect(x, y, w, .8);
    for (const [sx, sy] of [[x + 6, y + 6], [x + w - 6, y + 6], [x + 6, y + h - 6], [x + w - 6, y + h - 6]]) {
      g.fillStyle = '#7d766a'; g.beginPath(); g.arc(sx, sy, 1.7, 0, TAU); g.fill();
      g.strokeStyle = 'rgba(30,26,20,.8)'; g.lineWidth = .6;
      g.beginPath(); g.moveTo(sx - 1.2, sy); g.lineTo(sx + 1.2, sy); g.stroke();
    }
  }

  function doors_c(g) {
    const R = rng(20261102), BASE_C = [20, 72, 44];
    const sc = (c, k) => c.map(v => Math.min(255, Math.round(v * k)));
    let lg = g.createLinearGradient(0, 0, 0, W);
    lg.addColorStop(0, rgba(sc(BASE_C, 1.25), 1)); lg.addColorStop(.45, rgba(BASE_C, 1)); lg.addColorStop(1, rgba(sc(BASE_C, .55), 1));
    g.fillStyle = lg; g.fillRect(0, 0, W, W);
    for (let i = 0; i < 300; i++) {   /* vertical grain */
      const x = R() * W, y = R() * W * .45 - 50, h = W * (.35 + R() * .85);
      g.fillStyle = R() < .58 ? `rgba(0,0,0,${(.035 + R() * .065).toFixed(3)})` : rgba(SPECK, (.02 + R() * .03).toFixed(3));
      g.fillRect(x, y, .5 + R() * 1.3, h);
    }
    /* hinge shading at the screen's edges, a lintel shadow at the top (the designed set's frame) */
    lg = g.createLinearGradient(0, 0, 60, 0); lg.addColorStop(0, 'rgba(0,0,0,.55)'); lg.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = lg; g.fillRect(0, 0, 60, W);
    lg = g.createLinearGradient(W, 0, W - 60, 0); lg.addColorStop(0, 'rgba(0,0,0,.55)'); lg.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = lg; g.fillRect(W - 60, 0, 60, W);
    lg = g.createLinearGradient(0, 0, 0, 50); lg.addColorStop(0, 'rgba(0,0,0,.7)'); lg.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = lg; g.fillRect(0, 0, W, 50);

    for (const cx of [145, 321]) {   /* each leaf, centred where the designed set centres its glazing */
      const x0 = cx - 70, x1 = cx + 70;
      band(g, x0, x1, 62, 6);
      porthole(g, cx, 128, 46, 1, R);                 /* one large porthole, high */
      band(g, x0, x1, 186, 5);
      band(g, x0, x1, 276, 8);
      porthole(g, cx - 34, 330, 18, .5, R);           /* two small ones, low */
      porthole(g, cx + 34, 330, 18, .5, R);
      band(g, x0, x1, 374, 6);
      g.globalCompositeOperation = 'lighter';         /* light from the glass on the wood */
      const rg = g.createRadialGradient(cx, 128, 34, cx, 128, 150);
      rg.addColorStop(0, 'rgba(255,190,110,.12)'); rg.addColorStop(1, 'rgba(255,190,110,0)');
      g.fillStyle = rg; g.fillRect(0, 0, W, W);
      g.globalCompositeOperation = 'source-over';
    }
    plate(g, 96, 199, 98, 58);                        /* blank: no lettering */
    pull(g, 252, 238);
    /* the seam, and warm light through it */
    g.fillStyle = 'rgba(0,0,0,.9)'; g.fillRect(231.6, 0, 2.8, W);
    g.globalCompositeOperation = 'lighter';
    lg = g.createLinearGradient(C - 10, 0, C + 10, 0);
    lg.addColorStop(0, 'rgba(255,190,110,0)'); lg.addColorStop(.42, 'rgba(255,200,130,.22)'); lg.addColorStop(.5, 'rgba(255,236,200,.75)');
    lg.addColorStop(.58, 'rgba(255,200,130,.22)'); lg.addColorStop(1, 'rgba(255,190,110,0)');
    g.fillStyle = lg; g.fillRect(C - 10, 30, 20, W - 60);
    g.globalCompositeOperation = 'source-over';
    const vg = g.createRadialGradient(C, C * .9, 120, C, C, 250);   /* vignette toward the round edge */
    vg.addColorStop(0, 'rgba(0,0,0,0)'); vg.addColorStop(1, 'rgba(0,0,0,.55)');
    g.fillStyle = vg; g.fillRect(0, 0, W, W);
  }

  function front(g, geo) {
    const b5 = geo.b5, R = rng(55);
    const [sx, sy, sw, sh] = b5.sign, [lx, ly, lw, lh] = b5.lens;
    g.fillStyle = '#000'; g.fillRect(0, 0, W, W);
    /* the lamp: a bracket rising from the sign, a lamp body around the lens position (the lens is the face's sprite) */
    const lc = lx + lw / 2;
    g.fillStyle = brassGrad(g, lc - 2, 0, lc + 2, 0); g.fillRect(lc - 1.6, ly + lh + 3, 3.2, sy - (ly + lh + 3));
    g.fillStyle = '#2a1f0e'; g.beginPath(); g.arc(lc, ly + lh + 4, 5, 0, TAU); g.fill();
    g.fillStyle = brassGrad(g, lx - 4, 0, lx + lw + 4, 0);
    g.beginPath(); g.moveTo(lx - 4, ly + lh + 2); g.lineTo(lx + lw + 4, ly + lh + 2); g.lineTo(lx + lw + 1, ly + lh + 6); g.lineTo(lx - 1, ly + lh + 6); g.closePath(); g.fill();
    g.fillStyle = '#0c0a07'; g.fillRect(lx - 2, ly - 1, lw + 4, lh + 3);          /* the lamp body behind the lens */
    g.fillStyle = brassGrad(g, lx - 3, 0, lx + lw + 3, 0);
    g.fillRect(lx - 3, ly - 1, 1.6, lh + 3); g.fillRect(lx + lw + 1.4, ly - 1, 1.6, lh + 3);
    g.beginPath(); g.arc(lc, ly - 1, lw / 2 + 3, Math.PI, 0); g.closePath(); g.fill();   /* a domed cap */
    g.fillStyle = '#f1d79a'; g.beginPath(); g.arc(lc, ly - 1 - lw / 2 - 3, 1.6, 0, TAU); g.fill();

    /* the sign: a brass-rimmed panel of black glass with rounded corners (the face lights the time in it) */
    const rr = (x, y, w, h, r) => { g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); };
    g.fillStyle = brassGrad(g, 0, sy, 0, sy + sh); rr(sx, sy, sw, sh, 12); g.fill();
    g.fillStyle = '#020203'; rr(sx + 5, sy + 5, sw - 10, sh - 10, 8); g.fill();
    g.strokeStyle = 'rgba(255,236,190,.10)'; g.lineWidth = 1; rr(sx + 5.5, sy + 5.5, sw - 11, sh - 11, 8); g.stroke();

    /* the doors: two leaves under the sign, filled with the Modern leaves' green and outlined, a brass band and round
       glazing below the panes */
    const dy = sy + sh + 10, half = sw / 2;
    g.fillStyle = leafGrad(g);
    g.fillRect(sx + 6.75, dy, half - 9.5, W);
    g.fillRect(C + 2.75, dy, half - 9.5, W);
    g.strokeStyle = 'rgba(176,138,74,.7)'; g.lineWidth = 1.5;
    g.strokeRect(sx + 6.75, dy, half - 9.5, W);
    g.strokeRect(C + 2.75, dy, half - 9.5, W);
    g.fillStyle = 'rgba(176,138,74,.4)'; g.fillRect(C - 0.5, dy, 1, W);
    for (const [x, y, w, h] of b5.windows) {   /* the window surrounds, on the panes' rectangles */
      g.fillStyle = '#0d0b08'; g.fillRect(x - 5, y - 5, w + 10, h + 10);
      g.strokeStyle = 'rgba(201,163,91,.75)'; g.lineWidth = 1; g.strokeRect(x - 4.5, y - 4.5, w + 9, h + 9);
    }
    for (const [x, y, w, h] of b5.panes) { g.fillStyle = '#05070b'; g.fillRect(x, y, w, h); }
    for (const cx of [sx + 6 + (half - 9.5) / 2, C + 3 + (half - 9.5) / 2]) {
      band(g, cx - 72, cx + 72, 304, 5);
      porthole(g, cx, 430, 22, .35, R);
    }
    pull(g, C - 16, 334); pull(g, C + 16, 334);
  }

  window.DW_GENERIC = { doors, doors_c, front };
})();
