#ifndef VITARPS5_UI_PROFILE_H
#define VITARPS5_UI_PROFILE_H

#include <psp2/ctrl.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/vitarps5.h"

// Profile view functions
void ui_profile_init(void);
void ui_profile_cleanup(void);
void ui_profile_render(void);
void ui_profile_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad);

// PSN authentication state
void ui_profile_get_psn_credentials(char* username, char* password);
void ui_profile_set_psn_credentials(const char* username, const char* password);
bool ui_profile_is_psn_authenticated(void);
void ui_profile_set_psn_authenticated(bool authenticated);

// PSN ID (base64) for discovery
void ui_profile_set_psn_id_base64(const char* psn_id_base64);
void ui_profile_get_psn_id_base64(char* psn_id_base64, size_t size);
bool ui_profile_get_psn_id_valid(void);

// PSN account number for credentials
uint64_t ui_profile_get_psn_account_number(void);

// Get PSN account ID in binary format for session authentication
VitaRPS5Result ui_profile_get_psn_account_id_binary(uint8_t* account_id);

// PSN account instance access
struct PSNAccount* ui_profile_get_psn_account(void);

// IME input handling
void ui_profile_handle_ime_result(int field, const char* text);

#endif  // VITARPS5_UI_PROFILE_H