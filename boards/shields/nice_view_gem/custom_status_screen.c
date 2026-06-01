#include "widgets/screen.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include "assets/pixel_operator_mono.c"
#include "assets/custom_fonts.h"

#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_STATUS)
static struct zmk_widget_screen screen_widget;
#endif

#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED)
// Everything is drawn in the default polarity; invert the whole framebuffer
// once here. With LV_Z_BITS_PER_PIXEL=1 the flush buffer is packed 1bpp, so a
// byte-wise XOR over the flushed area is an exact full-screen negative.
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

static void install_invert_flush(void) {
    lv_disp_t *disp = lv_disp_get_default();
    if (disp == NULL || disp->driver == NULL) {
        return;
    }
    if (disp->driver->flush_cb == invert_flush_cb) {
        return;
    }
    orig_flush_cb = disp->driver->flush_cb;
    disp->driver->flush_cb = invert_flush_cb;
}
#endif

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen;
    screen = lv_obj_create(NULL);

#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_STATUS)
    zmk_widget_screen_init(&screen_widget, screen);
    lv_obj_align(zmk_widget_screen_obj(&screen_widget), LV_ALIGN_TOP_LEFT, 0, 0);
#endif

#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED)
    install_invert_flush();
#endif

    return screen;
}
