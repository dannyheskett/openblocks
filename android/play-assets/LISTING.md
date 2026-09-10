# openblocks — Google Play store listing

Copy/paste these into the Play Console (**Grow → Store presence → Main store
listing**, plus **Store settings** for category). The images in this folder are
the shipped assets.

The screenshots regenerate with `scripts/gen_store_screenshots.mjs`, which is
committed because the portrait UI changes and stale screenshots are a listing
accuracy problem. The icon and feature graphic were generated once and are now
fixed artifacts; recover their generator from history if needed
(`git log -- scripts/gen_play_assets.py`).

## Assets (this folder)

| File | Play field | Spec |
|------|-----------|------|
| `icon-512.png` | App icon | 512×512 PNG (32-bit) |
| `feature-graphic-1024x500.png` | Feature graphic | 1024×500 PNG/JPG |
| `screenshots/phone/` | Phone screenshots | 4× 1080×1920 PNG (9:16, promo-eligible) |
| `screenshots/tablet/` | 7-inch and 10-inch tablet screenshots | 4× 2160×3840 PNG (9:16, same files fit both slots) |

## App name (≤30 chars)

```
openblocks
```

## Short description (≤80 chars)

```
Classic falling-block puzzle. Free, open source, no ads, no tracking.
```

## Full description (≤4000 chars)

```
Stack falling blocks, clear lines, and chase higher levels in a clean, classic block puzzle — with none of the junk that clutters this genre.

No ads. No tracking. No accounts. No in-app purchases. openblocks requests zero permissions and never touches the network. It's just the game.

PURE CLASSIC GAMEPLAY
• Clear lines to score and level up, with gravity that ramps up as you go
• Plan ahead with the next-piece preview
• Soft drop for control, hard drop for speed
• Wall kicks for smooth, forgiving rotation
• Pause anytime and pick up where you left off

CONTROLS THAT GET OUT OF THE WAY
• Drag to slide a piece, tap to rotate
• Flick down to drop it instantly, or drag down slowly to guide it
• Prefer buttons? Turn on the on-screen control pad in the menu
• Crisp, minimal visuals that stay out of your way

BUILT RIGHT
• Fully offline — perfect for flights, commutes, anywhere
• Tiny download, easy on your battery

FREE AND OPEN SOURCE
openblocks is open source. Read the code, report a bug, or build it yourself: https://github.com/dannyheskett/openblocks

No dark patterns, no "energy" timers, no paywalled pieces. Just the timeless falling-block puzzle, done properly.
```

## Categorization (Store settings)

- **App or game:** Game
- **Category:** Puzzle
- **Tags:** puzzle, arcade, retro, blocks
- **Email:** dan@danheskett.com
- **Website:** https://danheskett.com
- **Content rating:** Everyone (no objectionable content; IARC questionnaire —
  answer "no" to all violence/adult/gambling items)

## Data safety (Policy → App content)

- Data collected: **None**
- Data shared: **None**
- App has no `INTERNET` permission (verify in the manifest) → "no data
  transmitted off the device" is truthful.
- Privacy policy URL: **https://danheskett.com/app/privacy-policy/** (live)

## Screenshots

Live on the listing (pushed via the Play API): `screenshots/phone/` (4x
1080x1920) in the phone slot, `screenshots/tablet/` (4x 2160x3840) in both the
7-inch and 10-inch tablet slots.

Captured from a released web build's portrait renderer -- the same
src/render_portrait.c that Android runs -- in a headless browser:

    npm i playwright-core
    gh release download release-N -p '*-web-wasm.zip'
    node scripts/gen_store_screenshots.mjs --src <unzipped-dir>

That one command also refreshes the App Store set under
`ios/app-store-assets/screenshots/`, so the two listings cannot drift apart.
Regenerate whenever the portrait layout, HUD or font changes.
