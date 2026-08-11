# openblocks — App Store listing

Copy/paste into App Store Connect. Mirrors `android/play-assets/LISTING.md`, with
the differences Apple requires (subtitle, keywords, promotional text).

Two rules that differ from Play:

- **Never mention Android, Google Play, or another platform** in the description.
  Apple rejects listings that reference competing stores.
- **The word "Tetris" appears nowhere.** Trademark of the Tetris Company,
  aggressively enforced. It is a "falling-block puzzle".

## New App form (My Apps -> + -> New App)

| Field | Value |
| --- | --- |
| Platform | iOS |
| Name | `openblocks` (must be unique App Store-wide; see fallbacks below) |
| Primary Language | English (U.S.) |
| Bundle ID | `com.danheskett.openblocks` |
| SKU | `openblocks` |
| User Access | Full Access |

If `openblocks` is taken, in order of preference: `Openblocks Puzzle`,
`Openblocks Blocks`, `Openblocks Game`. The name is public, capped at 30
characters, and can be changed with any later version — the SKU and bundle ID
cannot.

## Subtitle (<=30 chars)

```
Classic block puzzle
```

## Promotional text (<=170 chars)

Editable anytime without submitting a new build — use it for release notes or
seasonal copy.

```
No ads, no tracking, no accounts. Just the classic block puzzle, free and open source.
```

## Keywords (<=100 chars, comma-separated, no spaces after commas)

Do not repeat the app name — it is already indexed.

```
puzzle,blocks,falling blocks,retro,arcade,classic,offline,brick,stacking,tetromino
```

`tetromino` is a generic geometry term (a polyomino of four cells) and is not
itself a trademark, but drop it if you want maximum distance from the Tetris
Company.

## Description (<=4000 chars)

```
Stack falling blocks, clear lines, and chase higher levels in a clean, classic block puzzle with none of the junk that clutters this genre.

No ads. No tracking. No accounts. No in-app purchases. openblocks never touches the network. It's just the game. You can even play it in airplane-mode.

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

FREE AND OPEN SOURCE
openblocks is open source. Read the code, report a bug, or build it yourself: https://github.com/dannyheskett/openblocks

No dark patterns, no "energy" timers, no paywalled pieces. Just the timeless falling-block puzzle, done properly.
```

## App information

- **Category (primary):** Games -> Puzzle
- **Category (secondary):** Games -> Arcade
- **Content Rights:** does not contain third-party content
- **Age Rating:** answer "None" to every question -> **4+**
- **Copyright:** `2026 Daniel Heskett`
- **Support URL:** https://danheskett.com
- **Marketing URL:** https://danheskett.com/projects/openblocks/
- **Privacy Policy URL:** https://danheskett.com/app/privacy-policy/

## App Privacy (App Store Connect -> App Privacy)

Answer **"No, we do not collect data from this app."** — accurate and verified:
no network code, no analytics SDK, no permissions requested. This yields a
"Data Not Collected" privacy label.

## Pricing

Free. No in-app purchases. Requires the **Free Applications agreement** to be
Active under Business / Agreements, Tax, and Banking.

## Export compliance

Openblocks uses no encryption of any kind. Once `ITSAppUsesNonExemptEncryption
= false` is added to `ios/Info.plist` (pending, part of the signing work), App
Store Connect stops asking the encryption question on every upload. Until then,
answer "No" to the encryption prompt on each build.
