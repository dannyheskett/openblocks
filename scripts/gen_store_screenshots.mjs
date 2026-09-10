// Capture the Google Play and App Store screenshots from the web build.
//
// The store listings need screenshots of the PORTRAIT touch layout, which is the
// same renderer Android and iOS run (src/render_portrait.c) -- the web build
// picks it whenever the primary pointer is coarse, so a headless browser with
// touch emulation produces pixel-equivalent frames without a device or a farm.
//
// Regenerate whenever the portrait UI changes. The committed PNGs under
// android/play-assets/screenshots/ and ios/app-store-assets/screenshots/ are what
// scripts/play_listing.py and scripts/asc_listing.py push to the stores.
//
// Usage:
//   npm i playwright-core                    # not a repo dependency; dev-only
//   gh release download release-N -p '*-web-wasm.zip' && unzip it somewhere
//   node scripts/gen_store_screenshots.mjs --src <unzipped-web-dir>
//
// Options:
//   --src <dir>      unzipped web bundle (contains openblocks.html)   [required]
//   --out <dir>      repo root to write screenshots under         [default: cwd]
//   --chrome <path>  Chromium binary  [default: $CHROME, else the Playwright cache]
//   --only <name>    capture a single target (play-phone|play-tablet|ios-6.9)
//
// Why a browser and not the real app: the maintainer has no Android device and no
// Mac, and Device Farm returns video, not clean full-resolution stills.

import { chromium } from 'playwright-core';
import { createServer } from 'node:http';
import { readFile, mkdir } from 'node:fs/promises';
import { existsSync, readdirSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { homedir } from 'node:os';

// ---------------------------------------------------------------------------
// Targets. Play's tablet slot takes the same 9:16 frames at 2x, which is why one
// capture pass fills both the 7-inch and 10-inch slots (see play-assets/LISTING.md).
// The App Store's 6.9" slot is the only iPhone size that covers every device.
// ---------------------------------------------------------------------------
const TARGETS = [
  { name: 'play-phone',  w: 1080, h: 1920, out: 'android/play-assets/screenshots/phone' },
  { name: 'play-tablet', w: 2160, h: 3840, out: 'android/play-assets/screenshots/tablet' },
  { name: 'ios-6.9',     w: 1290, h: 2796, out: 'ios/app-store-assets/screenshots/iphone-6.9' },
];

// Capture is driven by how full the well is, not by a piece count. Aspect ratios
// differ enough between the three targets that a count which builds a nice stack
// on 16:9 tops the game out on the iPhone's 19.5:9 -- fill fraction is the same
// on all of them. MAX_PIECES is a safety stop, not the normal exit.
const SHALLOW_FILL = 0.06;
const DEEP_FILL = 0.15;
const MAX_PIECES = 30;

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
               '.data': 'application/octet-stream', '.png': 'image/png' };

function arg(flag, fallback) {
  const i = process.argv.indexOf(flag);
  return i > -1 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}

function findChrome() {
  const explicit = arg('--chrome', process.env.CHROME);
  if (explicit) return explicit;
  const cache = join(homedir(), '.cache/ms-playwright');
  if (!existsSync(cache)) return null;
  // Highest chromium-<rev> wins; Playwright keeps several revisions side by side.
  const dirs = readdirSync(cache)
    .filter(d => /^chromium-\d+$/.test(d))
    .sort((a, b) => +b.split('-')[1] - +a.split('-')[1]);
  for (const d of dirs) {
    for (const exe of ['chrome-linux64/chrome', 'chrome-linux/chrome', 'chrome-mac/Chromium.app/Contents/MacOS/Chromium']) {
      const p = join(cache, d, exe);
      if (existsSync(p)) return p;
    }
  }
  return null;
}

async function serve(dir) {
  const server = createServer(async (req, res) => {
    const rel = decodeURIComponent(req.url.split('?')[0]).replace(/^\/+/, '') || 'openblocks.html';
    try {
      const body = await readFile(join(dir, rel));
      res.writeHead(200, { 'Content-Type': MIME[extname(rel)] || 'application/octet-stream' });
      res.end(body);
    } catch {
      res.writeHead(404).end('not found');
    }
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  return { server, port: server.address().port };
}

async function capture(target, url, chrome) {
  const { w, h, out } = target;
  const browser = await chromium.launch({
    executablePath: chrome,
    // SwiftShader: the runner has no GPU, and raylib needs a real WebGL context.
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
  });
  // hasTouch is the whole trick: Chromium's touch emulation makes
  // matchMedia('(pointer: coarse)') match, which is exactly what src/main.c reads
  // to choose the portrait renderer. No page-script shim is needed.
  const ctx = await browser.newContext({
    viewport: { width: w, height: h }, deviceScaleFactor: 1, hasTouch: true,
  });
  const page = await ctx.newPage();
  const cdp = await ctx.newCDPSession(page);
  await page.goto(url, { waitUntil: 'load' });
  await page.waitForTimeout(6000);   // WASM boot + font/atlas upload

  const wait = ms => page.waitForTimeout(ms);
  const cell = Math.round(w * 0.0754);     // the well is 0.754*width across 10 columns
  const shot = name => page.screenshot({ path: join(out, `${name}.png`) });

  // The input layer samples the pointer once a frame and takes the gesture origin
  // from that sample. A move+press inside a single frame records the origin at the
  // PREVIOUS position, so the delta reads as a drag and the tap never fires --
  // every press below therefore settles first. A press also has to be held for
  // more than a couple of frames; 60ms is silently dropped, 120ms is reliable.
  const settle = async (x, y) => { await page.mouse.move(x, y); await wait(80); };

  async function tap(x, y) {
    await settle(x, y);
    await page.mouse.down(); await wait(120); await page.mouse.up(); await wait(150);
  }

  // Horizontal drag moves one column per cell-width, so walk in cell-sized steps
  // with a frame of dwell each. Release BEFORE dropping: one continuous
  // drag-then-flick exceeds the hard-drop time gate, so nothing would lock.
  async function slide(x, y, cols) {
    await settle(x, y);
    await page.mouse.down(); await wait(40);
    const dir = Math.sign(cols);
    for (let i = 1; i <= Math.abs(cols); i++) {
      await page.mouse.move(x + dir * i * cell, y); await wait(45);
    }
    await page.mouse.up(); await wait(120);
    return x + cols * cell;
  }

  // Hard drop is velocity-based (input.c): dur < 0.35s, dy > cell*1.2 and
  // dy/dur > cell*6. It must start at the piece's CURRENT column -- starting
  // anywhere else drags the piece back across the well first.
  async function flick(x, y) {
    await settle(x, y);
    await page.mouse.down();
    for (let i = 1; i <= 5; i++) { await page.mouse.move(x, y + i * (h / 38)); await wait(22); }
    await page.mouse.up(); await wait(260);
  }

  // Pause is a two-finger tap with no on-screen key, so it needs real multi-touch;
  // Playwright's mouse and touchscreen APIs are both single-pointer.
  async function twoFingerTap() {
    const pts = [{ x: w * 0.37, y: h * 0.52 }, { x: w * 0.63, y: h * 0.52 }];
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: pts });
    await wait(120);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await wait(600);
  }

  // Sample the canvas directly (preserveDrawingBuffer is on in web/shell.html, so
  // the back buffer survives for readback). Two uses: the top strip tells menu
  // from gameplay, and the well tells how full the board is.
  // Fraction of the sampled region brighter than the near-black board. Used both
  // as an in-game test (the title bar lights the top strip; the menu leaves it
  // black) and as the stack-depth signal.
  const sample = (sx, sy, sw, sh) => page.evaluate(([sx, sy, sw, sh]) => {
    const c = document.getElementById('canvas');
    const g = document.createElement('canvas');
    g.width = 200; g.height = 200;
    const x = g.getContext('2d');
    x.drawImage(c, sx, sy, sw, sh, 0, 0, 200, 200);
    const d = x.getImageData(0, 0, 200, 200).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 150) n++;
    return n / (200 * 200);
  }, [sx, sy, sw, sh]);

  async function assertInGame(where) {
    if (await sample(0, 0, w, Math.max(4, Math.round(h * 0.02))) === 0) {
      throw new Error(`${target.name}: not in-game at "${where}" (topped out or bounced to the menu)`);
    }
  }

  // The well, in canvas pixels: 0.754*w wide and centred, running from just under
  // the HUD band to the bottom margin.
  const wellFill = () => sample(w * 0.123, h * 0.133, w * 0.754, h * 0.845);

  await mkdir(out, { recursive: true });

  // 1. Title menu, untouched. Sound reads "Off" because it genuinely is off by
  //    default on every platform (src/sound.c:16) -- not a capture artifact.
  await shot('01-menu');

  await tap(w / 2, h * 0.5);          // New Game
  await wait(1500);
  await assertInGame('after New Game');

  // Walk the placement column left to right one step at a time. An even spread
  // (every other column) drops pieces into isolated towers with gaps between
  // them, which photographs as a mess; consecutive columns land each piece
  // against the last, so the floor fills, rows clear, and LINES/SCORE reach
  // values worth showing.
  const SWEEP = [-4, -3, -2, -1, 0, 1, 2, 3, 4];
  let seed = 7;                        // fixed: a rerun reproduces the same board
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
  const clamp = v => Math.max(w * 0.18, Math.min(w * 0.82, v));

  let shallowDone = false, deepDone = false;
  for (let i = 0; i < MAX_PIECES && !deepDone; i++) {
    const y = h * 0.5;
    const cols = SWEEP[i % SWEEP.length] + (rnd() > 0.8 ? 1 : 0);
    if (rnd() > 0.8) await tap(w / 2, h * 0.4);      // occasional rotate
    const x = cols ? clamp(await slide(w / 2, y, cols)) : w / 2;
    await flick(x, Math.min(y, h * 0.55));

    await assertInGame(`piece ${i}`);
    const fill = await wellFill();
    if (process.env.OB_DEBUG) console.log(`  ${target.name} piece ${i}: fill ${fill.toFixed(3)}`);
    if (!shallowDone && fill >= SHALLOW_FILL) { await shot('02-gameplay'); shallowDone = true; }
    if (fill >= DEEP_FILL) { await shot('03-gameplay-stack'); deepDone = true; }
  }
  if (!deepDone) throw new Error(`${target.name}: well never reached ${DEEP_FILL} fill in ${MAX_PIECES} pieces`);

  await twoFingerTap();
  await shot('04-pause');

  await browser.close();
  console.log(`${target.name}: 4 frames -> ${out}`);
}

const src = arg('--src');
if (!src) { console.error('--src <unzipped web bundle dir> is required'); process.exit(2); }
const root = resolve(arg('--out', process.cwd()));
const chrome = findChrome();
if (!chrome) { console.error('no Chromium found; pass --chrome or set $CHROME'); process.exit(2); }

const { server, port } = await serve(resolve(src));
const url = `http://127.0.0.1:${port}/openblocks.html`;
const only = arg('--only');

// Play is emergent, so a run can occasionally top out before the well is deep
// enough -- a stack that grows straight up blocks the spawn at a low fill. The
// board is worth rebuilding rather than failing the batch, and the seed is fixed,
// so a retry differs only in frame timing.
for (const t of TARGETS) {
  if (only && t.name !== only) continue;
  const target = { ...t, out: join(root, t.out) };
  for (let attempt = 1; ; attempt++) {
    try { await capture(target, url, chrome); break; }
    catch (e) {
      if (attempt === 3) throw e;
      console.log(`${t.name}: attempt ${attempt} failed (${e.message}); retrying`);
    }
  }
}
server.close();
