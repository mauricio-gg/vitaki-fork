#include "vita2d_ui.h"

#include "ui_core.h"

// Simple wrapper functions that delegate to the modular UI system

int ui_init(void) { return ui_core_init(); }

void ui_cleanup(void) { ui_core_cleanup(); }

void ui_update(SceCtrlData* pad) { ui_core_update(pad); }

void ui_render(void) { ui_core_render(); }

bool ui_is_ime_active(void) { return ui_core_is_ime_active(); }

// Keep touch utility function for compatibility
bool is_point_in_rect(float x, float y, float rx, float ry, float rw,
                      float rh) {
  return (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh);
}