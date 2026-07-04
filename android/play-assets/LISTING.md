# openblocks — Google Play store listing

Copy/paste these into the Play Console (**Grow → Store presence → Main store
listing**, plus **Store settings** for category). Images regenerate with
`python3 scripts/gen_play_assets.py`.

## Assets (this folder)

| File | Play field | Spec |
|------|-----------|------|
| `icon-512.png` | App icon | 512×512 PNG (32-bit) |
| `feature-graphic-1024x500.png` | Feature graphic | 1024×500 PNG/JPG |
| _screenshots/_ (TODO) | Phone screenshots | 2–8, min 320px, ≤2:1 aspect |

## App name (≤30 chars)

```
openblocks
```

## Short description (≤80 chars)

```
A clean, classic falling-block puzzle. Free, no ads, no tracking.
```

## Full description (≤4000 chars)

```
openblocks is a falling-block puzzle done the classic way. Rotate and drop the
pieces, complete horizontal lines to clear them, and keep the stack from
reaching the top. The rules, scoring, levels, and gravity all follow the
original that made the genre famous — if you've played that, this will feel
instantly familiar.

Built for one-handed play:
• Large on-screen buttons — move left/right, rotate, and drop
• Tap the playfield to rotate
• Tap Drop for a hard drop, hold it for a soft drop
• The layout adapts to any phone or tablet screen

Honest and lightweight:
• Completely free — no ads, ever
• No accounts, no sign-in, no internet permission
• Collects no data and shares nothing
• Tiny download and works fully offline
• Written in C — fast and light on battery

openblocks is a personal project: a from-scratch reimplementation built to get
the feel of a classic exactly right. It's open source — you can read the code,
build it yourself, or play it in your browser at https://danheskett.com.

Just the game. Drop some blocks.
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
- Privacy policy URL: **TODO** — host the one-pager (see `PRIVACY.md`, pending)
  at e.g. https://danheskett.com/openblocks-privacy

## Screenshots — TODO

Capture 2–8 portrait shots (menu, gameplay, next-piece, pause/game-over) at a
phone resolution. The web build's portrait renderer is pixel-identical to the
Android layout, so these can be captured from it or from a Device Farm run.
```
