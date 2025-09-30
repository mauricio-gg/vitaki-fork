#include "ui_core.h"

#include <math.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/console_registration.h"
#include "../core/console_storage.h"
#include "../core/registration_cache.h"
#include "../core/vitarps5.h"
#include "../discovery/ps5_discovery.h"
#include "../system/vita_system_info.h"
#include "../utils/logger.h"
#include "ui_components.h"
#include "ui_controller.h"
#include "ui_dashboard.h"
#include "ui_navigation.h"
#include "ui_profile.h"
#include "ui_psn_login.h"
#include "ui_registration.h"
#include "ui_settings.h"
#include "ui_streaming.h"
#include "vita2d_ui.h"

// TEMPORARY ERROR STRING FUNCTION FOR SOCKET TEST BUILD
static const char* error_string_placeholder(int error) { return "Error"; }

// Core UI state
static UIState current_state = UI_STATE_MAIN_DASHBOARD;
static vita2d_pgf* font = NULL;
static UIAssets ui_assets = {0};
static SceCtrlData prev_pad = {0};

// Animation timing
static float animation_time = 0.0f;

// Input modal/focus system - prevents navigation interference during focused UI
// interactions
static bool input_modal_active = false;

// IME Dialog state
static int ime_dialog_running = 0;
static int ime_input_field = -1;
static uint16_t ime_title_utf16[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t ime_initial_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t ime_input_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint8_t ime_input_text_utf8[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

// Console discovery state
static PS5Discovery* discovery_instance = NULL;
static PS5DiscoveryResults discovery_results = {0};
static bool discovery_active = false;
static float discovery_progress = 0.0f;

// RESEARCHER CLEANUP: PSN ID persistence across discovery lifecycles
static char saved_psn_id_base64[32] = {0};
static uint32_t selected_discovery_index = 0;

// Simulated discovery state
static bool simulated_mode = false;
static uint64_t discovery_start_time = 0;
static const uint32_t DISCOVERY_DURATION_MS = 10000;  // 10 seconds

// Old registration cache implementation removed - now using unified API in
// registration_cache.c

// Forward declarations
static int load_ui_assets(void);
static void cleanup_ui_assets(void);
static int ime_dialog_init(void);
static void ime_dialog_cleanup(void);
static int ime_dialog_update(void);
static void utf16_to_utf8(const uint16_t* utf16_str, uint8_t* utf8_str);
static void utf8_to_utf16(uint16_t* utf16_str, const uint8_t* utf8_str);

// Old registration cache functions removed - now using unified API

// Discovery functions
static VitaRPS5Result start_console_discovery(void);
static void stop_console_discovery(void);
static void on_console_found(const PS5ConsoleInfo* console, void* user_data);
static void update_console_cache_state(const PS5ConsoleInfo* console);
static void on_discovery_complete(const PS5DiscoveryResults* results,
                                  void* user_data);
static void render_discovery_ui(void);
static void handle_discovery_input(SceCtrlData* pad, SceCtrlData* prev_pad);
static void start_simulated_discovery(void);
static void update_discovery_progress(void);

// High-level streaming functions
VitaRPS5Result ui_core_start_streaming(const char* console_ip,
                                       uint8_t console_version) {
  if (!console_ip) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Starting Remote Play to %s (version %d)", console_ip,
           console_version);

  // Start streaming session
  VitaRPS5Result result = ui_streaming_start(console_ip, console_version);
  if (result != VITARPS5_SUCCESS) {
    // Check if this is an authentication error for PS5 console
    if (result == VITARPS5_ERROR_NOT_AUTHENTICATED && console_version == 12) {
      log_info(
          "PS5 authentication required - starting registration flow for %s",
          console_ip);

      // Start registration UI for this console
      VitaRPS5Result reg_result =
          ui_core_start_registration(console_ip, console_version);
      if (reg_result == VITARPS5_SUCCESS) {
        // Successfully started registration flow
        current_state = UI_STATE_REGISTRATION;
        return VITARPS5_SUCCESS;
      } else {
        log_error("Failed to start registration flow: %s",
                  error_string_placeholder(reg_result));
        return reg_result;
      }
    } else {
      log_error("Failed to start streaming: %s",
                error_string_placeholder(result));
      return result;
    }
  }

  // Switch to streaming UI
  current_state = UI_STATE_STREAMING;
  return VITARPS5_SUCCESS;
}

VitaRPS5Result ui_core_start_registration(const char* console_ip,
                                          uint8_t console_version) {
  if (!console_ip) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Starting PS5 registration flow for %s (version %d)", console_ip,
           console_version);

  // Prepare registration UI configuration (simplified for clean slate)
  RegistrationUIConfig reg_config = {0};
  strncpy(reg_config.console_ip, console_ip, sizeof(reg_config.console_ip) - 1);

  // Set console name based on type
  if (console_version == 12) {
    strncpy(reg_config.console_name, "PlayStation 5",
            sizeof(reg_config.console_name) - 1);
  } else {
    strncpy(reg_config.console_name, "PlayStation 4",
            sizeof(reg_config.console_name) - 1);
  }

  // Get PSN account ID from profile system (both formats)
  reg_config.psn_account_id = ui_profile_get_psn_account_number();
  ui_profile_get_psn_id_base64(reg_config.psn_account_id_base64,
                               sizeof(reg_config.psn_account_id_base64));

  // Start registration UI
  VitaRPS5Result result = ui_registration_start(&reg_config);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to start registration UI: %s",
              error_string_placeholder(result));
    return result;
  }

  log_info("Registration flow started successfully for %s", console_ip);
  return VITARPS5_SUCCESS;
}

// Discovery PSN ID interface
VitaRPS5Result ui_core_set_discovery_psn_id(const char* psn_id_base64) {
  if (!psn_id_base64) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("Setting PSN ID for discovery system: %s", psn_id_base64);

  // RESEARCHER CLEANUP: Save PSN ID for persistence across discovery lifecycles
  strncpy(saved_psn_id_base64, psn_id_base64, sizeof(saved_psn_id_base64) - 1);
  saved_psn_id_base64[sizeof(saved_psn_id_base64) - 1] = '\0';

  // If discovery instance exists, update it immediately with the new PSN ID
  if (discovery_instance) {
    VitaRPS5Result result =
        ps5_discovery_set_psn_account(discovery_instance, psn_id_base64);
    if (result == VITARPS5_SUCCESS) {
      log_info("✅ Discovery PSN ID updated immediately on live instance");
    } else {
      log_error("Failed to update discovery PSN ID: %s",
                error_string_placeholder(result));
    }
    return result;
  } else {
    log_info(
        "✅ PSN ID saved - will be applied immediately to next discovery "
        "instance");
    return VITARPS5_SUCCESS;
  }
}

// Network validation function
static bool validate_network_connectivity(void) {
  log_info("Validating network connectivity...");

  // TEMPORARILY DISABLED FOR SOCKET TEST BUILD
  // Check if network is initialized
  // SceNetCtlInfo info;
  // int ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
  // if (ret < 0) {
  //   log_warning("No network connection available (error: 0x%08X)", ret);
  //   return false;
  // }
  // log_info("Network connection found - IP: %s", info.ip_address);
  log_info("Network connectivity assumed for socket test build");
  return true;
}

VitaRPS5Result ui_core_start_discovery(void) {
  log_info("=== UI_CORE_START_DISCOVERY CALLED ===");
  log_info("Current UI state: %d", current_state);

  // Check network connectivity first
  if (!validate_network_connectivity()) {
    log_error("Network validation failed - cannot start discovery");
    // TODO: Show error message to user
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("Network validation passed, switching to discovery state...");

  // Switch to discovery state
  current_state = UI_STATE_ADD_CONNECTION;
  log_info("UI state changed to: %d (ADD_CONNECTION)", current_state);

  // Start the discovery scan
  log_info("Calling start_console_discovery()...");
  VitaRPS5Result result = start_console_discovery();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to start discovery: %s",
              error_string_placeholder(result));
    log_info("Reverting to dashboard state due to error");
    current_state = UI_STATE_MAIN_DASHBOARD;
    return result;
  }

  log_info(
      "Discovery started successfully, UI should now show discovery screen");
  return VITARPS5_SUCCESS;
}

bool ui_core_is_discovery_active(void) { return discovery_active; }

PS5Discovery* ui_get_discovery_instance(void) { return discovery_instance; }

int ui_core_init(void) {
  log_info("Initializing core UI system...");

  // Initialize touch system
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, 1);

  // Load default font
  font = vita2d_load_default_pgf();
  if (!font) {
    log_error("Failed to load default font");
    return -1;
  }

  // Load UI assets
  if (load_ui_assets() < 0) {
    log_error("Failed to load UI assets");
    return -1;
  }

  // Initialize IME Dialog system
  if (ime_dialog_init() < 0) {
    log_warning("IME Dialog initialization failed - text input may not work");
  }

  // CRITICAL FIX: Initialize console registration BEFORE dashboard
  // This ensures registrations are loaded before console storage cleanup
  log_info("Initializing console registration subsystem early...");
  VitaRPS5Result reg_init_result = console_registration_init();
  if (reg_init_result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize console registration: %s",
              vitarps5_result_string(reg_init_result));
    // Continue anyway - registration may not be critical for all operations
  }

  // Initialize subsystems
  ui_navigation_init();
  ui_components_init();
  ui_dashboard_init();
  ui_settings_init();
  ui_profile_init();
  ui_controller_init();

  // Initialize streaming subsystem with error checking
  VitaRPS5Result streaming_result = ui_streaming_init();
  if (streaming_result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize streaming UI: %s",
              vitarps5_result_string(streaming_result));
    return -1;
  }

  // Initialize registration subsystem with error checking
  VitaRPS5Result registration_result = ui_registration_init();
  if (registration_result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize registration UI: %s",
              vitarps5_result_string(registration_result));
    return -1;
  }

  // Initialize unified registration cache to prevent excessive filesystem reads
  VitaRPS5Result cache_result = registration_cache_init();
  if (cache_result != VITARPS5_SUCCESS) {
    log_warning("Failed to initialize unified registration cache: %s",
                vitarps5_result_string(cache_result));
    // Continue anyway - cache is not critical for basic functionality
  }

  log_info("Core UI system initialized successfully");
  return 0;
}

void ui_core_cleanup(void) {
  log_info("Cleaning up core UI system...");

  // Cleanup discovery if active
  if (discovery_active) {
    stop_console_discovery();
  }

  // Cleanup subsystems
  ui_streaming_cleanup();
  ui_registration_cleanup();
  ui_controller_cleanup();
  ui_profile_cleanup();
  ui_settings_cleanup();
  ui_dashboard_cleanup();
  ui_navigation_cleanup();

  // Cleanup unified registration cache
  registration_cache_cleanup();

  // Cleanup IME Dialog system
  ime_dialog_cleanup();

  // Cleanup assets
  cleanup_ui_assets();

  if (font) {
    vita2d_free_pgf(font);
    font = NULL;
  }
}

void ui_core_update(SceCtrlData* pad) {
  if (!pad) return;

  // Update animation time
  animation_time += ANIMATION_SPEED;
  if (animation_time > 2.0f * M_PI) animation_time -= 2.0f * M_PI;

  // Update subsystems
  ui_navigation_update(ANIMATION_SPEED);
  ui_components_update_animations(ANIMATION_SPEED);
  ui_streaming_update();
  ui_registration_update();

  // Handle IME Dialog if active
  if (ime_dialog_running) {
    int result = ime_dialog_update();
    if (result == 1) {
      // Input completed - notify appropriate handler for processing
      if (current_state == UI_STATE_PROFILE) {
        ui_profile_handle_ime_result(ime_input_field,
                                     (const char*)ime_input_text_utf8);
      } else if (current_state == UI_STATE_PSN_LOGIN) {
        ui_psn_login_handle_ime_result(ime_input_field,
                                       (const char*)ime_input_text_utf8);
      }
      ime_input_field = -1;
    } else if (result == -1) {
      log_info("IME Dialog canceled");
    }
    goto update_frame_count;
  }

  // Handle navigation input (only when not in modal/focused mode)
  if (!input_modal_active) {
    ui_navigation_handle_input(pad, &prev_pad);
    ui_navigation_handle_touch();
  }

  // Handle input based on current state
  switch (current_state) {
    case UI_STATE_MAIN_DASHBOARD:
      input_modal_active = false;  // Normal navigation
      ui_dashboard_handle_input(pad, &prev_pad);
      break;

    case UI_STATE_PROFILE:
      input_modal_active = false;  // Normal navigation
      ui_profile_handle_input(pad, &prev_pad);
      break;

    case UI_STATE_CONTROLLER_MAPPING:
      input_modal_active = false;  // Normal navigation
      ui_controller_handle_input(pad, &prev_pad);
      break;

    case UI_STATE_SETTINGS:
      input_modal_active = false;  // Normal navigation
      ui_settings_handle_input(pad, &prev_pad);
      break;

    case UI_STATE_PSN_LOGIN:
      input_modal_active = true;  // PSN login needs focused input
      ui_psn_login_handle_input(pad, &prev_pad);
      break;

    case UI_STATE_REGISTRATION:
      input_modal_active =
          ui_registration_is_active();  // Auto-manage modal for registration
      ui_registration_handle_input(pad, &prev_pad);
      // Check if registration completed and should return to dashboard
      if (!ui_registration_is_active()) {
        current_state = UI_STATE_MAIN_DASHBOARD;
      }
      break;

    case UI_STATE_STREAMING:
      input_modal_active = false;  // Normal navigation during streaming
      ui_streaming_handle_input(pad, &prev_pad);
      break;

    case UI_STATE_ADD_CONNECTION:
      input_modal_active = false;  // Normal navigation during discovery
      handle_discovery_input(pad, &prev_pad);
      break;

    default:
      input_modal_active = false;  // Normal navigation for unimplemented states
      // Default back navigation for unimplemented states
      if ((pad->buttons & SCE_CTRL_CIRCLE) &&
          !(prev_pad.buttons & SCE_CTRL_CIRCLE)) {
        current_state = UI_STATE_MAIN_DASHBOARD;
      }
      break;
  }

update_frame_count:
  prev_pad = *pad;
}

void ui_core_render(void) {
  if (!font) return;

  // Don't render UI if IME Dialog is active
  if (ime_dialog_running) {
    return;
  }

  // Always render background and navigation first
  ui_core_render_background();
  ui_navigation_render_particles();

  // Debug current state rendering
  static UIState last_logged_state = -1;
  if (current_state != last_logged_state) {
    log_info("=== RENDER STATE CHANGE: %d ===", current_state);
    last_logged_state = current_state;
  }

  // Render based on current state
  switch (current_state) {
    case UI_STATE_MAIN_DASHBOARD:
      ui_navigation_render();
      ui_dashboard_render();
      break;

    case UI_STATE_PROFILE:
      ui_navigation_render();
      ui_profile_render();
      break;

    case UI_STATE_CONTROLLER_MAPPING:
      ui_navigation_render();
      ui_controller_render();
      break;

    case UI_STATE_SETTINGS:
      ui_navigation_render();
      ui_settings_render();
      break;

    case UI_STATE_PSN_LOGIN:
      ui_psn_login_render();
      break;

    case UI_STATE_REGISTRATION:
      // PS5 console registration interface
      ui_registration_render();
      break;

    case UI_STATE_STREAMING:
      // Full Remote Play streaming interface
      ui_streaming_render();
      break;

    case UI_STATE_ADD_CONNECTION:
      // Console discovery interface
      ui_navigation_render();
      render_discovery_ui();
      break;

    default:
      vita2d_pgf_draw_text(font, 50, 150, UI_COLOR_TEXT_PRIMARY, 1.2f,
                           "State not implemented");
      vita2d_pgf_draw_text(font, 50, 520, UI_COLOR_TEXT_TERTIARY, 0.8f,
                           "Press CIRCLE to go back");
      break;
  }
}

// Asset management
UIAssets* ui_core_get_assets(void) { return &ui_assets; }

vita2d_pgf* ui_core_get_font(void) { return font; }

// State management
UIState ui_core_get_state(void) { return current_state; }

void ui_core_set_state(UIState new_state) {
  UIState old_state = current_state;

  // Convert state enums to readable strings for logging
  const char* state_names[] = {
      "MAIN_DASHBOARD",      // 0
      "PROFILE",             // 1
      "CONTROLLER_MAPPING",  // 2
      "SETTINGS",            // 3
      "ADD_CONNECTION",      // 4
      "PSN_LOGIN",           // 5
      "CONSOLE_PAIRING",     // 6
      "REGISTRATION",        // 7
      "STREAMING",           // 8
      "CONNECTION_DETAILS"   // 9
  };

  const char* old_name = (old_state < 10) ? state_names[old_state] : "UNKNOWN";
  const char* new_name = (new_state < 10) ? state_names[new_state] : "UNKNOWN";

  log_info("UI_CORE_SET_STATE: %s (%d) -> %s (%d)", old_name, old_state,
           new_name, new_state);

  // Special handling for critical transitions
  if (old_state == UI_STATE_STREAMING && new_state == UI_STATE_MAIN_DASHBOARD) {
    log_info(
        "UI_CORE_SET_STATE: *** CRITICAL - STREAMING TO DASHBOARD TRANSITION "
        "***");

    // Clear input modal state to prevent navigation blocking
    input_modal_active = false;
    log_info(
        "UI_CORE_SET_STATE: Cleared input_modal_active for dashboard "
        "transition");
  }

  // Validate state transition
  if (new_state >= 10) {
    log_error("UI_CORE_SET_STATE: Invalid state %d, ignoring transition",
              new_state);
    return;
  }

  // Perform state transition
  current_state = new_state;

  // Verify transition was successful
  if (current_state != new_state) {
    log_error(
        "UI_CORE_SET_STATE: State transition verification failed! Expected %d, "
        "got %d",
        new_state, current_state);
  } else {
    log_info("UI_CORE_SET_STATE: State transition verified successfully");
  }
}

// Shared utilities
void ui_core_render_background(void) {
  if (ui_assets.background) {
    vita2d_draw_texture(ui_assets.background, 0, 0);
  } else {
    vita2d_draw_rectangle(0, 0, 960, 544, UI_COLOR_BACKGROUND);
  }
}

void ui_core_render_logo(void) {
  if (ui_assets.vita_rps5_logo) {
    float scale = 0.15f;
    int padding = 15;
    int logo_width = vita2d_texture_get_width(ui_assets.vita_rps5_logo);
    int x = 960 - (logo_width * scale) - padding;
    int y = padding;
    vita2d_draw_texture_scale(ui_assets.vita_rps5_logo, x, y, scale, scale);
  }
}

void ui_core_render_rounded_rectangle(int x, int y, int width, int height,
                                      int radius, uint32_t color) {
  // Main rectangle body
  vita2d_draw_rectangle(x + radius, y, width - 2 * radius, height, color);
  vita2d_draw_rectangle(x, y + radius, width, height - 2 * radius, color);

  // Corner circles (simplified as small rectangles for performance)
  for (int i = 0; i < radius; i++) {
    for (int j = 0; j < radius; j++) {
      if (i * i + j * j <= radius * radius) {
        // Top-left corner
        vita2d_draw_rectangle(x + radius - i, y + radius - j, 1, 1, color);
        // Top-right corner
        vita2d_draw_rectangle(x + width - radius + i - 1, y + radius - j, 1, 1,
                              color);
        // Bottom-left corner
        vita2d_draw_rectangle(x + radius - i, y + height - radius + j - 1, 1, 1,
                              color);
        // Bottom-right corner
        vita2d_draw_rectangle(x + width - radius + i - 1,
                              y + height - radius + j - 1, 1, 1, color);
      }
    }
  }
}

void ui_core_render_card_with_shadow(int x, int y, int width, int height,
                                     int radius, uint32_t color) {
  // Render shadow first (offset by a few pixels)
  int shadow_offset = 4;
  uint32_t shadow_color = RGBA8(0, 0, 0, 60);  // Semi-transparent black
  ui_core_render_rounded_rectangle(x + shadow_offset, y + shadow_offset, width,
                                   height, radius, shadow_color);

  // Render the actual card on top
  ui_core_render_rounded_rectangle(x, y, width, height, radius, color);
}

void ui_core_render_status(int x, int y, StatusType type, const char* text,
                           float font_scale) {
  vita2d_pgf* font = ui_core_get_font();
  if (!font || !text) return;

  // Determine color based on status type
  uint32_t color;
  switch (type) {
    case STATUS_TYPE_AVAILABLE:
      color = UI_COLOR_STATUS_AVAILABLE;
      break;
    case STATUS_TYPE_UNAVAILABLE:
      color = UI_COLOR_STATUS_UNAVAILABLE;
      break;
    case STATUS_TYPE_CONNECTING:
      color = UI_COLOR_STATUS_CONNECTING;
      break;
    default:
      color = UI_COLOR_TEXT_SECONDARY;
      break;
  }

  // Render status with bullet point and text in one line
  // Using UTF-8 bullet character ● (U+25CF)
  char status_text[256];
  snprintf(status_text, sizeof(status_text), "● %s", text);
  vita2d_pgf_draw_text(font, x, y, color, font_scale, status_text);
}

// IME Dialog functions
bool ui_core_is_ime_active(void) { return ime_dialog_running; }

int ui_core_ime_open(int field, const char* title, const char* initial_text,
                     bool is_password) {
  if (ime_dialog_running) {
    log_warning("IME Dialog already active");
    return -1;
  }

  log_info("Opening IME Dialog: %s (field: %d, password: %s)",
           title ? title : "NULL", field, is_password ? "yes" : "no");

  // Convert title to UTF-16
  memset(ime_title_utf16, 0, sizeof(ime_title_utf16));
  if (title) {
    utf8_to_utf16(ime_title_utf16, (uint8_t*)title);
  }

  // Convert initial text to UTF-16
  memset(ime_initial_text_utf16, 0, sizeof(ime_initial_text_utf16));
  memset(ime_input_text_utf16, 0, sizeof(ime_input_text_utf16));
  if (initial_text && strlen(initial_text) > 0) {
    utf8_to_utf16(ime_initial_text_utf16, (uint8_t*)initial_text);
    for (int i = 0; ime_initial_text_utf16[i]; i++) {
      ime_input_text_utf16[i] = ime_initial_text_utf16[i];
    }
  }

  SceImeDialogParam param;
  sceImeDialogParamInit(&param);

  param.supportedLanguages = 0x0001FFFF;
  param.languagesForced = SCE_TRUE;
  param.type = SCE_IME_DIALOG_DIALOG_MODE_DEFAULT;
  param.option = 0;
  param.textBoxMode = is_password ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD
                                  : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;

  if (title) {
    param.title = ime_title_utf16;
  }

  if (initial_text && strlen(initial_text) > 0) {
    param.initialText = ime_initial_text_utf16;
  }

  param.maxTextLength = (field == 2) ? 8 : 64;
  param.inputTextBuffer = ime_input_text_utf16;

  int result = sceImeDialogInit(&param);
  if (result < 0) {
    log_error("Failed to open IME Dialog: 0x%08X", result);
    return -1;
  }

  ime_dialog_running = 1;
  ime_input_field = field;

  log_info("IME Dialog opened successfully");
  return 0;
}

// IME result accessor functions
int ui_core_get_ime_field(void) { return ime_input_field; }

const char* ui_core_get_ime_text(void) {
  return (const char*)ime_input_text_utf8;
}

void ui_core_clear_ime_result(void) {
  ime_input_field = -1;
  memset(ime_input_text_utf8, 0, sizeof(ime_input_text_utf8));
}

// Private functions
static int load_ui_assets(void) {
  log_info("Loading UI assets...");

  // Initialize all assets to NULL
  memset(&ui_assets, 0, sizeof(UIAssets));

  // Load background and waves
  ui_assets.background = vita2d_load_PNG_file("app0:assets/background.png");
  ui_assets.wave_top = vita2d_load_PNG_file("app0:assets/wave_top.png");
  ui_assets.wave_bottom = vita2d_load_PNG_file("app0:assets/wave_bottom.png");

  // Load PlayStation symbol particles
  ui_assets.symbol_triangle =
      vita2d_load_PNG_file("app0:assets/symbol_triangle.png");
  ui_assets.symbol_circle =
      vita2d_load_PNG_file("app0:assets/symbol_circle.png");
  ui_assets.symbol_ex = vita2d_load_PNG_file("app0:assets/symbol_ex.png");
  ui_assets.symbol_square =
      vita2d_load_PNG_file("app0:assets/symbol_square.png");

  // Load status ellipses
  ui_assets.ellipse_green =
      vita2d_load_PNG_file("app0:assets/ellipse_green.png");
  ui_assets.ellipse_red = vita2d_load_PNG_file("app0:assets/ellipse_red.png");
  ui_assets.ellipse_yellow =
      vita2d_load_PNG_file("app0:assets/ellipse_yellow.png");

  // Load buttons and cards
  ui_assets.button_add_new =
      vita2d_load_PNG_file("app0:assets/button_add_new.png");
  ui_assets.charcoal_button =
      vita2d_load_PNG_file("app0:assets/charcoal_button.png");
  ui_assets.console_card = vita2d_load_PNG_file("app0:assets/console_card.png");

  // Load logos and branding
  ui_assets.ps5_logo = vita2d_load_PNG_file("app0:assets/PS5_logo.png");
  ui_assets.vita_rps5_logo =
      vita2d_load_PNG_file("app0:assets/Vita_RPS5_Logo.png");

  // Load navigation icons
  ui_assets.profile_icon =
      vita2d_load_PNG_file("app0:assets/profile_white.png");
  ui_assets.controller_icon =
      vita2d_load_PNG_file("app0:assets/controller_white.png");
  ui_assets.settings_icon =
      vita2d_load_PNG_file("app0:assets/settings_white.png");
  ui_assets.play_icon = vita2d_load_PNG_file("app0:assets/symbol_triangle.png");

  // Load controller diagrams
  ui_assets.vita_front = vita2d_load_PNG_file("app0:assets/Vita_Front.png");
  ui_assets.vita_back = vita2d_load_PNG_file("app0:assets/Vita_Back.png");

  // Try to load user profile image from system
  ui_assets.user_profile_image = NULL;
  ui_assets.has_user_profile_image = false;

  if (vita_system_info_has_profile_image()) {
    char profile_image_path[256];
    if (vita_system_info_get_profile_image_path(profile_image_path,
                                                sizeof(profile_image_path)) ==
        VITARPS5_SUCCESS) {
      log_info("Attempting to load user profile image from: %s",
               profile_image_path);

      // Try to load the profile image
      ui_assets.user_profile_image = vita2d_load_PNG_file(profile_image_path);
      if (ui_assets.user_profile_image) {
        ui_assets.has_user_profile_image = true;
        log_info("Successfully loaded user profile image");
      } else {
        log_warning("Failed to load user profile image from %s",
                    profile_image_path);
      }
    }
  }

  log_info("UI assets loaded successfully");
  return 0;
}

static void cleanup_ui_assets(void) {
  if (ui_assets.background) vita2d_free_texture(ui_assets.background);
  if (ui_assets.wave_top) vita2d_free_texture(ui_assets.wave_top);
  if (ui_assets.wave_bottom) vita2d_free_texture(ui_assets.wave_bottom);

  if (ui_assets.symbol_triangle) vita2d_free_texture(ui_assets.symbol_triangle);
  if (ui_assets.symbol_circle) vita2d_free_texture(ui_assets.symbol_circle);
  if (ui_assets.symbol_ex) vita2d_free_texture(ui_assets.symbol_ex);
  if (ui_assets.symbol_square) vita2d_free_texture(ui_assets.symbol_square);

  if (ui_assets.ellipse_green) vita2d_free_texture(ui_assets.ellipse_green);
  if (ui_assets.ellipse_red) vita2d_free_texture(ui_assets.ellipse_red);
  if (ui_assets.ellipse_yellow) vita2d_free_texture(ui_assets.ellipse_yellow);

  if (ui_assets.button_add_new) vita2d_free_texture(ui_assets.button_add_new);
  if (ui_assets.charcoal_button) vita2d_free_texture(ui_assets.charcoal_button);
  if (ui_assets.console_card) vita2d_free_texture(ui_assets.console_card);

  if (ui_assets.ps5_logo) vita2d_free_texture(ui_assets.ps5_logo);
  if (ui_assets.vita_rps5_logo) vita2d_free_texture(ui_assets.vita_rps5_logo);

  if (ui_assets.profile_icon) vita2d_free_texture(ui_assets.profile_icon);
  if (ui_assets.controller_icon) vita2d_free_texture(ui_assets.controller_icon);
  if (ui_assets.settings_icon) vita2d_free_texture(ui_assets.settings_icon);
  if (ui_assets.play_icon) vita2d_free_texture(ui_assets.play_icon);

  if (ui_assets.vita_front) vita2d_free_texture(ui_assets.vita_front);
  if (ui_assets.vita_back) vita2d_free_texture(ui_assets.vita_back);
  if (ui_assets.ps5_controller) vita2d_free_texture(ui_assets.ps5_controller);

  if (ui_assets.user_profile_image)
    vita2d_free_texture(ui_assets.user_profile_image);

  memset(&ui_assets, 0, sizeof(ui_assets));
}

static int ime_dialog_init(void) {
  log_info("Initializing IME Dialog system");
  ime_dialog_running = 0;
  log_info("IME Dialog system initialized");
  return 0;
}

static void ime_dialog_cleanup(void) {
  log_info("Cleaning up IME Dialog system");
  if (ime_dialog_running) {
    sceImeDialogTerm();
    ime_dialog_running = 0;
  }
}

static int ime_dialog_update(void) {
  if (!ime_dialog_running) {
    return 0;
  }

  SceCommonDialogStatus status = sceImeDialogGetStatus();

  if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
    SceImeDialogResult result;
    memset(&result, 0, sizeof(SceImeDialogResult));
    sceImeDialogGetResult(&result);

    if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
      utf16_to_utf8(ime_input_text_utf16, ime_input_text_utf8);
      log_info("IME Dialog completed with text: '%s'", ime_input_text_utf8);

      sceImeDialogTerm();
      ime_dialog_running = 0;

      return 1;
    } else {
      log_info("IME Dialog canceled");

      sceImeDialogTerm();
      ime_dialog_running = 0;
      ime_input_field = -1;

      return -1;
    }
  }

  return 0;
}

static void utf16_to_utf8(const uint16_t* utf16_str, uint8_t* utf8_str) {
  size_t utf8_pos = 0;
  size_t utf16_pos = 0;

  while (utf16_str[utf16_pos] != 0 &&
         utf8_pos < SCE_IME_DIALOG_MAX_TEXT_LENGTH - 1) {
    uint16_t c = utf16_str[utf16_pos];

    if (c < 0x80) {
      utf8_str[utf8_pos++] = (uint8_t)c;
    } else if (c < 0x800) {
      if (utf8_pos + 1 < SCE_IME_DIALOG_MAX_TEXT_LENGTH - 1) {
        utf8_str[utf8_pos++] = 0xC0 | (c >> 6);
        utf8_str[utf8_pos++] = 0x80 | (c & 0x3F);
      }
    } else {
      if (utf8_pos + 2 < SCE_IME_DIALOG_MAX_TEXT_LENGTH - 1) {
        utf8_str[utf8_pos++] = 0xE0 | (c >> 12);
        utf8_str[utf8_pos++] = 0x80 | ((c >> 6) & 0x3F);
        utf8_str[utf8_pos++] = 0x80 | (c & 0x3F);
      }
    }
    utf16_pos++;
  }

  utf8_str[utf8_pos] = '\0';
}

static void utf8_to_utf16(uint16_t* utf16_str, const uint8_t* utf8_str) {
  size_t utf8_pos = 0;
  size_t utf16_pos = 0;

  while (utf8_str[utf8_pos] != 0 &&
         utf16_pos < SCE_IME_DIALOG_MAX_TEXT_LENGTH - 1) {
    uint8_t c = utf8_str[utf8_pos];

    if (c < 0x80) {
      utf16_str[utf16_pos++] = (uint16_t)c;
      utf8_pos++;
    } else if ((c & 0xE0) == 0xC0) {
      if (utf8_str[utf8_pos + 1] != 0) {
        uint16_t result = ((c & 0x1F) << 6) | (utf8_str[utf8_pos + 1] & 0x3F);
        utf16_str[utf16_pos++] = result;
        utf8_pos += 2;
      } else {
        break;
      }
    } else if ((c & 0xF0) == 0xE0) {
      if (utf8_str[utf8_pos + 1] != 0 && utf8_str[utf8_pos + 2] != 0) {
        uint16_t result = ((c & 0x0F) << 12) |
                          ((utf8_str[utf8_pos + 1] & 0x3F) << 6) |
                          (utf8_str[utf8_pos + 2] & 0x3F);
        utf16_str[utf16_pos++] = result;
        utf8_pos += 3;
      } else {
        break;
      }
    } else {
      utf8_pos++;
    }
  }

  utf16_str[utf16_pos] = 0;
}

// Console Discovery Implementation

static VitaRPS5Result start_console_discovery(void) {
  log_info("=== START_CONSOLE_DISCOVERY CALLED ===");
  log_info("Resetting discovery state...");

  // Reset discovery state
  memset(&discovery_results, 0, sizeof(discovery_results));
  discovery_active = true;
  discovery_progress = 0.0f;
  selected_discovery_index = 0;

  // Track discovery start time for progress calculation
  discovery_start_time =
      sceKernelGetSystemTimeWide() / 1000;  // Convert to milliseconds

  log_info("Discovery state reset - active: %s, progress: %.2f",
           discovery_active ? "true" : "false", discovery_progress);

  // Initialize PS5 discovery system
  log_info("Initializing PS5 discovery system...");
  VitaRPS5Result result = ps5_discovery_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize PS5 discovery: %s",
              error_string_placeholder(result));
    discovery_active = false;
    return result;
  }
  log_info("PS5 discovery system initialized successfully");

  // Configure discovery settings
  PS5DiscoveryConfig config = {
      .scan_timeout_ms =
          PS5_DISCOVERY_TIMEOUT_MS,  // Use 5-second constant from header
      .scan_interval_ms = 1000,      // Send discovery packet every second
      .enable_wake_on_lan = true,
      .filter_local_network_only = true,
      .console_found_callback = on_console_found,
      .discovery_complete_callback = on_discovery_complete,
      .user_data = NULL};

  // Create discovery instance
  result = ps5_discovery_create(&config, &discovery_instance);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create discovery instance: %s",
              error_string_placeholder(result));
    ps5_discovery_cleanup();
    return result;
  }

  // RESEARCHER CLEANUP: Apply PSN ID immediately when discovery instance is
  // created First try the cached PSN ID from ui_core_set_discovery_psn_id()
  const char* psn_id_to_use = NULL;
  if (strlen(saved_psn_id_base64) > 0) {
    psn_id_to_use = saved_psn_id_base64;
    log_info("Applying cached PSN ID to new discovery instance: %s",
             psn_id_to_use);
  } else if (ui_profile_get_psn_id_valid()) {
    // Fallback to profile PSN ID
    char psn_id_base64[32];
    ui_profile_get_psn_id_base64(psn_id_base64, sizeof(psn_id_base64));
    if (strlen(psn_id_base64) > 0) {
      psn_id_to_use = psn_id_base64;
      log_info("Applying profile PSN ID to new discovery instance: %s",
               psn_id_to_use);
    }
  }

  if (psn_id_to_use) {
    VitaRPS5Result psn_result =
        ps5_discovery_set_psn_account(discovery_instance, psn_id_to_use);
    if (psn_result == VITARPS5_SUCCESS) {
      log_info("✅ Discovery instance immediately configured with PSN ID");
    } else {
      log_error("Failed to configure discovery with PSN ID: %s",
                error_string_placeholder(psn_result));
    }
  } else {
    log_info("No PSN ID available - discovery will use unauthenticated mode");
  }

  // Start the discovery scan
  result = ps5_discovery_start_scan(discovery_instance);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to start discovery scan: %s",
              error_string_placeholder(result));
    log_info("Starting simulated discovery mode for testing...");

    // Clean up failed discovery instance
    ps5_discovery_destroy(discovery_instance);
    discovery_instance = NULL;

    // Start simulated discovery for testing
    start_simulated_discovery();
    return VITARPS5_SUCCESS;
  }

  log_info("Discovery scan started successfully");
  return VITARPS5_SUCCESS;
}

static void stop_console_discovery(void) {
  log_info("Stopping console discovery (simulated: %s)",
           simulated_mode ? "true" : "false");

  if (discovery_instance) {
    // Extract results BEFORE destroying instance to preserve found consoles
    PS5DiscoveryResults current_results = {0};
    VitaRPS5Result get_result =
        ps5_discovery_get_results(discovery_instance, &current_results);

    if (get_result == VITARPS5_SUCCESS && current_results.console_count > 0) {
      log_info("Preserving %d discovered consoles from cancelled discovery",
               current_results.console_count);
      // Copy to UI discovery results to preserve them
      discovery_results = current_results;

      // CRITICAL FIX: Also save preserved consoles to persistent storage
      log_info("Saving %d cancelled discovery consoles to persistent storage",
               current_results.console_count);

      // Load current console cache
      ConsoleCacheData current_cache = {0};
      VitaRPS5Result load_result = console_storage_load(&current_cache);
      if (load_result != VITARPS5_SUCCESS) {
        log_warn(
            "Failed to load existing console cache: %s, creating new cache",
            error_string_placeholder(load_result));
        console_storage_init_default_cache(&current_cache);
      }

      // Add each discovered console to cache (handles duplicates automatically)
      uint32_t consoles_added = 0;
      for (uint32_t i = 0; i < current_results.console_count; i++) {
        const PS5ConsoleInfo* console = &current_results.consoles[i];

        // Check if console already exists to avoid duplicates
        if (!console_storage_console_exists(&current_cache, console->host_id)) {
          VitaRPS5Result add_result =
              console_storage_add_console(&current_cache, console);
          if (add_result == VITARPS5_SUCCESS) {
            consoles_added++;
            log_info(
                "Added cancelled discovery console to storage: %s (%s) at %s",
                console->device_name, console->host_id, console->ip_address);
          } else {
            log_error(
                "Failed to add cancelled discovery console %s to storage: %s",
                console->device_name, error_string_placeholder(add_result));
          }
        } else {
          log_info(
              "Cancelled discovery console %s already exists in storage, "
              "skipping",
              console->host_id);
        }
      }

      // Save updated cache to persistent storage
      if (consoles_added > 0) {
        VitaRPS5Result save_result = console_storage_save(&current_cache);
        if (save_result == VITARPS5_SUCCESS) {
          log_info(
              "Successfully saved %d cancelled discovery consoles to "
              "persistent storage",
              consoles_added);
        } else {
          log_error("Failed to save cancelled discovery console cache: %s",
                    error_string_placeholder(save_result));
        }
      } else {
        log_info(
            "No new cancelled discovery consoles to save (all already "
            "existed)");
      }
    } else {
      log_info("No consoles to preserve from cancelled discovery");
    }

    ps5_discovery_stop_scan(discovery_instance);
    ps5_discovery_destroy(discovery_instance);
    discovery_instance = NULL;
    ps5_discovery_cleanup();
  }

  // Clean up simulated mode
  if (simulated_mode) {
    simulated_mode = false;
    log_info("Simulated discovery mode stopped");
  }

  discovery_active = false;
  discovery_progress = 1.0f;
}

static void on_console_found(const PS5ConsoleInfo* console, void* user_data) {
  if (!console || discovery_results.console_count >= PS5_MAX_CONSOLES) {
    return;
  }

  log_info("Console found: %s at %s", console->device_name,
           console->ip_address);

  // Check if console already exists in UI results (deduplication)
  bool found_existing = false;
  for (uint32_t i = 0; i < discovery_results.console_count; i++) {
    if (strcmp(discovery_results.consoles[i].host_id, console->host_id) == 0) {
      // Update existing console state, don't increment count
      discovery_results.consoles[i] = *console;
      found_existing = true;
      log_info("Updated existing console %s state: %s", console->host_id,
               console->is_awake ? "Ready" : "Standby");
      break;
    }
  }

  // Add new console only if not found
  if (!found_existing) {
    discovery_results.consoles[discovery_results.console_count] = *console;
    discovery_results.console_count++;
    log_info("Added new console: %s (total: %d)", console->device_name,
             discovery_results.console_count);
  }

  // Note: Console cache state will be updated during discovery completion
  // when consoles are confirmed to exist in persistent cache

  // Update progress based on elapsed time (like simulated discovery)
  uint64_t current_time =
      sceKernelGetSystemTimeWide() / 1000;  // Convert to milliseconds
  uint64_t elapsed = current_time - discovery_start_time;
  discovery_progress = (float)elapsed / (float)PS5_DISCOVERY_TIMEOUT_MS;
  if (discovery_progress > 1.0f) discovery_progress = 1.0f;

  log_info("Discovery progress: %.1f%% (%llu/%d ms)",
           discovery_progress * 100.0f, elapsed, PS5_DISCOVERY_TIMEOUT_MS);
}

static void update_console_cache_state(const PS5ConsoleInfo* console) {
  if (!console) {
    return;
  }

  // Find matching console in cache by IP address or host_id
  ConsoleCacheData cache_data = {0};
  VitaRPS5Result load_result = console_storage_load(&cache_data);
  if (load_result != VITARPS5_SUCCESS) {
    log_debug("No console cache to update");
    return;
  }

  bool found_and_updated = false;
  for (uint32_t i = 0; i < cache_data.console_count; i++) {
    // Match by IP address first, then by host_id
    if (strcmp(cache_data.consoles[i].ip_address, console->ip_address) == 0 ||
        strcmp(cache_data.consoles[i].host_id, console->host_id) == 0) {
      // Update discovery state based on PS5 response
      ConsoleDiscoveryState new_state = console->is_awake
                                            ? CONSOLE_DISCOVERY_STATE_READY
                                            : CONSOLE_DISCOVERY_STATE_STANDBY;

      if (cache_data.consoles[i].discovery_state != new_state) {
        cache_data.consoles[i].discovery_state = new_state;
        log_info(
            "Updated console %s state: %s", cache_data.consoles[i].display_name,
            new_state == CONSOLE_DISCOVERY_STATE_READY ? "READY" : "STANDBY");
        found_and_updated = true;
      }
      break;
    }
  }

  // Save updated cache if we made changes
  if (found_and_updated) {
    VitaRPS5Result save_result = console_storage_save(&cache_data);
    if (save_result != VITARPS5_SUCCESS) {
      log_warn("Failed to save updated console state to cache");
    }
  }
}

static void on_discovery_complete(const PS5DiscoveryResults* results,
                                  void* user_data) {
  if (!results) {
    return;
  }

  log_info("Discovery complete: found %d consoles", results->console_count);

  discovery_results = *results;
  discovery_active = false;
  discovery_progress = 1.0f;

  // CRITICAL FIX: Save discovered consoles to persistent storage
  log_info("=== DISCOVERY COMPLETION PROCESSING ===");
  log_info("Discovery found %d consoles", results->console_count);

  if (results->console_count > 0) {
    // Log all discovered consoles for debugging
    for (uint32_t i = 0; i < results->console_count; i++) {
      const PS5ConsoleInfo* console = &results->consoles[i];
      log_info("  Discovered[%d]: %s (%s) at %s - Type: %d", i,
               console->device_name, console->host_id, console->ip_address,
               console->console_type);
    }

    log_info("Loading existing console cache for comparison...");

    // Load current console cache
    ConsoleCacheData current_cache = {0};
    VitaRPS5Result load_result = console_storage_load(&current_cache);
    if (load_result != VITARPS5_SUCCESS) {
      log_warn("Failed to load existing console cache: %s, creating new cache",
               error_string_placeholder(load_result));
      console_storage_init_default_cache(&current_cache);
    } else {
      log_info("Loaded existing cache with %d consoles",
               current_cache.console_count);
      for (uint32_t i = 0; i < current_cache.console_count; i++) {
        log_info("  Existing[%d]: %s (%s) at %s", i,
                 current_cache.consoles[i].display_name,
                 current_cache.consoles[i].host_id,
                 current_cache.consoles[i].ip_address);
      }
    }

    // Add each discovered console to cache (handles duplicates automatically)
    uint32_t consoles_added = 0;
    for (uint32_t i = 0; i < results->console_count; i++) {
      const PS5ConsoleInfo* console = &results->consoles[i];

      // Check if console already exists to avoid duplicates
      bool already_exists =
          console_storage_console_exists(&current_cache, console->host_id);
      log_info("Console %s (%s) already exists: %s", console->device_name,
               console->host_id, already_exists ? "YES" : "NO");

      if (!already_exists) {
        log_info("Adding new console: %s (%s) at %s", console->device_name,
                 console->host_id, console->ip_address);
        VitaRPS5Result add_result =
            console_storage_add_console(&current_cache, console);
        if (add_result == VITARPS5_SUCCESS) {
          consoles_added++;
          log_info("✓ Successfully added console to storage: %s (%s) at %s",
                   console->device_name, console->host_id, console->ip_address);
        } else {
          log_error("✗ Failed to add console %s to storage: %s",
                    console->device_name, error_string_placeholder(add_result));
        }
      } else {
        // Console already exists - update its last_seen timestamp for cooldown
        // logic This ensures the discovery cooldown works correctly
        log_info(
            "⚠ Console %s already exists in storage, updating last_seen "
            "timestamp",
            console->host_id);

        // Find the existing console and update its last_seen timestamp
        for (uint32_t j = 0; j < current_cache.console_count; j++) {
          if (strcmp(current_cache.consoles[j].host_id, console->host_id) ==
              0) {
            uint64_t current_time = sceKernelGetSystemTimeWide() / 1000;
            current_cache.consoles[j].last_seen = current_time;
            log_debug("Updated last_seen for existing console %s: %llu",
                      console->host_id, current_time);
            break;
          }
        }
      }
    }

    log_info(
        "Discovery processing summary: %d new consoles added to cache (cache "
        "now has %d total)",
        consoles_added, current_cache.console_count);

    // Save updated cache to persistent storage
    if (consoles_added > 0) {
      log_info(
          "Saving updated cache with %d new consoles to persistent storage...",
          consoles_added);
      VitaRPS5Result save_result = console_storage_save(&current_cache);
      if (save_result == VITARPS5_SUCCESS) {
        log_info("✓ Successfully saved console cache to storage");

        // Verify by reloading and checking count
        ConsoleCacheData verification_cache = {0};
        VitaRPS5Result verify_result =
            console_storage_load(&verification_cache);
        if (verify_result == VITARPS5_SUCCESS) {
          log_info("✓ Verification: reloaded cache contains %d consoles",
                   verification_cache.console_count);
        } else {
          log_error("✗ Verification failed: couldn't reload cache: %s",
                    error_string_placeholder(verify_result));
        }
      } else {
        log_error("✗ Failed to save console cache: %s",
                  error_string_placeholder(save_result));
      }
    } else {
      log_info("No new consoles to save (all already existed)");
    }
  } else {
    log_info("No consoles discovered - nothing to save");
  }

  // Update console states for ALL discovered consoles (new and existing)
  if (results->console_count > 0) {
    log_info("Updating states for %d discovered consoles...",
             results->console_count);
    for (uint32_t i = 0; i < results->console_count; i++) {
      const PS5ConsoleInfo* console = &results->consoles[i];
      update_console_cache_state(console);
    }
  }

  // Auto-return to main dashboard after discovery completes (user feedback)
  if (results->console_count > 0) {
    log_info("Consoles found - returning to main dashboard automatically");
  } else {
    log_info("No consoles found - returning to main dashboard");
  }

  // IMPORTANT: Change state AFTER saving to ensure proper sequence
  // Add small delay to ensure storage operations complete before dashboard
  // reload
  log_info(
      "Discovery complete - allowing brief delay for storage completion...");
  sceKernelDelayThread(50000);  // 50ms delay to ensure storage write completes

  log_info(
      "Changing UI state from ADD_CONNECTION to MAIN_DASHBOARD to trigger "
      "reload");
  current_state = UI_STATE_MAIN_DASHBOARD;

  // CRITICAL FIX: Force dashboard to reload console data immediately
  log_info("Forcing dashboard to reload console data after discovery...");
  ui_dashboard_force_reload();
}

static void render_discovery_ui(void) {
  // Update discovery progress for both real and simulated discovery
  if (simulated_mode) {
    update_discovery_progress();
  } else if (discovery_active) {
    // Update real discovery progress based on elapsed time
    uint64_t current_time =
        sceKernelGetSystemTimeWide() / 1000;  // Convert to milliseconds
    uint64_t elapsed = current_time - discovery_start_time;
    discovery_progress = (float)elapsed / (float)PS5_DISCOVERY_TIMEOUT_MS;
    if (discovery_progress > 1.0f) discovery_progress = 1.0f;
  }

  // Only log on first frame or major changes to reduce spam
  static uint32_t last_console_count = 0;
  static bool last_active = false;
  static bool first_time = true;
  if (first_time || discovery_results.console_count != last_console_count ||
      discovery_active != last_active) {
    first_time = false;
    log_info("=== DISCOVERY UI UPDATE ===");
    log_info("Discovery active: %s", discovery_active ? "true" : "false");
    log_info("Discovery progress: %.2f", discovery_progress);
    log_info("Found consoles: %d", discovery_results.console_count);
    log_info("Simulated mode: %s", simulated_mode ? "true" : "false");
    last_console_count = discovery_results.console_count;
    last_active = discovery_active;
  }

  vita2d_pgf* font = ui_core_get_font();
  if (!font) return;

  // Title
  const char* title = simulated_mode
                          ? "PlayStation Console Discovery (Simulated)"
                          : "PlayStation Console Discovery";
  int text_width = vita2d_pgf_text_width(font, 1.2f, title);
  int x = (960 - text_width) / 2;
  uint32_t title_color =
      simulated_mode ? UI_COLOR_STATUS_CONNECTING : UI_COLOR_TEXT_PRIMARY;
  vita2d_pgf_draw_text(font, x, 100, title_color, 1.2f, title);

  if (discovery_active) {
    // Show scanning progress
    const char* scanning_msg = simulated_mode
                                   ? "Simulating console discovery..."
                                   : "Scanning for PlayStation consoles...";
    text_width = vita2d_pgf_text_width(font, 1.0f, scanning_msg);
    x = (960 - text_width) / 2;
    vita2d_pgf_draw_text(font, x, 180, UI_COLOR_TEXT_SECONDARY, 1.0f,
                         scanning_msg);

    // Progress bar
    int bar_width = 400;
    int bar_x = (960 - bar_width) / 2;
    int bar_y = 220;

    // Background
    vita2d_draw_rectangle(bar_x, bar_y, bar_width, 20, UI_COLOR_CARD_BG);

    // Progress fill
    int fill_width = (int)(bar_width * discovery_progress);
    if (fill_width > 0) {
      vita2d_draw_rectangle(bar_x, bar_y, fill_width, 20,
                            UI_COLOR_PRIMARY_BLUE);
    }

    // Found count
    char found_text[64];
    snprintf(found_text, sizeof(found_text), "Found %d console(s)",
             discovery_results.console_count);
    text_width = vita2d_pgf_text_width(font, 0.9f, found_text);
    x = (960 - text_width) / 2;
    vita2d_pgf_draw_text(font, x, 270, UI_COLOR_TEXT_SECONDARY, 0.9f,
                         found_text);

  } else if (discovery_results.console_count > 0) {
    // Show found consoles
    const char* found_msg = "PlayStation Consoles Found:";
    text_width = vita2d_pgf_text_width(font, 1.0f, found_msg);
    x = (960 - text_width) / 2;
    vita2d_pgf_draw_text(font, x, 160, UI_COLOR_TEXT_PRIMARY, 1.0f, found_msg);

    // Console list
    for (uint32_t i = 0; i < discovery_results.console_count && i < 6; i++) {
      const PS5ConsoleInfo* console = &discovery_results.consoles[i];

      int card_y = 200 + (i * 60);
      int card_height = 50;

      // Selection highlight
      uint32_t card_color = (i == selected_discovery_index)
                                ? UI_COLOR_PRIMARY_BLUE
                                : UI_COLOR_CARD_BG;
      ui_core_render_card_with_shadow(100, card_y, 760, card_height, 8,
                                      card_color);

      // Console info
      vita2d_pgf_draw_text(font, 120, card_y + 20, UI_COLOR_TEXT_PRIMARY, 0.9f,
                           console->device_name);
      vita2d_pgf_draw_text(font, 120, card_y + 35, UI_COLOR_TEXT_SECONDARY,
                           0.7f, console->ip_address);

      // Console type
      const char* type_str = ps5_console_type_string(console->console_type);
      vita2d_pgf_draw_text(font, 500, card_y + 20, UI_COLOR_TEXT_SECONDARY,
                           0.8f, type_str);
    }

    // Instructions
    vita2d_pgf_draw_text(
        font, 50, 480, UI_COLOR_TEXT_TERTIARY, 0.8f,
        "D-PAD: Select console, X: Add console, TRIANGLE: Manual entry");

  } else {
    // No consoles found
    const char* empty_msg =
        simulated_mode ? "Network discovery unavailable - Simulated mode active"
                       : "No PlayStation consoles found on network";
    text_width = vita2d_pgf_text_width(font, 1.0f, empty_msg);
    x = (960 - text_width) / 2;
    uint32_t msg_color =
        simulated_mode ? UI_COLOR_STATUS_CONNECTING : UI_COLOR_TEXT_SECONDARY;
    vita2d_pgf_draw_text(font, x, 250, msg_color, 1.0f, empty_msg);

    const char* help_msg =
        simulated_mode
            ? "Real network discovery failed due to socket binding error"
            : "Make sure your PlayStation console is turned on and connected "
              "to the same network";
    text_width = vita2d_pgf_text_width(font, 0.8f, help_msg);
    x = (960 - text_width) / 2;
    vita2d_pgf_draw_text(font, x, 300, UI_COLOR_TEXT_TERTIARY, 0.8f, help_msg);

    // Add additional help for simulated mode
    if (simulated_mode) {
      const char* tip_msg =
          "Press TRIANGLE to manually enter console IP address";
      text_width = vita2d_pgf_text_width(font, 0.8f, tip_msg);
      x = (960 - text_width) / 2;
      vita2d_pgf_draw_text(font, x, 340, UI_COLOR_TEXT_TERTIARY, 0.8f, tip_msg);
    }
  }

  // Back instruction (always visible)
  vita2d_pgf_draw_text(font, 50, 520, UI_COLOR_TEXT_TERTIARY, 0.8f,
                       "CIRCLE: Cancel and return to dashboard");
}

static void handle_discovery_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  bool x_pressed =
      (pad->buttons & SCE_CTRL_CROSS) && !(prev_pad->buttons & SCE_CTRL_CROSS);
  bool circle_pressed = (pad->buttons & SCE_CTRL_CIRCLE) &&
                        !(prev_pad->buttons & SCE_CTRL_CIRCLE);
  bool triangle_pressed = (pad->buttons & SCE_CTRL_TRIANGLE) &&
                          !(prev_pad->buttons & SCE_CTRL_TRIANGLE);
  bool dpad_up =
      (pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP);
  bool dpad_down =
      (pad->buttons & SCE_CTRL_DOWN) && !(prev_pad->buttons & SCE_CTRL_DOWN);

  // Handle back navigation - preserve found consoles when canceling
  if (circle_pressed) {
    if (discovery_results.console_count > 0) {
      log_info("Canceling discovery - preserving %d found consoles",
               discovery_results.console_count);
    } else {
      log_info("Canceling discovery - no consoles found to preserve");
    }
    stop_console_discovery();
    current_state = UI_STATE_MAIN_DASHBOARD;
    return;
  }

  // Handle manual IP entry
  if (triangle_pressed) {
    log_info("Opening manual console IP entry");
    // TODO: Open IME dialog for IP address entry
    // For now, add a hardcoded console for testing
    if (discovery_results.console_count < PS5_MAX_CONSOLES) {
      PS5ConsoleInfo* console =
          &discovery_results.consoles[discovery_results.console_count];
      strncpy(console->device_name, "PlayStation 5 (Manual)",
              sizeof(console->device_name) - 1);
      strncpy(console->ip_address, "192.168.1.X",
              sizeof(console->ip_address) - 1);
      console->console_type = PS_CONSOLE_PS5;
      console->port = 9295;
      console->is_awake = true;
      discovery_results.console_count++;
      log_info("Added manual console entry placeholder");
    }
    return;
  }

  // Don't handle other input if discovery is active
  if (discovery_active) {
    return;
  }

  // Handle console selection
  if (discovery_results.console_count > 0) {
    if (dpad_up && selected_discovery_index > 0) {
      selected_discovery_index--;
    } else if (dpad_down &&
               selected_discovery_index < discovery_results.console_count - 1) {
      selected_discovery_index++;
    }

    // Handle console selection with X
    if (x_pressed) {
      const PS5ConsoleInfo* selected =
          &discovery_results.consoles[selected_discovery_index];
      log_info("Adding console: %s at %s", selected->device_name,
               selected->ip_address);

      // Check if this is a PS5 that may need registration
      bool is_ps5_console = (selected->console_type == PS_CONSOLE_PS5 ||
                             selected->console_type == PS_CONSOLE_PS5_DIGITAL);

      if (is_ps5_console) {
        // Check if this PS5 is already registered
        // Check if console is registered using clean slate implementation
        bool is_registered =
            ui_core_is_console_registered(selected->ip_address);

        if (!is_registered) {
          log_info("PS5 console %s requires registration - displaying notice",
                   selected->ip_address);

          // Add console to storage first (so it appears in dashboard)
          ConsoleCacheData cache;
          VitaRPS5Result load_result = console_storage_load(&cache);
          if (load_result == VITARPS5_SUCCESS) {
            VitaRPS5Result result =
                console_storage_add_console(&cache, selected);
            if (result == VITARPS5_SUCCESS) {
              VitaRPS5Result save_result = console_storage_save(&cache);
              if (save_result == VITARPS5_SUCCESS) {
                log_info(
                    "PS5 console added to cache - user can register it from "
                    "dashboard");
              }
            }
          }

          // TODO: In future, could transition to registration UI here
          // For now, return to dashboard with message in logs
          log_info(
              "Returning to dashboard - PS5 console added but needs "
              "registration");
          log_info(
              "User should attempt to connect to trigger registration flow");

          stop_console_discovery();
          current_state = UI_STATE_MAIN_DASHBOARD;
          return;
        } else {
          log_info("PS5 console %s is already registered",
                   selected->ip_address);
        }
      }

      // Add console to storage using the proper API
      ConsoleCacheData cache;
      log_info("Loading current console cache...");
      VitaRPS5Result load_result = console_storage_load(&cache);
      if (load_result == VITARPS5_SUCCESS) {
        log_info("Current cache has %d consoles", cache.console_count);
        VitaRPS5Result result = console_storage_add_console(&cache, selected);
        if (result == VITARPS5_SUCCESS) {
          log_info("Console added to cache, new count: %d",
                   cache.console_count);
          VitaRPS5Result save_result = console_storage_save(&cache);
          if (save_result == VITARPS5_SUCCESS) {
            log_info("Console cache saved successfully");
          } else {
            log_error("Failed to save console cache: %s",
                      error_string_placeholder(save_result));
          }
        } else {
          log_error("Failed to add console to cache: %s",
                    error_string_placeholder(result));
        }
      } else {
        log_error("Failed to load console cache: %s",
                  error_string_placeholder(load_result));
      }

      // Return to dashboard
      stop_console_discovery();
      current_state = UI_STATE_MAIN_DASHBOARD;
    }
  }
}

// Simulated Discovery Implementation (for testing when no network/consoles
// available)

static void start_simulated_discovery(void) {
  log_info("=== STARTING SIMULATED DISCOVERY ===");

  simulated_mode = true;
  discovery_active = true;
  discovery_progress = 0.0f;

  // Get current time for progress calculation - use sceKernelGetProcessTimeWide
  // for simplicity
  discovery_start_time =
      sceKernelGetSystemTimeWide() / 1000;  // Convert to milliseconds

  // Clear previous results
  memset(&discovery_results, 0, sizeof(discovery_results));

  log_info("Simulated discovery started, will run for %d ms",
           DISCOVERY_DURATION_MS);
}

static void update_discovery_progress(void) {
  if (!discovery_active || !simulated_mode) {
    return;
  }

  // Calculate progress based on elapsed time
  uint64_t current_time =
      sceKernelGetSystemTimeWide() / 1000;  // Convert to milliseconds
  uint64_t elapsed = current_time - discovery_start_time;

  discovery_progress = (float)elapsed / (float)DISCOVERY_DURATION_MS;

  // Add simulated consoles at specific progress points
  if (discovery_progress >= 0.3f && discovery_results.console_count == 0) {
    // Add first simulated console at 30% progress
    PS5ConsoleInfo* console =
        &discovery_results.consoles[discovery_results.console_count];
    strncpy(console->device_name, "PlayStation 5 (Simulated)",
            sizeof(console->device_name) - 1);
    strncpy(console->ip_address, "192.168.1.100",
            sizeof(console->ip_address) - 1);
    console->console_type = PS_CONSOLE_PS5;
    console->port = 9295;
    console->is_awake = true;
    discovery_results.console_count++;
    log_info("Simulated console 1 found");
  }

  if (discovery_progress >= 0.7f && discovery_results.console_count == 1) {
    // Add second simulated console at 70% progress
    PS5ConsoleInfo* console =
        &discovery_results.consoles[discovery_results.console_count];
    strncpy(console->device_name, "PlayStation 4 Pro (Simulated)",
            sizeof(console->device_name) - 1);
    strncpy(console->ip_address, "192.168.1.101",
            sizeof(console->ip_address) - 1);
    console->console_type = PS_CONSOLE_PS4_PRO;
    console->port = 9295;
    console->is_awake = true;
    discovery_results.console_count++;
    log_info("Simulated console 2 found");
  }

  // Complete discovery when time is up
  if (discovery_progress >= 1.0f) {
    discovery_progress = 1.0f;
    discovery_active = false;
    simulated_mode = false;
    log_info("Simulated discovery completed - found %d consoles",
             discovery_results.console_count);

    // CRITICAL FIX: Call completion callback for simulated discovery
    // This ensures console saving works even when real discovery fails
    log_info("Calling completion callback for simulated discovery");
    on_discovery_complete(&discovery_results, NULL);
  }
}

// Public Registration Cache Functions - MIGRATED TO UNIFIED API

bool ui_core_is_console_registered(const char* console_ip) {
  if (!console_ip) {
    return false;
  }
  // MIGRATION: Use unified registration cache API
  return registration_cache_is_registered(console_ip);
}

void ui_core_invalidate_registration_cache(const char* console_ip) {
  // MIGRATION: Use unified registration cache API
  registration_cache_invalidate_console(console_ip);
}

void ui_core_clear_registration_cache(void) {
  // MIGRATION: Use unified registration cache API
  registration_cache_clear_all();
}

// Old registration cache implementation removed - migrated to unified
// registration_cache.c

// RESEARCHER PHASE 2: UI event emission for registration state changes
void ui_emit_console_state_changed(const char* console_ip,
                                   ConsoleStateEvent state) {
  if (!console_ip) {
    return;
  }

  const char* state_str =
      (state == CONSOLE_STATE_REGISTERED) ? "REGISTERED" : "UNREGISTERED";
  log_info("REGSYNC: UI event emitted for %s - state changed to %s", console_ip,
           state_str);

  // TODO: Add actual UI refresh logic here when needed
  // For now, just logging the event is sufficient for the researcher's
  // requirements
}