# Inverted-theme fix: how `CONFIG_NICE_VIEW_WIDGET_INVERTED` works now

This documents the rework that makes `CONFIG_NICE_VIEW_WIDGET_INVERTED=y`
produce a clean **light background / dark content** theme, instead of the
default **dark background / light content**.

It applies to the **LVGL v8 / ZMK v0.3.0** line (the version this branch is
based on). The earlier per-element approach is gone.

## TL;DR

We no longer try to invert colors element by element. Instead:

1. **Everything is drawn in the default (known-good) polarity.** The
   `LVGL_BACKGROUND` / `LVGL_FOREGROUND` macros and all image palettes are
   pinned to their default values regardless of the inverted flag.
2. **The whole framebuffer is inverted once, at flush time**, when
   `CONFIG_NICE_VIEW_WIDGET_INVERTED=y`.

The inverted screen is therefore a literal full-screen *negative* of the
default render, which is already known to be correct. Nothing can be
selectively wrong.

## Why the old approach produced black boxes

The original design flipped two things on `CONFIG_NICE_VIEW_WIDGET_INVERTED`:

- the `lv_color_t` macros used for rects/text/lines/canvas fills, and
- a 2-entry palette swap inside every `LV_IMG_CF_INDEXED_1BIT` asset.

Two problems made that fall apart in inverted mode:

1. **LVGL v8 ignores the palette for `INDEXED_1BIT` images drawn onto a
   depth-1 (`LV_IMG_CF_TRUE_COLOR`, 1-bpp) canvas.** The pixel shade is decided
   by the raw index bit, not the palette color. So the per-asset palette swap
   was effectively a no-op — the images kept rendering in default polarity
   while the macro-drawn elements flipped. The mismatch showed up as solid
   blocks wherever an opaque image sat (the `grid`, the `crystal`).
2. **The screen's parent object background was never set.** In the default
   theme its default (white→dark) background happens to match the canvases, so
   you never notice. In inverted mode the canvases flipped to light but the
   parent background stayed dark, so every region *not* covered by a canvas —
   the gap between canvases on the central screen, and the whole lower area on
   the peripheral — showed through as a dark block.

Editing palette bytes (swaps, transparency, bit-inversion) can never fully fix
this, because (1) the palette is ignored and (2) the parent background isn't an
image at all.

## The fix

Invert at the one place that sees the fully-composed image: the display flush.
With `CONFIG_LV_Z_BITS_PER_PIXEL=1` the flush buffer is packed 1-bit-per-pixel,
so a byte-wise `XOR 0xFF` over the flushed area is an exact negative of every
pixel — canvases, parent background, the crystal animation, and text alike.

### Files changed

- **`boards/shields/nice_view_gem/widgets/util.h`**
  `LVGL_BACKGROUND` / `LVGL_FOREGROUND` are pinned to the default polarity
  (`lv_color_white()` / `lv_color_black()`); they no longer branch on the
  inverted flag.

- **`boards/shields/nice_view_gem/assets/images.c`**,
  **`boards/shields/nice_view_gem/assets/crystal.c`**
  The `#if CONFIG_NICE_VIEW_WIDGET_INVERTED` palette swaps were removed; every
  image keeps its single default palette and original bitmap. (The swaps were
  no-ops on this renderer anyway.)

- **`boards/shields/nice_view_gem/custom_status_screen.c`**
  Adds, behind `#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED)`, a flush-cb
  wrapper that inverts the framebuffer:

  ```c
  static void (*orig_flush_cb)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *);

  static void invert_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
      uint32_t w = area->x2 - area->x1 + 1;
      uint32_t h = area->y2 - area->y1 + 1;
      uint8_t *buf = (uint8_t *)px;
      for (uint32_t i = 0; i < (w * h) / 8; i++) {
          buf[i] ^= 0xFF;
      }
      orig_flush_cb(drv, area, px);
  }
  ```

  `install_invert_flush()` saves the original `flush_cb` from
  `lv_disp_get_default()->driver` and swaps in the wrapper; it is called once
  from `zmk_display_status_screen()`.

### How it's gated

- `CONFIG_NICE_VIEW_WIDGET_INVERTED=n` (default): the flush hook is compiled
  out, macros/assets are default → the normal dark theme, untouched.
- `CONFIG_NICE_VIEW_WIDGET_INVERTED=y`: same default render, plus the
  framebuffer negative → light background, dark content.

Set it in your `*.conf`:

```ini
CONFIG_NICE_VIEW_WIDGET_INVERTED=y
```

## Assumptions / maintenance notes

- Relies on the flush buffer being **packed 1-bpp with byte-aligned rows**,
  which holds for `LV_Z_BITS_PER_PIXEL=1` on the Sharp `ls0xx` mono path. If
  that ever changes, the `(w * h) / 8` byte count in `invert_flush_cb` is what
  to revisit. A wrong assumption here degrades to a few mis-inverted edge
  pixels, not boxes, and never affects the non-inverted build.
- Because inversion is global, **new widgets/images need no inverted-specific
  handling** — draw them in the normal (default) polarity and they invert for
  free.
- This targets LVGL v8. On a future LVGL v9 / newer-ZMK rebase, the flush-cb
  signature and 1-bpp buffer handling should be re-checked, but the
  "draw-default + invert-at-flush" strategy carries over.
