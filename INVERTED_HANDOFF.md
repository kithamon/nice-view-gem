# Handoff: `CONFIG_NICE_VIEW_WIDGET_INVERTED` black-block bug

## Goal
Make the nice-view-gem displays **invert cleanly** (black background, white
content) when `CONFIG_NICE_VIEW_WIDGET_INVERTED=y`. Today that build shows
large solid-black blocks instead of a clean dark theme.

## Where things live
- Repo: `/home/kit/Projects/keyboards/nice-view-gem`
- Shield: `boards/shields/nice_view_gem/`
- LVGL/ZMK target: **LVGL v9 / ZMK main** (per README; uses Zephyr 4.1+).
- Photos of the bug: `photos/IMG_1059.JPG`, `photos/IMG_1060.JPG`.

## How inversion is currently implemented (TWO separate paths)
1. **Macro path** — `widgets/util.h`:
   ```c
   #define LVGL_BACKGROUND  IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_black() : lv_color_white()
   #define LVGL_FOREGROUND  IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_white() : lv_color_black()
   ```
   Used for: parent object bg (`screen.c`/`screen_peripheral.c` `zmk_widget_screen_init`),
   `fill_background()` / `rotate_canvas()` (`util.c`), and every rect/line/label
   color in `widgets/*.c` (`battery.c`, `output.c`, `wpm.c`, `profile.c`, `layer.c`).
2. **Image-palette path** — `assets/images.c` and `assets/crystal.c`:
   every image is `LV_COLOR_FORMAT_I1` with a 2-entry palette that is swapped
   under `#if CONFIG_NICE_VIEW_WIDGET_INVERTED` (index0 = background = matches
   screen; index1 = foreground). Assets: `bolt, bt, bt_no_signal, bt_unbonded,
   usb, gauge, grid, profiles, crystal_01..16`.
   - NOTE: `bt_unbonded` has its palette swap **reversed** vs all the others
     (separate latent bug for the unbonded state; not the main issue).

On paper both paths flip under the same Kconfig symbol, so inversion *should*
be symmetric.

## What the screens are
- **L = central** (`screen.c`): 3 stacked 68×68 canvases, drawn then rotated 270°:
  top = battery/output (SIG/BAT/BT badge), middle = WPM gauge+grid+graph,
  bottom = profile dots + layer name (BASE).
- **R = peripheral** (`screen_peripheral.c`): **only** the top status canvas is
  created + the floating **crystal** gem drawn as an `lv_img`/`lv_animimg` child
  of the parent (`draw_animation`, `widgets/animation.c`). The middle/bottom of
  the right screen is therefore just the **parent object background**.

## What the photos show (KEY EVIDENCE)
- **IMG_1059** = a **clean** fully-dark render (black bg, white content), both
  halves correct. This is the *target* look.
- **IMG_1060** = the **broken** build matching the report:
  - Overall the screens render in **NORMAL polarity** — white/light background,
    **black** text and graphics (SIG, BAT 100%, gauge, grid, gem = black diamond
    on white, BASE = black on white).
  - EXCEPT for **solid-black rectangular blocks in the lower region** of each
    screen: left = a black band around/below the `WPM 0` / profile area; right =
    a black block in the lower portion **under the gem**.

## Diagnosis so far (not fully nailed — interrupted here)
- A faithful Python re-render of the *widget logic + real I1 assets* (script at
  `/tmp/nvg_render.py`, outputs `/tmp/nvg_{L,R}_{normal,inverted}.png`) produces
  **clean** inversion in both modes. So the blocks are **not** explained by the
  palette bytes or the macros in isolation.
- The decisive clue is the **right/peripheral** half: its lower region is **only
  the parent background**. In IMG_1060 that region is **black** while the gem and
  status text render **non-inverted** (black-on-white). i.e. the **background/fill
  path appears to invert (→black) while the content/image path renders
  non-inverted (white bg, black ink)** — the two inversion paths are **out of
  sync**, and the "blocks" are the inverted background showing through wherever
  non-inverted opaque content doesn't paint over it.
- This points at an **inconsistency between the two inversion paths** and/or an
  **LVGL v9 `LV_COLOR_FORMAT_I1` rendering quirk** (v9 has documented I1 issues;
  indexed formats require `LV_DRAW_SW_SUPPORT_ARGB8888=1`, palettes are ARGB8888).

## Recommended next steps (in order)
1. **Confirm the polarity map with pixels, don't eyeball.** Sample background
   luminance in top/middle/bottom of each screen in IMG_1060 to state factually
   which draw operations inverted and which didn't. (Crops kept landing on the
   case bezel; screens sit roughly L≈x[150,360], R≈x[430,660] in the 1063×1417
   image — re-derive with a grid.)
2. **Verify how the flag reaches each path.** Read `Kconfig.defconfig:57`
   (`config NICE_VIEW_WIDGET_INVERTED`) and confirm the user's `.conf` actually
   sets `=y`. Confirm `IS_ENABLED(...)` (widget .c) and bare `#if CONFIG_...`
   (assets) both see the same value at compile time (they should for a Kconfig
   bool; verify there isn't a header-include / build-ordering reason the assets
   compile without autoconf).
3. **Strongly consider abandoning the dual-path swap** in favor of ONE of:
   - (a) Render everything in a **single fixed polarity** and invert the whole
     panel at the **display-controller / LVGL display** level (the Sharp `ls0xx`
     mono display + LVGL support a global inversion). This removes the fragile
     per-asset palette swap entirely and is the most robust "invert cleanly" fix.
   - (b) Make I1 image **index-0 transparent** (`alpha 0x00`) so images never
     paint a background box; then the canvas/parent `LVGL_BACKGROUND` is the sole
     background everywhere and content is `LVGL_FOREGROUND`/index-1 only. This
     eliminates the "image-box vs background polarity mismatch" class of bug.
   Evaluate (a) vs (b); (a) is usually less churn and fully deterministic.
4. **Do not modify code until the plan is approved.** The repo owner wants to
   review the plan and give an explicit "go" before edits.

## Useful facts / gotchas
- Canvas format is `LV_COLOR_FORMAT_L8`; images are `I1`. Canvases are drawn in a
  68×68 buffer then rotated 270° (`util.c rotate_canvas`) and tiled at x-offsets
  0 / -44 / -112 (`util.h BUFFER_OFFSET_*`), parent size 160×68.
- Crystal descriptors declare `.header.w = 69` (canvas is 68) — harmless 1px, not
  the bug.
- `git` works here; repo is clean on `main` (origin `kithamon/nice-view-gem`,
  branch `origin/inverted-color` is identical to `main`). One early scratch edit
  to `wpm.c` was reverted via `git checkout` — tree is clean.
- This session had intermittent garbled tool output early on; if it recurs,
  start a fresh session.
