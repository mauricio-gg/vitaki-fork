#ifndef VITARPS5_UI_CORE_H
#define VITARPS5_UI_CORE_H

#include <psp2/ctrl.h>
#include <vita2d.h>

#include "../core/vitarps5.h"
#include "vita2d_ui.h"

// Using types from vita2d_ui.h

// High-level UI functions
VitaRPS5Result ui_core_start_streaming(const char* console_ip,
                                       uint8_t console_version);
VitaRPS5Result ui_core_start_discovery(void);
VitaRPS5Result ui_core_start_registration(const char* console_ip,
                                          uint8_t console_version);

// Discovery integration
VitaRPS5Result ui_core_set_discovery_psn_id(const char* psn_id_base64);
bool ui_core_is_discovery_active(void);

// Registration cache functions
bool ui_core_is_console_registered(const char* console_ip);
void ui_core_invalidate_registration_cache(const char* console_ip);
void ui_core_clear_registration_cache(void);

// RESEARCHER PHASE 2: Registration state change events
typedef enum {
  CONSOLE_STATE_REGISTERED,
  CONSOLE_STATE_UNREGISTERED
} ConsoleStateEvent;

void ui_emit_console_state_changed(const char* console_ip,
                                   ConsoleStateEvent state);

// Core UI functions
int ui_core_init(void);
void ui_core_cleanup(void);
void ui_core_update(SceCtrlData* pad);
void ui_core_render(void);

// Asset management
UIAssets* ui_core_get_assets(void);
vita2d_pgf* ui_core_get_font(void);

// State management
UIState ui_core_get_state(void);
void ui_core_set_state(UIState new_state);

// Shared utilities
void ui_core_render_background(void);
void ui_core_render_logo(void);
void ui_core_render_rounded_rectangle(int x, int y, int width, int height,
                                      int radius, uint32_t color);
void ui_core_render_card_with_shadow(int x, int y, int width, int height,
                                     int radius, uint32_t color);

// IME Dialog functions
bool ui_core_is_ime_active(void);
int ui_core_ime_open(int field, const char* title, const char* initial_text,
                     bool is_password);

// IME result functions
int ui_core_get_ime_field(void);
const char* ui_core_get_ime_text(void);
void ui_core_clear_ime_result(void);

// Status indicator rendering
typedef enum {
  STATUS_TYPE_AVAILABLE,
  STATUS_TYPE_UNAVAILABLE,
  STATUS_TYPE_CONNECTING
} StatusType;

void ui_core_render_status(int x, int y, StatusType type, const char* text,
                           float font_scale);

// Forward declarations
typedef struct PS5Discovery PS5Discovery;

// Discovery system access
PS5Discovery* ui_get_discovery_instance(void);

#endif  // VITARPS5_UI_CORE_H