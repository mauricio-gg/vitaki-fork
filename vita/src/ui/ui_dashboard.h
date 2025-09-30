#ifndef VITARPS5_UI_DASHBOARD_H
#define VITARPS5_UI_DASHBOARD_H

#include <psp2/ctrl.h>
#include <stdint.h>

#include "../core/console_storage.h"
#include "console_state_thread.h"
#include "vita2d_ui.h"

// Dashboard view functions
void ui_dashboard_init(void);
void ui_dashboard_cleanup(void);
void ui_dashboard_render(void);
void ui_dashboard_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad);

// Dashboard state management - NEW: Real console data API
void ui_dashboard_set_console_data(const UIConsoleInfo* console);
bool ui_dashboard_has_saved_console(void);
uint32_t ui_dashboard_get_console_count(void);
UIConsoleInfo* ui_dashboard_get_selected_console(void);

// Dashboard cache management
void ui_dashboard_force_reload(void);

// Thread access for session manager
ConsoleStateThread* ui_dashboard_get_state_thread(void);

#endif  // VITARPS5_UI_DASHBOARD_H