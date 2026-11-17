// Decode the QR in a device screenshot (tools/shot.py PNG) and compare it with the expected payload.
// Usage: node tools/qr_check.mjs <shot.png> <expected-json>     (once: npm --prefix tools ci)
// Exit 0 when the decoded text equals the expected string exactly, 1 otherwise.
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');
const jsQR = require('jsqr');

const [png, expected] = process.argv.slice(2);
if (!png || expected === undefined) {
  console.error('usage: node tools/qr_check.mjs <shot.png> <expected-json>');
  process.exit(2);
}
const img = PNG.sync.read(readFileSync(png));
const code = jsQR(new Uint8ClampedArray(img.data), img.width, img.height);
if (!code) {
  console.log('QR NOT FOUND');
  process.exit(1);
}
console.log(`decoded (version ${code.version}): ${code.data}`);
const ok = code.data === expected;
console.log(ok ? 'MATCH' : `MISMATCH, expected: ${expected}`);
process.exit(ok ? 0 : 1);
