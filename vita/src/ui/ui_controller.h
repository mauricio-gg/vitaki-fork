#ifndef VITARPS5_UI_CONTROLLER_H
#define VITARPS5_UI_CONTROLLER_H

#include <psp2/ctrl.h>
#include <stdbool.h>

#include "vita2d_ui.h"

// Vita button identifiers for mapping
typedef enum {
  VITA_BUTTON_CROSS = 0,
  VITA_BUTTON_CIRCLE,
  VITA_BUTTON_SQUARE,
  VITA_BUTTON_TRIANGLE,
  VITA_BUTTON_DPAD_UP,
  VITA_BUTTON_DPAD_DOWN,
  VITA_BUTTON_DPAD_LEFT,
  VITA_BUTTON_DPAD_RIGHT,
  VITA_BUTTON_L,
  VITA_BUTTON_R,
  VITA_BUTTON_START,
  VITA_BUTTON_SELECT,
  VITA_BUTTON_LTRIGGER,  // Rear touch L2
  VITA_BUTTON_RTRIGGER,  // Rear touch R2
  VITA_BUTTON_LSTICK,    // Analog stick click
  VITA_BUTTON_RSTICK,    // Analog stick click
  VITA_BUTTON_COUNT
} VitaButtonId;

// PS5 button identifiers
typedef enum {
  PS5_BUTTON_CROSS = 0,
  PS5_BUTTON_CIRCLE,
  PS5_BUTTON_SQUARE,
  PS5_BUTTON_TRIANGLE,
  PS5_BUTTON_DPAD_UP,
  PS5_BUTTON_DPAD_DOWN,
  PS5_BUTTON_DPAD_LEFT,
  PS5_BUTTON_DPAD_RIGHT,
  PS5_BUTTON_L1,
  PS5_BUTTON_R1,
  PS5_BUTTON_L2,
  PS5_BUTTON_R2,
  PS5_BUTTON_L3,
  PS5_BUTTON_R3,
  PS5_BUTTON_OPTIONS,
  PS5_BUTTON_SHARE,
  PS5_BUTTON_PS,
  PS5_BUTTON_TOUCHPAD,
  PS5_BUTTON_COUNT
} PS5ButtonId;

// Button hit zone for interactive mapping
typedef struct {
  int x, y;                // Button center position
  int width, height;       // Hit zone dimensions
  VitaButtonId button_id;  // Which button this represents
  bool is_rear_touch;      // Whether this is on the back panel
} ButtonHitZone;

// Controller mapping state
typedef struct {
  int selected_row;   // Currently selected table row (0-based, relative to
                      // visible area)
  int scroll_offset;  // First visible row index for scrolling
  bool in_assignment_mode;        // Whether we're assigning a PS5 button
  VitaButtonId rebinding_button;  // Button currently being rebound (-1 = none)
  ControllerPreset current_preset;  // Active controller preset
  ButtonHitZone front_buttons[12];  // Front controller hit zones
  ButtonHitZone rear_buttons[4];    // Rear touch zones
  int front_button_count;
  int rear_button_count;
} ControllerMappingState;

// Controller view functions
void ui_controller_init(void);
void ui_controller_cleanup(void);
void ui_controller_render(void);
void ui_controller_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad);

// Controller mapping functions
void controller_mapping_init_hit_zones(void);
VitaButtonId controller_mapping_check_hit_zone(int x, int y, bool is_rear);
void controller_mapping_select_button(VitaButtonId button);
void controller_mapping_assign_ps5_button(PS5ButtonId ps5_button);
const char* vita_button_name(VitaButtonId button);
const char* ps5_button_name(PS5ButtonId button);

#endif  // VITARPS5_UI_CONTROLLER_H