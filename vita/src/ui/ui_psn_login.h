#ifndef VITARPS5_UI_PSN_LOGIN_H
#define VITARPS5_UI_PSN_LOGIN_H

#include <psp2/ctrl.h>
#include <stdbool.h>

/**
 * @file ui_psn_login.h
 * @brief PSN login and account setup UI
 *
 * This module provides the PSN sign-in interface where users can enter:
 * - PSN Username
 * - PSN Password
 * - PSN Account Number (for discovery authentication)
 */

// PSN login view functions
void ui_psn_login_init(void);
void ui_psn_login_cleanup(void);
void ui_psn_login_render(void);
void ui_psn_login_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad);

// PSN login state management
bool ui_psn_login_is_active(void);
void ui_psn_login_reset_fields(void);

// IME input handling for PSN login
void ui_psn_login_handle_ime_result(int field, const char* text);

#endif  // VITARPS5_UI_PSN_LOGIN_H