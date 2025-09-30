#ifndef VITARPS5_UI_COMPONENTS_H
#define VITARPS5_UI_COMPONENTS_H

#include <vita2d.h>

#include "vita2d_ui.h"

// Component initialization
void ui_components_init(void);

// Modern UI element drawing
void ui_components_draw_toggle_switch(int x, int y, bool is_on,
                                      float animation_progress, bool enabled);
void ui_components_draw_dropdown_menu(int x, int y, int width,
                                      const char* value, bool is_open,
                                      bool enabled);
void ui_components_draw_dropdown_list(int x, int y, int width,
                                      const char* value, bool enabled);
void ui_components_draw_slider(int x, int y, int width, float value,
                               bool is_dragging, bool enabled);

// Helper drawing functions
void ui_components_draw_circle(int cx, int cy, int radius, uint32_t color);
void ui_components_draw_dropdown_arrow(int x, int y, uint32_t color);

// UI element state management
UIElementStates* ui_components_get_states(void);
void ui_components_update_animations(float delta_time);

// Console card rendering
void ui_components_render_console_card(const ConsoleInfo* console, int x, int y,
                                       bool is_authenticated);

// Button interactions
bool ui_components_check_wake_button_hit(const ConsoleInfo* console, int card_x,
                                         int card_y, float touch_x,
                                         float touch_y);
bool ui_components_check_register_button_hit(const ConsoleInfo* console,
                                             int card_x, int card_y,
                                             float touch_x, float touch_y);

#endif  // VITARPS5_UI_COMPONENTS_H