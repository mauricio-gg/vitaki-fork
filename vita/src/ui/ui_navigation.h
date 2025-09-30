#ifndef VITARPS5_UI_NAVIGATION_H
#define VITARPS5_UI_NAVIGATION_H

#include <psp2/ctrl.h>
#include <vita2d.h>

#include "vita2d_ui.h"

// Navigation initialization and cleanup
void ui_navigation_init(void);
void ui_navigation_cleanup(void);

// Navigation update and rendering
void ui_navigation_update(float delta_time);
void ui_navigation_render(void);
void ui_navigation_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad);

// Touch input for navigation
void ui_navigation_handle_touch(void);

// Navigation state
int ui_navigation_get_selected_icon(void);
void ui_navigation_set_selected_icon(int icon);

// Particle system
void ui_navigation_update_particles(void);
void ui_navigation_render_particles(void);

#endif  // VITARPS5_UI_NAVIGATION_H