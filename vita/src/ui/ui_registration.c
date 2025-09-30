#include "ui_registration.h"

#include <ctype.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <string.h>

#include "../auth/account_id.h"
#include "../chiaki/chiaki_integration.h"
#include "../console/vitaki_bridge.h"
#include "../core/registration_cache.h"
#include "../network/session_init.h"
#include "../psn/psn_id_utils.h"
#include "../ui/ui_dashboard.h"
#include "../utils/diagnostics.h"
#include "../utils/helpers.h"
#include "../utils/http_client.h"
#include "../utils/logger.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "vita2d_ui.h"

// Screen layout constants
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define CARD_WIDTH 700
#define CARD_HEIGHT 450
#define CARD_X ((SCREEN_WIDTH - CARD_WIDTH) / 2)
#define CARD_Y ((SCREEN_HEIGHT - CARD_HEIGHT) / 2)

// PIN entry constants
#define PIN_DIGIT_COUNT 8
#define PIN_DIGIT_WIDTH 60
#define PIN_DIGIT_HEIGHT 70
#define PIN_DIGIT_SPACING 10
#define PIN_TOTAL_WIDTH                  \
  ((PIN_DIGIT_WIDTH * PIN_DIGIT_COUNT) + \
   (PIN_DIGIT_SPACING * (PIN_DIGIT_COUNT - 1)))
#define PIN_START_X (CARD_X + (CARD_WIDTH - PIN_TOTAL_WIDTH) / 2)
#define PIN_Y (CARD_Y + 200)

// Button constants
#define BUTTON_WIDTH 120
#define BUTTON_HEIGHT 40
#define BUTTON_SPACING 20

// Registration UI state - updated for clean implementation
static struct {
  bool initialized;
  RegistrationUIState ui_state;
  RegistrationUIConfig config;

  // Clean registration data
  ConsoleRegistration registration_data;

  // PIN entry
  PinEntryState pin_state;

  // Progress tracking
  RegistrationProgress progress;
  char status_message[256];
  char error_message[256];

  // Retry logic
  int registration_attempts;
  int max_registration_attempts;

  // Timing
  uint64_t registration_start_time;
  uint64_t last_update_time;

  // Animation
  float progress_animation;
  bool show_cursor;
  uint32_t cursor_blink_timer;

  // Chiaki integration state
  bool chiaki_registration_active;
  bool chiaki_registration_waiting_for_pin;
  VitaRPS5RegistrationData chiaki_registration_result;

  // Guard to avoid duplicate storing/verification
  bool registration_saved;

} registration_ui = {0};

// Forward declarations
static void render_pin_entry_screen(void);
static void render_registration_progress_screen(void);
static void render_success_screen(void);
static void render_error_screen(void);
static void render_pin_digit(vita2d_pgf* font, int x, int y, uint32_t digit,
                             bool is_current, bool has_value);
static void render_progress_bar(int x, int y, int width, int height,
                                float progress);
static void update_pin_entry_input(SceCtrlData* pad, SceCtrlData* prev_pad);
static void update_registration_progress(void);
static void handle_registration_completion(void);
static uint64_t get_timestamp_ms(void);
static void set_ui_state(RegistrationUIState state);
static void update_cursor_animation(void);

// Chiaki registration callback
static void chiaki_registration_callback(VitaRPS5RegistrationEvent event,
                                         const VitaRPS5RegistrationData* data,
                                         void* user_data);

// PS5 Registration Implementation - performs actual Chiaki registration
// protocol
static VitaRPS5Result perform_ps5_registration(const char* console_ip,
                                               uint32_t pin,
                                               const char* psn_account_b64,
                                               ConsoleRegistration* reg_out) {
  if (!console_ip || !psn_account_b64 || !reg_out) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Starting real PS5 Chiaki registration for %s with PIN %08u",
           console_ip, pin);

  // Start Vitaki bridge registration (async, same callback semantics)
  log_info("Starting Vitaki-bridge registration with PIN %08u", pin);

  // Convert pin to 8-char string for bridge validation
  char pin_str[9];
  snprintf(pin_str, sizeof(pin_str), "%08u", pin);

  VitaRPS5Result start_result =
      vitaki_register_ps5_start(console_ip, pin_str, psn_account_b64,
                                chiaki_registration_callback, &registration_ui);
  if (start_result != VITARPS5_SUCCESS) {
    log_error("Failed to start Vitaki registration: %d", start_result);
    return start_result;
  }

  // Set state to indicate registration is active
  registration_ui.chiaki_registration_active = true;
  registration_ui.chiaki_registration_waiting_for_pin = false;
  registration_ui.registration_saved = false;

  log_info("Vitaki-bridge registration started; waiting for PS5 response...");
  return VITARPS5_SUCCESS;
}

// API Implementation

VitaRPS5Result ui_registration_init(void) {
  if (registration_ui.initialized) {
    return VITARPS5_SUCCESS;
  }

  // Initialize HTTP client for PS5 communication (non-fatal)
  VitaRPS5Result http_result = http_client_init();
  if (http_result != VITARPS5_SUCCESS) {
    log_warn(
        "HTTP client initialization failed - registration will work in offline "
        "mode");
    log_info("Network features will be limited but UI can still function");
  } else {
    log_info("HTTP client initialized successfully for registration");
  }

  memset(&registration_ui, 0, sizeof(registration_ui));
  registration_ui.ui_state = REGISTRATION_UI_STATE_IDLE;
  registration_ui.initialized = true;

  // Check HTTP availability and log status for user awareness
  if (http_client_is_available()) {
    log_info(
        "Registration UI initialized - HTTP client ready for PS5 registration");
  } else {
    log_info(
        "Registration UI initialized - HTTP client unavailable (network "
        "features limited)");
  }

  return VITARPS5_SUCCESS;
}

void ui_registration_cleanup(void) {
  if (!registration_ui.initialized) {
    return;
  }

  // Cleanup HTTP client
  http_client_cleanup();

  memset(&registration_ui, 0, sizeof(registration_ui));
  log_info("Registration UI cleaned up");
}

VitaRPS5Result ui_registration_start(const RegistrationUIConfig* config) {
  if (!registration_ui.initialized || !config) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Check HTTP availability but don't make it fatal - registration can proceed
  if (!http_client_is_available()) {
    log_warn(
        "HTTP client unavailable - registration will use network-limited mode");
    log_info(
        "User will be notified of network requirements during registration "
        "process");
  } else {
    log_info("HTTP client available - full network registration possible");
  }

  // RESEARCHER FIX 6: Enhanced pre-registration network diagnostic
  log_info("Running comprehensive pre-registration diagnostic for %s...",
           config->console_ip);
  PS5DiagnosticReport diagnostic_report;
  VitaRPS5Result diagnostic_result =
      diagnostics_test_ps5_connectivity(config->console_ip, &diagnostic_report);

  log_info("=== PRE-REGISTRATION DIAGNOSTIC REPORT ===");
  if (diagnostic_result != VITARPS5_SUCCESS) {
    log_warn("⚠️  DIAGNOSTIC FAILED: %s",
             vitarps5_result_string(diagnostic_result));
    log_warn("Registration may fail due to network connectivity problems");
    log_warn("Recommended actions:");
    log_warn(
        "  • Check PS5 Remote Play settings: Settings > System > Remote Play");
    log_warn("  • Ensure PS5 and Vita are on same network");
    log_warn("  • Verify PS5 is powered on and not in rest mode");
    log_warn("  • Check network firewall settings");
  } else {
    // Consider diagnostic passed only if control port is reachable
    bool diag_ok = diagnostic_report.control_port.service_available;
    if (diag_ok) {
      log_info("✅ DIAGNOSTIC PASSED: Network connectivity looks good");
    } else {
      log_error(
          "❌ DIAGNOSTIC FAILED: Control port 9295 is unreachable — "
          "registration will likely fail");
    }
    log_info("Port connectivity results:");
    log_info(
        "  • Control port (9295): %s%s",
        diagnostic_report.control_port.service_available ? "✅ OK" : "❌ FAIL",
        diagnostic_report.control_port.service_available
            ? ""
            : " - Required for registration");
    log_info(
        "  • Stream port (9296): %s%s",
        diagnostic_report.stream_port.service_available ? "✅ OK" : "⚠️  FAIL",
        diagnostic_report.stream_port.service_available
            ? ""
            : " - Will affect streaming");
    log_info(
        "  • Wake port (9302): %s%s",
        diagnostic_report.wake_port.service_available ? "✅ OK" : "⚠️  FAIL",
        diagnostic_report.wake_port.service_available
            ? ""
            : " - Will affect wake-up");

    // Check for potential issues even if diagnostic passed
    int failed_ports = 0;
    if (!diagnostic_report.control_port.service_available) failed_ports++;
    if (!diagnostic_report.stream_port.service_available) failed_ports++;
    if (!diagnostic_report.wake_port.service_available) failed_ports++;

    if (failed_ports > 0) {
      log_warn("⚠️  Warning: %d port(s) failed - some features may not work",
               failed_ports);
      if (!diagnostic_report.control_port.service_available) {
        log_warn("❌ Control port failure will likely prevent registration");
      }
    }
  }
  log_info("=== END DIAGNOSTIC REPORT ===");

  // Copy configuration
  registration_ui.config = *config;

  // Initialize clean registration data
  memset(&registration_ui.registration_data, 0, sizeof(ConsoleRegistration));

  // Use the original base64 PSN ID directly (no conversion needed)
  log_debug("Using original PSN ID base64: %s", config->psn_account_id_base64);

  // Validate the base64 PSN ID
  if (strlen(config->psn_account_id_base64) == 0) {
    log_error("PSN account ID base64 is empty - registration will fail");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Copy the original base64 directly (no conversion)
  strncpy(registration_ui.registration_data.np_account_b64,
          config->psn_account_id_base64,
          sizeof(registration_ui.registration_data.np_account_b64) - 1);
  registration_ui.registration_data
      .np_account_b64[sizeof(registration_ui.registration_data.np_account_b64) -
                      1] = '\0';

  // Decode to get LE8 for backward compatibility
  uint8_t psn_binary[8];
  VitaRPS5Result decode_result =
      psn_id_base64_to_binary(config->psn_account_id_base64, psn_binary, false);
  if (decode_result == VITARPS5_SUCCESS) {
    memcpy(registration_ui.registration_data.np_account_le8, psn_binary, 8);
  } else {
    log_error("Failed to decode PSN ID base64 for LE8 format: %s",
              config->psn_account_id_base64);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_debug("PSN account ID conversion successful:");
  log_debug("  - Account ID: %llu", config->psn_account_id);
  log_debug("  - Base64: %.15s",
            registration_ui.registration_data.np_account_b64);
  log_debug("  - LE8 (hex): %02X%02X%02X%02X%02X%02X%02X%02X",
            registration_ui.registration_data.np_account_le8[0],
            registration_ui.registration_data.np_account_le8[1],
            registration_ui.registration_data.np_account_le8[2],
            registration_ui.registration_data.np_account_le8[3],
            registration_ui.registration_data.np_account_le8[4],
            registration_ui.registration_data.np_account_le8[5],
            registration_ui.registration_data.np_account_le8[6],
            registration_ui.registration_data.np_account_le8[7]);

  // Reset PIN state
  memset(&registration_ui.pin_state, 0, sizeof(PinEntryState));

  // Initialize retry logic
  registration_ui.registration_attempts = 0;
  registration_ui.max_registration_attempts = 3;  // Allow 3 attempts

  // Start with PIN entry
  set_ui_state(REGISTRATION_UI_STATE_PIN_ENTRY);

  registration_ui.registration_start_time = get_timestamp_ms();
  strcpy(registration_ui.status_message, "Enter PS5 session PIN");

  log_info("Registration started for console: %s (%s)", config->console_name,
           config->console_ip);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ui_registration_cancel(void) {
  if (!registration_ui.initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  set_ui_state(REGISTRATION_UI_STATE_IDLE);
  strcpy(registration_ui.status_message, "Registration cancelled");

  log_info("Registration cancelled by user");
  return VITARPS5_SUCCESS;
}

VitaRPS5Result ui_registration_update(void) {
  if (!registration_ui.initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  registration_ui.last_update_time = get_timestamp_ms();
  update_cursor_animation();

  // Update progress based on current state
  switch (registration_ui.ui_state) {
    case REGISTRATION_UI_STATE_REGISTERING:
      update_registration_progress();
      break;
    case REGISTRATION_UI_STATE_SUCCESS:
    case REGISTRATION_UI_STATE_ERROR:
      handle_registration_completion();
      break;
    default:
      break;
  }

  return VITARPS5_SUCCESS;
}

void ui_registration_render(void) {
  if (!registration_ui.initialized) {
    return;
  }

  switch (registration_ui.ui_state) {
    case REGISTRATION_UI_STATE_PIN_ENTRY:
      render_pin_entry_screen();
      break;
    case REGISTRATION_UI_STATE_REGISTERING:
      render_registration_progress_screen();
      break;
    case REGISTRATION_UI_STATE_SUCCESS:
      render_success_screen();
      break;
    case REGISTRATION_UI_STATE_ERROR:
      render_error_screen();
      break;
    default:
      break;
  }
}

void ui_registration_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  if (!registration_ui.initialized || !pad || !prev_pad) {
    return;
  }

  switch (registration_ui.ui_state) {
    case REGISTRATION_UI_STATE_PIN_ENTRY:
      update_pin_entry_input(pad, prev_pad);
      break;
    case REGISTRATION_UI_STATE_SUCCESS:
      // Handle success screen input - X to continue, Triangle to cancel
      if ((pad->buttons & SCE_CTRL_CROSS) &&
          !(prev_pad->buttons & SCE_CTRL_CROSS)) {
        log_info(
            "User confirmed registration success - returning to dashboard");
        ui_registration_cancel();  // This transitions back to main UI
      } else if ((pad->buttons & SCE_CTRL_TRIANGLE) &&
                 !(prev_pad->buttons & SCE_CTRL_TRIANGLE)) {
        log_info("User cancelled from success screen");
        ui_registration_cancel();
      }
      break;
    case REGISTRATION_UI_STATE_ERROR:
      // Handle error screen input - X to retry, Triangle to cancel
      if ((pad->buttons & SCE_CTRL_CROSS) &&
          !(prev_pad->buttons & SCE_CTRL_CROSS)) {
        log_info("User chose to retry registration from error screen");
        set_ui_state(REGISTRATION_UI_STATE_PIN_ENTRY);
        ui_registration_clear_pin();
        strcpy(registration_ui.status_message, "Enter PS5 session PIN");
      } else if ((pad->buttons & SCE_CTRL_TRIANGLE) &&
                 !(prev_pad->buttons & SCE_CTRL_TRIANGLE)) {
        log_info("User cancelled from error screen");
        ui_registration_cancel();
      }
      break;
    default:
      // Handle common navigation (Cancel with Triangle)
      if ((pad->buttons & SCE_CTRL_TRIANGLE) &&
          !(prev_pad->buttons & SCE_CTRL_TRIANGLE)) {
        ui_registration_cancel();
      }
      break;
  }
}

RegistrationUIState ui_registration_get_state(void) {
  return registration_ui.initialized ? registration_ui.ui_state
                                     : REGISTRATION_UI_STATE_IDLE;
}

bool ui_registration_is_active(void) {
  return registration_ui.initialized &&
         registration_ui.ui_state != REGISTRATION_UI_STATE_IDLE;
}

VitaRPS5Result ui_registration_get_progress(RegistrationProgress* progress) {
  if (!registration_ui.initialized || !progress) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  progress->ui_state = registration_ui.ui_state;
  progress->status_message = registration_ui.status_message;
  progress->elapsed_time_ms =
      get_timestamp_ms() - registration_ui.registration_start_time;
  progress->has_error =
      (registration_ui.ui_state == REGISTRATION_UI_STATE_ERROR);
  progress->error_message = registration_ui.error_message;

  // Calculate progress percentage
  switch (registration_ui.ui_state) {
    case REGISTRATION_UI_STATE_PIN_ENTRY:
      progress->progress_percentage = 0.0f;
      break;
    case REGISTRATION_UI_STATE_REGISTERING:
      progress->progress_percentage =
          25.0f + (registration_ui.progress_animation * 65.0f);
      break;
    case REGISTRATION_UI_STATE_SUCCESS:
      progress->progress_percentage = 100.0f;
      break;
    default:
      progress->progress_percentage = 0.0f;
      break;
  }

  return VITARPS5_SUCCESS;
}

// PIN Entry Functions

VitaRPS5Result ui_registration_get_pin_state(PinEntryState* pin_state) {
  if (!registration_ui.initialized || !pin_state) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *pin_state = registration_ui.pin_state;
  return VITARPS5_SUCCESS;
}

VitaRPS5Result ui_registration_set_pin_digit(uint32_t position,
                                             uint32_t digit) {
  if (!registration_ui.initialized || position >= PIN_DIGIT_COUNT ||
      digit > 9) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  registration_ui.pin_state.pin_digits[position] = digit;

  // Update completion status
  registration_ui.pin_state.pin_complete = true;
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    if (registration_ui.pin_state.pin_digits[i] > 9) {
      registration_ui.pin_state.pin_complete = false;
      break;
    }
  }

  if (registration_ui.pin_state.pin_complete) {
    registration_ui.pin_state.complete_pin =
        ui_registration_pin_to_number(&registration_ui.pin_state);
  }

  return VITARPS5_SUCCESS;
}

void ui_registration_clear_pin(void) {
  if (!registration_ui.initialized) {
    return;
  }

  memset(&registration_ui.pin_state, 0, sizeof(PinEntryState));
  // Initialize with invalid values to indicate empty
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    registration_ui.pin_state.pin_digits[i] = 10;  // Invalid digit
  }
}

VitaRPS5Result ui_registration_submit_pin(void) {
  if (!registration_ui.initialized ||
      !ui_registration_is_pin_complete(&registration_ui.pin_state)) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("PIN submitted: %08u", registration_ui.pin_state.complete_pin);

  // Start session initialization process directly
  set_ui_state(REGISTRATION_UI_STATE_REGISTERING);
  strcpy(registration_ui.status_message, "Initializing PS5 session...");

  // Start registration immediately (don't wait for progress_animation)
  if (!registration_ui.chiaki_registration_active) {
    VitaRPS5Result reg_result = perform_ps5_registration(
        registration_ui.config.console_ip,
        registration_ui.pin_state.complete_pin,
        registration_ui.registration_data.np_account_b64,
        &registration_ui.registration_data);
    if (reg_result != VITARPS5_SUCCESS) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg),
               "Failed to start PS5 registration: %d\nCheck PIN and network.",
               reg_result);
      ui_registration_set_error(error_msg);
      return reg_result;
    }
    strcpy(registration_ui.status_message, "Communicating with PS5...");
    log_info("Chiaki registration started immediately after PIN submission");
  }

  return VITARPS5_SUCCESS;
}

// State Management

void ui_registration_set_state(RegistrationUIState state) {
  set_ui_state(state);
}

void ui_registration_set_error(const char* error_message) {
  if (!registration_ui.initialized || !error_message) {
    return;
  }

  strncpy(registration_ui.error_message, error_message,
          sizeof(registration_ui.error_message) - 1);
  registration_ui.error_message[sizeof(registration_ui.error_message) - 1] =
      '\0';
  set_ui_state(REGISTRATION_UI_STATE_ERROR);
}

void ui_registration_set_status(const char* status_message) {
  if (!registration_ui.initialized || !status_message) {
    return;
  }

  strncpy(registration_ui.status_message, status_message,
          sizeof(registration_ui.status_message) - 1);
  registration_ui.status_message[sizeof(registration_ui.status_message) - 1] =
      '\0';
}

// Utility Functions

void ui_registration_format_pin_display(const PinEntryState* pin_state,
                                        char* display_text) {
  if (!pin_state || !display_text) {
    return;
  }

  char temp[32] = {0};
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    if (pin_state->pin_digits[i] <= 9) {
      temp[i * 2] = '0' + pin_state->pin_digits[i];
    } else {
      temp[i * 2] = '_';
    }
    if (i < PIN_DIGIT_COUNT - 1) {
      temp[i * 2 + 1] = ' ';
    }
  }
  strcpy(display_text, temp);
}

bool ui_registration_is_pin_complete(const PinEntryState* pin_state) {
  if (!pin_state) {
    return false;
  }

  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    if (pin_state->pin_digits[i] > 9) {
      return false;
    }
  }
  return true;
}

uint32_t ui_registration_pin_to_number(const PinEntryState* pin_state) {
  if (!pin_state || !ui_registration_is_pin_complete(pin_state)) {
    return 0;
  }

  uint32_t pin = 0;
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    pin = pin * 10 + pin_state->pin_digits[i];
  }
  return pin;
}

bool ui_registration_is_pin_valid(const PinEntryState* pin_state) {
  return ui_registration_is_pin_complete(pin_state);
}

// Internal Helper Functions

static void render_pin_entry_screen(void) {
  // Get font
  vita2d_pgf* font = ui_core_get_font();
  if (!font) {
    return;
  }

  // Draw background card
  ui_core_render_rounded_rectangle(CARD_X, CARD_Y, CARD_WIDTH, CARD_HEIGHT, 8,
                                   UI_COLOR_CARD_BG);

  // Title
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 50, UI_COLOR_TEXT_PRIMARY,
                       1.2f, "PS5 Console Registration");

  // Console info
  char console_info[128];
  snprintf(console_info, sizeof(console_info), "Console: %s (%s)",
           registration_ui.config.console_name,
           registration_ui.config.console_ip);
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 100, UI_COLOR_TEXT_SECONDARY,
                       0.9f, console_info);

  // Instructions
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 150, UI_COLOR_TEXT_PRIMARY,
                       0.9f,
                       "Enter the 8-digit session PIN displayed on your PS5:");

  // PIN digits
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    int x = PIN_START_X + i * (PIN_DIGIT_WIDTH + PIN_DIGIT_SPACING);
    bool is_current = (registration_ui.pin_state.current_digit == i);
    bool has_value = (registration_ui.pin_state.pin_digits[i] <= 9);
    render_pin_digit(font, x, PIN_Y, registration_ui.pin_state.pin_digits[i],
                     is_current, has_value);
  }

  // Status message
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 350, UI_COLOR_TEXT_PRIMARY,
                       0.9f, registration_ui.status_message);

  // Navigation hints
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 400, UI_COLOR_TEXT_SECONDARY,
                       0.8f,
                       "D-Pad: Navigate  X: Confirm digit  Triangle: Cancel");
}

static void render_registration_progress_screen(void) {
  vita2d_pgf* font = ui_core_get_font();
  if (!font) {
    return;
  }

  // Draw background card
  ui_core_render_rounded_rectangle(CARD_X, CARD_Y, CARD_WIDTH, CARD_HEIGHT, 8,
                                   UI_COLOR_CARD_BG);

  // Title
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 50, UI_COLOR_TEXT_PRIMARY,
                       1.2f, "Connecting to PS5...");

  // Progress bar
  int progress_x = CARD_X + 50;
  int progress_y = CARD_Y + 200;
  int progress_width = CARD_WIDTH - 100;
  int progress_height = 20;

  render_progress_bar(progress_x, progress_y, progress_width, progress_height,
                      registration_ui.progress_animation);

  // Status (simple wrap on '\n')
  const char* sm = registration_ui.status_message;
  int line_y = CARD_Y + 250;
  while (sm && *sm) {
    const char* nl = strchr(sm, '\n');
    if (!nl) {
      vita2d_pgf_draw_text(font, CARD_X + 20, line_y, UI_COLOR_TEXT_PRIMARY,
                           0.9f, sm);
      break;
    } else {
      char line[256];
      size_t len = (size_t)(nl - sm);
      if (len >= sizeof(line)) len = sizeof(line) - 1;
      memcpy(line, sm, len);
      line[len] = '\0';
      vita2d_pgf_draw_text(font, CARD_X + 20, line_y, UI_COLOR_TEXT_PRIMARY,
                           0.9f, line);
      line_y += 24;
      sm = nl + 1;
    }
  }

  // Elapsed time
  uint64_t elapsed =
      get_timestamp_ms() - registration_ui.registration_start_time;
  char time_text[64];
  snprintf(time_text, sizeof(time_text), "Elapsed time: %llu.%llu seconds",
           elapsed / 1000, (elapsed % 1000) / 100);
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 300, UI_COLOR_TEXT_SECONDARY,
                       0.8f, time_text);
}

static void render_success_screen(void) {
  vita2d_pgf* font = ui_core_get_font();
  if (!font) {
    return;
  }

  // Draw background card
  ui_core_render_rounded_rectangle(CARD_X, CARD_Y, CARD_WIDTH, CARD_HEIGHT, 8,
                                   UI_COLOR_CARD_BG);

  // Success icon (simple checkmark)
  int icon_x = CARD_X + CARD_WIDTH / 2 - 25;
  int icon_y = CARD_Y + 100;
  vita2d_pgf_draw_text(font, icon_x, icon_y, UI_COLOR_STATUS_AVAILABLE, 2.0f,
                       "✓");

  // Title
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 200, UI_COLOR_TEXT_PRIMARY,
                       1.2f, "Registration Successful!");

  // Status
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 250, UI_COLOR_TEXT_PRIMARY,
                       0.9f, "PS5 console has been registered successfully.");

  // Navigation hint
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 400, UI_COLOR_TEXT_SECONDARY,
                       0.8f, "Press X to continue");
}

static void render_error_screen(void) {
  vita2d_pgf* font = ui_core_get_font();
  if (!font) {
    return;
  }

  // Draw background card
  ui_core_render_rounded_rectangle(CARD_X, CARD_Y, CARD_WIDTH, CARD_HEIGHT, 8,
                                   UI_COLOR_CARD_BG);

  // Error icon
  int icon_x = CARD_X + CARD_WIDTH / 2 - 25;
  int icon_y = CARD_Y + 100;
  vita2d_pgf_draw_text(font, icon_x, icon_y, UI_COLOR_STATUS_UNAVAILABLE, 2.0f,
                       "✗");

  // Title
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 200,
                       UI_COLOR_STATUS_UNAVAILABLE, 1.2f,
                       "Registration Failed");

  // Error message (simple wrap on '\n')
  const char* em = registration_ui.error_message;
  int ey = CARD_Y + 250;
  while (em && *em) {
    const char* nl = strchr(em, '\n');
    if (!nl) {
      vita2d_pgf_draw_text(font, CARD_X + 20, ey, UI_COLOR_TEXT_PRIMARY, 0.9f,
                           em);
      break;
    } else {
      char line[256];
      size_t len = (size_t)(nl - em);
      if (len >= sizeof(line)) len = sizeof(line) - 1;
      memcpy(line, em, len);
      line[len] = '\0';
      vita2d_pgf_draw_text(font, CARD_X + 20, ey, UI_COLOR_TEXT_PRIMARY, 0.9f,
                           line);
      ey += 24;
      em = nl + 1;
    }
  }

  // Navigation hint
  vita2d_pgf_draw_text(font, CARD_X + 20, CARD_Y + 400, UI_COLOR_TEXT_SECONDARY,
                       0.8f, "Press X to retry  Triangle: Cancel");
}

static void render_pin_digit(vita2d_pgf* font, int x, int y, uint32_t digit,
                             bool is_current, bool has_value) {
  // Digit box
  uint32_t box_color = is_current ? UI_COLOR_PRIMARY_BLUE : UI_COLOR_CARD_BG;
  ui_core_render_rounded_rectangle(x, y, PIN_DIGIT_WIDTH, PIN_DIGIT_HEIGHT, 4,
                                   box_color);

  // Digit text
  if (has_value && digit <= 9) {
    char digit_text[2] = {'0' + digit, '\0'};
    int text_x = x + PIN_DIGIT_WIDTH / 2 - 10;
    int text_y = y + PIN_DIGIT_HEIGHT / 2 + 10;
    vita2d_pgf_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, 1.5f,
                         digit_text);
  } else if (is_current && registration_ui.show_cursor) {
    // Cursor
    int cursor_x = x + PIN_DIGIT_WIDTH / 2;
    int cursor_y1 = y + 10;
    int cursor_y2 = y + PIN_DIGIT_HEIGHT - 10;
    vita2d_draw_line(cursor_x, cursor_y1, cursor_x, cursor_y2,
                     UI_COLOR_TEXT_PRIMARY);
  }
}

static void render_progress_bar(int x, int y, int width, int height,
                                float progress) {
  // Background
  if (progress < 0.0f) progress = 0.0f;
  if (progress > 1.0f) progress = 1.0f;
  ui_core_render_rounded_rectangle(x, y, width, height, 2, UI_COLOR_CARD_BG);

  // Progress fill
  int fill_width = (int)(width * progress);
  if (fill_width > 0) {
    ui_core_render_rounded_rectangle(x, y, fill_width, height, 2,
                                     UI_COLOR_PRIMARY_BLUE);
  }
}

static void update_pin_entry_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  // Navigation
  if ((pad->buttons & SCE_CTRL_LEFT) && !(prev_pad->buttons & SCE_CTRL_LEFT)) {
    if (registration_ui.pin_state.current_digit > 0) {
      registration_ui.pin_state.current_digit--;
    }
  }
  if ((pad->buttons & SCE_CTRL_RIGHT) &&
      !(prev_pad->buttons & SCE_CTRL_RIGHT)) {
    if (registration_ui.pin_state.current_digit < PIN_DIGIT_COUNT - 1) {
      registration_ui.pin_state.current_digit++;
    }
  }

  // Digit input
  if ((pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP)) {
    uint32_t* digit = &registration_ui.pin_state
                           .pin_digits[registration_ui.pin_state.current_digit];
    if (*digit > 9)
      *digit = 0;
    else
      *digit = (*digit + 1) % 10;
    ui_registration_set_pin_digit(registration_ui.pin_state.current_digit,
                                  *digit);
  }
  if ((pad->buttons & SCE_CTRL_DOWN) && !(prev_pad->buttons & SCE_CTRL_DOWN)) {
    uint32_t* digit = &registration_ui.pin_state
                           .pin_digits[registration_ui.pin_state.current_digit];
    if (*digit > 9)
      *digit = 9;
    else
      *digit = (*digit + 9) % 10;
    ui_registration_set_pin_digit(registration_ui.pin_state.current_digit,
                                  *digit);
  }

  // Confirm PIN
  if ((pad->buttons & SCE_CTRL_CROSS) &&
      !(prev_pad->buttons & SCE_CTRL_CROSS)) {
    if (ui_registration_is_pin_complete(&registration_ui.pin_state)) {
      ui_registration_submit_pin();
    }
  }

  // Clear current digit
  if ((pad->buttons & SCE_CTRL_SQUARE) &&
      !(prev_pad->buttons & SCE_CTRL_SQUARE)) {
    registration_ui.pin_state
        .pin_digits[registration_ui.pin_state.current_digit] = 10;  // Invalid
    ui_registration_set_pin_digit(registration_ui.pin_state.current_digit, 10);
  }
}

static void update_registration_progress(void) {
  // Real registration progress with PS5 handshake and retry logic
  registration_ui.progress_animation += 0.02f;  // Faster progress updates
  if (registration_ui.progress_animation > 1.0f)
    registration_ui.progress_animation = 1.0f;

  if (registration_ui.progress_animation >= 0.3f &&
      strlen(registration_ui.registration_data.registkey_hex) == 0 &&
      !registration_ui.chiaki_registration_active) {
    // Phase 1: Start PS5 Chiaki registration process
    registration_ui.registration_attempts++;

    snprintf(registration_ui.status_message,
             sizeof(registration_ui.status_message),
             "Attempting PS5 registration (%d/%d)...",
             registration_ui.registration_attempts,
             registration_ui.max_registration_attempts);

    VitaRPS5Result reg_result = perform_ps5_registration(
        registration_ui.config.console_ip,
        registration_ui.pin_state.complete_pin,
        registration_ui.registration_data.np_account_b64,
        &registration_ui.registration_data);

    if (reg_result != VITARPS5_SUCCESS) {
      // Chiaki registration failed to start
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg),
               "Failed to start PS5 registration: %d\nCheck network connection "
               "and PS5 status.",
               reg_result);
      ui_registration_set_error(error_msg);
      return;
    }

    strcpy(registration_ui.status_message, "Communicating with PS5...");
    log_info("Chiaki registration started, waiting for PS5 response");
  }

  // Check if Chiaki registration completed successfully via callback
  if (!registration_ui.registration_saved &&
      registration_ui.progress_animation >= 0.5f &&
      strlen(registration_ui.registration_data.registkey_hex) == 8 &&
      !registration_ui.chiaki_registration_active) {
    log_info("PS5 registration successful - key: %s",
             registration_ui.registration_data.registkey_hex);

    // Store the complete registration data with all credentials via cache API
    // to ensure immediate UI consistency and cache invalidation
    VitaRPS5Result storage_result = registration_cache_add_registration(
        registration_ui.config.console_ip, &registration_ui.registration_data);

    if (storage_result == VITARPS5_SUCCESS) {
      registration_ui.registration_saved = true;
      strcpy(registration_ui.status_message,
             "Registration stored successfully, finalizing...");
      log_info("Registration data stored for console %s",
               registration_ui.config.console_name);

      // RESEARCHER FIX 3: Verify registration can be reloaded after storage
      RegistrationCredentials verify_creds;
      bool verify_result = registration_get_by_ip(
          registration_ui.config.console_ip, &verify_creds);
      if (verify_result && verify_creds.is_valid &&
          strlen(verify_creds.regkey_hex8) == 8) {
        log_info(
            "✅ REGISTRATION VERIFICATION: Successfully reloaded registration "
            "for %s",
            registration_ui.config.console_ip);
        log_info("   Verified regkey: %s", verify_creds.regkey_hex8);
        log_info("   Verified console: %s", verify_creds.console_name);
        log_info("   Is valid: %s", verify_creds.is_valid ? "true" : "false");
      } else {
        log_error(
            "❌ REGISTRATION VERIFICATION FAILED: Cannot reload stored "
            "registration");
        if (!verify_result) {
          log_error("   Failed to find stored registration");
        } else if (!verify_creds.is_valid) {
          log_error("   Stored registration marked as invalid");
        } else if (strlen(verify_creds.regkey_hex8) != 8) {
          log_error("   Invalid registration key length: %zu",
                    strlen(verify_creds.regkey_hex8));
        }
        strcpy(registration_ui.status_message,
               "Registration stored but verification failed");
      }

      // RESEARCHER FIX 2: Add REGSYNC after registration success to update
      // storage cache This ensures dashboard shows the console as registered
      // immediately
      log_info(
          "Running post-registration storage sync to update dashboard "
          "state...");

      // Force reload console data and clear cache for immediate UI reflection
      ui_core_invalidate_registration_cache(registration_ui.config.console_ip);
      ui_dashboard_force_reload();

      log_info(
          "✅ Post-registration sync complete - dashboard should show "
          "REGISTERED");
    } else {
      strcpy(registration_ui.status_message,
             "Registration successful but storage failed");
      log_warn("Failed to store registration data: %d", storage_result);
    }
  }

  if (registration_ui.progress_animation >= 1.0f &&
      strlen(registration_ui.registration_data.registkey_hex) == 8) {
    // Phase 2: Complete the registration process
    set_ui_state(REGISTRATION_UI_STATE_SUCCESS);
    strcpy(registration_ui.status_message,
           "PS5 registration completed successfully!");
    log_info("PS5 registration process completed successfully");

    // Note: Session initialization will be handled separately when user starts
    // streaming Registration and session init are separate phases in the PS5
    // protocol
  }
}

static void handle_registration_completion(void) {
  // Handle completion state input
  // This would transition back to main UI
}

static uint64_t get_timestamp_ms(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick / 1000;
}

static void set_ui_state(RegistrationUIState state) {
  registration_ui.ui_state = state;
  registration_ui.progress_animation = 0.0f;
  if (state == REGISTRATION_UI_STATE_PIN_ENTRY) {
    // Reset flags and clear previous error/status when retrying
    registration_ui.error_message[0] = '\0';
    registration_ui.status_message[0] = '\0';
    registration_ui.registration_saved = false;
    registration_ui.chiaki_registration_active = false;
    registration_ui.chiaki_registration_waiting_for_pin = false;
  }
}

static void update_cursor_animation(void) {
  registration_ui.cursor_blink_timer++;
  if (registration_ui.cursor_blink_timer >= 30) {  // ~0.5 second at 60fps
    registration_ui.show_cursor = !registration_ui.show_cursor;
    registration_ui.cursor_blink_timer = 0;
  }
}

// Chiaki registration callback implementation
static void chiaki_registration_callback(VitaRPS5RegistrationEvent event,
                                         const VitaRPS5RegistrationData* data,
                                         void* user_data) {
  if (!user_data) {
    log_error("Chiaki registration callback called with null user data");
    return;
  }

  log_info("Chiaki registration callback: event=%d", event);

  switch (event) {
    case REGISTRATION_EVENT_PIN_REQUEST:
      log_info("PS5 is requesting PIN - ready for PIN entry");
      registration_ui.chiaki_registration_waiting_for_pin = true;
      break;

    case REGISTRATION_EVENT_SUCCESS:
      log_info("PS5 registration successful!");
      if (data) {
        // Copy the real registration data from PS5
        registration_ui.chiaki_registration_result = *data;

        // Store complete registration credentials from PS5
        // 1) rp_regist_key (16 bytes) is required for session-init; copy
        memcpy(registration_ui.registration_data.rp_key, data->rp_regist_key,
               sizeof(registration_ui.registration_data.rp_key));

        // 2) Morning key (16 bytes) for subsequent encryption
        memcpy(registration_ui.registration_data.morning, data->rp_key,
               sizeof(registration_ui.registration_data.morning));

        // Accept partial RegistKey like chiaki-ng; rp_regist_key may be shorter
        // and zero-padded.

        // 3) Derive the canonical 8-hex registration key for wake/UI and
        // sess/init Robust handling:
        // - If rp_regist_key begins with ASCII hex digits (PS5 often returns
        // 8),
        //   copy the first 8 chars directly as registkey_hex.
        // - Otherwise, hex-encode the first 4 binary bytes to 8 hex.
        {
          char* hex8 = registration_ui.registration_data.registkey_hex;
          const uint8_t* src =
              (const uint8_t*)data->rp_regist_key;  // original Chiaki buffer
          bool ascii_hex_prefix = true;
          for (int i = 0; i < 8; ++i) {
            uint8_t b = src[i];
            bool is_hex = (b >= '0' && b <= '9') || (b >= 'a' && b <= 'f') ||
                          (b >= 'A' && b <= 'F');
            if (!is_hex) {
              ascii_hex_prefix = false;
              break;
            }
          }
          if (ascii_hex_prefix) {
            // Copy first 8 ASCII hex digits as-is
            for (int i = 0; i < 8; ++i) {
              char c = (char)src[i];
              // normalize to lowercase
              if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
              hex8[i] = c;
            }
            hex8[8] = '\0';
            log_info(
                "✅ REGISTRATION SUCCESS: captured 8-hex from RP-RegistKey "
                "ASCII prefix: %s",
                hex8);
          } else {
            // Hex-encode first 4 binary bytes of rp_regist_key (stored in
            // registration_data.rp_key) into 8 hex
            static const char* H = "0123456789abcdef";
            for (int i = 0; i < 4; ++i) {
              uint8_t b = (uint8_t)registration_ui.registration_data.rp_key[i];
              hex8[2 * i] = H[(b >> 4) & 0xF];
              hex8[2 * i + 1] = H[b & 0xF];
            }
            hex8[8] = '\0';
            log_info(
                "✅ REGISTRATION SUCCESS: derived 8-hex from binary "
                "rp_regist_key: %s",
                hex8);
          }
        }

        // 4. Store console name from PS5
        strncpy(registration_ui.registration_data.console_name,
                data->server_nickname,
                sizeof(registration_ui.registration_data.console_name) - 1);
        registration_ui.registration_data.console_name
            [sizeof(registration_ui.registration_data.console_name) - 1] = '\0';

        // 5. Set other registration data
        registration_ui.registration_data.is_registered = true;
        registration_ui.registration_data.is_valid =
            true;  // RESEARCHER FIX: Mark as valid
        registration_ui.registration_data.target = CONSOLE_TARGET_PS5_1;
        registration_ui.registration_data.rp_key_type =
            1;  // Standard PS5 key type

        log_info("Stored complete registration credentials:");
        log_info("  Server nickname: %s", data->server_nickname);
        log_info("  Console PIN: %u", data->console_pin);
        log_info("  Registration key: %s",
                 registration_ui.registration_data.registkey_hex);
        log_debug("  RP regist key: %02X%02X%02X%02X...",
                  data->rp_regist_key[0], data->rp_regist_key[1],
                  data->rp_regist_key[2], data->rp_regist_key[3]);
        log_debug("  RP key (morning): %02X%02X%02X%02X...", data->rp_key[0],
                  data->rp_key[1], data->rp_key[2], data->rp_key[3]);

        // 6. Set wake credential from registration key
        // Use registration key directly as wake credential - no conversion
        // needed
        if (strlen(registration_ui.registration_data.registkey_hex) == 8) {
          strncpy(
              registration_ui.registration_data.wake_credential_dec,
              registration_ui.registration_data.registkey_hex,
              sizeof(registration_ui.registration_data.wake_credential_dec) -
                  1);
          registration_ui.registration_data.wake_credential_dec
              [sizeof(registration_ui.registration_data.wake_credential_dec) -
               1] = '\0';

          log_info("Set wake credential from registration key: %s",
                   registration_ui.registration_data.wake_credential_dec);
          log_info("Using registration key (8-hex) as wake credential");
        } else {
          log_warn(
              "Cannot set wake credential - invalid registration key length");
          registration_ui.registration_data.wake_credential_dec[0] = '\0';
        }

        log_info("Real PS5 registration completed with server: %s",
                 data->server_nickname);

        // Persist registration immediately to avoid UI race conditions
        VitaRPS5Result storage_result = registration_cache_add_registration(
            registration_ui.config.console_ip,
            &registration_ui.registration_data);
        if (storage_result == VITARPS5_SUCCESS) {
          registration_ui.registration_saved = true;
          log_info("Registration persisted successfully");
        } else {
          log_warn("Registration persistence failed: %d", storage_result);
        }

        // Trigger immediate UI success transition
        log_info(
            "Registration callback complete - transitioning to SUCCESS UI");
        set_ui_state(REGISTRATION_UI_STATE_SUCCESS);
        strcpy(registration_ui.status_message,
               "PS5 registration completed successfully!");
      } else {
        log_error("Registration success event received but no data provided");
        ui_registration_set_error(
            "Registration succeeded but no credentials received");
        return;
      }

      registration_ui.chiaki_registration_active = false;
      registration_ui.chiaki_registration_waiting_for_pin = false;
      break;

    case REGISTRATION_EVENT_FAILED:
      log_error("PS5 registration failed");
      registration_ui.chiaki_registration_active = false;
      registration_ui.chiaki_registration_waiting_for_pin = false;

      // RESEARCHER FIX 4: Provide specific error messages for registration
      // failure Enhanced error messaging to help user understand what went
      // wrong
      char error_msg[256];
      if (registration_ui.registration_attempts >=
          registration_ui.max_registration_attempts) {
        snprintf(error_msg, sizeof(error_msg),
                 "Registration failed after %d attempts. Please check:\n"
                 "• PIN is correct and not expired\n"
                 "• PS5 Remote Play is enabled\n"
                 "• Network connection is stable",
                 registration_ui.registration_attempts);
      } else {
        snprintf(error_msg, sizeof(error_msg),
                 "PS5 registration failed (attempt %d/%d).\n"
                 "Check PIN and network connection.",
                 registration_ui.registration_attempts,
                 registration_ui.max_registration_attempts);
      }

      ui_registration_set_error(error_msg);
      break;

    case REGISTRATION_EVENT_CANCELLED:
      log_info("PS5 registration cancelled");
      registration_ui.chiaki_registration_active = false;
      registration_ui.chiaki_registration_waiting_for_pin = false;
      break;

    default:
      log_warn("Unknown Chiaki registration event: %d", event);
      break;
  }
}
