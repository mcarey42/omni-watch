/* faces-practical.js \u2014 the practical faces of the Omni Watch family.
   Group 'Faces'. Plain JS, Canvas 2D only. Shared engine: textures, solar/lunar maths, a lookup-table globe,
   brass helpers. Every globe is an orthographic (or polar) projection rendered per pixel from a cached LUT into an
   ImageData of the globe region only, then blitted \u2014 the same structure the ESP32-S3 would use. */
(function () {
  'use strict';
  window.OMNI_FACES = window.OMNI_FACES || [];

  const TAU = Math.PI * 2, D2R = Math.PI / 180, R2D = 180 / Math.PI;
  const FD = '"Cormorant SC", "Cormorant Garamond", Georgia, serif';
  const FS = '"IBM Plex Sans", system-ui, sans-serif';
  const FM = '"IBM Plex Mono", ui-monospace, monospace';
  const H24 = false;                       // a setting on the device; 12 h default (US convention)
  const C = {
    brass: '#c9a35b', brassHi: '#ecd29a', brassDim: '#8c703c', brassDeep: '#4a3818',
    ink: '#ece6dc', inkDim: '#a39a8c', inkFaint: '#5d564c', red: '#d8443a', green: '#4fd06a',
  };

  /* ------------------------------------------------------------ small utils */
  const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
  const easeOut3 = x => 1 - Math.pow(1 - clamp(x, 0, 1), 3);
  const easeInOut = x => { x = clamp(x, 0, 1); return x < .5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2; };
  const pad2 = n => (n < 10 ? '0' : '') + n;
  const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
  function fmtTime(d, h24) {
    h24 = h24 === undefined ? H24 : h24;
    const h = d.getHours(), m = d.getMinutes();
    if (h24) return { hm: pad2(h) + ':' + pad2(m), ap: '' };
    return { hm: ((h + 11) % 12 + 1) + ':' + pad2(m), ap: h < 12 ? 'AM' : 'PM' };
  }
  const fmtShort = d => { const f = fmtTime(d); return f.hm + (f.ap ? f.ap[0].toLowerCase() : ''); };
  function fmtDur(ms) { const m = Math.round(Math.abs(ms) / 60000); return Math.floor(m / 60) + 'h ' + pad2(m % 60) + 'm'; }

  /* Burn-in drift: the static parts of a face wander on a slow 9-step orbit, max 4 px, one step a minute. */
  const ORBIT = [[0, 0], [3, 1], [1, 3], [-2, 2], [-3, -1], [-1, -3], [2, -2], [4, 0], [0, 4]];
  function drift(d, scale) { const o = ORBIT[Math.floor(d.getTime() / 60000) % ORBIT.length]; scale = scale || 1; return [o[0] * scale, o[1] * scale]; }

  function spaced(ctx, s, px) { ctx.letterSpacing = px + 'px'; }

  /* Draw digits in fixed-width cells so proportional fonts never jitter. */
  function drawCells(ctx, str, x, y, cell, colon, align) {
    let w = 0; for (const ch of str) w += (ch === ':' || ch === '.') ? colon : cell;
    let px = align === 'left' ? x : align === 'right' ? x - w : x - w / 2;
    ctx.textAlign = 'center';
    for (const ch of str) { const cw = (ch === ':' || ch === '.') ? colon : cell; ctx.fillText(ch, px + cw / 2, y); px += cw; }
    return w;
  }

  /* Text set along a circle, centred on angle a (canvas radians). Bottom-half text reads left-to-right too. */
  function arcText(ctx, str, cx, cy, r, a, flip) {
    ctx.save();
    const chars = [...str], widths = chars.map(c => ctx.measureText(c).width);
    const total = widths.reduce((s, v) => s + v, 0);
    const dir = flip ? -1 : 1;
    let ang = a - dir * (total / 2) / r;
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    for (let i = 0; i < chars.length; i++) {
      const half = widths[i] / 2 / r;
      ang += dir * half;
      ctx.save();
      ctx.translate(cx + Math.cos(ang) * r, cy + Math.sin(ang) * r);
      ctx.rotate(ang + (flip ? -Math.PI / 2 : Math.PI / 2));
      ctx.fillText(chars[i], 0, 0);
      ctx.restore();
      ang += dir * half;
    }
    ctx.restore();
  }

  function offscreen(w, h) { const c = document.createElement('canvas'); c.width = w; c.height = h || w; return c; }

  /* ------------------------------------------------------------ astronomy (plenty for a watch) */
  function sunPos(date) {
    const d = date.getTime() / 864e5 - 10957.5;                         // days since J2000.0
    const g = (357.529 + 0.98560028 * d) * D2R;
    const q = 280.459 + 0.98564736 * d;
    const L = (q + 1.915 * Math.sin(g) + 0.020 * Math.sin(2 * g)) * D2R;
    const e = (23.439 - 0.00000036 * d) * D2R;
    const ra = Math.atan2(Math.cos(e) * Math.sin(L), Math.cos(L)) * R2D;
    const dec = Math.asin(Math.sin(e) * Math.sin(L));
    let eq = ((q - ra) % 360 + 540) % 360 - 180;                         // degrees
    const eot = eq / 15;                                                // hours (apparent \u2212 mean)
    const utcH = ((date.getTime() / 36e5) % 24 + 24) % 24;
    let lon = -15 * (utcH - 12 + eot); lon = ((lon + 540) % 360) - 180;
    return { dec, lon, eot };
  }
  function sunAltAz(date, lat, lon) {
    const s = sunPos(date), p = lat * D2R, H = (lon - s.lon) * D2R;
    const mu = Math.sin(p) * Math.sin(s.dec) + Math.cos(p) * Math.cos(s.dec) * Math.cos(H);
    const alt = Math.asin(clamp(mu, -1, 1));
    let az = Math.atan2(Math.sin(H), Math.cos(H) * Math.sin(p) - Math.tan(s.dec) * Math.cos(p)) * R2D + 180;
    return { alt: alt * R2D, az: (az + 360) % 360 };
  }
  /* Rise/set for the local calendar day of `day` at altitude altDeg. Returns Dates, or flags for polar day/night. */
  function sunTimes(day, lat, lon, altDeg) {
    const base = Date.UTC(day.getFullYear(), day.getMonth(), day.getDate());
    let noon = base + (12 - lon / 15) * 36e5;
    let s = sunPos(new Date(noon));
    noon = base + (12 - s.eot - lon / 15) * 36e5;
    s = sunPos(new Date(noon));
    const p = lat * D2R;
    const cosH = (Math.sin(altDeg * D2R) - Math.sin(p) * Math.sin(s.dec)) / (Math.cos(p) * Math.cos(s.dec));
    if (cosH > 1) return { noon: new Date(noon), polar: 'night' };
    if (cosH < -1) return { noon: new Date(noon), polar: 'day' };
    const H = Math.acos(cosH) * R2D / 15 * 36e5;
    return { noon: new Date(noon), rise: new Date(noon - H), set: new Date(noon + H) };
  }
  const SYNODIC = 29.530588853, NEW_REF = Date.UTC(2000, 0, 6, 18, 14);
  function moonPhase(date) {
    const age = (((date.getTime() - NEW_REF) / 864e5) % SYNODIC + SYNODIC) % SYNODIC;
    const f = age / SYNODIC;
    const illum = (1 - Math.cos(f * TAU)) / 2;
    const names = ['New moon', 'Waxing crescent', 'First quarter', 'Waxing gibbous', 'Full moon', 'Waning gibbous', 'Last quarter', 'Waning crescent'];
    const name = names[Math.floor(f * 8 + 0.5) % 8];
    const nextFull = new Date(date.getTime() + (((0.5 - f) + 1) % 1) * SYNODIC * 864e5);
    const nextNew = new Date(date.getTime() + ((1 - f) % 1) * SYNODIC * 864e5);
    return { age, f, illum, name, nextFull, nextNew };
  }

  /* ------------------------------------------------------------ textures (equirect \u2192 byte arrays) */
  const texCache = {};
  function getTex(env, w) {
    if (texCache[w]) return texCache[w];
    const h = w / 2, c = offscreen(w, h), g = c.getContext('2d', { willReadFrequently: true });
    g.imageSmoothingEnabled = true; g.imageSmoothingQuality = 'high';
    const out = { w, h };
    for (const [k, key, fb] of [['day', 'earthDay', '#1d3b6a'], ['night', 'earthNight', '#02040a']]) {
      g.clearRect(0, 0, w, h);
      if (env.assets && env.assets[key]) g.drawImage(env.assets[key], 0, 0, w, h);
      else { g.fillStyle = fb; g.fillRect(0, 0, w, h); }
      out[k] = g.getImageData(0, 0, w, h).data;
    }
    texCache[w] = out; return out;
  }

  /* ------------------------------------------------------------ the LUT globe
     opts: lat0 (view-centre latitude, deg), roll (deg, screen-plane tilt, e.g. 23.4 for a desk globe),
           texW (512 / 1024), proj: 'ortho' | 'polar' (north-polar azimuthal equidistant, to colatitude maxColat),
           light: view-space unit vector for a fixed studio light (spin face) \u2014 omit for real sunlight. */
  function Globe(D, opts) {
    this.D = D | 0; this.R = this.D / 2; this.o = opts || {};
    this.cv = offscreen(this.D); this.g = this.cv.getContext('2d');
    this.img = this.g.createImageData(this.D, this.D); this.buf = this.img.data;
    this.ready = false;
  }
  Globe.prototype.build = function (env) {
    const D = this.D, R = this.R, o = this.o;
    const T = this.tex = getTex(env, o.texW || 512);
    const lat0 = (o.lat0 || 0) * D2R, roll = (o.roll || 0) * D2R;
    const cl = Math.cos(lat0), sl = Math.sin(lat0), cr = Math.cos(roll), sr = Math.sin(roll);
    const N = D * D;
    const pix = new Int32Array(N), lonU = new Float32Array(N), row = new Int32Array(N);
    const sLat = new Float32Array(N), cLat = new Float32Array(N), cL = new Float32Array(N), sL = new Float32Array(N);
    const limb = new Float32Array(N), rim = new Float32Array(N), alpha = new Uint8Array(N), ndl = new Float32Array(N);
    const uScale = T.w / TAU;
    const Lv = o.light;
    const polar = o.proj === 'polar', maxColat = (o.maxColat || 150) * D2R;
    let n = 0;
    for (let py = 0; py < D; py++) for (let px = 0; px < D; px++) {
      let xs = (px + 0.5 - R) / R, ys = -(py + 0.5 - R) / R;
      let r = Math.hypot(xs, ys);
      if (r > 1 + 1 / R) continue;
      const cov = clamp((1 - r) * R + 0.5, 0, 1);
      if (r > 0.9995) { xs *= 0.9995 / r; ys *= 0.9995 / r; r = 0.9995; }
      let lat, lonRel, z;
      if (polar) {
        const colat = r * maxColat;
        lat = Math.PI / 2 - colat;
        lonRel = -(Math.atan2(-ys, xs) + Math.PI / 2);                 // screen angle \u2192 longitude offset
        z = 1;
        limb[n] = 1 - 0.28 * Math.pow(r, 6);
        rim[n] = 0;
      } else {
        const xv = xs * cr + ys * sr, yv = -xs * sr + ys * cr;
        z = Math.sqrt(Math.max(0, 1 - xv * xv - yv * yv));
        const Y = yv * cl + z * sl, Zf = -yv * sl + z * cl;
        lat = Math.asin(clamp(Y, -1, 1)); lonRel = Math.atan2(xv, Zf);
        limb[n] = 0.42 + 0.58 * Math.pow(z, 0.55);
        rim[n] = Math.pow(1 - z, 2.6);
        if (Lv) ndl[n] = xv * Lv[0] + yv * Lv[1] + z * Lv[2];
      }
      pix[n] = (py * D + px) * 4;
      lonU[n] = lonRel * uScale;
      row[n] = clamp(Math.floor((0.5 - lat / Math.PI) * T.h), 0, T.h - 1) * T.w;
      sLat[n] = Math.sin(lat); cLat[n] = Math.cos(lat);
      cL[n] = Math.cos(lonRel); sL[n] = Math.sin(lonRel);
      alpha[n] = Math.round(cov * 255);
      n++;
    }
    Object.assign(this, { n, pix, lonU, row, sLat, cLat, cL, sL, limb, rim, alpha, ndl, uOffBase: T.w / 2 });
    this.ready = true;
  };
  /* Render with longitude lon0Deg at the view centre. sun = sunPos() or null (studio light). p: {gain, night, desat} */
  Globe.prototype.render = function (env, lon0Deg, sun, p) {
    if (!this.ready) this.build(env);
    p = p || {};
    const T = this.tex, W = T.w, day = T.day, night = T.night, buf = this.buf;
    const { n, pix, lonU, row, sLat, cLat, cL, sL, limb, rim, alpha, ndl } = this;
    const gain = p.gain === undefined ? 1 : p.gain, nightGain = (p.night === undefined ? 1.25 : p.night) * gain;
    const atmo = p.atmo === undefined ? 1 : p.atmo;
    const uOff = lon0Deg * D2R * (W / TAU) + W / 2;
    let sd = 0, cd = 1, cD = 1, sD = 0;
    const useSun = !!sun;
    if (useSun) { sd = Math.sin(sun.dec); cd = Math.cos(sun.dec); const dl = (lon0Deg - sun.lon) * D2R; cD = Math.cos(dl); sD = Math.sin(dl); }
    for (let k = 0; k < n; k++) {
      let u = lonU[k] + uOff; u -= Math.floor(u / W) * W; const t = (row[k] + (u | 0)) * 4;
      const mu = useSun ? sLat[k] * sd + cLat[k] * cd * (cL[k] * cD - sL[k] * sD) : ndl[k];
      let x = (mu + 0.09) * 5.5; x = x < 0 ? 0 : x > 1 ? 1 : x;
      const s = x * x * (3 - 2 * x);                                   // soft terminator ~10\u00b0 wide
      const lit = s * (0.30 + 0.70 * Math.sqrt(mu > 0 ? mu : 0)) * gain;
      const nk = (1 - s);
      const tw = s * nk * 4;                                            // warm twilight band
      const L = limb[k];
      const dr = day[t], dg = day[t + 1], db = day[t + 2];
      let r = (dr * lit * (1 + 0.22 * tw) + night[t] * nk * nightGain + dr * nk * 0.05 * gain) * L;
      let g = (dg * lit * (1 + 0.04 * tw) + night[t + 1] * nk * nightGain + dg * nk * 0.06 * gain) * L;
      let b = (db * lit * (1 - 0.18 * tw) + night[t + 2] * nk * nightGain * 0.9 + db * nk * 0.09 * gain) * L;
      const a = rim[k] * atmo * (0.18 + 0.82 * s) * gain;
      r += 60 * a; g += 135 * a; b += 255 * a;
      const q = pix[k];
      buf[q] = r; buf[q + 1] = g; buf[q + 2] = b; buf[q + 3] = alpha[k];
    }
    this.g.putImageData(this.img, 0, 0);
    return this.cv;
  };
  /* Project lat/lon (deg) onto this globe: returns [x, y, z] in unit-disc screen coords (y down), z>0 = visible. */
  Globe.prototype.project = function (lat, lon, lon0) {
    const o = this.o;
    if (o.proj === 'polar') {
      const r = (90 - lat) / (o.maxColat || 150), lonRel = (lon - lon0) * D2R;
      const a = -lonRel - Math.PI / 2;                                  // inverse of the LUT mapping
      return [Math.cos(a) * r, Math.sin(a) * r, r <= 1 ? 1 : -1];
    }
    const lat0 = (o.lat0 || 0) * D2R, roll = (o.roll || 0) * D2R, p = lat * D2R, dl = (lon - lon0) * D2R;
    const X = Math.cos(p) * Math.sin(dl), Y = Math.sin(p), Zf = Math.cos(p) * Math.cos(dl);
    const yv = Y * Math.cos(lat0) - Zf * Math.sin(lat0), z = Y * Math.sin(lat0) + Zf * Math.cos(lat0);
    const xs = X * Math.cos(roll) - yv * Math.sin(roll), ys = X * Math.sin(roll) + yv * Math.cos(roll);
    return [xs, -ys, z];
  };

  /* Soft outer atmosphere halo, brighter toward the sun. dir = [x,y] unit-ish screen direction or null. */
  function halo(ctx, cx, cy, R, dir, strength) {
    strength = strength === undefined ? 1 : strength;
    ctx.save();
    let gr = ctx.createRadialGradient(cx, cy, R * 0.985, cx, cy, R * 1.07);
    gr.addColorStop(0, `rgba(110,170,255,${0.30 * strength})`);
    gr.addColorStop(0.35, `rgba(70,130,255,${0.10 * strength})`);
    gr.addColorStop(1, 'rgba(40,90,255,0)');
    ctx.fillStyle = gr; ctx.beginPath(); ctx.arc(cx, cy, R * 1.07, 0, TAU); ctx.arc(cx, cy, R * 0.985, 0, TAU, true); ctx.fill();
    if (dir) {
      const dl = Math.hypot(dir[0], dir[1]) || 1, ux = dir[0] / dl, uy = dir[1] / dl;
      gr = ctx.createRadialGradient(cx + ux * R * 0.9, cy + uy * R * 0.9, 0, cx + ux * R * 0.9, cy + uy * R * 0.9, R * 0.75);
      gr.addColorStop(0, `rgba(150,200,255,${0.22 * strength})`); gr.addColorStop(1, 'rgba(150,200,255,0)');
      ctx.fillStyle = gr; ctx.beginPath(); ctx.arc(cx, cy, R * 1.07, 0, TAU); ctx.arc(cx, cy, R * 0.99, 0, TAU, true); ctx.fill();
    }
    ctx.restore();
  }

  /* Glass dome: a soft window highlight top-left and a thin caustic crescent bottom-right. Pre-rendered per radius. */
  const domeCache = {};
  function dome(R) {
    if (domeCache[R]) return domeCache[R];
    const S = Math.ceil(R * 2 + 4), c = offscreen(S), g = c.getContext('2d'), m = S / 2;
    g.save(); g.beginPath(); g.arc(m, m, R, 0, TAU); g.clip();
    // inner glass edge \u2014 darkens the very rim like a real dome's refraction
    let gr = g.createRadialGradient(m, m, R * 0.86, m, m, R);
    gr.addColorStop(0, 'rgba(0,0,0,0)'); gr.addColorStop(1, 'rgba(0,0,0,0.35)');
    g.fillStyle = gr; g.fillRect(0, 0, S, S);
    // window reflection
    g.save(); g.translate(m - R * 0.34, m - R * 0.42); g.rotate(-0.6); g.scale(1, 0.55);
    gr = g.createRadialGradient(0, 0, 0, 0, 0, R * 0.42);
    gr.addColorStop(0, 'rgba(255,250,235,0.11)'); gr.addColorStop(0.55, 'rgba(255,250,235,0.04)'); gr.addColorStop(1, 'rgba(255,250,235,0)');
    g.fillStyle = gr; g.beginPath(); g.arc(0, 0, R * 0.42, 0, TAU); g.fill(); g.restore();
    // caustic crescent
    g.lineCap = 'round';
    gr = g.createLinearGradient(m + R * 0.2, m + R * 0.9, m + R * 0.9, m + R * 0.2);
    gr.addColorStop(0, 'rgba(255,240,210,0)'); gr.addColorStop(0.5, 'rgba(255,240,210,0.16)'); gr.addColorStop(1, 'rgba(255,240,210,0)');
    g.strokeStyle = gr; g.lineWidth = R * 0.025; g.beginPath(); g.arc(m, m, R * 0.93, 0.05, 1.45); g.stroke();
    g.restore();
    return (domeCache[R] = c);
  }

  /* Brass: tarnished, brushed, bevelled annulus, pre-rendered once (stand-in for a device bitmap). */
  function brassRing(g, cx, cy, r0, r1, opt) {
    opt = opt || {};
    const dim = opt.dim === undefined ? 1 : opt.dim;
    g.save();
    g.beginPath(); g.arc(cx, cy, r1, 0, TAU); g.arc(cx, cy, r0, 0, TAU, true); g.clip('evenodd');
    const cg = g.createConicGradient(-2.3, cx, cy);
    const stops = ['#5a4420', '#a8853f', '#e0c27c', '#9b7a3a', '#4c3a1b', '#7d6230', '#c8a45c', '#6b5226', '#5a4420'];
    stops.forEach((s, i) => cg.addColorStop(i / (stops.length - 1), s));
    g.fillStyle = cg; g.fillRect(cx - r1, cy - r1, r1 * 2, r1 * 2);
    // brushing: faint concentric hairlines
    for (let r = r0 + 0.5; r < r1; r += 1.3) {
      g.strokeStyle = `rgba(${Math.random() < .5 ? '0,0,0' : '255,240,200'},${0.05 + Math.random() * 0.06})`;
      g.lineWidth = 0.6; g.beginPath(); g.arc(cx, cy, r, 0, TAU); g.stroke();
    }
    // bevel: light top-left on the outer edge, shadow on the inner edge
    const lg = g.createLinearGradient(cx - r1, cy - r1, cx + r1, cy + r1);
    lg.addColorStop(0, 'rgba(255,245,215,0.55)'); lg.addColorStop(0.5, 'rgba(255,245,215,0.05)'); lg.addColorStop(1, 'rgba(0,0,0,0.5)');
    g.strokeStyle = lg; g.lineWidth = 1.4; g.beginPath(); g.arc(cx, cy, r1 - 0.7, 0, TAU); g.stroke();
    const lg2 = g.createLinearGradient(cx - r0, cy - r0, cx + r0, cy + r0);
    lg2.addColorStop(0, 'rgba(0,0,0,0.6)'); lg2.addColorStop(1, 'rgba(255,240,205,0.45)');
    g.strokeStyle = lg2; g.lineWidth = 1.2; g.beginPath(); g.arc(cx, cy, r0 + 0.6, 0, TAU); g.stroke();
    if (dim !== 1) { g.fillStyle = `rgba(0,0,0,${1 - dim})`; g.fillRect(cx - r1, cy - r1, r1 * 2, r1 * 2); }
    g.restore();
  }
  function brassFill(g, x0, y0, x1, y1) {
    const lg = g.createLinearGradient(x0, y0, x1, y1);
    lg.addColorStop(0, '#f1d9a0'); lg.addColorStop(0.45, '#c9a35b'); lg.addColorStop(0.55, '#8f6f35'); lg.addColorStop(1, '#5e4720');
    return lg;
  }

  /* Small status glyphs (battery, Wi-Fi) drawn as strokes, not emoji. */
  function battery(ctx, x, y, pct, col) {
    ctx.save(); ctx.strokeStyle = col; ctx.fillStyle = col; ctx.lineWidth = 1.2;
    ctx.beginPath(); ctx.roundRect(x, y - 6, 20, 11, 2.5); ctx.stroke();
    ctx.fillRect(x + 20.5, y - 2.5, 2, 4);
    ctx.globalAlpha = .85; ctx.fillRect(x + 2, y - 4, 16 * pct, 7); ctx.restore();
  }
  function wifi(ctx, x, y, ok, col) {
    ctx.save(); ctx.strokeStyle = col; ctx.fillStyle = col; ctx.lineWidth = 1.5; ctx.lineCap = 'round';
    for (let i = 1; i <= 3; i++) { ctx.globalAlpha = ok || i === 1 ? 1 : .3; ctx.beginPath(); ctx.arc(x, y + 4, i * 4.2, -Math.PI * .75, -Math.PI * .25); ctx.stroke(); }
    ctx.globalAlpha = 1; ctx.beginPath(); ctx.arc(x, y + 4, 1.4, 0, TAU); ctx.fill(); ctx.restore();
  }
  /* The mock device state shared by the faces (the real firmware would supply it). */
  const DEV = { battery: 0.76, wifiOk: true, lastSyncMin: 14 };

  function homeOf(env) { return env.location || { name: 'Home', lat: 0, lon: 0 }; }

  /* ===================================================================== 1. REAL-TIME GLOBE */
  const live = {
    globe: null, dragging: false, lastX: 0, lastY: 0, offLon: 0, offLat: 0, releaseAt: -1e9, vLon: 0,
  };
  function liveGlobe(env) {
    const h = homeOf(env);
    const lat0 = Math.round(clamp(h.lat * 0.55, -45, 45));
    if (!live.globe || live.globe.o.lat0 !== lat0) live.globe = new Globe(262, { lat0, texW: 1024 });
    return live.globe;
  }

  OMNI_FACES.push({
    id: 'globe-live', group: 'Faces', title: 'Real-time globe',
    summary: 'Earth right now: sunlit day side, city lights at night, your home marked, sunrise and sunset.',
    notes: `<p><b>What it is.</b> Earth as it is at this moment. The subsolar point comes from <code>env.now()</code>
      (declination plus equation of time, accurate to a few arc-minutes). The day texture blends into the
      night-lights texture across a terminator about 10\u00b0 wide, with a warm band where it is dusk on the ground.
      The view is centred on the home longitude and tilted to just over half the home latitude. Home then sits a
      little above the centre, and the southern hemisphere still has room.</p>
      <ul><li><b>Home</b> is a brass ring with a quiet pulse. The <b>sun</b> appears as a small glow where it is
      overhead, and as a brass bead on the thin ring around the globe. The bead always points to where the sun is,
      even when the sun is on the far side.</li>
      <li><b>What earns its place:</b> the time (large), the date in Cormorant small caps (the family voice), and
      sunrise and sunset for home in the two side margins. Those margins are wasted space on a round dial
      anyway.</li></ul>
      <p><b>Interaction.</b> Drag to look around the planet (with inertia). It drifts home 4 s after you let go.
      <b>Wake:</b> a 1.4 s spin-up to home longitude. This is the practical face's version of the "flip it open"
      moment; with reduced motion it fades only.</p>
      <p><b>Glance / always-on</b> (implemented: press <i>Put away</i>): no globe. The time and the sunset (or
      sunrise) countdown are drawn at 28% luminance and drift on a 9-step, \u00b14 px orbit once a minute.</p>
      <p><b>Device.</b> A 262 px disc is 54k pixels. Per pixel there is one texture fetch for each of 2 textures and
      about 15 float ops, with no trig (cos/sin of each pixel's longitude are in the LUT). That suits 30 fps on
      the S3. At real-time speed the terminator moves 0.25\u00b0 a minute, so on the device the globe only re-renders
      once every 10\u201330 s; it needs full fps only while dragging or on wake. LUT about 54k \u00d7 16 B \u2248 0.9 MB in
      PSRAM. A 512\u00d7256 texture is enough at this size (the mockup uses 1024 for the sharper screenshots).</p>`,
    init(env) { liveGlobe(env); },
    wake() { live.offLon = 0; live.offLat = 0; live.vLon = 0; },
    pointer(type, x, y, env) {
      if (type === 'down') { live.dragging = true; live.lastX = x; live.lastY = y; live.vLon = 0; live.lastT = performance.now(); }
      else if (type === 'move' && live.dragging) {
        const now = performance.now(), dt = Math.max(1, now - live.lastT);
        const dx = x - live.lastX; live.offLon -= dx * 0.45; live.vLon = -dx * 0.45 / dt * 1000 * 0.6 + live.vLon * 0.4;
        live.offLat = clamp(live.offLat + (y - live.lastY) * 0.2, -25, 25);
        live.lastX = x; live.lastY = y; live.lastT = now;
      } else if (type === 'up') { live.dragging = false; live.releaseAt = performance.now(); }
    },
    frame(ctx, t, env) {
      const G = liveGlobe(env), h = homeOf(env), now = env.now(), sun = sunPos(now);
      const W = env.W, cx = env.cx, gy = 212, R = G.R;
      // reveal: spin-up into home
      const rm = env.reducedMotion, k = rm ? 1 : easeOut3(t / 1400);
      const fade = clamp(t / (rm ? 500 : 350), 0, 1);
      // drag inertia + return home
      const pn = performance.now();
      if (!live.dragging) {
        live.offLon += live.vLon / 60; live.vLon *= 0.93;
        if (pn - live.releaseAt > 4000) { live.offLon *= 0.94; live.offLat *= 0.94; }
      }
      const lon0 = h.lon + live.offLon - (1 - k) * 220;
      const img = G.render(env, lon0, sun, { gain: 0.35 + 0.65 * fade });
      ctx.save();
      ctx.globalAlpha = fade;
      // faint hour ring + sun bead
      ctx.strokeStyle = 'rgba(201,163,91,0.28)'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(cx, gy, R + 9, 0, TAU); ctx.stroke();
      for (let i = 0; i < 24; i++) {
        const a = i / 24 * TAU, r0 = R + (i % 6 ? 6.5 : 4), r1 = R + 9;
        ctx.strokeStyle = i % 6 ? 'rgba(201,163,91,0.25)' : 'rgba(201,163,91,0.55)';
        ctx.beginPath(); ctx.moveTo(cx + Math.cos(a) * r0, gy + Math.sin(a) * r0); ctx.lineTo(cx + Math.cos(a) * r1, gy + Math.sin(a) * r1); ctx.stroke();
      }
      const sp = G.project(sun.dec * R2D, sun.lon, lon0);
      halo(ctx, cx, gy, R, sp[2] > -0.3 ? [sp[0], sp[1]] : null, 0.9);
      ctx.drawImage(img, cx - R, gy - R);
      // fine reticle (the prop had one on its dome) \u2014 barely there
      ctx.strokeStyle = 'rgba(236,210,154,0.10)'; ctx.lineWidth = 0.8;
      ctx.beginPath(); ctx.moveTo(cx - R * 0.96, gy); ctx.lineTo(cx + R * 0.96, gy); ctx.moveTo(cx, gy - R * 0.96); ctx.lineTo(cx, gy + R * 0.96); ctx.stroke();
      ctx.drawImage(dome(R), cx - R - 2, gy - R - 2);
      // sun: bead on ring (always), glow on disc (when visible)
      { const a = Math.atan2(sp[1], sp[0]), rr = R + 9;
        const bx = cx + Math.cos(a) * rr, by = gy + Math.sin(a) * rr;
        const gl = ctx.createRadialGradient(bx, by, 0, bx, by, 12);
        gl.addColorStop(0, 'rgba(255,214,130,0.9)'); gl.addColorStop(1, 'rgba(255,214,130,0)');
        ctx.fillStyle = gl; ctx.beginPath(); ctx.arc(bx, by, 12, 0, TAU); ctx.fill();
        ctx.fillStyle = '#ffe2a6'; ctx.beginPath(); ctx.arc(bx, by, 3.2, 0, TAU); ctx.fill();
        if (sp[2] > 0) {
          const sx = cx + sp[0] * R, sy = gy + sp[1] * R;
          const g2 = ctx.createRadialGradient(sx, sy, 0, sx, sy, 26);
          g2.addColorStop(0, 'rgba(255,240,200,0.45)'); g2.addColorStop(1, 'rgba(255,240,200,0)');
          ctx.fillStyle = g2; ctx.beginPath(); ctx.arc(sx, sy, 26, 0, TAU); ctx.fill();
        } }
      // home marker
      { const hp = G.project(h.lat, h.lon, lon0);
        if (hp[2] > 0) {
          const hx = cx + hp[0] * R, hy = gy + hp[1] * R, pulse = (pn % 2400) / 2400;
          ctx.strokeStyle = `rgba(236,210,154,${0.55 * (1 - pulse)})`; ctx.lineWidth = 1.2;
          ctx.beginPath(); ctx.arc(hx, hy, 6 + pulse * 10, 0, TAU); ctx.stroke();
          ctx.strokeStyle = 'rgba(0,0,0,0.7)'; ctx.lineWidth = 3.5; ctx.beginPath(); ctx.arc(hx, hy, 5.5, 0, TAU); ctx.stroke();
          ctx.strokeStyle = C.brassHi; ctx.lineWidth = 1.8; ctx.beginPath(); ctx.arc(hx, hy, 5.5, 0, TAU); ctx.stroke();
          ctx.fillStyle = C.brassHi; ctx.beginPath(); ctx.arc(hx, hy, 1.6, 0, TAU); ctx.fill();
        } }
      // date, top
      ctx.textBaseline = 'alphabetic'; ctx.textAlign = 'center';
      ctx.fillStyle = C.brass; ctx.font = `600 19px ${FD}`; spaced(ctx, 0, 2.2);
      ctx.fillText(`${DAYS[now.getDay()]} \u00b7 ${now.getDate()} ${MONTHS[now.getMonth()]}`, cx, 56);
      spaced(ctx, 0, 0);
      // sunrise / sunset margins
      const st = sunTimes(now, h.lat, h.lon, -0.833);
      const side = (x, label, val) => {
        ctx.textAlign = 'center';
        ctx.fillStyle = C.inkDim; ctx.font = `600 9.5px ${FS}`; spaced(ctx, 0, 1.6); ctx.fillText(label, x, gy - 8); spaced(ctx, 0, 0);
        ctx.fillStyle = C.ink; ctx.font = `500 16px ${FM}`; ctx.fillText(val, x, gy + 12);
      };
      side(52, 'SUNRISE', st.rise ? fmtShort(st.rise) : '\u2014');
      side(W - 52, 'SUNSET', st.set ? fmtShort(st.set) : '\u2014');
      // sun up/down arrows (tiny chevrons)
      ctx.strokeStyle = C.brass; ctx.lineWidth = 1.3;
      ctx.beginPath(); ctx.moveTo(47, gy - 25); ctx.lineTo(52, gy - 30); ctx.lineTo(57, gy - 25); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(W - 57, gy - 30); ctx.lineTo(W - 52, gy - 25); ctx.lineTo(W - 47, gy - 30); ctx.stroke();
      // time
      const tm = fmtTime(now);
      ctx.fillStyle = C.ink; ctx.font = `500 50px ${FS}`;
      const tw = drawCells(ctx, tm.hm, cx - (tm.ap ? 12 : 0), 398, 29, 14);
      if (tm.ap) { ctx.fillStyle = C.brass; ctx.font = `600 13px ${FS}`; spaced(ctx, 0, 1); ctx.textAlign = 'left'; ctx.fillText(tm.ap, cx - 12 + tw / 2 + 5, 380); spaced(ctx, 0, 0); }
      ctx.textAlign = 'center'; ctx.fillStyle = C.inkDim; ctx.font = `500 10.5px ${FS}`; spaced(ctx, 0, 1.8);
      ctx.fillText((h.name || '').toUpperCase(), cx, 424); spaced(ctx, 0, 0);
      ctx.restore();
    },
    sleepFrame(ctx, t, env) { glanceTime(ctx, env, { sub: 'sun' }); },
  });

  /* Shared glance (always-on) renderer: dim time + one fact, drifting against burn-in. */
  function glanceTime(ctx, env, opt) {
    opt = opt || {};
    const now = env.now(), [dx, dy] = drift(now), cx = env.cx + dx, cy = env.cy + dy, h = homeOf(env);
    const tm = fmtTime(now);
    ctx.save();
    ctx.fillStyle = 'rgba(236,230,220,0.30)'; ctx.font = `500 ${opt.size || 76}px ${FS}`; ctx.textBaseline = 'alphabetic';
    drawCells(ctx, tm.hm, cx, cy + 22, (opt.size || 76) * 0.58, (opt.size || 76) * 0.28);
    ctx.fillStyle = 'rgba(201,163,91,0.38)'; ctx.font = `600 15px ${FD}`; spaced(ctx, 0, 2); ctx.textAlign = 'center';
    ctx.fillText(`${DAYS[now.getDay()].slice(0, 3)} ${now.getDate()} ${MONTHS[now.getMonth()].slice(0, 3)}${tm.ap ? '  \u00b7  ' + tm.ap : ''}`, cx, cy - 52);
    spaced(ctx, 0, 0);
    if (opt.sub === 'sun') {
      const st = sunTimes(now, h.lat, h.lon, -0.833);
      let txt = '';
      if (st.rise && now < st.rise) txt = 'sunrise in ' + fmtDur(st.rise - now);
      else if (st.set && now < st.set) txt = 'sunset in ' + fmtDur(st.set - now);
      else { const tmr = sunTimes(new Date(now.getTime() + 864e5), h.lat, h.lon, -0.833); if (tmr.rise) txt = 'sunrise ' + fmtShort(tmr.rise); }
      ctx.fillStyle = 'rgba(163,154,140,0.38)'; ctx.font = `500 13px ${FM}`; ctx.fillText(txt, cx, cy + 58);
    } else if (opt.text) {
      ctx.fillStyle = 'rgba(163,154,140,0.38)'; ctx.font = `500 13px ${FM}`; ctx.fillText(opt.text, cx, cy + 58);
    }
    ctx.restore();
  }

  /* ===================================================================== 2. SPINNING GLOBE */
  const spin = { globe: null, lon: -40, v: 0, dragging: false, lastX: 0, lastT: 0, showUntil: 0, downAt: 0, downX: 0, moved: false, lastFrame: 0 };
  const SPIN_IDLE = 7;             // degrees per second: one turn in ~51 s, calm
  const TILT = 23.44;
  spin.light = (() => { const v = [-0.52, 0.48, 0.70], l = Math.hypot(...v); return v.map(c => c / l); })();
  let spinMeridian = null;
  function spinGlobe(env) { if (!spin.globe) spin.globe = new Globe(316, { lat0: 14, roll: TILT, texW: 1024, light: spin.light }); return spin.globe; }
  function buildMeridian(env) {
    if (spinMeridian) return spinMeridian;
    const W = env.W, c = offscreen(W), g = c.getContext('2d'), cx = W / 2, cy = 222, R = 158;
    brassRing(g, cx, cy, R + 7, R + 13, { dim: 0.8 });
    // engraved degree ticks on the meridian
    // -TILT: the same way the globe's axis leans (Globe.project with roll +TILT), so the pins sit on the poles (D3)
    g.save(); g.translate(cx, cy); g.rotate(-TILT * D2R);
    for (let i = 0; i < 72; i++) {
      const a = i / 72 * TAU, r0 = R + 9, r1 = R + (i % 6 ? 11 : 12.5);
      g.strokeStyle = 'rgba(30,20,5,0.75)'; g.lineWidth = i % 6 ? 0.6 : 1;
      g.beginPath(); g.moveTo(Math.cos(a) * r0, Math.sin(a) * r0); g.lineTo(Math.cos(a) * r1, Math.sin(a) * r1); g.stroke();
    }
    // axis pins at the poles
    for (const s of [-1, 1]) {
      const y = s * (R + 10);
      const pg = g.createRadialGradient(-1.5, y - 1.5, 0, 0, y, 6);
      pg.addColorStop(0, '#fff1c8'); pg.addColorStop(0.5, '#c9a35b'); pg.addColorStop(1, '#3d2d10');
      g.fillStyle = pg; g.beginPath(); g.arc(0, y, 6, 0, TAU); g.fill();
      g.strokeStyle = 'rgba(0,0,0,.6)'; g.lineWidth = 1; g.stroke();
    }
    g.restore();
    return (spinMeridian = c);
  }

  OMNI_FACES.push({
    id: 'globe-spin', group: 'Faces', title: 'Spinning globe',
    summary: 'A calm, continuously turning desk globe under glass. Fling it; it coasts back to its own pace.',
    notes: `<p><b>What it is.</b> The ambient showpiece: a desk globe tilted 23.4\u00b0 on a brass meridian, under a glass
      dome. It turns once every ~51 s. A fixed studio light (upper left) gives it volume. City lights show on the
      shadowed limb, so the night side has life without claiming to be the real terminator. The live globe does
      that job.</p>
      <ul><li><b>Wake:</b> the globe arrives already turning fast and decelerates to its idle pace over about 1.5 s.
      That is the "watching the globe spin up" moment, shortened for a face you see many times a day.</li>
      <li><b>Fling:</b> drag sideways to spin it. Let go and it coasts with inertia, then settles back to idle
      speed. Dragging is clamped so it never feels slippery.</li>
      <li><b>Tap:</b> the quiet time brightens and the date appears for 4 s, then both fall back to a whisper.</li>
      <li><b>Time</b> sits in small brass Cormorant below the meridian, at 55% luminance.</li></ul>
      <p><b>Glance:</b> the globe stops and the face goes black, apart from the drifting time at 30%. A motionless lit
      globe is exactly the static bright area that causes burn-in.</p>
      <p><b>Device.</b> A 316 px disc is about 78k pixels. Lighting and limb are static per pixel (the light is fixed
      in view space), so each frame is only a texture fetch and a multiply. That fits 30 fps on an S3 with the
      globe region double-buffered in PSRAM, and the meridian and dome are pre-rendered ARGB bitmaps composited on
      top. Open question: this face keeps the CPU busy. Suggest it drops to 15 fps after 20 s and the screen sleeps
      on the normal timeout.</p>`,
    init(env) { spinGlobe(env); buildMeridian(env); },
    wake(env) { spin.v = env.reducedMotion ? SPIN_IDLE : 520; spin.showUntil = performance.now() + 3000; spin.lastFrame = 0; },
    pointer(type, x, y) {
      const now = performance.now();
      if (type === 'down') { spin.dragging = true; spin.lastX = x; spin.lastT = now; spin.downAt = now; spin.downX = x; spin.moved = false; spin.v = 0; }
      else if (type === 'move' && spin.dragging) {
        const dt = Math.max(1, now - spin.lastT), dx = x - spin.lastX;
        if (Math.abs(x - spin.downX) > 6) spin.moved = true;
        spin.lon -= dx * 0.42;
        spin.v = clamp(0.5 * spin.v + 0.5 * (dx * 0.42 / dt * 1000), -900, 900);
        spin.lastX = x; spin.lastT = now;
      } else if (type === 'up') {
        spin.dragging = false;
        if (!spin.moved && now - spin.downAt < 350) { spin.showUntil = now + 4000; spin.v = SPIN_IDLE; }
        if (now - spin.lastT > 80) spin.v = SPIN_IDLE;   // held still before release: no fling
      }
    },
    frame(ctx, t, env) {
      const G = spinGlobe(env), now = performance.now(), cx = env.cx, cy = 222, R = G.R;
      const dt = spin.lastFrame ? Math.min(0.05, (now - spin.lastFrame) / 1000) : 0; spin.lastFrame = now;
      if (!spin.dragging) {
        spin.v += (SPIN_IDLE - spin.v) * (1 - Math.exp(-dt / 0.55));     // coast back to idle
        spin.lon -= spin.v * dt;                                          // Earth turns W\u2192E: the longitude facing us decreases
      }
      const lon0 = spin.lon;
      const appear = env.reducedMotion ? clamp(t / 400, 0, 1) : easeOut3(t / 900);
      const img = G.render(env, lon0, null, { gain: 0.25 + 0.75 * appear, night: 1.1, atmo: 0.8 });
      ctx.save();
      ctx.globalAlpha = appear;
      halo(ctx, cx, cy, R, [-0.6, -0.55], 0.75);
      ctx.drawImage(img, cx - R, cy - R);
      ctx.drawImage(dome(R), cx - R - 2, cy - R - 2);
      ctx.drawImage(buildMeridian(env), 0, 0);
      // quiet time
      const d = env.now(), tm = fmtTime(d), loud = clamp((spin.showUntil - now) / 600, 0, 1);
      ctx.textAlign = 'center'; ctx.textBaseline = 'alphabetic';
      ctx.fillStyle = `rgba(201,163,91,${0.55 + 0.45 * loud})`;
      ctx.font = `600 ${24 + 4 * loud}px ${FD}`; spaced(ctx, 0, 1.5);
      ctx.fillText(tm.hm + (tm.ap ? ' ' + tm.ap.toLowerCase() : ''), cx, 428 - 6 * loud);
      if (loud > 0) {
        ctx.globalAlpha = appear * loud; ctx.fillStyle = C.inkDim; ctx.font = `500 11px ${FS}`; spaced(ctx, 0, 2);
        ctx.fillText(`${DAYS[d.getDay()].toUpperCase()} ${d.getDate()} ${MONTHS[d.getMonth()].slice(0, 3).toUpperCase()}`, cx, 440);
      }
      spaced(ctx, 0, 0);
      ctx.restore();
    },
    sleepFrame(ctx, t, env) { glanceTime(ctx, env, {}); },
  });

  /* ===================================================================== 3. ANALOG OVER THE GLOBE */
  const ana = { globe: null, dial: null };
  function anaGlobe(env) {
    const h = homeOf(env), lat0 = Math.round(clamp(h.lat * 0.55, -45, 45));
    if (!ana.globe || ana.globe.o.lat0 !== lat0) ana.globe = new Globe(300, { lat0, texW: 1024 });
    return ana.globe;
  }
  function buildDial(env) {
    if (ana.dial) return ana.dial;
    const W = env.W, c = offscreen(W), g = c.getContext('2d'), cx = W / 2, cy = W / 2;
    // dark dial ring, slight radial sheen (enamel-black, not pure black so it reads as a surface)
    let gr = g.createRadialGradient(cx, cy - 40, 120, cx, cy, 233);
    gr.addColorStop(0, '#16130f'); gr.addColorStop(0.6, '#0c0a08'); gr.addColorStop(1, '#030302');
    g.fillStyle = gr; g.beginPath(); g.arc(cx, cy, 233, 0, TAU); g.arc(cx, cy, 152, 0, TAU, true); g.fill('evenodd');
    // brass bezel hugging the globe
    brassRing(g, cx, cy, 150, 157, { dim: 0.85 });
    // minute track
    for (let i = 0; i < 60; i++) {
      const a = i / 60 * TAU - Math.PI / 2, five = i % 5 === 0;
      g.strokeStyle = five ? 'rgba(220,196,140,0.85)' : 'rgba(180,160,120,0.45)'; g.lineWidth = five ? 2 : 1;
      const r0 = five ? 212 : 215, r1 = 222;
      g.beginPath(); g.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0); g.lineTo(cx + Math.cos(a) * r1, cy + Math.sin(a) * r1); g.stroke();
    }
    g.strokeStyle = 'rgba(180,160,120,0.35)'; g.lineWidth = 0.8; g.beginPath(); g.arc(cx, cy, 223.5, 0, TAU); g.stroke();
    // applied brass batons
    for (let hIdx = 0; hIdx < 12; hIdx++) {
      const a = hIdx / 12 * TAU - Math.PI / 2;
      g.save(); g.translate(cx + Math.cos(a) * 197, cy + Math.sin(a) * 197); g.rotate(a + Math.PI / 2);
      const draw = (ox) => {
        g.shadowColor = 'rgba(0,0,0,0.9)'; g.shadowBlur = 4; g.shadowOffsetX = 1.5; g.shadowOffsetY = 2;
        g.fillStyle = brassFill(g, ox - 3.5, -11, ox + 3.5, 11);
        g.beginPath(); g.roundRect(ox - 3.5, -11, 7, 22, 1.2); g.fill();
        g.shadowColor = 'transparent';
        g.fillStyle = 'rgba(255,248,225,0.55)'; g.fillRect(ox - 2.6, -10, 1, 20);
        g.fillStyle = 'rgba(30,20,5,0.6)'; g.fillRect(ox - 0.5, -8, 1, 16);
      };
      if (hIdx === 0) { draw(-5.5); draw(5.5); } else if (hIdx !== 3) draw(0);
      g.restore();
    }
    // numerals 12/6/9 in the family serif, on the dark ring between globe and batons
    g.fillStyle = 'rgba(214,190,138,0.92)'; g.font = `700 22px ${FD}`; g.textAlign = 'center'; g.textBaseline = 'middle';
    [[12, -Math.PI / 2], [6, Math.PI / 2], [9, Math.PI]].forEach(([n, a]) => g.fillText(String(n), cx + Math.cos(a) * 172, cy + Math.sin(a) * 172 + 1));
    // date window frame at 3
    g.save(); g.translate(cx + 176, cy);
    g.fillStyle = '#050403'; g.beginPath(); g.roundRect(-17, -13, 34, 26, 3); g.fill();
    g.strokeStyle = brassFill(g, -17, -13, 17, 13); g.lineWidth = 2; g.stroke();
    g.restore();
    return (ana.dial = c);
  }
  function drawHand(ctx, ang, spec) {
    // spec: len, tail, w (half-width at widest), at (0..1 where widest), lume
    ctx.save(); ctx.rotate(ang);
    const { len, tail, w, at } = spec, wm = at * len;
    const path = () => {
      ctx.beginPath();
      ctx.moveTo(-w * 0.55, tail);
      ctx.lineTo(-w, -wm); ctx.quadraticCurveTo(-w * 0.95, -len * 0.8, 0, -len);
      ctx.quadraticCurveTo(w * 0.95, -len * 0.8, w, -wm); ctx.lineTo(w * 0.55, tail); ctx.closePath();
    };
    // shadow pass (light from upper-left, hand floats ~1 mm above the globe)
    ctx.save(); ctx.shadowColor = 'rgba(0,0,0,0.85)'; ctx.shadowBlur = 9; ctx.shadowOffsetX = 3; ctx.shadowOffsetY = 4;
    path(); ctx.fillStyle = '#000'; ctx.fill(); ctx.restore();
    // dark outline so the hand reads over oceans and lights alike
    path(); ctx.lineWidth = 3; ctx.strokeStyle = 'rgba(8,6,3,0.9)'; ctx.stroke();
    ctx.fillStyle = brassFill(ctx, -w, 0, w, 0); ctx.fill();
    // centre ridge (faceted hand)
    ctx.beginPath(); ctx.moveTo(0, tail * 0.3); ctx.lineTo(0, -len + 3);
    ctx.strokeStyle = 'rgba(255,246,220,0.35)'; ctx.lineWidth = 0.9; ctx.stroke();
    if (spec.lume) {
      ctx.beginPath(); const l0 = len * 0.28, l1 = len * 0.86, lw = w * 0.42;
      ctx.moveTo(-lw, -l0); ctx.lineTo(-lw, -wm); ctx.quadraticCurveTo(-lw, -l1 * 0.95, 0, -l1);
      ctx.quadraticCurveTo(lw, -l1 * 0.95, lw, -wm); ctx.lineTo(lw, -l0); ctx.closePath();
      ctx.fillStyle = '#efe4c6'; ctx.fill();
      ctx.strokeStyle = 'rgba(60,45,15,0.6)'; ctx.lineWidth = 0.6; ctx.stroke();
    }
    ctx.restore();
  }

  OMNI_FACES.push({
    id: 'globe-analog', group: 'Faces', title: 'Globe analog',
    summary: 'The live globe is the dial: brass batons on an enamel-black ring, outlined and shadowed hands.',
    notes: `<p><b>What it is.</b> A watchmaker's dial built around the real-time Earth. The 300 px live globe
      (same engine, same terminator) sits in a brass bezel. An enamel-black chapter ring outside it carries
      applied brass batons (doubled at 12), Cormorant numerals at 12, 6 and 9, a minute track, and a framed
      date aperture at 3.</p>
      <ul><li><b>Legibility over a busy map:</b> the hands are faceted brass leaves with cream lume inlays. Each has a
      3 px near-black outline and a soft drop shadow offset to the lower right, as if the hands float a millimetre
      above the dome. The globe is rendered at 82% gain, so the brass always stands out from the map.</li>
      <li><b>Seconds:</b> a slim brass seconds hand with a counterweight and a small Omni-red tip. It moves in
      8 beats a second, a mechanical-watch sweep rather than a quartz tick.</li>
      <li><b>Interaction:</b> tap the date window to flip the globe between home and the subsolar view
      (the hands stay put). There is nothing else to learn.</li></ul>
      <p><b>Glance</b> (implemented): the globe is gone. Hour and minute hands are drawn as 1.5 px outlines at 35%
      luminance, the 12 batons as dots, and the whole set drifts \u00b14 px. Under 3% of pixels are lit.</p>
      <p><b>Device.</b> Every frame redraws the 300 px globe and the hands, so the dirty area is the full 466 disc.
      Better: render the globe only when the terminator moves (every 30 s) into a cached layer, then composite the
      cached globe, a pre-rendered dial ARGB layer, and hands drawn as rotated LVGL images. That holds about 30 fps
      for the sweep. At 8 beats a second only the hand bounding boxes are dirty.</p>`,
    init(env) { anaGlobe(env); buildDial(env); ana.sunView = false; },
    pointer(type, x, y, env) {
      if (type === 'up' && Math.hypot(x - (env.cx + 176), y - env.cy) < 30) ana.sunView = !ana.sunView;
    },
    frame(ctx, t, env) {
      const G = anaGlobe(env), h = homeOf(env), now = env.now(), sun = sunPos(now), cx = env.cx, cy = env.cy, R = G.R;
      const rm = env.reducedMotion, k = rm ? 1 : easeOut3(t / 1200), fade = clamp(t / 300, 0, 1);
      const lonT = ana.sunView ? sun.lon : h.lon;
      ana.lon = ana.lon === undefined ? lonT : ana.lon + (((lonT - ana.lon + 540) % 360) - 180) * 0.08;
      const lon0 = ana.lon - (1 - k) * 160;
      ctx.save(); ctx.globalAlpha = fade;
      ctx.drawImage(G.render(env, lon0, sun, { gain: 0.9 * (0.4 + 0.6 * fade), atmo: 0.7 }), cx - R, cy - R);
      ctx.drawImage(dome(R), cx - R - 2, cy - R - 2);
      ctx.drawImage(buildDial(env), 0, 0);
      // home marker
      const hp = G.project(h.lat, h.lon, lon0);
      if (hp[2] > 0) { const hx = cx + hp[0] * R, hy = cy + hp[1] * R;
        ctx.strokeStyle = 'rgba(0,0,0,.7)'; ctx.lineWidth = 3; ctx.beginPath(); ctx.arc(hx, hy, 4.5, 0, TAU); ctx.stroke();
        ctx.strokeStyle = C.brassHi; ctx.lineWidth = 1.5; ctx.stroke(); }
      // date
      ctx.fillStyle = C.brassHi; ctx.font = `600 16px ${FS}`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(String(now.getDate()), cx + 176, cy + 1);
      // hands
      const ms = now.getMilliseconds(), s = now.getSeconds() + Math.floor(ms / 125) / 8, m = now.getMinutes() + s / 60, hr = (now.getHours() % 12) + m / 60;
      const settle = rm ? 1 : easeInOut(t / 900);                         // hands sweep in from 12 on wake
      ctx.translate(cx, cy);
      drawHand(ctx, hr / 12 * TAU * settle, { len: 112, tail: 18, w: 7.5, at: 0.36, lume: true });
      drawHand(ctx, m / 60 * TAU * settle, { len: 192, tail: 22, w: 6, at: 0.3, lume: true });
      // seconds
      ctx.save(); ctx.rotate(s / 60 * TAU * settle);
      ctx.shadowColor = 'rgba(0,0,0,0.8)'; ctx.shadowBlur = 6; ctx.shadowOffsetX = 2; ctx.shadowOffsetY = 3;
      ctx.strokeStyle = '#e7cf95'; ctx.lineWidth = 1.6; ctx.lineCap = 'round';
      ctx.beginPath(); ctx.moveTo(0, 42); ctx.lineTo(0, -206); ctx.stroke();
      ctx.shadowColor = 'transparent';
      ctx.strokeStyle = C.red; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(0, -192); ctx.lineTo(0, -210); ctx.stroke();
      ctx.fillStyle = '#c9a35b'; ctx.beginPath(); ctx.arc(0, 34, 5, 0, TAU); ctx.fill();
      ctx.restore();
      // cap
      const cg = ctx.createRadialGradient(-2, -2, 0, 0, 0, 8);
      cg.addColorStop(0, '#fff1c8'); cg.addColorStop(0.5, '#c9a35b'); cg.addColorStop(1, '#4a3818');
      ctx.fillStyle = cg; ctx.beginPath(); ctx.arc(0, 0, 7.5, 0, TAU); ctx.fill();
      ctx.strokeStyle = 'rgba(0,0,0,.7)'; ctx.lineWidth = 1; ctx.stroke();
      ctx.fillStyle = '#2a1e0a'; ctx.beginPath(); ctx.arc(0, 0, 1.6, 0, TAU); ctx.fill();
      ctx.restore();
    },
    sleepFrame(ctx, t, env) {
      const now = env.now(), [dx, dy] = drift(now), cx = env.cx + dx, cy = env.cy + dy;
      ctx.save(); ctx.translate(cx, cy);
      ctx.fillStyle = 'rgba(201,163,91,0.40)';
      for (let i = 0; i < 12; i++) { const a = i / 12 * TAU - Math.PI / 2; ctx.beginPath(); ctx.arc(Math.cos(a) * 197, Math.sin(a) * 197, i === 0 ? 3.2 : 2.2, 0, TAU); ctx.fill(); }
      const m = now.getMinutes() + now.getSeconds() / 60, hr = now.getHours() % 12 + m / 60;
      ctx.strokeStyle = 'rgba(236,230,220,0.38)'; ctx.lineWidth = 1.5; ctx.lineJoin = 'round';
      for (const [a, len, w] of [[hr / 12 * TAU, 112, 7.5], [m / 60 * TAU, 192, 6]]) {
        ctx.save(); ctx.rotate(a); ctx.beginPath();
        ctx.moveTo(-w * 0.55, 14); ctx.lineTo(-w, -len * 0.33); ctx.lineTo(0, -len); ctx.lineTo(w, -len * 0.33); ctx.lineTo(w * 0.55, 14); ctx.closePath(); ctx.stroke();
        ctx.restore();
      }
      ctx.beginPath(); ctx.arc(0, 0, 4, 0, TAU); ctx.stroke();
      ctx.restore();
    },
  });

  /* ===================================================================== 4. STOCK DIGITAL */
  OMNI_FACES.push({
    id: 'digital', group: 'Faces', title: 'Digital',
    summary: 'No-nonsense numerals, family serif for the date, three quiet complications, a brass seconds rail.',
    notes: `<p><b>What it is.</b> The face for reading the time at arm's length in daylight. Large IBM Plex Sans
      numerals in fixed-width cells so the layout never jitters, in warm white rather than pure white (less glare,
      less wear). The date in Cormorant small caps is the family nod. A thin brass rail runs round the rim as a
      seconds indicator. It is the only moving element, and its lit area is tiny.</p>
      <ul><li><b>Complications</b> (bottom row, 13 px mono, 64% luminance): battery, Wi-Fi/time-sync state, and next
      sun event. The sync item matters more than on a phone. The board has <b>no battery-backed RTC</b>, so the
      face states how fresh its time is ("synced 14m"). If the last NTP sync is old, the item turns brass. If
      the time has never been set, the numerals show dashed and the item reads "no time: tap to set up Wi-Fi".</li>
      <li><b>Tap a complication</b> to jump to its detail: battery \u2192 settings, Wi-Fi \u2192 provisioning, sun \u2192 almanac
      (via <code>env.goto</code>). The targets are 64\u00d744 px or larger.</li></ul>
      <p><b>Glance</b> (implemented): time at 30% luminance, 76 px, with the short date above. The digit cells
      themselves drift on a 9-step orbit (\u00b14 px a minute), so no pixel stays lit at the same level for long.
      About 4% of pixels lit.</p>
      <p><b>Device.</b> Trivial for LVGL: label widgets with a pre-rendered Plex font at 2 sizes. The seconds rail is
      an arc widget, so only its bounding box is dirty. This is the face to fall back to if PSRAM is short.</p>`,
    pointer(type, x, y, env) {
      if (type !== 'up') return;
      if (y > 300 && y < 350) {
        if (x > 250 && x < 340) env.goto('almanac');
      }
    },
    frame(ctx, t, env) {
      const now = env.now(), cx = env.cx, cy = env.cy, h = homeOf(env), fade = clamp(t / 260, 0, 1);
      ctx.save(); ctx.globalAlpha = fade;
      // seconds rail
      const sec = now.getSeconds() + now.getMilliseconds() / 1000;
      ctx.strokeStyle = 'rgba(201,163,91,0.16)'; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(cx, cy, 222, 0, TAU); ctx.stroke();
      for (let i = 0; i < 60; i += 5) { const a = i / 60 * TAU - Math.PI / 2;
        ctx.strokeStyle = 'rgba(201,163,91,0.40)'; ctx.lineWidth = 1.5;
        ctx.beginPath(); ctx.moveTo(cx + Math.cos(a) * 214, cy + Math.sin(a) * 214); ctx.lineTo(cx + Math.cos(a) * 218, cy + Math.sin(a) * 218); ctx.stroke(); }
      const a1 = -Math.PI / 2 + sec / 60 * TAU;
      const grd = ctx.createConicGradient(-Math.PI / 2, cx, cy);
      grd.addColorStop(0, 'rgba(201,163,91,0.0)'); grd.addColorStop(Math.max(0.001, sec / 60), 'rgba(236,210,154,0.9)'); grd.addColorStop(Math.min(1, sec / 60 + 0.0001), 'rgba(201,163,91,0)');
      ctx.strokeStyle = grd; ctx.lineWidth = 2; ctx.lineCap = 'round';
      ctx.beginPath(); ctx.arc(cx, cy, 222, -Math.PI / 2, a1); ctx.stroke();
      ctx.fillStyle = C.brassHi; ctx.beginPath(); ctx.arc(cx + Math.cos(a1) * 222, cy + Math.sin(a1) * 222, 3, 0, TAU); ctx.fill();
      // date
      ctx.textAlign = 'center'; ctx.textBaseline = 'alphabetic';
      ctx.fillStyle = C.brass; ctx.font = `600 25px ${FD}`; spaced(ctx, 0, 2.5);
      ctx.fillText(DAYS[now.getDay()], cx, 128);
      ctx.fillStyle = C.inkDim; ctx.font = `500 17px ${FD}`; spaced(ctx, 0, 2);
      ctx.fillText(`${now.getDate()} ${MONTHS[now.getMonth()]} ${now.getFullYear()}`, cx, 154);
      spaced(ctx, 0, 0);
      // time
      const tm = fmtTime(now);
      ctx.fillStyle = C.ink; ctx.font = `500 118px ${FS}`;
      const w = drawCells(ctx, tm.hm, cx - (tm.ap ? 10 : 0), cy + 48, 68, 30);
      if (tm.ap) { ctx.fillStyle = C.brass; ctx.font = `600 15px ${FS}`; spaced(ctx, 0, 1.5); ctx.textAlign = 'left';
        ctx.fillText(tm.ap, cx - 10 + w / 2 + 6, cy - 22); spaced(ctx, 0, 0);
        ctx.fillStyle = C.inkFaint; ctx.font = `500 15px ${FM}`; ctx.fillText(pad2(now.getSeconds()), cx - 10 + w / 2 + 7, cy + 48); }
      // hairline
      ctx.strokeStyle = 'rgba(201,163,91,0.35)'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(cx - 120, cy + 76); ctx.lineTo(cx + 120, cy + 76); ctx.stroke();
      // complications
      const y = cy + 104, col = 'rgba(236,230,220,0.64)';
      ctx.font = `500 13px ${FM}`; ctx.textAlign = 'left'; ctx.fillStyle = col;
      battery(ctx, cx - 140, y - 4, DEV.battery, col); ctx.fillText(Math.round(DEV.battery * 100) + '%', cx - 113, y);
      const stale = DEV.lastSyncMin > 24 * 60;
      wifi(ctx, cx - 30, y - 9, DEV.wifiOk, stale ? C.brass : col);
      ctx.fillStyle = stale ? C.brass : col; ctx.fillText(`${DEV.lastSyncMin}m`, cx - 16, y);
      const st = sunTimes(now, h.lat, h.lon, -0.833);
      let ev = 'sunset', at = st.set;
      if (st.rise && now < st.rise) { ev = 'sunrise'; at = st.rise; }
      else if (st.set && now > st.set) { ev = 'sunrise'; at = sunTimes(new Date(now.getTime() + 864e5), h.lat, h.lon, -0.833).rise; }
      ctx.strokeStyle = col; ctx.lineWidth = 1.3; const sx = cx + 40;
      ctx.beginPath(); ctx.arc(sx + 7, y - 1, 6, Math.PI, 0); ctx.stroke(); ctx.beginPath(); ctx.moveTo(sx - 1, y - 0.5); ctx.lineTo(sx + 15, y - 0.5); ctx.stroke();
      ctx.beginPath(); if (ev === 'sunset') { ctx.moveTo(sx + 4, y - 13); ctx.lineTo(sx + 7, y - 10); ctx.lineTo(sx + 10, y - 13); } else { ctx.moveTo(sx + 4, y - 10); ctx.lineTo(sx + 7, y - 13); ctx.lineTo(sx + 10, y - 10); } ctx.stroke();
      ctx.fillStyle = col; ctx.fillText(at ? fmtShort(at) : '\u2014', sx + 21, y);
      // home, very quiet
      ctx.textAlign = 'center'; ctx.fillStyle = C.inkFaint; ctx.font = `500 10px ${FS}`; spaced(ctx, 0, 2);
      ctx.fillText((h.name || '').toUpperCase(), cx, cy + 142); spaced(ctx, 0, 0);
      ctx.restore();
    },
    sleepFrame(ctx, t, env) { glanceTime(ctx, env, {}); },
  });

  /* ===================================================================== 5. WORLD CLOCK (invented) */
  const CITIES = [
    ['LON', 'London', 'Europe/London', 51.5, -0.13], ['NYC', 'New York', 'America/New_York', 40.7, -74.0],
    ['LAX', 'Los Angeles', 'America/Los_Angeles', 34.05, -118.24], ['HNL', 'Honolulu', 'Pacific/Honolulu', 21.3, -157.86],
    ['TYO', 'Tokyo', 'Asia/Tokyo', 35.68, 139.69], ['SYD', 'Sydney', 'Australia/Sydney', -33.87, 151.21],
    ['SIN', 'Singapore', 'Asia/Singapore', 1.35, 103.82], ['DEL', 'Delhi', 'Asia/Kolkata', 28.61, 77.21],
    ['DXB', 'Dubai', 'Asia/Dubai', 25.2, 55.27], ['CAI', 'Cairo', 'Africa/Cairo', 30.04, 31.24],
    ['SAO', 'S\u00e3o Paulo', 'America/Sao_Paulo', -23.55, -46.63], ['AKL', 'Auckland', 'Pacific/Auckland', -36.85, 174.76],
  ];
  const fmtCache = {};
  function cityTime(tz, d) {
    if (!fmtCache[tz]) fmtCache[tz] = new Intl.DateTimeFormat('en-US', { timeZone: tz, hour: 'numeric', minute: '2-digit', hourCycle: H24 ? 'h23' : 'h12', weekday: 'short', day: 'numeric' });
    const parts = {}; for (const p of fmtCache[tz].formatToParts(d)) parts[p.type] = p.value;
    return parts;
  }
  const world = { map: null, sel: -1, selUntil: 0, frame: null };
  const WM = 166;   // map radius
  function worldMap(env) { if (!world.map) world.map = new Globe(WM * 2, { proj: 'polar', maxColat: 145, texW: 1024 }); return world.map; }
  function buildWorldFrame(env) {
    if (world.frame) return world.frame;
    const W = env.W, c = offscreen(W), g = c.getContext('2d'), cx = W / 2, cy = W / 2;
    // city band (dark) and fixed 24-hour ring (brass) outside it
    g.fillStyle = '#070605'; g.beginPath(); g.arc(cx, cy, 200, 0, TAU); g.arc(cx, cy, WM + 1, 0, TAU, true); g.fill('evenodd');
    brassRing(g, cx, cy, 200, 226, { dim: 0.55 });
    // hour numerals: noon at top, hours run anticlockwise (true to Earth seen from above the pole)
    g.textAlign = 'center'; g.textBaseline = 'middle';
    for (let hh = 0; hh < 24; hh++) {
      const a = -Math.PI / 2 - (hh - 12) / 24 * TAU;
      const x = cx + Math.cos(a) * 213, y = cy + Math.sin(a) * 213;
      if (hh % 2 === 0) {
        g.save(); g.translate(x, y); g.rotate(a + Math.PI / 2);
        if (Math.sin(a) > 0.2) g.rotate(Math.PI);
        g.fillStyle = hh === 12 ? '#1d1404' : 'rgba(25,17,4,0.9)'; g.font = `${hh % 6 ? 600 : 700} ${hh % 6 ? 12 : 14}px ${FS}`;
        g.fillText(hh === 0 ? '24' : String(hh), 0, 0.5); g.restore();
      } else {
        g.fillStyle = 'rgba(25,17,4,0.8)'; g.beginPath(); g.arc(x, y, 1.6, 0, TAU); g.fill();
      }
    }
    // day/night half-shading of the hour ring: night half (6 pm \u2192 6 am, the bottom) slightly darker
    const lg = g.createLinearGradient(0, cy - 226, 0, cy + 226);
    lg.addColorStop(0, 'rgba(0,0,0,0)'); lg.addColorStop(0.5, 'rgba(0,0,0,0.05)'); lg.addColorStop(1, 'rgba(0,0,10,0.45)');
    g.save(); g.beginPath(); g.arc(cx, cy, 226, 0, TAU); g.arc(cx, cy, 200, 0, TAU, true); g.clip('evenodd'); g.fillStyle = lg; g.fillRect(0, 0, W, W); g.restore();
    // noon index triangle
    g.fillStyle = '#e8c97f'; g.beginPath(); g.moveTo(cx, cy - 199); g.lineTo(cx - 5, cy - 191); g.lineTo(cx + 5, cy - 191); g.closePath(); g.fill();
    // fine rings
    g.strokeStyle = 'rgba(201,163,91,0.35)'; g.lineWidth = 1; g.beginPath(); g.arc(cx, cy, WM + 1.5, 0, TAU); g.stroke();
    return (world.frame = c);
  }

  // graticule: equator and tropics, faint (factored out for tools/export: the device's wt_over layer)
  function worldGraticule(ctx, cx, cy) {
    ctx.lineWidth = 0.8;
    for (const [lat, a] of [[0, 0.22], [23.44, 0.1], [-23.44, 0.1]]) {
      ctx.strokeStyle = `rgba(236,210,154,${a})`; ctx.setLineDash(lat ? [2, 4] : []);
      ctx.beginPath(); ctx.arc(cx, cy, (90 - lat) / 145 * WM, 0, TAU); ctx.stroke();
    }
    ctx.setLineDash([]);
  }

  OMNI_FACES.push({
    id: 'world', group: 'Faces', title: 'World time',
    summary: 'A pole-centred day/night map turns under a fixed 24-hour ring; twelve cities ride the edge.',
    notes: `<p><b>Invented: the world-time dial, done properly.</b> The map is a north-polar azimuthal projection out to
      60\u00b0S, so Sydney, S\u00e3o Paulo and Auckland still appear. It is lit by the real sun, with the curved
      terminator, the night lights and the warm dusk band from the globe engine. The map turns once a day under a
      fixed brass 24-hour ring, keeping the meridian where it is noon under the \u25bc at the top. This is the classic
      world-timer complication made literal: the hour shown against any longitude is its solar time, and the
      terminator shows who is awake.</p>
      <ul><li><b>Cities</b> are IATA codes on a dark band that rides with the map. Each sits at its true longitude,
      with a dot at its true position on the map. Home (brass) is always one of them.</li>
      <li><b>Centre cartouche</b> (it covers the Arctic, which nobody needs): home time. <b>Tap any city</b> and the
      cartouche shows that city's civil time (true time zone, DST-aware), weekday, and offset from home, for 6 s.
      The tap target is the city's whole sector of the band, about 50 px.</li>
      <li>Hours run anticlockwise. That is what the Earth does seen from above the pole, so the map never runs
      against the ring. Open question: mirror it (south-up) for a clockwise ring?</li></ul>
      <p><b>Glance:</b> the map turns off. The 24-hour ring is reduced to 4 dim brass ticks, and home plus one
      pinned city (the last one tapped) show as text, drifting.</p>
      <p><b>Device.</b> In the polar LUT the lighting term depends only on the declination, which changes once a day.
      Each frame is therefore a pure texture-offset copy of 87k pixels plus a cached shading buffer. The city
      band is 12 small labels. Time zones come from the POSIX TZ strings in the firmware's city table.</p>`,
    init(env) { worldMap(env); buildWorldFrame(env); },
    pointer(type, x, y, env) {
      if (type !== 'up') return;
      const r = Math.hypot(x - env.cx, y - env.cy);
      if (r < 60) { world.sel = -1; return; }
      if (r < 120) return;
      const sun = sunPos(env.now()), aTap = Math.atan2(y - env.cy, x - env.cx);
      let best = -1, bd = 1e9;
      const all = cityList(env);
      all.forEach((c, i) => { const a = -Math.PI / 2 - (c[4] - sun.lon) * D2R; const d = Math.abs(((aTap - a + 3 * Math.PI) % TAU) - Math.PI); if (d < bd) { bd = d; best = i; } });
      if (bd < 0.25) { world.sel = best; world.selUntil = performance.now() + 6000; }
    },
    frame(ctx, t, env) {
      const now = env.now(), sun = sunPos(now), cx = env.cx, cy = env.cy, M = worldMap(env), h = homeOf(env);
      const rm = env.reducedMotion, k = rm ? 1 : easeOut3(t / 1300), fade = clamp(t / 300, 0, 1);
      const lonC = sun.lon + (1 - k) * 120;
      ctx.save(); ctx.globalAlpha = fade;
      ctx.drawImage(buildWorldFrame(env), 0, 0);
      ctx.drawImage(M.render(env, lonC, { dec: sun.dec, lon: sun.lon }, { gain: 0.92, night: 1.35 }), cx - WM, cy - WM);
      worldGraticule(ctx, cx, cy);
      // city band + dots
      const all = cityList(env), pn = performance.now();
      if (pn > world.selUntil) world.sel = -1;
      ctx.font = `600 10.5px ${FS}`; spaced(ctx, 0, 1);
      all.forEach((c, i) => {
        const a = -Math.PI / 2 - (c[4] - lonC) * D2R;
        const isHome = c[5], isSel = i === world.sel;
        const p = M.project(c[3], c[4], lonC), px = cx + p[0] * WM, py = cy + p[1] * WM;
        // dot on map
        ctx.fillStyle = 'rgba(0,0,0,.7)'; ctx.beginPath(); ctx.arc(px, py, isHome ? 4 : 3, 0, TAU); ctx.fill();
        ctx.fillStyle = isHome ? C.brassHi : isSel ? '#fff' : 'rgba(236,230,220,.85)'; ctx.beginPath(); ctx.arc(px, py, isHome ? 2.6 : 1.8, 0, TAU); ctx.fill();
        // leader tick at map edge
        ctx.strokeStyle = isHome ? C.brass : 'rgba(236,230,220,.35)'; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(cx + Math.cos(a) * (WM + 2), cy + Math.sin(a) * (WM + 2)); ctx.lineTo(cx + Math.cos(a) * (WM + 6), cy + Math.sin(a) * (WM + 6)); ctx.stroke();
        ctx.fillStyle = isHome ? C.brassHi : isSel ? '#fff' : 'rgba(236,230,220,.72)';
        const flip = Math.sin(a) > 0.15;
        arcText(ctx, c[0], cx, cy, flip ? 186 : 182, a, flip);
      });
      spaced(ctx, 0, 0);
      // centre cartouche: small, so it hides only the Arctic (north of ~57N)
      ctx.fillStyle = 'rgba(4,4,6,0.88)'; ctx.beginPath(); ctx.arc(cx, cy, 38, 0, TAU); ctx.fill();
      ctx.strokeStyle = 'rgba(201,163,91,0.65)'; ctx.lineWidth = 1.2; ctx.stroke();
      ctx.textAlign = 'center'; ctx.textBaseline = 'alphabetic';
      let big, top, sub;
      if (world.sel >= 0 && !all[world.sel][5]) {
        const c = all[world.sel], p = cityTime(c[2], now), off = cityOffsetHours(c[2], now);
        big = `${p.hour}:${p.minute}`; top = c[0];
        sub = `${p.weekday.toUpperCase()} ${(p.dayPeriod || '')}`.trim() + ` ${off >= 0 ? '+' : '\u2212'}${fmtOff(Math.abs(off))}`;
      } else {
        const tm = fmtTime(now); big = tm.hm; top = all[all.length - 1][0];
        sub = `${DAYS[now.getDay()].slice(0, 3).toUpperCase()} ${tm.ap}`.trim();
      }
      ctx.fillStyle = C.ink; ctx.font = `500 21px ${FS}`; ctx.fillText(big, cx, cy + 7);
      ctx.fillStyle = C.brass; ctx.font = `700 10px ${FS}`; spaced(ctx, 0, 1.5); ctx.fillText(top, cx + 0.75, cy - 15);
      ctx.fillStyle = C.inkDim; ctx.font = `500 8.5px ${FS}`; spaced(ctx, 0, 0.8); ctx.fillText(sub, cx + 0.4, cy + 21); spaced(ctx, 0, 0);
      ctx.restore();
    },
    sleepFrame(ctx, t, env) {
      const now = env.now(), [dx, dy] = drift(now), cx = env.cx + dx, cy = env.cy + dy;
      ctx.save(); ctx.fillStyle = 'rgba(201,163,91,0.35)';
      for (let i = 0; i < 4; i++) { const a = i / 4 * TAU; ctx.fillRect(cx + Math.cos(a) * 213 - 1.5, cy + Math.sin(a) * 213 - 1.5, 3, 3); }
      ctx.restore();
      const c = world.sel >= 0 ? cityList(env)[world.sel] : CITIES[0], p = cityTime(c[2], now);
      glanceTime(ctx, env, { size: 64, text: `${c[0]}  ${p.hour}:${p.minute} ${(p.dayPeriod || '').toLowerCase()}` });
    },
  });
  function cityList(env) {
    const h = homeOf(env);
    const home = [h.name ? h.name.slice(0, 3).toUpperCase() : 'HOM', h.name || 'Home', Intl.DateTimeFormat().resolvedOptions().timeZone, h.lat, h.lon, true];
    if (h.name && /columbus/i.test(h.name)) home[0] = 'CMH';
    return CITIES.map(c => c.concat(false)).concat([home]);
  }
  function cityOffsetHours(tz, d) {
    const p = new Intl.DateTimeFormat('en-US', { timeZone: tz, hourCycle: 'h23', year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric' }).formatToParts(d);
    const o = {}; p.forEach(x => o[x.type] = +x.value);
    const asUTC = Date.UTC(o.year, o.month - 1, o.day, o.hour % 24, o.minute);
    const local = Date.UTC(d.getFullYear(), d.getMonth(), d.getDate(), d.getHours(), d.getMinutes());
    return (asUTC - local) / 36e5;
  }
  function fmtOff(hh) { const m = Math.round(hh * 60); return Math.floor(m / 60) + (m % 60 ? ':' + pad2(m % 60) : '') + 'h'; }

  /* ===================================================================== 6. ALMANAC (invented) */
  const alm = { ringKey: '', ring: null, moon: null, moonKey: -1, panel: 0 };
  const hourAngle = hh => Math.PI / 2 + hh / 24 * TAU;           // midnight at the bottom, noon at the top, clockwise
  function skyColor(alt) {
    // altitude \u2192 band colour: night, astro, nautical, civil, golden, day (muted, AMOLED-dark)
    const stops = [[-90, [6, 8, 16]], [-18, [8, 11, 24]], [-12, [16, 26, 52]], [-6, [34, 56, 96]], [-0.8, [120, 92, 60]], [6, [150, 114, 58]], [20, [110, 98, 72]], [90, [118, 106, 80]]];
    for (let i = 1; i < stops.length; i++) if (alt <= stops[i][0]) {
      const [a0, c0] = stops[i - 1], [a1, c1] = stops[i], f = (alt - a0) / (a1 - a0);
      return c0.map((v, j) => Math.round(v + (c1[j] - v) * f));
    }
    return stops[stops.length - 1][1];
  }
  function buildAlmRing(env, day) {
    const h = homeOf(env), key = day.toDateString() + h.lat + h.lon;
    if (alm.ringKey === key) return alm.ring;
    const W = env.W, c = alm.ring || offscreen(W), g = c.getContext('2d'), cx = W / 2, cy = W / 2;
    g.clearRect(0, 0, W, W);
    const base = new Date(day); base.setHours(0, 0, 0, 0);
    const N = 288;
    for (let i = 0; i < N; i++) {
      const d = new Date(base.getTime() + (i + 0.5) * 300000), alt = sunAltAz(d, h.lat, h.lon).alt;
      const [r, gg, b] = skyColor(alt);
      g.fillStyle = `rgb(${r},${gg},${b})`;
      const a0 = hourAngle(i / N * 24) - 0.004, a1 = hourAngle((i + 1) / N * 24) + 0.004;
      g.beginPath(); g.arc(cx, cy, 214, a0, a1); g.arc(cx, cy, 190, a1, a0, true); g.closePath(); g.fill();
    }
    // engraved hour ticks over the band
    for (let hh = 0; hh < 24; hh++) {
      const a = hourAngle(hh), major = hh % 6 === 0;
      g.strokeStyle = major ? 'rgba(0,0,0,0.75)' : 'rgba(0,0,0,0.45)'; g.lineWidth = major ? 2 : 1;
      g.beginPath(); g.moveTo(cx + Math.cos(a) * 190, cy + Math.sin(a) * 190); g.lineTo(cx + Math.cos(a) * (major ? 214 : 197), cy + Math.sin(a) * (major ? 214 : 197)); g.stroke();
    }
    // brass hairlines
    g.strokeStyle = 'rgba(201,163,91,0.55)'; g.lineWidth = 1;
    g.beginPath(); g.arc(cx, cy, 189.5, 0, TAU); g.stroke(); g.beginPath(); g.arc(cx, cy, 214.5, 0, TAU); g.stroke();
    // hour numerals inside
    g.fillStyle = 'rgba(163,154,140,0.85)'; g.font = `500 11px ${FM}`; g.textAlign = 'center'; g.textBaseline = 'middle';
    for (const hh of [0, 3, 6, 9, 12, 15, 18, 21]) {
      const a = hourAngle(hh), lab = H24 ? pad2(hh) : (hh === 0 ? '12a' : hh === 12 ? '12p' : String((hh + 11) % 12 + 1));
      g.fillText(lab, cx + Math.cos(a) * 178, cy + Math.sin(a) * 178);
    }
    alm.ringKey = key; alm.ring = c; return c;
  }
  const MARIA = [[-0.3, -0.35, 0.26], [0.18, -0.32, 0.16], [0.33, -0.06, 0.19], [0.64, -0.24, 0.1], [0.55, 0.14, 0.13], [-0.15, 0.36, 0.15],
    [-0.62, -0.02, 0.3], [0.36, 0.27, 0.08], [-0.42, 0.18, 0.12], [-0.05, -0.62, 0.1]];
  function buildMoon(D, f) {
    const key = Math.round(f * 400);
    if (alm.moonKey === key && alm.moon) return alm.moon;
    const c = alm.moon || offscreen(D), g = c.getContext('2d'), img = g.createImageData(D, D), b = img.data, R = D / 2;
    const ph = f * TAU, Lx = Math.sin(ph), Lz = -Math.cos(ph);
    let seed = 7; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
    const craters = []; for (let i = 0; i < 70; i++) craters.push([rnd() * 2 - 1, rnd() * 2 - 1, 0.015 + rnd() * 0.045]);
    for (let y = 0; y < D; y++) for (let x = 0; x < D; x++) {
      const nx = (x + 0.5 - R) / R, ny = (y + 0.5 - R) / R, r2 = nx * nx + ny * ny;
      const i = (y * D + x) * 4; if (r2 > 1.02) { b[i + 3] = 0; continue; }
      const nz = Math.sqrt(Math.max(0, 1 - r2));
      let alb = 0.82;
      for (const [mx, my, mr] of MARIA) { const d = Math.hypot(nx - mx, ny - my) / mr; if (d < 1.4) alb -= 0.16 * (1 - clamp((d - 0.4) / 1.0, 0, 1)); }
      for (const [mx, my, mr] of craters) { const d = Math.hypot(nx - mx, ny - my) / mr; if (d < 1) alb += d > 0.7 ? 0.035 * (1 - d) / 0.3 : -0.018 * (1 - d); }
      alb += 0.25 * Math.max(0, 1 - Math.hypot(nx + 0.1, ny - 0.7) / 0.07);   // Tycho
      const mu = nx * Lx + nz * Lz;
      let s = clamp((mu + 0.04) / 0.1, 0, 1); s = s * s * (3 - 2 * s);
      const lum = (s * (0.35 + 0.65 * Math.pow(Math.max(mu, 0), 0.35)) * alb + 0.035 * (1 - s)) * (0.75 + 0.25 * nz);
      b[i] = 255 * lum * 1.0; b[i + 1] = 255 * lum * 0.97; b[i + 2] = 255 * lum * 0.9;
      b[i + 3] = 255 * clamp((1 - Math.sqrt(r2)) * R + 0.5, 0, 1);
    }
    g.putImageData(img, 0, 0);
    alm.moonKey = key; alm.moon = c; return c;
  }

  OMNI_FACES.push({
    id: 'almanac', group: 'Faces', title: 'Almanac',
    summary: 'Today\'s light as a 24-hour sky ring (night, twilights, day), the sun\'s place now, and the moon.',
    notes: `<p><b>Invented: a pocket almanac.</b> The questions this answers are the ones a pocket watch on a walk
      should answer: <i>how much light is left, when is it dark, what is the moon doing.</i></p>
      <ul><li><b>Sky ring:</b> a 24-hour dial (midnight at the bottom, noon at the top) coloured by the sun's
      altitude at home, sampled every 5 minutes. Astronomical, nautical and civil twilight show as bands of
      deepening blue. Golden hour is the warm shoulder, and daylight a muted brass. The proportions change with the
      seasons, which is lovely to watch over a year. Sunrise and sunset get engraved ticks and times.</li>
      <li><b>The sun now:</b> a glowing bead on the ring (a hollow ring when below the horizon) with a fine needle,
      so the face reads like a 24-hour watch too.</li>
      <li><b>Centre:</b> day length with the change since yesterday ("+1m 58s" is what people actually notice),
      the moon rendered at its true phase with maria, its name, illumination, and the next full or new moon.</li>
      <li><b>Tap the centre</b> to swap the moon for a sun panel: altitude, azimuth (computed, so no compass is
      needed; it says where to look relative to north), solar noon, and the length of a 1 m stick's shadow.</li></ul>
      <p><b>Glance:</b> only the sun bead, the sunset/sunrise ticks and the "light left" countdown, in dim brass.</p>
      <p><b>Device.</b> The ring is a cached bitmap rebuilt once a day, and so is the moon (88 px). Each frame draws the
      bead and needle plus text, so the CPU cost is close to nothing. The rise/set maths is NOAA-lite, good to about
      a minute below 60\u00b0 latitude. Polar day and night are handled ("sun does not set").</p>`,
    pointer(type, x, y, env) { if (type === 'up' && Math.hypot(x - env.cx, y - env.cy) < 120) alm.panel = 1 - alm.panel; },
    frame(ctx, t, env) {
      const now = env.now(), h = homeOf(env), cx = env.cx, cy = env.cy, fade = clamp(t / 400, 0, 1);
      ctx.save(); ctx.globalAlpha = fade;
      ctx.drawImage(buildAlmRing(env, now), 0, 0);
      const st = sunTimes(now, h.lat, h.lon, -0.833);
      const hourOf = d => d.getHours() + d.getMinutes() / 60 + d.getSeconds() / 3600;
      // rise/set ticks + labels
      for (const [d, lab] of [[st.rise, 'rise'], [st.set, 'set']]) {
        if (!d) continue;
        const a = hourAngle(hourOf(d));
        ctx.strokeStyle = C.brassHi; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.moveTo(cx + Math.cos(a) * 186, cy + Math.sin(a) * 186); ctx.lineTo(cx + Math.cos(a) * 218, cy + Math.sin(a) * 218); ctx.stroke();
        ctx.fillStyle = C.ink; ctx.font = `500 12.5px ${FM}`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        ctx.fillText(fmtShort(d), cx + Math.cos(a) * 158, cy + Math.sin(a) * 158 - 6);
        ctx.fillStyle = C.brass; ctx.font = `600 11px ${FD}`; spaced(ctx, 0, 1.5);
        ctx.fillText(lab === 'rise' ? 'SUNRISE' : 'SUNSET', cx + Math.cos(a) * 158, cy + Math.sin(a) * 158 + 9); spaced(ctx, 0, 0);
      }
      // sun now
      const alt = sunAltAz(now, h.lat, h.lon).alt, an = hourAngle(hourOf(now));
      const k = env.reducedMotion ? 1 : easeOut3(t / 1100), aNow = hourAngle(0) + (an - hourAngle(0)) * k;
      ctx.strokeStyle = 'rgba(236,210,154,0.45)'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(cx + Math.cos(aNow) * 130, cy + Math.sin(aNow) * 130); ctx.lineTo(cx + Math.cos(aNow) * 186, cy + Math.sin(aNow) * 186); ctx.stroke();
      const bx = cx + Math.cos(aNow) * 202, by = cy + Math.sin(aNow) * 202;
      if (alt > -0.8) {
        const gl = ctx.createRadialGradient(bx, by, 0, bx, by, 20); gl.addColorStop(0, 'rgba(255,220,140,0.85)'); gl.addColorStop(1, 'rgba(255,220,140,0)');
        ctx.fillStyle = gl; ctx.beginPath(); ctx.arc(bx, by, 20, 0, TAU); ctx.fill();
        ctx.fillStyle = '#fff0c4'; ctx.beginPath(); ctx.arc(bx, by, 7, 0, TAU); ctx.fill();
      } else {
        ctx.fillStyle = '#000'; ctx.beginPath(); ctx.arc(bx, by, 7.5, 0, TAU); ctx.fill();
        ctx.strokeStyle = C.brassHi; ctx.lineWidth = 1.8; ctx.stroke();
      }
      // centre
      ctx.textAlign = 'center'; ctx.textBaseline = 'alphabetic';
      const yest = sunTimes(new Date(now.getTime() - 864e5), h.lat, h.lon, -0.833);
      let dayLen = st.polar === 'day' ? 864e5 : st.polar === 'night' ? 0 : st.set - st.rise;
      let yLen = yest.polar === 'day' ? 864e5 : yest.polar === 'night' ? 0 : yest.set - yest.rise;
      const dd = Math.round((dayLen - yLen) / 1000);
      ctx.fillStyle = C.brass; ctx.font = `600 13px ${FD}`; spaced(ctx, 0, 2.5); ctx.fillText('DAYLIGHT', cx, cy - 88); spaced(ctx, 0, 0);
      ctx.fillStyle = C.ink; ctx.font = `500 30px ${FS}`; ctx.fillText(fmtDur(dayLen).replace(' ', '  '), cx, cy - 56);
      ctx.fillStyle = dd < 0 ? C.inkDim : '#c8d9a8'; ctx.font = `500 12px ${FM}`;
      ctx.fillText(`${dd < 0 ? '\u2212' : '+'}${Math.floor(Math.abs(dd) / 60)}m ${pad2(Math.abs(dd) % 60)}s vs yesterday`, cx, cy - 37);
      if (alm.panel === 0) {
        const mp = moonPhase(now), MD = 88;
        ctx.drawImage(buildMoon(MD, mp.f), cx - MD / 2, cy + 18 - MD / 2);
        ctx.fillStyle = C.ink; ctx.font = `600 15px ${FD}`; spaced(ctx, 0, 1.2); ctx.fillText(mp.name, cx, cy + 82); spaced(ctx, 0, 0);
        const nxt = mp.f < 0.5 ? ['Full', mp.nextFull] : ['New', mp.nextNew];
        ctx.fillStyle = C.inkDim; ctx.font = `500 11.5px ${FM}`;
        ctx.fillText(`${Math.round(mp.illum * 100)}% \u00b7 ${nxt[0]} ${DAYS[nxt[1].getDay()].slice(0, 3)} ${nxt[1].getDate()} ${MONTHS[nxt[1].getMonth()].slice(0, 3)}`, cx, cy + 101);
      } else {
        const aa = sunAltAz(now, h.lat, h.lon), dirs = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
        ctx.fillStyle = C.ink; ctx.font = `500 40px ${FS}`; ctx.fillText(`${aa.alt.toFixed(0).replace('-', '\u2212')}\u00b0`, cx, cy + 22);
        ctx.fillStyle = C.brass; ctx.font = `600 12px ${FD}`; spaced(ctx, 0, 2); ctx.fillText('SUN ALTITUDE', cx, cy - 12); spaced(ctx, 0, 0);
        ctx.fillStyle = C.inkDim; ctx.font = `500 12px ${FM}`;
        ctx.fillText(`azimuth ${aa.az.toFixed(0)}\u00b0 ${dirs[Math.round(aa.az / 45) % 8]}`, cx, cy + 50);
        ctx.fillText(`solar noon ${fmtShort(st.noon)}`, cx, cy + 68);
        ctx.fillText(aa.alt > 1 ? `1 m stick \u2192 ${(1 / Math.tan(aa.alt * D2R)).toFixed(1)} m shadow` : 'sun below horizon', cx, cy + 86);
      }
      ctx.restore();
    },
    sleepFrame(ctx, t, env) {
      const now = env.now(), h = homeOf(env), [dx, dy] = drift(now), cx = env.cx + dx, cy = env.cy + dy;
      const st = sunTimes(now, h.lat, h.lon, -0.833), hourOf = d => d.getHours() + d.getMinutes() / 60;
      ctx.save(); ctx.strokeStyle = 'rgba(201,163,91,0.35)'; ctx.lineWidth = 2;
      for (const d of [st.rise, st.set]) if (d) { const a = hourAngle(hourOf(d)); ctx.beginPath(); ctx.moveTo(cx + Math.cos(a) * 192, cy + Math.sin(a) * 192); ctx.lineTo(cx + Math.cos(a) * 212, cy + Math.sin(a) * 212); ctx.stroke(); }
      const a = hourAngle(hourOf(now)); ctx.beginPath(); ctx.arc(cx + Math.cos(a) * 202, cy + Math.sin(a) * 202, 5, 0, TAU); ctx.stroke();
      ctx.restore();
      glanceTime(ctx, env, { sub: 'sun', size: 64 });
    },
  });

  /* ===================================================================== 7. TIMER / STOPWATCH (invented) */
  const tmr = { mode: 'idle', setMin: 0, endAt: 0, remain: 0, swStart: 0, swAcc: 0, down: null, holdStart: 0, lastAng: 0, doneAt: 0 };
  const rimAngle = (x, y, env) => { let a = Math.atan2(x - env.cx, -(y - env.cy)); return a < 0 ? a + TAU : a; };   // 0 at top, clockwise
  function tmrRemaining() { return tmr.mode === 'count' ? Math.max(0, tmr.endAt - performance.now()) : tmr.remain; }
  function swElapsed() { return tmr.swAcc + (tmr.mode === 'sw' ? performance.now() - tmr.swStart : 0); }

  OMNI_FACES.push({
    id: 'timer', group: 'Faces', title: 'Timer & stopwatch',
    summary: 'Wind the rim like a kitchen timer to set a countdown; tap the centre for a stopwatch.',
    notes: `<p><b>Invented: the one tool a pocket watch is always asked for.</b> There are no menus. The two modes are
      reached by two different gestures:</p>
      <ul><li><b>Wind the rim</b> (drag along the outer ring clockwise from 12) to set a countdown of 1 to 60 minutes,
      snapping to whole minutes. A brass sector fills the ring as you wind. This is the same "drag around the rim
      like a dial" gesture as the rim keyboard, so it is learned once and used in both places. Let go and it starts.
      The sector then drains anticlockwise like a mechanical timer.</li>
      <li><b>Tap the centre</b> with no countdown set to start a stopwatch (m:ss.cc). A brass bead circles the ring
      once a minute. Tap again to pause or resume.</li>
      <li><b>Long-press the centre</b> (0.6 s, with a brass progress ring under your thumb) to reset either.</li>
      <li><b>Done:</b> the ring turns Omni red and pulses, with the speaker and haptic chime on the device. Tap
      anywhere to dismiss. In the last minute the sector turns red as a heads-up.</li></ul>
      <p><b>Glance</b>: a countdown in progress overrides the normal glance on every face. Remaining minutes appear
      large at 35%, and the sector shows as a thin outline. A running stopwatch shows whole seconds only.</p>
      <p><b>Device.</b> Timing uses <code>esp_timer</code>, not wall time, so it is correct even before NTP sync.
      In this mockup it uses real time even when the simulated clock runs fast. The countdown keeps running
      while asleep, and an RTC-timer wake rings the alarm. The rim drag needs touch sampling at 60 Hz or more,
      which the CST9217 provides.</p>`,
    pointer(type, x, y, env) {
      const r = Math.hypot(x - env.cx, y - env.cy), now = performance.now();
      if (tmr.mode === 'done') { if (type === 'up') { tmr.mode = 'idle'; tmr.setMin = 0; } return; }
      if (type === 'down') {
        tmr.down = { x, y, t: now, rim: r > 140, moved: false };
        if (r > 140 && tmr.mode !== 'sw' && tmr.mode !== 'swpause') { tmr.prevMode = tmr.mode; tmr.mode = 'set'; tmr.lastAng = rimAngle(x, y, env); tmr.setMin = Math.max(0, Math.round(tmr.lastAng / TAU * 60)); }
        else if (r <= 140) tmr.holdStart = now;
        return;
      }
      if (!tmr.down) return;
      if (type === 'move') {
        if (Math.hypot(x - tmr.down.x, y - tmr.down.y) > 8) tmr.down.moved = true;
        if (tmr.mode === 'set') {
          const a = rimAngle(x, y, env);
          let m = a / TAU * 60;
          if (tmr.setMin > 45 && m < 15) m = 60; else if (tmr.setMin < 5 && m > 50) m = 0;   // no wrap past 12
          tmr.setMin = clamp(Math.round(m), 0, 60);
        }
        return;
      }
      if (type === 'up') {
        const d = tmr.down; tmr.down = null; tmr.holdStart = 0;
        if (tmr.mode === 'set') {
          if (tmr.setMin > 0) { tmr.mode = 'count'; tmr.endAt = now + tmr.setMin * 60000; }
          else tmr.mode = 'idle';
          return;
        }
        if (d.rim) return;
        if (now - d.t > 600) { tmr.mode = 'idle'; tmr.setMin = 0; tmr.swAcc = 0; tmr.remain = 0; return; }   // long-press reset
        if (d.moved) return;
        if (tmr.mode === 'idle') { tmr.mode = 'sw'; tmr.swStart = now; tmr.swAcc = 0; }
        else if (tmr.mode === 'sw') { tmr.swAcc += now - tmr.swStart; tmr.mode = 'swpause'; }
        else if (tmr.mode === 'swpause') { tmr.mode = 'sw'; tmr.swStart = now; }
        else if (tmr.mode === 'count') { tmr.remain = tmrRemaining(); tmr.mode = 'paused'; }
        else if (tmr.mode === 'paused') { tmr.mode = 'count'; tmr.endAt = now + tmr.remain; }
      }
    },
    frame(ctx, t, env) {
      const cx = env.cx, cy = env.cy, now = performance.now(), fade = clamp(t / 300, 0, 1);
      if (tmr.mode === 'count' && tmrRemaining() <= 0) { tmr.mode = 'done'; tmr.doneAt = now; }
      ctx.save(); ctx.globalAlpha = fade;
      const R0 = 176, R1 = 214;
      // scale ring
      ctx.fillStyle = '#0b0a08'; ctx.beginPath(); ctx.arc(cx, cy, R1, 0, TAU); ctx.arc(cx, cy, R0, 0, TAU, true); ctx.fill('evenodd');
      // sector
      let mins = 0, red = false;
      if (tmr.mode === 'set') mins = tmr.setMin;
      else if (tmr.mode === 'count' || tmr.mode === 'paused') { mins = tmrRemaining() / 60000; red = mins < 1; }
      else if (tmr.mode === 'done') { mins = 60; red = true; }
      if (mins > 0) {
        const a0 = -Math.PI / 2, a1 = a0 + mins / 60 * TAU;
        const pulse = tmr.mode === 'done' ? 0.45 + 0.4 * Math.sin((now - tmr.doneAt) / 160) : tmr.mode === 'paused' ? 0.45 + 0.15 * Math.sin(now / 300) : 0.75;
        const gr = ctx.createRadialGradient(cx, cy, R0, cx, cy, R1);
        if (red) { gr.addColorStop(0, `rgba(160,40,30,${pulse})`); gr.addColorStop(1, `rgba(216,68,58,${pulse})`); }
        else { gr.addColorStop(0, `rgba(120,92,40,${pulse})`); gr.addColorStop(0.6, `rgba(201,163,91,${pulse})`); gr.addColorStop(1, `rgba(150,118,60,${pulse})`); }
        ctx.fillStyle = gr; ctx.beginPath(); ctx.arc(cx, cy, R1, a0, a1); ctx.arc(cx, cy, R0, a1, a0, true); ctx.closePath(); ctx.fill();
        // leading edge
        ctx.strokeStyle = red ? '#ff8a7a' : C.brassHi; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.moveTo(cx + Math.cos(a1) * (R0 - 4), cy + Math.sin(a1) * (R0 - 4)); ctx.lineTo(cx + Math.cos(a1) * (R1 + 4), cy + Math.sin(a1) * (R1 + 4)); ctx.stroke();
      }
      // ticks + numerals (over sector, engraved look)
      for (let i = 0; i < 60; i++) {
        const a = -Math.PI / 2 + i / 60 * TAU, five = i % 5 === 0;
        ctx.strokeStyle = five ? 'rgba(236,230,220,0.75)' : 'rgba(236,230,220,0.30)'; ctx.lineWidth = five ? 1.6 : 1;
        ctx.beginPath(); ctx.moveTo(cx + Math.cos(a) * (five ? R1 - 12 : R1 - 7), cy + Math.sin(a) * (five ? R1 - 12 : R1 - 7)); ctx.lineTo(cx + Math.cos(a) * (R1 - 2), cy + Math.sin(a) * (R1 - 2)); ctx.stroke();
      }
      ctx.fillStyle = 'rgba(236,230,220,0.8)'; ctx.font = `500 12px ${FM}`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      for (let i = 0; i < 60; i += 5) { const a = -Math.PI / 2 + i / 60 * TAU; ctx.fillText(String(i), cx + Math.cos(a) * (R0 + 11), cy + Math.sin(a) * (R0 + 11)); }
      ctx.strokeStyle = 'rgba(201,163,91,0.5)'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(cx, cy, R1 + 0.5, 0, TAU); ctx.stroke(); ctx.beginPath(); ctx.arc(cx, cy, R0 - 0.5, 0, TAU); ctx.stroke();
      // stopwatch bead
      if (tmr.mode === 'sw' || tmr.mode === 'swpause') {
        const e = swElapsed(), a = -Math.PI / 2 + (e % 60000) / 60000 * TAU;
        ctx.strokeStyle = 'rgba(201,163,91,0.55)'; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.arc(cx, cy, R0 - 10, -Math.PI / 2, a); ctx.stroke();
        ctx.fillStyle = C.brassHi; ctx.beginPath(); ctx.arc(cx + Math.cos(a) * (R0 - 10), cy + Math.sin(a) * (R0 - 10), 5, 0, TAU); ctx.fill();
      }
      // centre readout
      ctx.textBaseline = 'alphabetic';
      let big = '', small = '', label = '', col = C.ink;
      if (tmr.mode === 'idle') { big = '0:00'; col = C.inkFaint; label = 'TIMER'; }
      else if (tmr.mode === 'set') { big = tmr.setMin + ':00'; label = 'SET \u00b7 RELEASE TO START'; }
      else if (tmr.mode === 'count' || tmr.mode === 'paused') { const s = Math.ceil(tmrRemaining() / 1000); big = Math.floor(s / 60) + ':' + pad2(s % 60); label = tmr.mode === 'paused' ? 'PAUSED' : 'COUNTDOWN'; if (s < 60) col = '#ff9c8c'; }
      else if (tmr.mode === 'done') { big = '0:00'; col = C.red; label = 'TIME'; }
      else { const e = swElapsed(), s = Math.floor(e / 1000); big = Math.floor(s / 60) + ':' + pad2(s % 60); small = '.' + pad2(Math.floor(e / 10) % 100); label = tmr.mode === 'swpause' ? 'PAUSED' : 'STOPWATCH'; }
      ctx.fillStyle = col; ctx.font = `500 76px ${FS}`;
      const w = drawCells(ctx, big, cx - (small ? 20 : 0), cy + 26, 44, 20);
      if (small) { ctx.fillStyle = C.inkDim; ctx.font = `500 26px ${FM}`; ctx.textAlign = 'left'; ctx.fillText(small, cx - 20 + w / 2 + 2, cy + 26); }
      ctx.textAlign = 'center'; ctx.fillStyle = tmr.mode === 'done' ? C.red : C.brass; ctx.font = `600 13px ${FD}`; spaced(ctx, 0, 2.5);
      ctx.fillText(label, cx, cy - 46); spaced(ctx, 0, 0);
      // hints
      ctx.fillStyle = C.inkFaint; ctx.font = `500 10.5px ${FS}`; spaced(ctx, 0, 1.2);
      const hint = tmr.mode === 'idle' ? 'TAP \u00b7 STOPWATCH     WIND RIM \u00b7 TIMER' : tmr.mode === 'done' ? 'TAP TO DISMISS' : tmr.mode === 'set' ? '' : 'TAP \u00b7 PAUSE     HOLD \u00b7 RESET';
      ctx.fillText(hint, cx, cy + 66);
      // wall time, tiny
      const tm = fmtTime(env.now()); ctx.fillStyle = C.inkDim; ctx.font = `500 13px ${FM}`; spaced(ctx, 0, 0);
      ctx.fillText(tm.hm + (tm.ap ? ' ' + tm.ap.toLowerCase() : ''), cx, cy + 112);
      // long-press progress
      if (tmr.holdStart && tmr.down && !tmr.down.moved && tmr.mode !== 'idle') {
        const p = clamp((now - tmr.holdStart - 150) / 450, 0, 1);
        if (p > 0) { ctx.strokeStyle = C.brassHi; ctx.lineWidth = 3; ctx.lineCap = 'round'; ctx.beginPath(); ctx.arc(cx, cy, 128, -Math.PI / 2, -Math.PI / 2 + p * TAU); ctx.stroke(); }
      }
      ctx.restore();
    },
    sleepFrame(ctx, t, env) {
      if (tmr.mode === 'count' || tmr.mode === 'paused') {
        const [dx, dy] = drift(env.now()), cx = env.cx + dx, cy = env.cy + dy, m = tmrRemaining() / 60000;
        ctx.save(); ctx.strokeStyle = 'rgba(201,163,91,0.4)'; ctx.lineWidth = 1.5;
        ctx.beginPath(); ctx.arc(cx, cy, 205, -Math.PI / 2, -Math.PI / 2 + m / 60 * TAU); ctx.stroke();
        ctx.fillStyle = 'rgba(236,230,220,0.35)'; ctx.font = `500 72px ${FS}`; ctx.textBaseline = 'alphabetic';
        drawCells(ctx, String(Math.ceil(m)), cx, cy + 24, 42, 18);
        ctx.fillStyle = 'rgba(201,163,91,0.38)'; ctx.font = `600 13px ${FD}`; ctx.textAlign = 'center'; spaced(ctx, 0, 2);
        ctx.fillText('MIN LEFT', cx, cy + 50); spaced(ctx, 0, 0); ctx.restore();
      } else glanceTime(ctx, env, {});
    },
  });

  /* Export hook for tools/export (the device's brass layers are drawn from this very code). */
  window.OMNI_PRACTICAL = { Globe, sunPos, halo, dome, brassRing, brassFill, buildMeridian, buildDial, drawHand,
    buildWorldFrame, worldGraticule, WM,
    /* the export loads the IBM Plex faces after the thumbnails; the cached world frame is drawn again with them */
    resetWorldFrame() { world.frame = null; } };
})();
