# iOS backend spike — the complete raylib surface

Goal: ship openblocks on iOS **without raylib** (raylib has no official iOS
support). This inventories *every* raylib API the game uses, maps each to its
iOS-native replacement, and sizes the work. `game.c` (pure logic) is untouched;
only `render.c` / `input.c` / `sound.c` / the app loop get a second backend.

## Scope reduction (why this is smaller than it looks)

iOS is a **portrait + touch** target (`OB_PORTRAIT`, like Android). That deletes
two whole subsystems from the iOS surface:

- **No letterbox canvas.** `RenderTexture2D`, `LoadRenderTexture`,
  `BeginTextureMode`/`EndTextureMode`, `DrawTexturePro`, `calculate_scale`,
  `present()` are all `#ifdef OB_LANDSCAPE` (desktop/web-landscape only). Portrait
  draws straight to the screen — iOS needs none of them.
- **No keyboard, no mouse.** `IsKeyDown/IsKeyPressed/GetKeyPressed/SetExitKey` +
  `KEY_*` are `#if !PLATFORM_ANDROID` (desktop/web); `GetMousePosition` /
  `IsMouseButton*` / `MOUSE_*` are web-only. iOS input = touch + gestures only.

## Full raylib surface, by subsystem

### A. App / window / lifecycle → UIKit + CADisplayLink
| raylib | iOS replacement | Effort |
|--------|-----------------|--------|
| `InitWindow`, `CloseWindow` | `UIApplication` + `UIView`/`CAMetalLayer` setup | S |
| `WindowShouldClose` | never true on iOS (OS controls lifetime) | trivial |
| `GetScreenWidth`, `GetScreenHeight` | view `bounds.size` × scale | S |
| `GetMonitorWidth/Height`, `GetCurrentMonitor` | not used in portrait path | none |
| `SetConfigFlags`, `SetWindowSize`, `ToggleFullscreen`, `IsWindowFullscreen` | no-ops on iOS | trivial |
| `IsWindowFocused` (auto-pause) | `UIApplication` active/resign notifications | S |
| `SetTargetFPS`, `GetTime` | `CADisplayLink` (drives the loop) + `CACurrentMediaTime()` | S |
| loop (`BeginDrawing`/`EndDrawing` bracket) | `CADisplayLink` callback → `ios_update()` | S |

### B. Input → UIKit touches + gesture recognizers
| raylib | iOS replacement | Effort |
|--------|-----------------|--------|
| `GetTouchPointCount`, `GetTouchPosition` | `touchesBegan/Moved/Ended` → active-point list | S |
| `GetGestureDetected` + `GESTURE_TAP/DOUBLETAP/SWIPE_UP/SWIPE_DOWN` | `UITapGestureRecognizer` + `UISwipeGestureRecognizer` | S |
| `CheckCollisionPointRec` | trivial point-in-rect helper (pure C, drop raylib dep) | trivial |
| `KEY_BACK` (Android back) | not applicable on iOS | none |

### C. Drawing → Metal (2D, ~10 primitives)
| raylib | iOS replacement | Effort |
|--------|-----------------|--------|
| `ClearBackground` | render-pass clear color | S |
| `DrawRectangle`, `DrawRectangleLines` | colored quad / 4 thin quads | S |
| `DrawRectangleRounded`, `DrawRectangleRoundedLinesEx` | rounded-quad (SDF or triangulated) | M |
| `DrawLine` | thin quad | S |
| `DrawPoly`, `DrawRing`, `DrawCircle` | triangle-fan (used only for button icons) | M |
| `DrawText`, `MeasureText` | **bitmap font atlas** + textured quads | M |
| `TextFormat` | it's just `snprintf` — pure C, no Metal | trivial |
| `Color`, `Vector2`, `Rectangle` | our own tiny structs (or reuse) | trivial |

All draws are 2D colored/textured quads → **one pipeline, one shader**
(position + color + optional UV), a vertex buffer, and a font-atlas texture.

### D. Audio → CoreAudio / AVAudioEngine
| raylib | iOS replacement | Effort |
|--------|-----------------|--------|
| `InitAudioDevice`, `CloseAudioDevice`, `IsAudioDeviceReady` | `AVAudioEngine` start/stop | S |
| `LoadSoundFromWave` (int16 mono PCM @ SAMPLE_RATE) | wrap each PCM buffer in an `AVAudioPCMBuffer` | S |
| `PlaySound`, `UnloadSound` | `AVAudioPlayerNode.scheduleBuffer` / release | S |
| `Wave`, `Sound` types | our own PCM-buffer handle | trivial |

`sound.c` already synthesizes ~5 short int16 mono PCM effects in memory — iOS
just needs to play a precomputed PCM buffer on trigger.

### E. Content: the font
`DrawText`/`MeasureText` use raylib's embedded pixel font. The iOS backend needs
an equivalent **bitmap font atlas** (PNG + glyph metrics) to keep the exact look.
Generate once from the same font; bundle in the app. Effort: S.

## Proposed structure

Split the raylib-coupled files behind thin interfaces, each with a **raylib**
impl (desktop/web/android, today's code) and an **iOS** impl:

```
src/gfx.h     gfx_clear, gfx_rect, gfx_rect_lines, gfx_rounded, gfx_line,
              gfx_poly, gfx_ring, gfx_circle, gfx_text, gfx_measure
src/gfx_raylib.c   (wraps DrawRectangle/DrawText/... — trivial)
src/gfx_metal.m    (Metal: one quad pipeline + font atlas)

src/plat.h    plat_screen_size, plat_time, plat_focused, plat_should_close
src/plat_raylib.c / plat_ios.m

input: extend poll_touch source to an iOS touch/gesture provider
audio: sound.c calls audio_load(pcm)/audio_play(id) → raylib vs CoreAudio impl
```

`main.c` already has `frame_step()` factored out for Emscripten → maps directly
to the iOS callback loop (`ios_ready`/`ios_update`/`ios_destroy`).

## Effort summary

| Piece | Size | Risk |
|-------|------|------|
| App shell / loop (CADisplayLink, view) | S | low |
| Touch + gesture input | S | low |
| Metal 2D renderer (~10 primitives, 1 shader) | **M** | med (biggest piece) |
| Font atlas + text | M | low |
| CoreAudio PCM playback | S | low |
| Build/sign/package (xcodebuild, .ipa, CI) | M | med (Apple toolchain) |

No single hard part; the Metal renderer + text is the bulk, and it's a small,
well-understood 2D pipeline.

## Spike order (de-risk cheaply, each a checkpoint)

1. **Build shell:** empty Metal `UIView` app via `xcodebuild` on a macOS CI
   runner → clears the screen a color on the Simulator. Proves the toolchain.
2. **Renderer:** implement `gfx_*` in Metal; draw the menu (rects + text) — no
   game logic yet. Proves the 2D pipeline + font atlas.
3. **Wire the core:** link `game.c` + the portrait `render.c` calls through
   `gfx.h`; drive `frame_step` from `CADisplayLink`. Playfield renders.
4. **Input:** touch + gestures → the existing `Input` struct. Playable.
5. **Audio:** CoreAudio PCM. Feature-complete.
6. **Package + sign:** `.ipa`, provisioning, TestFlight — secret-gated CI step
   mirroring the Play AAB.

Checkpoints 1–2 are the real spike: if the Metal shell + `gfx_*` land cleanly on
CI, the rest is mechanical.
