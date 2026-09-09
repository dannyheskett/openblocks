# App Store assets

Artwork and listing copy for the iOS App Store, mirroring `android/play-assets/`
for Google Play. Nothing here is compiled into the app — the in-bundle app icon
lives in the asset catalog, not in this folder.

## Required

| Asset | Size | Notes |
| --- | --- | --- |
| `icon-1024.png` | 1024x1024 | **No alpha channel, no transparency, no rounded corners.** Apple masks the corners itself; a submitted icon with an alpha channel is rejected outright. sRGB, flattened. |
| `screenshots/iphone-6.9/` | 1290x2796 | Required. 1-10 images, portrait. Covers every current iPhone; Apple scales this set down for older devices, so no other iPhone size is needed. |

Openblocks ships **iPhone only** (`UIDeviceFamily = [1]` in `ios/Info.plist`), so
no iPad screenshots are needed. iPads can still install and run it scaled. If
iPad is ever declared, add `2` to `UIDeviceFamily`, set `UIRequiresFullScreen`
to opt out of Split View, and add a `screenshots/ipad-13/` set at 2064x2752 —
Apple requires that set whenever iPad is supported.

Screenshots must be PNG or JPEG, sRGB, with no alpha channel. They are ordered
in the listing by filename, hence the `01-`/`02-` prefixes.

## Generating screenshots

`scripts/gen_store_screenshots.mjs` captures this set and the two Play sets in
one run, straight from a released web build at the exact target size:

    npm i playwright-core
    gh release download release-N -p '*-web-wasm.zip'
    node scripts/gen_store_screenshots.mjs --src <unzipped-dir>

No resize step and no `matchMedia` shim: the browser context is created with
`hasTouch`, which makes `matchMedia('(pointer: coarse)')` match natively, and
that is exactly what `src/main.c` reads to select the portrait renderer.

Traps the script already handles, worth knowing if it is ever rewritten:

- A pointer move and press inside one frame records the gesture origin at the
  PREVIOUS position, so the delta reads as a drag and the tap never fires.
  Settle after moving, before pressing.
- A press has to be held past a couple of frames. 60ms is silently dropped;
  120ms is reliable.
- Pause is a two-finger tap, which needs CDP `Input.dispatchTouchEvent`;
  Playwright's mouse and touchscreen APIs are both single-pointer.

## Trademark

The word "Tetris" appears nowhere in any asset, filename, screenshot, or listing
text. It is a trademark of the Tetris Company and its use is aggressively
enforced. Describe the game as a "falling-block puzzle".
