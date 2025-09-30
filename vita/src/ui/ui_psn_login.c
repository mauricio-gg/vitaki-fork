#include "ui_psn_login.h"

#include <stdio.h>
#include <string.h>

#include "../psn/psn_account.h"
#include "../psn/psn_id_utils.h"
#include "../utils/logger.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "ui_profile.h"
#include "vita2d_ui.h"

// Screen layout constants
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define CARD_WIDTH 600
#define CARD_HEIGHT 400
#define CARD_X ((SCREEN_WIDTH - CARD_WIDTH) / 2)
#define CARD_Y ((SCREEN_HEIGHT - CARD_HEIGHT) / 2)
#define FIELD_SPACING 70  // Increased from 60 to prevent field overlap
#define BUTTON_WIDTH 140
#define BUTTON_HEIGHT 40

// PSN login input fields (for online remote play only)
typedef enum {
  PSN_FIELD_USERNAME = 0,
  PSN_FIELD_PASSWORD = 1,
  PSN_FIELD_COUNT = 2
} PSNField;

// PSN login state (for online remote play only)
static struct {
  PSNField selected_field;
  char username[64];
  char password[64];
  bool login_in_progress;
  bool initialized;
} psn_login_state = {0};

// Forward declarations
static void render_field(vita2d_pgf* font, int x, int y, const char* label,
                         const char* value, bool is_password, bool is_selected,
                         bool is_valid);
static void render_button(vita2d_pgf* font, int x, int y, const char* text,
                          bool is_enabled);
static void handle_field_input(void);
static void handle_login_attempt(void);
static void reset_login_state(void);

void ui_psn_login_init(void) {
  log_info("Initializing PSN login UI");
  reset_login_state();
  psn_login_state.initialized = true;
}

void ui_psn_login_cleanup(void) {
  log_info("Cleaning up PSN login UI");
  reset_login_state();
  psn_login_state.initialized = false;
}

void ui_psn_login_render(void) {
  if (!psn_login_state.initialized) {
    ui_psn_login_init();
  }

  vita2d_pgf* font = ui_core_get_font();

  // Render background
  ui_core_render_background();

  // Main card with shadow
  ui_core_render_card_with_shadow(CARD_X, CARD_Y, CARD_WIDTH, CARD_HEIGHT, 12,
                                  UI_COLOR_CARD_BG);

  // Header
  const char* header_text = "PlayStation Network Sign In";
  int header_width = vita2d_pgf_text_width(font, 1.4f, header_text);
  int header_x = CARD_X + (CARD_WIDTH - header_width) / 2;
  vita2d_pgf_draw_text(font, header_x, CARD_Y + 50, UI_COLOR_TEXT_PRIMARY, 1.4f,
                       header_text);

  // Subtitle
  const char* subtitle = "Sign in to PSN for online remote play";
  int subtitle_width = vita2d_pgf_text_width(font, 0.8f, subtitle);
  int subtitle_x = CARD_X + (CARD_WIDTH - subtitle_width) / 2;
  vita2d_pgf_draw_text(font, subtitle_x, CARD_Y + 80, UI_COLOR_TEXT_SECONDARY,
                       0.8f, subtitle);

  // Input fields
  int field_x = CARD_X + 40;
  int field_y = CARD_Y + 130;

  // Username field
  render_field(font, field_x, field_y,
               "PSN Username:", psn_login_state.username, false,
               psn_login_state.selected_field == PSN_FIELD_USERNAME, true);

  // Password field
  render_field(font, field_x, field_y + FIELD_SPACING,
               "PSN Password:", psn_login_state.password, true,
               psn_login_state.selected_field == PSN_FIELD_PASSWORD, true);

  // Note: PSN ID field removed - it's now entered via Profile card for local
  // remote play

  // Buttons - improved positioning for better visual balance
  int button_y =
      CARD_Y + CARD_HEIGHT - 90;  // Move up slightly to avoid hints overlap
  int login_button_x =
      CARD_X + CARD_WIDTH - BUTTON_WIDTH - 30;  // More margin from edge
  int cancel_button_x =
      login_button_x - BUTTON_WIDTH - 30;  // More space between buttons

  // Cancel button
  render_button(font, cancel_button_x, button_y, "Cancel", true);

  // Login button (only enabled if username and password are filled)
  bool can_login = (strlen(psn_login_state.username) > 0 &&
                    strlen(psn_login_state.password) > 0 &&
                    !psn_login_state.login_in_progress);

  const char* login_text =
      psn_login_state.login_in_progress ? "Signing In..." : "Sign In";
  render_button(font, login_button_x, button_y, login_text, can_login);

  // Navigation hints inside the card at bottom - better visual integration
  const char* hints = "▲▼ Navigate    ✕ Edit Field    ○ Cancel    □ Sign In";
  int hints_width = vita2d_pgf_text_width(font, 0.7f, hints);
  int hints_x = CARD_X + (CARD_WIDTH - hints_width) / 2;  // Center the hints
  vita2d_pgf_draw_text(font, hints_x, CARD_Y + CARD_HEIGHT - 25,
                       UI_COLOR_TEXT_TERTIARY, 0.7f, hints);
}

void ui_psn_login_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  if (!psn_login_state.initialized || psn_login_state.login_in_progress) {
    return;
  }

  // Button press detection
  bool up_pressed =
      (pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP);
  bool down_pressed =
      (pad->buttons & SCE_CTRL_DOWN) && !(prev_pad->buttons & SCE_CTRL_DOWN);
  bool x_pressed =
      (pad->buttons & SCE_CTRL_CROSS) && !(prev_pad->buttons & SCE_CTRL_CROSS);
  bool circle_pressed = (pad->buttons & SCE_CTRL_CIRCLE) &&
                        !(prev_pad->buttons & SCE_CTRL_CIRCLE);
  bool square_pressed = (pad->buttons & SCE_CTRL_SQUARE) &&
                        !(prev_pad->buttons & SCE_CTRL_SQUARE);

  // Field navigation
  if (up_pressed && psn_login_state.selected_field > 0) {
    psn_login_state.selected_field--;
    log_debug("Selected field: %d", psn_login_state.selected_field);
  } else if (down_pressed &&
             psn_login_state.selected_field < PSN_FIELD_COUNT - 1) {
    psn_login_state.selected_field++;
    log_debug("Selected field: %d", psn_login_state.selected_field);
  }

  // Field input
  if (x_pressed) {
    handle_field_input();
  }

  // Login attempt
  if (square_pressed) {
    handle_login_attempt();
  }

  // Cancel
  if (circle_pressed) {
    log_info("PSN login cancelled");
    ui_core_set_state(UI_STATE_PROFILE);
  }
}

void ui_psn_login_handle_ime_result(int field, const char* text) {
  if (!text || !psn_login_state.initialized) {
    return;
  }

  log_info("PSN login IME result - field: %d, text: '%s'", field, text);

  switch (field) {
    case PSN_FIELD_USERNAME:
      strncpy(psn_login_state.username, text,
              sizeof(psn_login_state.username) - 1);
      psn_login_state.username[sizeof(psn_login_state.username) - 1] = '\0';
      log_info("PSN username updated: '%s'", psn_login_state.username);
      break;

    case PSN_FIELD_PASSWORD:
      strncpy(psn_login_state.password, text,
              sizeof(psn_login_state.password) - 1);
      psn_login_state.password[sizeof(psn_login_state.password) - 1] = '\0';
      log_info("PSN password updated (length: %d)",
               (int)strlen(psn_login_state.password));
      break;

      // PSN ID field removed - now handled via Profile card for local remote
      // play
  }
}

bool ui_psn_login_is_active(void) { return psn_login_state.initialized; }

void ui_psn_login_reset_fields(void) { reset_login_state(); }

// Private functions

static void render_field(vita2d_pgf* font, int x, int y, const char* label,
                         const char* value, bool is_password, bool is_selected,
                         bool is_valid) {
  // Label
  uint32_t label_color =
      is_selected ? UI_COLOR_PRIMARY_BLUE : UI_COLOR_TEXT_SECONDARY;
  vita2d_pgf_draw_text(font, x, y, label_color, 0.9f, label);

  // Input box
  int box_y = y + 25;
  int box_width = 400;
  int box_height = 35;

  // Box background
  uint32_t box_color =
      is_selected ? RGBA8(60, 60, 60, 255) : RGBA8(40, 40, 40, 255);
  ui_core_render_rounded_rectangle(x, box_y, box_width, box_height, 6,
                                   box_color);

  // Box border
  uint32_t border_color;
  if (is_selected) {
    border_color = UI_COLOR_PRIMARY_BLUE;
  } else if (!is_valid && strlen(value) > 0) {
    border_color = UI_COLOR_STATUS_UNAVAILABLE;  // Red for invalid
  } else {
    border_color = RGBA8(80, 80, 80, 255);
  }

  // Draw border
  vita2d_draw_rectangle(x, box_y, box_width, 2, border_color);  // Top
  vita2d_draw_rectangle(x, box_y + box_height - 2, box_width, 2,
                        border_color);                           // Bottom
  vita2d_draw_rectangle(x, box_y, 2, box_height, border_color);  // Left
  vita2d_draw_rectangle(x + box_width - 2, box_y, 2, box_height,
                        border_color);  // Right

  // Value text
  char display_value[64];
  if (is_password && strlen(value) > 0) {
    // Show asterisks for password
    memset(display_value, '*', strlen(value));
    display_value[strlen(value)] = '\0';
  } else {
    strncpy(display_value, value, sizeof(display_value) - 1);
    display_value[sizeof(display_value) - 1] = '\0';
  }

  uint32_t text_color = UI_COLOR_TEXT_PRIMARY;
  if (strlen(display_value) > 0) {
    vita2d_pgf_draw_text(font, x + 10, box_y + 22, text_color, 0.8f,
                         display_value);
  }

  // Placeholder text if empty
  if (strlen(value) == 0) {
    const char* placeholder = "Tap ✕ to enter";
    vita2d_pgf_draw_text(font, x + 10, box_y + 22, UI_COLOR_TEXT_TERTIARY, 0.8f,
                         placeholder);
  }
}

static void render_button(vita2d_pgf* font, int x, int y, const char* text,
                          bool is_enabled) {
  // Button background
  uint32_t bg_color =
      is_enabled ? UI_COLOR_PRIMARY_BLUE : RGBA8(60, 60, 60, 255);
  ui_core_render_rounded_rectangle(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, 8,
                                   bg_color);

  // Button text
  uint32_t text_color =
      is_enabled ? UI_COLOR_TEXT_PRIMARY : UI_COLOR_TEXT_TERTIARY;
  int text_width = vita2d_pgf_text_width(font, 0.8f, text);
  int text_x = x + (BUTTON_WIDTH - text_width) / 2;
  int text_y = y + (BUTTON_HEIGHT / 2) + 6;
  vita2d_pgf_draw_text(font, text_x, text_y, text_color, 0.8f, text);
}

static void handle_field_input(void) {
  const char* ime_titles[] = {"Enter PSN Username", "Enter PSN Password"};

  const char* current_values[] = {psn_login_state.username,
                                  psn_login_state.password};

  bool is_password = (psn_login_state.selected_field == PSN_FIELD_PASSWORD);

  log_info("Opening IME for PSN field %d", psn_login_state.selected_field);
  ui_core_ime_open(psn_login_state.selected_field,
                   ime_titles[psn_login_state.selected_field],
                   current_values[psn_login_state.selected_field], is_password);
}

static void handle_login_attempt(void) {
  // Check if all fields are valid
  if (strlen(psn_login_state.username) == 0) {
    log_warning("Cannot sign in: username is empty");
    return;
  }

  if (strlen(psn_login_state.password) == 0) {
    log_warning("Cannot sign in: password is empty");
    return;
  }

  // Note: PSN ID is no longer required for PSN sign-in (online remote play)
  // PSN ID is now entered separately via Profile card for local remote play

  log_info("Attempting PSN sign in for user: %s", psn_login_state.username);
  psn_login_state.login_in_progress = true;

  // Set credentials in profile system
  ui_profile_set_psn_credentials(psn_login_state.username,
                                 psn_login_state.password);

  // Mark as authenticated and return to profile
  ui_profile_set_psn_authenticated(true);

  log_info("PSN sign in completed successfully");
  ui_core_set_state(UI_STATE_PROFILE);
}

static void reset_login_state(void) {
  psn_login_state.selected_field = PSN_FIELD_USERNAME;
  memset(psn_login_state.username, 0, sizeof(psn_login_state.username));
  memset(psn_login_state.password, 0, sizeof(psn_login_state.password));
  psn_login_state.login_in_progress = false;
}
