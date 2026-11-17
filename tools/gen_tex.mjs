// Generate the globe texture for the assets partition: firmware/apps/omni/assets/earth512.tex (earth1024.tex is retired, R69), or
// with --grade omni the Omni globe's firmware/apps/omni/assets/omni512.tex (the mockup's grade on the day map; night plane 0).
// Sources are the equirectangular day and night maps, design/assets/earth-{day,night}-1024.jpg (1024 x 512).
// earth512 is the 2:1 box filter of the 8-bit maps. Each texel is a little-endian u32: day RGB565 | night RGB565 << 16,
// rows top (north) to bottom, the layout omni_globe_tex_t reads.
// Usage: node tools/gen_tex.mjs [--grade omni]   (deps: npm --prefix tools ci)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import jpeg from 'jpeg-js';

export const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');

/* Decodes an equirectangular JPEG to RGBA8 {width, height, data}, insisting on w x h. */
export function decodeEquirect(file, w = 1024, h = 512) {
  const img = jpeg.decode(fs.readFileSync(file), { useTArray: true });
  if (img.width !== w || img.height !== h) throw new Error(`${file}: ${img.width}x${img.height}, expected ${w}x${h}`);
  return img;
}

/* k:1 box filter of an RGBA8 image (alpha ignored): mean of each k x k block, rounded. Returns RGBA8 (alpha 255). */
export function boxDown(img, k) {
  const W = img.width / k, H = img.height / k, n = k * k, data = new Uint8Array(W * H * 4);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    let r = 0, g = 0, b = 0;
    for (let dy = 0; dy < k; dy++) for (let dx = 0; dx < k; dx++) {
      const i = ((y * k + dy) * img.width + x * k + dx) * 4;
      r += img.data[i]; g += img.data[i + 1]; b += img.data[i + 2];
    }
    const o = (y * W + x) * 4;
    data[o] = Math.round(r / n); data[o + 1] = Math.round(g / n); data[o + 2] = Math.round(b / n); data[o + 3] = 255;
  }
  return { width: W, height: H, data };
}

/* RGB565 by truncation, as LVGLImage.py packs every other RGB565 asset. */
export const rgb565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

/* Interleaved texture: u32 LE day565 | night565 << 16 per texel. */
export function packTex(day, night) {
  if (day.width !== night.width || day.height !== night.height) throw new Error('day/night size mismatch');
  const n = day.width * day.height, out = Buffer.alloc(n * 4);
  for (let i = 0; i < n; i++) {
    const d = day.data, q = night.data, j = i * 4;
    out.writeUInt32LE((rgb565(d[j], d[j + 1], d[j + 2]) | (rgb565(q[j], q[j + 1], q[j + 2]) << 16)) >>> 0, j);
  }
  return out;
}

/* The Omni globe's grade (faces-omni.js initGlobe, 332-339) toward the prop's painted map insert: warmer land, deeper
   ocean, a touch of contrast. Verbatim, on the 1024 day map as the mockup grades it, into a Uint8ClampedArray as the
   mockup's ImageData is (its stores round to nearest, ties to even, and clamp). Returns RGBA8. */
export function gradeOmni(img) {
  const d = new Uint8ClampedArray(img.data);
  const clamp = (x, a, b) => x < a ? a : x > b ? b : x;
  for (let i = 0; i < d.length; i += 4) {
    let r = d[i], gg = d[i + 1], b = d[i + 2];
    const ocean = b > r + 8 && b > gg;
    if (ocean) { r = r * .5 + 4; gg = gg * .78 + 10; b = b * .82 + 12; }
    else { r = r * 1.08 + 10; gg = gg * 1.0 + 4; b = b * .82; }
    d[i] = clamp((r - 128) * 1.08 + 128, 0, 255); d[i + 1] = clamp((gg - 128) * 1.08 + 128, 0, 255); d[i + 2] = clamp((b - 128) * 1.08 + 128, 0, 255);
  }
  return { width: img.width, height: img.height, data: d };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const grade = process.argv.indexOf('--grade') > 0 ? process.argv[process.argv.indexOf('--grade') + 1] : null;
  if (grade !== null && grade !== 'omni') throw new Error(`unknown --grade ${grade} (only omni)`);
  const day = decodeEquirect(path.join(root, 'design/assets/earth-day-1024.jpg'));
  const night = decodeEquirect(path.join(root, 'design/assets/earth-night-1024.jpg'));
  const outDir = path.join(root, 'firmware/apps/omni/assets');
  fs.mkdirSync(outDir, { recursive: true });
  // --grade omni: omni512.tex, the graded day map boxed to 512 x 256 (fixed, C3), night plane 0
  const dark = { width: 512, height: 256, data: new Uint8Array(512 * 256 * 4) };
  const jobs = grade ? [['omni512.tex', boxDown(gradeOmni(day), 2), dark]] : [['earth512.tex', boxDown(day, 2), boxDown(night, 2)]];
  for (const [name, d, n] of jobs) {
    const buf = packTex(d, n);
    fs.writeFileSync(path.join(outDir, name), buf);
    console.log(`wrote firmware/apps/omni/assets/${name} (${d.width}x${d.height}, ${buf.length} bytes)`);
  }
}
