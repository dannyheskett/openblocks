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

Same recipe as the Play assets: capture from a CI web build in headless
Playwright with portrait forced via a `matchMedia('pointer: coarse')` shim, then
resize to the exact target dimensions. See the notes in
`android/play-assets/LISTING.md`.

Two traps that cost time before:

- The `matchMedia` shim must include no-op `addListener`/`addEventListener`
  members. A bare `{matches: true}` object crashes emscripten and the WebGL
  context is lost.
- Playwright clicks with the default 0ms delay are missed by the frame-sampled
  touch input. Use `click({delay: 60})`.

## Trademark

The word "Tetris" appears nowhere in any asset, filename, screenshot, or listing
text. It is a trademark of the Tetris Company and its use is aggressively
enforced. Describe the game as a "falling-block puzzle".
