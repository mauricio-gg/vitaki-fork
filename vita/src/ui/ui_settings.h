#ifndef VITARPS5_UI_SETTINGS_H
#define VITARPS5_UI_SETTINGS_H

#include <psp2/ctrl.h>

#include "vita2d_ui.h"

// Settings view functions
void ui_settings_init(void);
void ui_settings_cleanup(void);
void ui_settings_render(void);
void ui_settings_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad);

// Settings state management
SettingsTab ui_settings_get_current_tab(void);
void ui_settings_set_current_tab(SettingsTab tab);
int ui_settings_get_selected_item(void);

// Settings panel management
void ui_settings_init_panels(void);
const SettingsPanel* ui_settings_get_panel(SettingsTab tab);

#endif  // VITARPS5_UI_SETTINGS_H