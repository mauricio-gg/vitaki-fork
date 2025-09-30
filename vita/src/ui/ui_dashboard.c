#include "ui_dashboard.h"

#include <math.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <string.h>

#include "../core/console_registration.h"
#include "../core/console_storage.h"
#include "../core/session_manager.h"  // For session_wake_and_wait()
#include "../discovery/ps5_discovery.h"
#include "../utils/logger.h"
#include "console_state_thread.h"
#include "ui_components.h"
#include "ui_core.h"
#include "ui_navigation.h"  // For UIState enum
#include "ui_profile.h"
#include "ui_registration.h"
#include "vita2d_ui.h"

// Dashboard state - NEW: Real console data management
static ConsoleCacheData console_cache;
static bool console_data_loaded = false;
static ConsoleStateThread* background_state_thread = NULL;
static bool loading_consoles = false;
static uint32_t selected_console_index = 0;
static uint64_t last_refresh_time = 0;

// Navigation state for empty dashboard
static bool add_button_selected = false;

// Animation state for button feedback
static bool add_button_pressed = false;
static float add_button_press_time = 0.0f;
static float console_button_press_time = 0.0f;

// Touch state
static bool touch_active = false;
static float last_touch_x = 0.0f;
static float last_touch_y = 0.0f;

// Background state checking is handled by console_state_thread.c

// Wake progress tracking for user feedback
typedef struct {
  bool is_waking;                 // Is a console currently being woken
  uint32_t waking_console_index;  // Which console is being woken
  uint64_t wake_start_time;       // When wake signal was sent
  uint32_t wake_timeout_seconds;  // How long to show progress (15 seconds)
  bool confirmation_started;      // Has wake confirmation check started
  ConsoleState final_state;       // Final console state after wake attempt
  char wake_status_message[128];  // Status message for wake progress
} WakeProgressState;

static WakeProgressState wake_progress = {0};

// Layout constants for proper centering
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define CONTENT_AREA_HEIGHT 400  // Available area excluding header/footer

// Forward declarations
static void render_main_content(void);
static void render_console_grid(void);
static void render_add_connection_button(void);
static void render_centered_add_button(bool is_pressed);
static void render_empty_state(void);
static void render_loading_state(void);
static void update_animations(float delta_time);
static VitaRPS5Result load_console_data(void);
static const char* get_console_type_string(PSConsoleType type);
static void handle_touch_input(void);
static bool is_point_in_button(float x, float y, int button_x, int button_y,
                               int button_w, int button_h);
static void trigger_add_console_action(void);
static void trigger_console_connect_action(uint32_t console_index);
static void trigger_console_remove_action(uint32_t console_index);
static void trigger_console_wake_action(uint32_t console_index);
static void trigger_console_register_action(uint32_t console_index);
// Removed: check_existing_console_states - replaced by background thread

// Wake progress management functions
static void start_wake_progress(uint32_t console_index);
static void update_wake_progress(void);
static void stop_wake_progress(void);
static bool is_console_waking(uint32_t console_index);
static float get_wake_progress_percentage(void);
static void render_wake_progress_overlay(int card_x, int card_y,
                                         uint32_t console_index);

void ui_dashboard_init(void) {
  log_info("Initializing dashboard...");

  // Initialize console storage system
  VitaRPS5Result result = console_storage_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize console storage: %s",
              vitarps5_result_string(result));
    return;
  }

  // Load console data from persistent storage
  load_console_data();

  // Initialize and start background state checking thread
  result = console_state_thread_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize background state thread: %s",
              vitarps5_result_string(result));
    return;
  }

  // Start background thread for non-blocking state checking
  if (console_data_loaded && console_cache.console_count > 0) {
    ConsoleStateThreadConfig thread_config;
    console_state_thread_get_default_config(&thread_config);

    result = console_state_thread_start(&thread_config, &console_cache,
                                        &background_state_thread);
    if (result == VITARPS5_SUCCESS) {
      log_info("Background state checking thread started successfully");
    } else {
      log_warning("Failed to start background state thread: %s",
                  vitarps5_result_string(result));
    }
  }
}

void ui_dashboard_cleanup(void) {
  log_info("Cleaning up dashboard...");

  // Stop background state checking thread
  if (background_state_thread) {
    console_state_thread_stop(background_state_thread);
    background_state_thread = NULL;
  }

  // Cleanup thread subsystem
  console_state_thread_cleanup();

  // Save current console data before cleanup
  if (console_data_loaded) {
    VitaRPS5Result result = console_storage_save(&console_cache);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to save console data during cleanup: %s",
                vitarps5_result_string(result));
    }
  }

  console_storage_cleanup();
}

void ui_dashboard_render(void) {
  ui_core_render_logo();

  // Check if we need to reload console data (e.g., after returning from
  // discovery)
  static bool initialized = false;
  static UIState last_state = UI_STATE_MAIN_DASHBOARD;
  UIState current_ui_state = ui_core_get_state();

  // Only update if we're actually in the dashboard state
  if (current_ui_state == UI_STATE_MAIN_DASHBOARD) {
    // Reload data if this is first render or we came from another state
    if (!initialized || last_state != UI_STATE_MAIN_DASHBOARD) {
      log_info("=== DASHBOARD RELOAD TRIGGERED ===");
      log_info("Current state: %d, Last state: %d, Initialized: %s",
               current_ui_state, last_state, initialized ? "true" : "false");
      log_info("Reloading console data from persistent storage...");
      load_console_data();

      // Console discovery is now manual only - users trigger via Add New button
      // State checking is handled by background thread for non-blocking UI
      if (console_cache.console_count > 0) {
        log_info(
            "Loaded %d existing consoles - background thread will handle state "
            "checking",
            console_cache.console_count);
      }
      log_info(
          "Console data loaded - discovery is manual, state checking is "
          "background threaded");

      log_info("=== DASHBOARD RELOAD COMPLETE ===");
      initialized = true;
    }

    // State checking is now handled by background thread - no UI blocking
    // The console_cache is automatically updated by the background thread

    // Update wake progress tracking
    update_wake_progress();

    last_state = UI_STATE_MAIN_DASHBOARD;
  } else {
    // We're not in dashboard anymore, update last_state
    if (last_state != current_ui_state) {
      log_info("Dashboard state changed: %d -> %d", last_state,
               current_ui_state);
    }
    last_state = current_ui_state;
  }

  // Update animations with fixed delta time (60fps)
  update_animations(0.016f);

  render_main_content();
}

void ui_dashboard_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  // Handle touch input first
  handle_touch_input();

  bool x_pressed =
      (pad->buttons & SCE_CTRL_CROSS) && !(prev_pad->buttons & SCE_CTRL_CROSS);
  bool circle_pressed = (pad->buttons & SCE_CTRL_CIRCLE) &&
                        !(prev_pad->buttons & SCE_CTRL_CIRCLE);
  bool triangle_pressed = (pad->buttons & SCE_CTRL_TRIANGLE) &&
                          !(prev_pad->buttons & SCE_CTRL_TRIANGLE);
  bool square_pressed = (pad->buttons & SCE_CTRL_SQUARE) &&
                        !(prev_pad->buttons & SCE_CTRL_SQUARE);
  bool dpad_up =
      (pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP);
  bool dpad_down =
      (pad->buttons & SCE_CTRL_DOWN) && !(prev_pad->buttons & SCE_CTRL_DOWN);
  bool start_pressed =
      (pad->buttons & SCE_CTRL_START) && !(prev_pad->buttons & SCE_CTRL_START);

  // Handle START button - refresh console list
  if (start_pressed) {
    log_info("Refreshing console list");
    load_console_data();
    return;
  }

  // Handle navigation based on current state
  if (console_cache.console_count > 0) {
    // Console grid navigation
    if (dpad_up && selected_console_index > 0) {
      selected_console_index--;
      add_button_selected = false;
    } else if (dpad_down) {
      if (selected_console_index < console_cache.console_count - 1) {
        selected_console_index++;
        add_button_selected = false;
      } else {
        // Move to "Add New" button at bottom
        add_button_selected = true;
      }
    }

    // Handle selection of add button
    if (add_button_selected && dpad_up) {
      add_button_selected = false;
      selected_console_index = console_cache.console_count - 1;
    }
  } else {
    // Empty state - add button is always selected
    add_button_selected = true;
  }

  // Handle Triangle button press - remove console
  if (triangle_pressed) {
    if (console_cache.console_count > 0 && !add_button_selected) {
      // Triangle on existing console card: Remove console
      trigger_console_remove_action(selected_console_index);
    }
  }

  // Handle X button press
  if (x_pressed) {
    log_info("=== X BUTTON PRESSED - DEBUG INFO ===");
    log_info("Console count: %d", console_cache.console_count);
    log_info("Add button selected: %s", add_button_selected ? "true" : "false");
    log_info("Selected console index: %d", selected_console_index);

    if (console_cache.console_count > 0 && !add_button_selected) {
      // X on existing console card: Check registration status first
      UIConsoleInfo* console = &console_cache.consoles[selected_console_index];

      log_info("X pressed on console card:");
      log_info("  Console name: %s", console->display_name);
      log_info("  Console IP: %s", console->ip_address);
      log_info("  Console type: %d", console->console_type);
      log_info("  Discovery state: %d", console->discovery_state);

      // RESEARCHER FIX 1: Use unified registration accessor instead of stale
      // storage cache Replace split-brain check with single source of truth
      log_info(
          "Checking console registration status using unified accessor...");
      RegistrationCredentials creds;
      bool is_registered =
          registration_get_by_ip(console->ip_address, &creds) && creds.is_valid;
      log_info(
          "Registration check result: %s (from unified accessor: valid=%s)",
          is_registered ? "REGISTERED" : "NOT_REGISTERED",
          is_registered ? "true" : "false");

      if (is_registered) {
        // Registered console: Start Remote Play
        log_info("X pressed on registered console %s - starting connection",
                 console->display_name);
        log_info("Console IP: %s", console->ip_address);
        log_info("Console type: %d", console->console_type);
        log_info("Calling trigger_console_connect_action(%d)...",
                 selected_console_index);
        trigger_console_connect_action(selected_console_index);
        log_info("trigger_console_connect_action() call completed");
      } else {
        // Unregistered console: Go to registration screen
        log_info("X pressed on unregistered console %s - starting registration",
                 console->display_name);
        log_info("Calling trigger_console_register_action(%d)...",
                 selected_console_index);
        trigger_console_register_action(selected_console_index);
      }
    } else {
      // X on "Add Console" button or empty state: Start discovery
      log_info(
          "X pressed on Add Console button or empty state - starting "
          "discovery");
      trigger_add_console_action();
    }
    log_info("=== X BUTTON PRESS HANDLING COMPLETE ===");
  }

  // DEBUGGING: Handle CIRCLE button press to remove mock registration
  if (circle_pressed) {
    log_info("=== CIRCLE BUTTON PRESSED - REMOVE REGISTRATION ===");
    if (console_cache.console_count > 0 && !add_button_selected) {
      UIConsoleInfo* console = &console_cache.consoles[selected_console_index];
      log_info("CIRCLE pressed on console: %s (%s)", console->display_name,
               console->ip_address);

      // Check if console is registered
      if (ui_core_is_console_registered(console->ip_address)) {
        log_info("Removing registration for console %s", console->display_name);
        VitaRPS5Result remove_result =
            console_registration_remove(console->ip_address);
        if (remove_result == VITARPS5_SUCCESS) {
          log_info("Successfully removed registration for %s",
                   console->display_name);
          log_info("Console should now appear grayed-out");

          // Invalidate registration cache for this console
          ui_core_invalidate_registration_cache(console->ip_address);
        } else {
          log_error("Failed to remove registration: %s",
                    vitarps5_result_string(remove_result));
        }
      } else {
        log_info("Console %s is not registered - nothing to remove",
                 console->display_name);
      }
    }
    log_info("=== CIRCLE BUTTON PRESS HANDLING COMPLETE ===");
  }

  // Handle SQUARE button press - Re-pair (remove registration and start
  // registration flow)
  if (square_pressed) {
    log_info("=== SQUARE BUTTON PRESSED - RE-PAIR CONSOLE ===");
    if (console_cache.console_count > 0 && !add_button_selected) {
      UIConsoleInfo* console = &console_cache.consoles[selected_console_index];
      log_info("SQUARE pressed on console: %s (%s)", console->display_name,
               console->ip_address);

      // Remove existing registration if present
      if (ui_core_is_console_registered(console->ip_address)) {
        log_info("Re-pair: removing existing registration for %s",
                 console->display_name);
        VitaRPS5Result remove_result =
            console_registration_remove(console->ip_address);
        if (remove_result == VITARPS5_SUCCESS) {
          log_info("Re-pair: registration removed - starting registration UI");
          ui_core_invalidate_registration_cache(console->ip_address);
        } else {
          log_error("Re-pair: failed to remove registration: %s",
                    vitarps5_result_string(remove_result));
        }
      } else {
        log_info("Re-pair: console not registered - proceeding to register");
      }

      // Start registration flow for this console
      trigger_console_register_action(selected_console_index);
    }
    log_info("=== SQUARE BUTTON PRESS HANDLING COMPLETE ===");
  }
}

void ui_dashboard_set_console_data(const UIConsoleInfo* console) {
  if (console && console_cache.console_count < MAX_SAVED_CONSOLES) {
    // Add new console to cache
    console_cache.consoles[console_cache.console_count] = *console;
    console_cache.console_count++;
    console_cache.last_updated = sceRtcGetTickResolution();

    // Save to persistent storage
    VitaRPS5Result result = console_storage_save(&console_cache);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to save console data: %s",
                vitarps5_result_string(result));
    }
  }
}

bool ui_dashboard_has_saved_console(void) {
  return console_data_loaded && console_cache.console_count > 0;
}

uint32_t ui_dashboard_get_console_count(void) {
  return console_cache.console_count;
}

UIConsoleInfo* ui_dashboard_get_selected_console(void) {
  if (console_cache.console_count > 0 &&
      selected_console_index < console_cache.console_count) {
    return &console_cache.consoles[selected_console_index];
  }
  return NULL;
}

void ui_dashboard_force_reload(void) {
  log_info("=== FORCE DASHBOARD RELOAD TRIGGERED ===");
  log_info("Current console count: %d", console_cache.console_count);

  // Force reload console data from storage immediately
  VitaRPS5Result result = load_console_data();
  if (result == VITARPS5_SUCCESS) {
    log_info("✓ Dashboard force reload completed successfully");
    log_info("New console count: %d", console_cache.console_count);

    // Log all consoles for verification
    for (uint32_t i = 0; i < console_cache.console_count; i++) {
      log_info("  Console[%d]: %s (%s) at %s", i,
               console_cache.consoles[i].display_name,
               console_cache.consoles[i].host_id,
               console_cache.consoles[i].ip_address);
    }
  } else {
    log_error("✗ Dashboard force reload failed: %s",
              vitarps5_result_string(result));
  }

  log_info("=== FORCE DASHBOARD RELOAD COMPLETE ===");
}

// Private functions
static void render_main_content(void) {
  vita2d_pgf* font = ui_core_get_font();

  // Calculate vertical centering for content area
  int content_start_y = (SCREEN_HEIGHT - CONTENT_AREA_HEIGHT) / 2 +
                        50;  // Add slight offset for header

  // Main header text - centered horizontally and positioned for vertical layout
  const char* msg = "Which do you want to connect?";
  float scale = 1.2f;
  int screen_center_x = SCREEN_WIDTH / 2;

  int text_width = vita2d_pgf_text_width(font, scale, msg);
  int start_x = screen_center_x - (text_width / 2);

  vita2d_pgf_draw_text(font, start_x, content_start_y, UI_COLOR_TEXT_PRIMARY,
                       scale, msg);

  if (loading_consoles) {
    render_loading_state();
  } else if (console_cache.console_count > 0) {
    render_console_grid();
    render_add_connection_button();
  } else {
    render_empty_state();
  }
}

static void render_add_connection_button(void) {
  if (console_cache.console_count == 0) return;

  // Defensive check: if add_button_selected is true but we have consoles,
  // reset the navigation state to prevent UI glitches
  if (add_button_selected) {
    log_debug(
        "Fixing navigation state: add_button_selected=true but "
        "console_count=%d",
        console_cache.console_count);
    add_button_selected = false;
    selected_console_index = 0;
  }

  UIAssets* assets = ui_core_get_assets();

  // Position button at bottom, properly centered
  int button_height = assets->button_add_new
                          ? vita2d_texture_get_height(assets->button_add_new)
                          : 50;
  int button_y = SCREEN_HEIGHT - 100 - button_height;

  int button_width = assets->button_add_new
                         ? vita2d_texture_get_width(assets->button_add_new)
                         : 200;
  int button_x = (SCREEN_WIDTH - button_width) / 2;

  // Draw selection highlight if add button is selected - much more visible
  if (add_button_selected) {
    // Draw thick, bright highlight border with rounded corners
    ui_core_render_rounded_rectangle(button_x - 8, button_y - 8,
                                     button_width + 16, button_height + 16, 8,
                                     UI_COLOR_PRIMARY_BLUE);
    // Add inner glow effect
    ui_core_render_rounded_rectangle(button_x - 4, button_y - 4,
                                     button_width + 8, button_height + 8, 6,
                                     0x80FF9034);  // Semi-transparent blue glow
  }

  // Apply press animation
  uint32_t button_color = UI_COLOR_PRIMARY_BLUE;
  if (add_button_pressed && add_button_press_time < 0.2f) {
    // Brighten button when pressed (simple color shift)
    button_color = 0xFFFFB050;  // Brighter blue
  }

  if (assets->button_add_new) {
    vita2d_draw_texture(assets->button_add_new, button_x, button_y);

    // Add color overlay for press animation
    if (add_button_pressed && add_button_press_time < 0.2f) {
      vita2d_draw_rectangle(button_x, button_y, button_width, button_height,
                            0x40FFFFFF);  // Semi-transparent white overlay
    }
  } else {
    // Fallback button with rounded corners and better styling
    vita2d_pgf* font = ui_core_get_font();
    ui_core_render_rounded_rectangle(button_x, button_y, button_width,
                                     button_height, 8, button_color);

    // Center text properly
    const char* button_text = "Add New";
    int text_width = vita2d_pgf_text_width(font, 0.9f, button_text);
    int text_x = button_x + (button_width - text_width) / 2;
    int text_y = button_y + (button_height / 2) + 8;

    vita2d_pgf_draw_text(font, text_x, text_y, RGBA8(255, 255, 255, 255), 0.9f,
                         button_text);
  }

  // Instructions - centered at bottom
  vita2d_pgf* font = ui_core_get_font();
  const char* instruction =
      console_cache.console_count > 0
          ? "D-PAD: Navigate, X: Select, □: Re-pair, △: Remove, START: Refresh"
          : "D-PAD: Navigate, X: Select, START: Refresh";
  int text_width = vita2d_pgf_text_width(font, 0.7f, instruction);
  int start_x = (SCREEN_WIDTH - text_width) / 2;
  vita2d_pgf_draw_text(font, start_x, SCREEN_HEIGHT - 30,
                       UI_COLOR_TEXT_TERTIARY, 0.7f, instruction);
}

static void render_console_grid(void) {
  int screen_center_x = SCREEN_WIDTH / 2;

  // Log console count for debugging
  static uint32_t last_logged_count = UINT32_MAX;
  if (console_cache.console_count != last_logged_count) {
    log_info("Rendering console grid with %d consoles",
             console_cache.console_count);
    last_logged_count = console_cache.console_count;
  }

  // Calculate vertical centering for console grid with equal spacing
  // above/below
  int card_height = 100;   // Actual console card height
  int card_spacing = 120;  // Spacing between cards

  // Get header and button positions for clean spacing calculation
  int header_y =
      (SCREEN_HEIGHT - CONTENT_AREA_HEIGHT) / 2 + 50;  // Header position
  int header_bottom =
      header_y + 25;  // Header text + margin (1.2f scale ≈ 20px + 5px margin)

  // Calculate button position (matching render_add_connection_button logic)
  UIAssets* assets = ui_core_get_assets();
  int button_height = assets->button_add_new
                          ? vita2d_texture_get_height(assets->button_add_new)
                          : 50;
  int button_y = SCREEN_HEIGHT - 100 - button_height;
  int button_top =
      button_y - 115;  // Button position - proper margin (moved up 100px)

  // Calculate available space for console cards
  int available_height = button_top - header_bottom;
  int total_cards_height = console_cache.console_count * card_spacing;

  // Center console cards in available space for perfect visual balance
  int content_start_y =
      header_bottom + (available_height - total_cards_height) / 2;

  // Render console cards
  for (uint32_t i = 0; i < console_cache.console_count; i++) {
    UIConsoleInfo* console = &console_cache.consoles[i];

    // Calculate card position (centered horizontally and vertically)
    int card_width = CONSOLE_CARD_WIDTH;  // 300 pixels
    int card_x = screen_center_x - (card_width / 2);
    int card_y = content_start_y + (i * card_spacing);

    // Highlight selected console with rounded selection border matching actual
    // card size
    if (i == selected_console_index) {
      // Get actual console card dimensions from assets
      UIAssets* assets = ui_core_get_assets();
      int actual_card_width =
          assets->console_card ? vita2d_texture_get_width(assets->console_card)
                               : CONSOLE_CARD_WIDTH;
      int actual_card_height =
          assets->console_card ? vita2d_texture_get_height(assets->console_card)
                               : card_height;

      // Calculate centered position for actual card
      int screen_center_x = SCREEN_WIDTH / 2;
      int actual_card_x = screen_center_x - (actual_card_width / 2);

      // Draw selection border
      uint32_t border_color = UI_COLOR_PRIMARY_BLUE;

      // Apply press animation by brightening the border
      if (console_button_press_time >= 0.0f &&
          console_button_press_time < 0.2f) {
        border_color = 0xFFFFB050;  // Brighter blue when pressed
      }

      // Draw rounded selection border using ui_core function
      int border_width = 3;
      ui_core_render_rounded_rectangle(actual_card_x - border_width,
                                       card_y - border_width,
                                       actual_card_width + (border_width * 2),
                                       actual_card_height + (border_width * 2),
                                       8,          // Corner radius
                                       0x00000000  // Transparent fill
      );

      // Draw border outline with rounded corners
      ui_core_render_rounded_rectangle(actual_card_x - border_width,
                                       card_y - border_width,
                                       actual_card_width + (border_width * 2),
                                       actual_card_height + (border_width * 2),
                                       8,            // Corner radius
                                       border_color  // Border color
      );

      // Draw inner area to "cut out" the fill, leaving just the border
      ui_core_render_rounded_rectangle(
          actual_card_x, card_y, actual_card_width, actual_card_height,
          5,          // Slightly smaller radius
          0x00000000  // Transparent to cut out center
      );
    }

    // Convert UIConsoleInfo to legacy ConsoleInfo for rendering
    ConsoleInfo legacy_console = {0};
    strncpy(legacy_console.console_name, console->display_name,
            sizeof(legacy_console.console_name) - 1);
    strncpy(legacy_console.console_type,
            get_console_type_string(console->console_type),
            sizeof(legacy_console.console_type) - 1);
    strncpy(legacy_console.ip_address, console->ip_address,
            sizeof(legacy_console.ip_address) - 1);
    // Map discovery_state to legacy ConsoleStatus for UI compatibility
    switch (console->discovery_state) {
      case CONSOLE_DISCOVERY_STATE_READY:
        legacy_console.status = CONSOLE_STATUS_AVAILABLE;
        legacy_console.console_state = 1;  // CONSOLE_STATE_READY
        break;
      case CONSOLE_DISCOVERY_STATE_STANDBY:
        legacy_console.status =
            CONSOLE_STATUS_CONNECTING;     // Yellow = standby/needs wake
        legacy_console.console_state = 2;  // CONSOLE_STATE_STANDBY
        break;
      case CONSOLE_DISCOVERY_STATE_UNKNOWN:
      default:
        legacy_console.status = CONSOLE_STATUS_UNKNOWN;
        legacy_console.console_state = 0;  // CONSOLE_STATE_UNKNOWN
        break;
    }
    legacy_console.signal_strength =
        (int)(console->signal_strength * 4);  // Convert 0.0-1.0 to 0-4
    legacy_console.last_connected = (uint32_t)console->last_connected;
    legacy_console.is_paired = true;  // Assume paired if in storage

    // Check authentication status for PS5 consoles
    bool is_authenticated =
        true;  // Default to authenticated for non-PS5 consoles
    if (console->console_type == PS_CONSOLE_PS5 ||
        console->console_type == PS_CONSOLE_PS5_DIGITAL) {
      // For PS5 consoles, check registration status
      is_authenticated = ui_core_is_console_registered(console->ip_address);
    }

    ui_components_render_console_card(&legacy_console, card_x, card_y,
                                      is_authenticated);

    // Render wake progress overlay if this console is being woken
    if (is_console_waking(i)) {
      render_wake_progress_overlay(card_x, card_y, i);
    }
  }
}

static void render_empty_state(void) {
  vita2d_pgf* font = ui_core_get_font();

  // Calculate vertical centering for empty state
  int content_center_y = SCREEN_HEIGHT / 2;

  // "No PlayStation consoles found" message - centered above button
  const char* empty_msg = "No PlayStation consoles found";
  float scale = 1.0f;
  int text_width = vita2d_pgf_text_width(font, scale, empty_msg);
  int start_x = (SCREEN_WIDTH - text_width) / 2;
  vita2d_pgf_draw_text(font, start_x, content_center_y - 60,
                       UI_COLOR_TEXT_SECONDARY, scale, empty_msg);

  // Large "Search for Consoles" button - perfectly centered
  render_centered_add_button(add_button_pressed &&
                             add_button_press_time < 0.2f);

  // Instructions - centered below button
  const char* instruction = "Press X or tap to add a new console";
  text_width = vita2d_pgf_text_width(font, 0.8f, instruction);
  start_x = (SCREEN_WIDTH - text_width) / 2;
  vita2d_pgf_draw_text(font, start_x, content_center_y + 80,
                       UI_COLOR_TEXT_TERTIARY, 0.8f, instruction);
}

static void render_loading_state(void) {
  vita2d_pgf* font = ui_core_get_font();

  // Center loading message vertically
  int content_center_y = SCREEN_HEIGHT / 2;

  const char* loading_msg = "Loading consoles...";
  float scale = 1.0f;
  int text_width = vita2d_pgf_text_width(font, scale, loading_msg);
  int start_x = (SCREEN_WIDTH - text_width) / 2;
  vita2d_pgf_draw_text(font, start_x, content_center_y, UI_COLOR_TEXT_PRIMARY,
                       scale, loading_msg);
}

static void render_centered_add_button(bool is_pressed) {
  // When no consoles, center the Add New button perfectly
  vita2d_pgf* font = ui_core_get_font();
  const char* button_text = "Add New";

  // Use larger font size to match "No PlayStation consoles found" text
  float font_scale = 1.0f;  // Same as the "No consoles found" message

  // Calculate button size based on text and padding - smaller than before
  int text_width = vita2d_pgf_text_width(font, font_scale, button_text);
  int button_width = text_width + 40;  // Reduced padding
  int button_height = 50;              // Smaller height

  int button_x = (SCREEN_WIDTH - button_width) / 2;
  int button_y = (SCREEN_HEIGHT - button_height) / 2;

  // Draw selection highlight with rounded corners - much more visible
  if (add_button_selected) {
    // Draw a thick, bright highlight border
    ui_core_render_rounded_rectangle(button_x - 8, button_y - 8,
                                     button_width + 16, button_height + 16, 12,
                                     UI_COLOR_PRIMARY_BLUE);
    // Add inner glow effect
    ui_core_render_rounded_rectangle(button_x - 4, button_y - 4,
                                     button_width + 8, button_height + 8, 8,
                                     0x80FF9034);  // Semi-transparent blue glow
  }

  // Apply press animation
  uint32_t button_color = UI_COLOR_PRIMARY_BLUE;
  if (is_pressed) {
    button_color = 0xFFFFB050;  // Brighter blue when pressed
  }

  // Draw rounded button background
  ui_core_render_rounded_rectangle(button_x, button_y, button_width,
                                   button_height, 8, button_color);

  // Add press animation overlay
  if (is_pressed) {
    ui_core_render_rounded_rectangle(button_x, button_y, button_width,
                                     button_height, 8, 0x40FFFFFF);
  }

  // Center text within button
  int text_x = button_x + (button_width - text_width) / 2;
  int text_y = button_y + (button_height / 2) + 8;  // Adjust for font

  vita2d_pgf_draw_text(font, text_x, text_y, RGBA8(255, 255, 255, 255),
                       font_scale, button_text);
}

// Helper function implementations
static VitaRPS5Result load_console_data(void) {
  loading_consoles = true;
  console_data_loaded = false;

  log_info("Loading console data from storage");

  // Initialize console cache
  memset(&console_cache, 0, sizeof(console_cache));

  // Load from persistent storage
  VitaRPS5Result result = console_storage_load(&console_cache);
  if (result == VITARPS5_SUCCESS) {
    log_info("Successfully loaded console data from storage");
    log_info("  Console count: %d", console_cache.console_count);

    // Clean up any mock-registered consoles
    uint32_t removed_count = 0;
    console_storage_cleanup_invalid_consoles(&console_cache, &removed_count);
    if (removed_count > 0) {
      log_info("Cleaned up %d invalid/mock consoles", removed_count);
      // Save the cleaned cache back to storage
      console_storage_save(&console_cache);
    }

    // Reset all console states to UNKNOWN for fresh detection on app start
    // This prevents stale states (like incorrect "READY") from persisting
    for (uint32_t i = 0; i < console_cache.console_count; i++) {
      log_info("  Console[%d]: %s @ %s - Loaded State: %d", i,
               console_cache.consoles[i].display_name,
               console_cache.consoles[i].ip_address,
               console_cache.consoles[i].discovery_state);

      // Reset to UNKNOWN to force fresh state detection
      ConsoleDiscoveryState old_state =
          console_cache.consoles[i].discovery_state;
      console_cache.consoles[i].discovery_state =
          CONSOLE_DISCOVERY_STATE_UNKNOWN;

      log_info(
          "    Console %s: State reset from %s to UNKNOWN (fresh detection)",
          console_cache.consoles[i].display_name,
          (old_state == CONSOLE_DISCOVERY_STATE_READY)     ? "READY"
          : (old_state == CONSOLE_DISCOVERY_STATE_STANDBY) ? "STANDBY"
                                                           : "UNKNOWN");
    }

    // CRITICAL: Validate and repair incomplete registrations during UI load
    log_info("Checking for incomplete console registrations...");
    uint32_t repaired_count = 0;
    for (uint32_t i = 0; i < console_cache.console_count; i++) {
      UIConsoleInfo* console = &console_cache.consoles[i];

      // Only check PS5 consoles that might have registrations
      if (console->console_type == PS_CONSOLE_PS5 ||
          console->console_type == PS_CONSOLE_PS5_DIGITAL) {
        if (console_registration_is_incomplete(console->ip_address)) {
          log_info(
              "Found incomplete registration for console %s (%s) - attempting "
              "repair",
              console->display_name, console->ip_address);

          VitaRPS5Result repair_result =
              console_registration_repair_incomplete(console->ip_address);
          if (repair_result == VITARPS5_SUCCESS) {
            log_info(
                "Successfully repaired incomplete registration for console %s",
                console->display_name);
            repaired_count++;
          } else {
            log_warning(
                "Failed to repair incomplete registration for console %s: %s",
                console->display_name, vitarps5_result_string(repair_result));
            log_warning("=== CONSOLE REGISTRATION INCOMPLETE ===");
            log_warning("Console: %s (%s)", console->display_name,
                        console->ip_address);
            log_warning(
                "This console will show wake buttons but wake operations will "
                "fail");
            log_warning("To fix: Remove and re-register this console");
            log_warning("=====================================");
          }
        }
      }
    }

    if (repaired_count > 0) {
      log_info("Successfully repaired %d incomplete console registration(s)",
               repaired_count);
    } else {
      log_info(
          "No incomplete registrations found - all consoles properly "
          "configured");
    }

    console_data_loaded = true;
  } else {
    log_error("Failed to load console data: %s",
              vitarps5_result_string(result));
    // Initialize empty cache on any error
    console_storage_init_default_cache(&console_cache);
    console_data_loaded = true;
  }

  loading_consoles = false;
  selected_console_index = 0;

  // Reset navigation state to prevent UI glitches when transitioning from empty
  // to populated state
  add_button_selected = (console_cache.console_count == 0);

  // Restart background thread with updated console cache
  if (console_data_loaded && console_cache.console_count > 0) {
    // Stop existing thread if running
    if (background_state_thread) {
      console_state_thread_stop(background_state_thread);
      background_state_thread = NULL;
    }

    // Start new thread with current console cache
    ConsoleStateThreadConfig thread_config;
    console_state_thread_get_default_config(&thread_config);

    VitaRPS5Result thread_result = console_state_thread_start(
        &thread_config, &console_cache, &background_state_thread);
    if (thread_result == VITARPS5_SUCCESS) {
      log_info("Background state checking thread restarted for %d consoles",
               console_cache.console_count);
    } else {
      log_warning("Failed to restart background state thread: %s",
                  vitarps5_result_string(thread_result));
    }
  } else if (background_state_thread) {
    // Stop thread if no consoles to check
    console_state_thread_stop(background_state_thread);
    background_state_thread = NULL;
    log_info("Stopped background thread - no consoles to check");
  }

  last_refresh_time = sceRtcGetTickResolution();

  log_info(
      "Console data load complete. Count: %d, Selected index: %d, Add button "
      "selected: %s",
      console_cache.console_count, selected_console_index,
      add_button_selected ? "true" : "false");

  return result;
}

static void update_animations(float delta_time) {
  // Update add button press animation
  if (add_button_pressed) {
    add_button_press_time += delta_time;
    if (add_button_press_time >= 0.2f) {  // 200ms press animation
      add_button_pressed = false;
      add_button_press_time = 0.0f;
    }
  }

  // Update console button press animation
  if (console_button_press_time >= 0.0f) {
    console_button_press_time += delta_time;
    if (console_button_press_time >= 0.2f) {  // 200ms press animation
      console_button_press_time = -1.0f;      // Reset
    }
  }
}

static const char* get_console_type_string(PSConsoleType type) {
  switch (type) {
    case PS_CONSOLE_PS4:
      return "PlayStation 4";
    case PS_CONSOLE_PS4_PRO:
      return "PS4 Pro";
    case PS_CONSOLE_PS5:
      return "PlayStation 5";
    case PS_CONSOLE_PS5_DIGITAL:
      return "PS5 Digital";
    default:
      return "PlayStation";
  }
}

// Touch input implementation
static void handle_touch_input(void) {
  SceTouchData touch_data;
  int touch_result = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch_data, 1);

  if (touch_result < 0 || touch_data.reportNum == 0) {
    touch_active = false;
    return;
  }

  // Convert touch coordinates to screen coordinates
  float touch_x = (float)touch_data.report[0].x * 960.0f / 1920.0f;
  float touch_y = (float)touch_data.report[0].y * 544.0f / 1088.0f;

  // Only process new touches (prevent multiple triggers)
  bool is_new_touch = !touch_active || (fabs(touch_x - last_touch_x) > 10.0f ||
                                        fabs(touch_y - last_touch_y) > 10.0f);

  if (!is_new_touch) {
    return;
  }

  touch_active = true;
  last_touch_x = touch_x;
  last_touch_y = touch_y;

  log_info("Touch detected at (%.1f, %.1f)", touch_x, touch_y);

  // Skip wave navigation area (handled by ui_navigation)
  if (touch_x <= WAVE_NAV_WIDTH) {
    return;
  }

  if (console_cache.console_count > 0) {
    // Check console cards
    int screen_center_x = SCREEN_WIDTH / 2;
    int total_cards_height = console_cache.console_count * 120;
    int content_start_y = (SCREEN_HEIGHT - total_cards_height) / 2 + 30;

    for (uint32_t i = 0; i < console_cache.console_count; i++) {
      int card_x = screen_center_x - (CONSOLE_CARD_WIDTH / 2);
      int card_y = content_start_y + (i * 120);

      // Convert UIConsoleInfo to legacy ConsoleInfo for wake button check
      UIConsoleInfo* ui_console = &console_cache.consoles[i];
      ConsoleInfo legacy_console = {0};
      strncpy(legacy_console.console_name, ui_console->display_name,
              sizeof(legacy_console.console_name) - 1);
      strncpy(legacy_console.console_type,
              get_console_type_string(ui_console->console_type),
              sizeof(legacy_console.console_type) - 1);
      strncpy(legacy_console.ip_address, ui_console->ip_address,
              sizeof(legacy_console.ip_address) - 1);

      // Map discovery_state to console_state for UI compatibility
      switch (ui_console->discovery_state) {
        case CONSOLE_DISCOVERY_STATE_READY:
          legacy_console.console_state = 1;  // CONSOLE_STATE_READY
          break;
        case CONSOLE_DISCOVERY_STATE_STANDBY:
          legacy_console.console_state = 2;  // CONSOLE_STATE_STANDBY
          break;
        default:
          legacy_console.console_state = 0;  // CONSOLE_STATE_UNKNOWN
          break;
      }

      // Check for wake button hit first (only for STANDBY consoles)
      if (ui_components_check_wake_button_hit(&legacy_console, card_x, card_y,
                                              touch_x, touch_y)) {
        log_info("Wake button touched for console %d (%s)", i,
                 ui_console->display_name);
        selected_console_index = i;
        trigger_console_wake_action(i);  // New wake-specific action
        return;
      }

      // Check for register button hit (only for unregistered STANDBY consoles)
      if (ui_components_check_register_button_hit(&legacy_console, card_x,
                                                  card_y, touch_x, touch_y)) {
        log_info("Register button touched for console %d (%s)", i,
                 ui_console->display_name);
        selected_console_index = i;
        trigger_console_register_action(i);  // New register-specific action
        return;
      }

      // Check for regular console card hit
      if (is_point_in_button(touch_x, touch_y, card_x, card_y,
                             CONSOLE_CARD_WIDTH, 100)) {
        log_info("Console card %d touched", i);
        selected_console_index = i;
        trigger_console_connect_action(i);
        return;
      }
    }

    // Check "Add New" button (when consoles exist)
    UIAssets* assets = ui_core_get_assets();
    int button_height = assets->button_add_new
                            ? vita2d_texture_get_height(assets->button_add_new)
                            : 50;
    int button_width = assets->button_add_new
                           ? vita2d_texture_get_width(assets->button_add_new)
                           : 200;
    int button_x = (SCREEN_WIDTH - button_width) / 2;
    int button_y = SCREEN_HEIGHT - 100 - button_height;

    if (is_point_in_button(touch_x, touch_y, button_x, button_y, button_width,
                           button_height)) {
      log_info("Add console button touched");
      trigger_add_console_action();
      return;
    }
  } else {
    // Check centered "Add New" button (empty state)
    UIAssets* assets = ui_core_get_assets();
    int button_width = assets->button_add_new
                           ? vita2d_texture_get_width(assets->button_add_new)
                           : 200;
    int button_height = assets->button_add_new
                            ? vita2d_texture_get_height(assets->button_add_new)
                            : 50;
    int button_x = (SCREEN_WIDTH - button_width) / 2;
    int button_y = (SCREEN_HEIGHT - button_height) / 2;

    if (is_point_in_button(touch_x, touch_y, button_x, button_y, button_width,
                           button_height)) {
      log_info("Centered add console button touched");
      trigger_add_console_action();
      return;
    }
  }
}

static bool is_point_in_button(float x, float y, int button_x, int button_y,
                               int button_w, int button_h) {
  return (x >= button_x && x <= button_x + button_w && y >= button_y &&
          y <= button_y + button_h);
}

static void trigger_add_console_action(void) {
  log_info("=== ADD NEW BUTTON PRESSED ===");
  log_info("Current console count: %d", console_cache.console_count);
  log_info("Starting console discovery scan");

  // Set add button animation
  add_button_pressed = true;
  add_button_press_time = 0.0f;

  log_info("Calling ui_core_start_discovery()...");
  VitaRPS5Result result = ui_core_start_discovery();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to start discovery: %s", vitarps5_result_string(result));
  } else {
    log_info(
        "Discovery started successfully, should transition to ADD_CONNECTION "
        "state");
  }
}

static void trigger_console_connect_action(uint32_t console_index) {
  log_info("=== TRIGGER_CONSOLE_CONNECT_ACTION CALLED ===");
  log_info("Console index: %d", console_index);
  log_info("Console cache count: %d", console_cache.console_count);

  // RESEARCHER E) PATCH: Add clear log at the actual UI handler entry
  if (console_index < console_cache.console_count) {
    UIConsoleInfo* console = &console_cache.consoles[console_index];
    log_info("UI: Start pressed (is_registered=%d, state=%d)",
             console->is_registered, console->discovery_state);
  }

  if (console_index >= console_cache.console_count) {
    log_error("Invalid console index: %d (max: %d)", console_index,
              console_cache.console_count);
    return;
  }

  UIConsoleInfo* console = &console_cache.consoles[console_index];
  log_info("Console info retrieved:");
  log_info("  Name: %s", console->display_name);
  log_info("  IP: %s", console->ip_address);
  log_info("  Type: %d", console->console_type);
  log_info("  Discovery state: %d", console->discovery_state);

  // RESEARCHER FIX C: Enhanced precondition checks using unified accessor
  log_info("=== SESSION PRECONDITION CHECKS ===");

  // 1. Check registration credentials using unified accessor
  log_info("1. Checking registration credentials (unified accessor)...");
  RegistrationCredentials creds;
  bool has_valid_creds = registration_get_by_ip(console->ip_address, &creds);

  if (!has_valid_creds) {
    log_error("❌ PRECONDITION FAILED: No valid credentials for %s",
              console->ip_address);
    log_error("   Unified accessor returned: %s",
              has_valid_creds ? "valid" : "invalid/missing");

    // Check if it's a corrupted registration that needs re-pairing
    bool is_registered_storage =
        ui_core_is_console_registered(console->ip_address);
    if (is_registered_storage) {
      log_error(
          "   Split-brain detected: Storage says REGISTERED but credentials "
          "invalid");
      log_error(
          "   This indicates registration key corruption - RE-PAIRING "
          "REQUIRED");
      log_error("   Solution: Re-pair this console to get fresh credentials");
      // TODO: Show UI error banner: "Registration corrupted; re-pair required"
    } else {
      log_error("   Solution: Please register this console first");
      // TODO: Show UI error banner: "Console not registered - please register
      // first"
    }

    trigger_console_register_action(console_index);
    return;
  }

  log_info("✅ Registration credentials: VALID");
  log_info("   Regkey: %.4s... (8 chars)", creds.regkey_hex8);
  log_info("   Console: %s", creds.console_name);
  log_info("   Wake cred: %s", creds.wake_credential_dec);

  // 2. Check PSN account status
  log_info("2. Checking PSN account status...");
  char psn_id_test[32];
  ui_profile_get_psn_id_base64(psn_id_test, sizeof(psn_id_test));
  bool psn_valid = (strlen(psn_id_test) > 0);
  if (!psn_valid) {
    log_error("❌ PRECONDITION FAILED: PSN account not configured");
    log_error("Solution: Please set your PSN account in the Profile section");
    // TODO: Show UI error banner: "PSN account not configured - check Profile
    // settings"
    return;
  }
  log_info("✅ PSN account: CONFIGURED (%s)", psn_id_test);

  // 3. Check console state (advisory only)
  log_info("3. Checking console state...");
  if (console->discovery_state == CONSOLE_DISCOVERY_STATE_UNKNOWN) {
    log_warning("⚠️  Console state unknown - will attempt connection anyway");
  } else if (console->discovery_state == CONSOLE_DISCOVERY_STATE_STANDBY) {
    log_info("💤 Console in standby - will send wake packet first");
  } else {
    log_info("✅ Console state: READY for connection");
  }

  log_info("=== ALL PRECONDITIONS PASSED - PROCEEDING ===");

  // RESEARCHER E) PATCH: Call centralized preconditions function directly with
  // available data Since PS5ConsoleInfo doesn't have is_registered field, call
  // the session manager logic directly
  log_info("UI: Start pressed (is_registered=%d, state=%d)",
           console->is_registered, console->discovery_state);

  // RESEARCHER FIX 1: Use unified accessor for registration check instead of
  // stale cache
  RegistrationCredentials verify_creds;
  bool has_valid_registration =
      registration_get_by_ip(console->ip_address, &verify_creds) &&
      verify_creds.is_valid;
  if (!has_valid_registration) {
    log_error(
        "❌ UI precondition failed: console not registered (unified accessor)");
    log_error("   Unified accessor result: %s",
              has_valid_registration ? "valid" : "invalid/missing");
    return;
  }

  log_info("✅ UI preconditions passed - dispatching session start");

  // RESEARCHER FIX 4: Add the 3 critical session start breadcrumbs for
  // debugging Breadcrumb 1: SESSION_START pressed
  log_info("SESSION_START pressed for %s (registered=1, state=%s)",
           console->ip_address,
           console->discovery_state == CONSOLE_DISCOVERY_STATE_READY ? "READY"
           : console->discovery_state == CONSOLE_DISCOVERY_STATE_STANDBY
               ? "STANDBY"
               : "UNKNOWN");

  // CRITICAL FIX: Check for incomplete registrations and repair them
  // automatically
  if (console_registration_is_incomplete(console->ip_address)) {
    log_info(
        "Detected incomplete registration for console %s - attempting repair",
        console->display_name);
    VitaRPS5Result repair_result =
        console_registration_repair_incomplete(console->ip_address);
    if (repair_result == VITARPS5_SUCCESS) {
      log_info("Successfully repaired incomplete registration for console %s",
               console->display_name);
    } else {
      log_error("Failed to repair incomplete registration for console %s: %s",
                console->display_name, vitarps5_result_string(repair_result));

      // ENHANCED ERROR HANDLING: Provide clear user feedback
      log_error("=== REGISTRATION REPAIR FAILED ===");
      log_error("Console: %s (%s)", console->display_name, console->ip_address);
      log_error("Issue: Missing wake credentials required for PS5 remote wake");
      log_error(
          "Solution: Re-register the console to generate wake credentials");
      log_error(
          "Note: Wake credentials are automatically generated during "
          "registration");
      log_error("===============================");

      // For now, redirect to registration - in future versions we can add
      // manual PIN entry
      log_info("Redirecting to registration screen for manual re-registration");
      trigger_console_register_action(console_index);
      return;
    }
  }

  log_info("=== CONSOLE INTERACTION ATTEMPT ===");
  log_info("Console: %s (%s)", console->display_name, console->ip_address);
  log_info("Console type: %d", console->console_type);
  log_info("Console discovery state: %d", console->discovery_state);

  // Set console button animation
  console_button_press_time = 0.0f;

  // Determine console version from type
  uint8_t console_version = (console->console_type == PS_CONSOLE_PS5) ? 12 : 7;
  log_info("Using console version: %d for %s", console_version,
           (console->console_type == PS_CONSOLE_PS5) ? "PS5" : "PS4");

  if (console->discovery_state == CONSOLE_DISCOVERY_STATE_READY) {
    // CRITICAL FIX 3: Don't send wake when console is already READY
    // This eliminates unnecessary noise that can cause state flips during
    // session init
    log_info("Console %s detected as READY - skipping wake (already awake)",
             console->display_name);

    // CRITICAL FIX: Add timeout and error handling to prevent UI freeze
    log_info("Attempting Remote Play connection to %s...",
             console->display_name);

    VitaRPS5Result result =
        ui_core_start_streaming(console->ip_address, console_version);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to start Remote Play: %s",
                vitarps5_result_string(result));

      // Provide helpful error message to user based on error type
      if (result == VITARPS5_ERROR_NETWORK) {
        log_error(
            "Network connection failed - check if console is on same network");
      } else if (result == VITARPS5_ERROR_TIMEOUT) {
        log_error("Connection timed out - console may not be responding");
      } else if (result == VITARPS5_ERROR_AUTH_FAILED) {
        log_error("Authentication failed - may need to re-register console");
      } else {
        log_error("Connection failed with error code: %s",
                  vitarps5_result_string(result));
      }
    } else {
      log_info(
          "Remote Play started successfully - transitioning to streaming UI");
    }
  } else {
    // Console sleeping or unknown - wake then connect (VITAKI APPROACH)
    const char* status_str =
        (console->discovery_state == CONSOLE_DISCOVERY_STATE_STANDBY)
            ? "STANDBY"
        : (console->discovery_state == CONSOLE_DISCOVERY_STATE_UNKNOWN)
            ? "UNKNOWN"
            : "INVALID";

    log_info(
        "Console %s is in %s state - attempting wake signal "
        "(Vitaki-compatible)",
        console->display_name, status_str);

    // CRITICAL FIX: Use non-blocking wake instead of session_wake_and_wait
    // which was causing 48+ second hangs
    log_info("Sending wake packet to console %s at %s", console->display_name,
             console->ip_address);

    // Convert console info to PS5ConsoleInfo for non-blocking wake function
    PS5ConsoleInfo ps5_console = {0};
    strncpy(ps5_console.ip_address, console->ip_address,
            sizeof(ps5_console.ip_address) - 1);
    strncpy(ps5_console.device_name, console->display_name,
            sizeof(ps5_console.device_name) - 1);
    ps5_console.console_type = console->console_type;

    // Send wake packet (non-blocking) - same as wake button functionality
    VitaRPS5Result wake_result = ps5_discovery_wake_console(&ps5_console);

    if (wake_result == VITARPS5_SUCCESS) {
      log_info("✓ Wake packet sent successfully to console %s",
               console->display_name);

      // Wake confirmation is now handled inside ps5_discovery_wake_console()
      // It will send discovery broadcasts and wait for actual response
      log_info("Wake process completed for console %s", console->display_name);

    } else if (wake_result == VITARPS5_ERROR_NOT_REGISTERED) {
      log_info(
          "Console %s not registered - skipping wake delay, attempting direct "
          "connection",
          console->display_name);
      log_info("Note: Console registration required for wake functionality");
    } else {
      log_error("Wake signal failed for console %s: %s", console->display_name,
                vitarps5_result_string(wake_result));

      if (wake_result == VITARPS5_ERROR_TIMEOUT) {
        log_error(
            "Console %s did not respond to wake signal - may be powered off",
            console->display_name);
        log_info(
            "Trying connection anyway in case console is already awake...");
      } else {
        log_info("Continuing to connection attempt despite wake failure...");
      }
    }

    // Start connection after wake attempt (non-blocking)
    log_info("Attempting Remote Play connection to %s after wake...",
             console->display_name);

    VitaRPS5Result result =
        ui_core_start_streaming(console->ip_address, console_version);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to start Remote Play after wake attempt: %s",
                vitarps5_result_string(result));

      // Provide helpful error message based on error type
      if (result == VITARPS5_ERROR_NETWORK) {
        log_error(
            "Network connection failed - console may still be waking up or "
            "powered off");
        log_info("Try waiting a few seconds and attempting connection again");
      } else if (result == VITARPS5_ERROR_TIMEOUT) {
        log_error(
            "Connection timed out - console may not have responded to wake "
            "signal");
        log_info(
            "Check if console is properly connected to network and try again");
      } else if (result == VITARPS5_ERROR_AUTH_FAILED) {
        log_error("Authentication failed - registration may be invalid");
        log_info("Consider re-registering this console");
      } else {
        log_error("Connection failed with error: %s",
                  vitarps5_result_string(result));
      }
    } else {
      log_info("Remote Play started successfully after wake sequence");
    }
  }
}

static void trigger_console_remove_action(uint32_t console_index) {
  if (console_index >= console_cache.console_count) {
    log_error("Invalid console index for removal: %d", console_index);
    return;
  }

  UIConsoleInfo* console = &console_cache.consoles[console_index];

  log_info("=== CONSOLE REMOVAL TRIGGERED ===");
  log_info("Removing console: %s (%s)", console->display_name,
           console->ip_address);
  log_info("Console type: %d", console->console_type);

  // Remove the console from cache
  VitaRPS5Result result =
      console_storage_remove_console(&console_cache, console->host_id);
  if (result == VITARPS5_SUCCESS) {
    log_info("Successfully removed console %s from storage",
             console->display_name);

    // Also remove any registration data for this console
    log_info("CONSOLE REMOVAL DEBUG: About to remove registration for %s",
             console->ip_address);
    VitaRPS5Result reg_remove_result =
        console_registration_remove(console->ip_address);
    log_info("CONSOLE REMOVAL DEBUG: Registration removal result: %s",
             vitarps5_result_string(reg_remove_result));

    // CRITICAL DEBUG: Immediately verify registration was removed
    bool still_registered = ui_core_is_console_registered(console->ip_address);
    log_info("CONSOLE REMOVAL DEBUG: Registration check after removal: %s",
             still_registered ? "STILL_REGISTERED" : "NOT_REGISTERED");

    // Save the updated cache
    result = console_storage_save(&console_cache);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to save console cache after removal: %s",
                vitarps5_result_string(result));
    }

    // Adjust selected index if needed
    if (selected_console_index >= console_cache.console_count &&
        console_cache.console_count > 0) {
      selected_console_index = console_cache.console_count - 1;
    } else if (console_cache.console_count == 0) {
      selected_console_index = 0;
      add_button_selected = true;
    }

    log_info("Console removal complete. Remaining consoles: %d",
             console_cache.console_count);
  } else {
    log_error("Failed to remove console: %s", vitarps5_result_string(result));
  }
}

static void trigger_console_wake_action(uint32_t console_index) {
  if (console_index >= console_cache.console_count) {
    log_error("Invalid console index for wake action: %d", console_index);
    return;
  }

  UIConsoleInfo* console = &console_cache.consoles[console_index];

  log_info("=== CONSOLE WAKE ACTION TRIGGERED ===");
  log_info("Waking console: %s (%s)", console->display_name,
           console->ip_address);
  log_info("Console type: %d", console->console_type);
  log_info("Current discovery state: %d", console->discovery_state);

  // Only wake STANDBY consoles - provide specific error messages for other
  // states
  if (console->discovery_state != CONSOLE_DISCOVERY_STATE_STANDBY) {
    log_warning("Console %s is not in STANDBY state - current state: %d",
                console->display_name, console->discovery_state);

    // Start wake progress to show error message
    start_wake_progress(console_index);

    // Set specific error message based on console state
    switch (console->discovery_state) {
      case CONSOLE_DISCOVERY_STATE_READY:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console is already awake and ready");
        wake_progress.final_state = CONSOLE_STATE_READY;
        break;
      case CONSOLE_DISCOVERY_STATE_UNKNOWN:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console state unknown - check connection");
        wake_progress.final_state = CONSOLE_STATE_UNKNOWN;
        break;
      default:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console cannot be woken - state: %d",
                 console->discovery_state);
        wake_progress.final_state = CONSOLE_STATE_UNKNOWN;
        break;
    }

    return;
  }

  // RESEARCHER FIX 1: Use unified accessor for wake registration check
  // Wake operations require legitimate registration keys (no placeholders work)
  RegistrationCredentials wake_creds;
  bool is_registered =
      registration_get_by_ip(console->ip_address, &wake_creds) &&
      wake_creds.is_valid;
  if (!is_registered) {
    log_error("✗ Console %s must be registered before wake operations can work",
              console->display_name);
    log_info(
        "Wake functionality requires legitimate PlayStation registration keys");
    log_info(
        "Please register this console through the Add Console -> Registration "
        "flow");

    // Start wake progress to show user-friendly error message
    start_wake_progress(console_index);
    snprintf(wake_progress.wake_status_message,
             sizeof(wake_progress.wake_status_message),
             "Console must be registered first");
    wake_progress.final_state = CONSOLE_STATE_UNKNOWN;

    return;
  }

  log_info("✓ Console %s is properly registered - wake operation should work",
           console->display_name);

  // NOTE: Comprehensive validation temporarily disabled to prevent UI freeze
  // The validation function has buffer safety issues that cause freezes
  // TODO: Fix console_registration_validate_wake_credential_flow() buffer
  // issues
  log_debug(
      "Skipping comprehensive wake credential validation to prevent freeze");

  // Convert console info to PS5ConsoleInfo for wake function
  PS5ConsoleInfo ps5_console = {0};
  strncpy(ps5_console.ip_address, console->ip_address,
          sizeof(ps5_console.ip_address) - 1);
  strncpy(ps5_console.device_name, console->display_name,
          sizeof(ps5_console.device_name) - 1);
  ps5_console.console_type = console->console_type;

  // Call the wake function from ps5_discovery (just send wake packet -
  // non-blocking)
  log_info("Sending wake signal to console %s at %s...", console->display_name,
           console->ip_address);
  VitaRPS5Result wake_result = ps5_discovery_wake_console(&ps5_console);

  if (wake_result == VITARPS5_SUCCESS) {
    log_info("✓ Wake signal sent successfully to console %s",
             console->display_name);

    // Start wake progress tracking for UI feedback
    start_wake_progress(console_index);

    log_info(
        "Console %s wake packet sent - showing 15-second progress indicator",
        console->display_name);

    // The background state checking thread will detect when console
    // actually wakes up and update from STANDBY -> READY automatically

  } else {
    log_error("✗ Failed to send wake signal to console %s: %s",
              console->display_name, vitarps5_result_string(wake_result));

    // Start wake progress to show error message
    start_wake_progress(console_index);

    // Set specific error message based on failure type
    switch (wake_result) {
      case VITARPS5_ERROR_NETWORK:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Network error - check connection");
        break;
      case VITARPS5_ERROR_NOT_REGISTERED:
      case VITARPS5_ERROR_AUTH_FAILED:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console not registered - register first");
        break;
      case VITARPS5_ERROR_INVALID_PARAM:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Invalid console IP address");
        break;
      case VITARPS5_ERROR_TIMEOUT:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Wake signal timeout - console may be off");
        break;
      case VITARPS5_ERROR_NOT_CONNECTED:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console unreachable - check network");
        break;
      default:
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Wake failed - error code: %d", wake_result);
        break;
    }

    wake_progress.final_state = CONSOLE_STATE_UNKNOWN;
  }
}

static void trigger_console_register_action(uint32_t console_index) {
  log_info("=== TRIGGER_CONSOLE_REGISTER_ACTION CALLED ===");
  log_info("Console index: %d", console_index);
  log_info("Console cache count: %d", console_cache.console_count);

  if (console_index >= console_cache.console_count) {
    log_error("Invalid console index for registration: %d (max: %d)",
              console_index, console_cache.console_count);
    return;
  }

  UIConsoleInfo* console = &console_cache.consoles[console_index];

  log_info("=== CONSOLE REGISTRATION REQUESTED ===");
  log_info("Console: %s (%s)", console->display_name, console->ip_address);
  log_info("Console type: %d", console->console_type);

  // Check if already registered (defensive check)
  log_info("Performing defensive registration check...");
  bool already_registered = ui_core_is_console_registered(console->ip_address);
  log_info("Defensive check result: %s",
           already_registered ? "ALREADY_REGISTERED" : "NOT_REGISTERED");

  if (already_registered) {
    log_warning("Console %s is already registered - aborting registration",
                console->ip_address);
    return;
  }

  log_info("Starting registration process for console %s",
           console->display_name);

  // Initialize registration UI if not already done
  log_info("Initializing registration UI...");
  VitaRPS5Result init_result = ui_registration_init();
  log_info("Registration UI init result: %s",
           vitarps5_result_string(init_result));

  if (init_result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize registration UI: %s",
              vitarps5_result_string(init_result));
    log_error("REGISTRATION FLOW BLOCKED: UI initialization failed");

    // Give user clear information about the issue
    log_error("=== REGISTRATION UI UNAVAILABLE ===");
    log_error("Issue: Network modules failed to initialize");
    log_error("This prevents the PIN entry screen from working");
    log_error("Possible causes:");
    log_error("  - Network already in use by another app");
    log_error("  - Insufficient system memory");
    log_error("  - System network conflict");
    log_error("===================================");

    // TODO: In future versions, show error dialog to user and retry option
    // For now, don't automatically mock-register - let user retry by pressing X
    // again
    log_info("Registration failed - user can retry by pressing X again");
    log_info("Future versions will show error dialog with retry option");
    log_info(
        "NOTE: If console was previously mock-registered, press CIRCLE to "
        "remove it first");
    return;
  }

  // Prepare registration configuration
  log_info("Preparing registration configuration...");
  RegistrationUIConfig reg_config = {0};
  strncpy(reg_config.console_name, console->display_name,
          sizeof(reg_config.console_name) - 1);
  strncpy(reg_config.console_ip, console->ip_address,
          sizeof(reg_config.console_ip) - 1);

  // Set console target based on type
  // Console type is determined automatically in clean slate implementation

  // Get PSN account ID from profile (both formats)
  reg_config.psn_account_id = ui_profile_get_psn_account_number();
  ui_profile_get_psn_id_base64(reg_config.psn_account_id_base64,
                               sizeof(reg_config.psn_account_id_base64));

  log_info("Registration config prepared:");
  log_info("  Console name: %s", reg_config.console_name);
  log_info("  Console IP: %s", reg_config.console_ip);
  log_info("  PSN account ID: %llu", reg_config.psn_account_id);

  // Debug: Verify PSN account ID is valid
  if (reg_config.psn_account_id == 0) {
    log_error("PSN account ID is 0 - registration will likely fail");
  } else {
    log_debug("PSN account ID appears valid for registration");
  }

  // Start registration UI
  log_info("Starting registration UI...");
  VitaRPS5Result reg_result = ui_registration_start(&reg_config);
  log_info("Registration UI start result: %s",
           vitarps5_result_string(reg_result));

  if (reg_result != VITARPS5_SUCCESS) {
    log_error("Failed to start registration UI: %s",
              vitarps5_result_string(reg_result));
    log_error("REGISTRATION FLOW BLOCKED: UI start failed");
    return;
  }

  // Transition to registration UI state
  log_info("Transitioning to registration UI for console %s",
           console->display_name);
  log_info("Setting UI state to UI_STATE_REGISTRATION...");
  ui_core_set_state(UI_STATE_REGISTRATION);
  log_info("UI state transition complete");
  log_info("=== REGISTRATION TRIGGER COMPLETE ===");
}

// REMOVED: check_existing_console_states() function
// Replaced by background thread in console_state_thread.c for non-blocking UI
// The background thread automatically updates the console_cache with current
// states

// Wake Progress Management Functions

static void start_wake_progress(uint32_t console_index) {
  wake_progress.is_waking = true;
  wake_progress.waking_console_index = console_index;
  wake_progress.wake_start_time =
      sceKernelGetSystemTimeWide() / 1000;  // Convert to ms
  wake_progress.wake_timeout_seconds =
      VITARPS5_TIMEOUT_SECONDS;  // Global timeout for PS5 wake
  wake_progress.confirmation_started = false;
  wake_progress.final_state = CONSOLE_STATE_UNKNOWN;
  snprintf(wake_progress.wake_status_message,
           sizeof(wake_progress.wake_status_message), "Sending wake signal...");

  log_info("Started wake progress tracking for console index %d",
           console_index);
}

static void update_wake_progress(void) {
  if (!wake_progress.is_waking) {
    return;
  }

  uint64_t current_time = sceKernelGetSystemTimeWide() / 1000;  // Convert to ms
  uint64_t elapsed_ms = current_time - wake_progress.wake_start_time;
  uint64_t timeout_ms = wake_progress.wake_timeout_seconds * 1000;

  // Check if wake timeout has elapsed
  if (elapsed_ms >= timeout_ms) {
    // Final confirmation attempt before timeout
    if (wake_progress.waking_console_index < console_cache.console_count) {
      UIConsoleInfo* console =
          &console_cache.consoles[wake_progress.waking_console_index];
      ConsoleState final_state;
      VitaRPS5Result result = ps5_discovery_wait_for_ready(
          console->ip_address, console->console_type, 1000, &final_state);

      if (result == VITARPS5_SUCCESS && final_state == CONSOLE_STATE_READY) {
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console awakened successfully!");
        wake_progress.final_state = CONSOLE_STATE_READY;
        log_info("Console wake confirmed at timeout - success");
      } else {
        // Provide specific timeout error messages based on final state
        switch (final_state) {
          case CONSOLE_STATE_STANDBY:
            snprintf(wake_progress.wake_status_message,
                     sizeof(wake_progress.wake_status_message),
                     "Console received signal but still in standby");
            break;
          case CONSOLE_STATE_UNKNOWN:
            if (result == VITARPS5_ERROR_NOT_CONNECTED ||
                result == VITARPS5_ERROR_NETWORK) {
              snprintf(wake_progress.wake_status_message,
                       sizeof(wake_progress.wake_status_message),
                       "Console unreachable - check network");
            } else {
              snprintf(wake_progress.wake_status_message,
                       sizeof(wake_progress.wake_status_message),
                       "Wake timeout - console may be off");
            }
            break;
          default:
            snprintf(wake_progress.wake_status_message,
                     sizeof(wake_progress.wake_status_message),
                     "Wake incomplete - console state: %d", final_state);
            break;
        }
        wake_progress.final_state = final_state;
        log_warning("Console wake timeout - final state: %d, result: %d",
                    final_state, result);
      }
    }

    log_info("Wake progress timeout reached, stopping progress tracking");
    stop_wake_progress();
    return;
  }

  // Start wake confirmation check after initial 3-second delay
  if (!wake_progress.confirmation_started && elapsed_ms >= 3000) {
    wake_progress.confirmation_started = true;
    snprintf(wake_progress.wake_status_message,
             sizeof(wake_progress.wake_status_message),
             "Checking console response...");
    log_info("Starting wake confirmation check");
  }

  // Perform periodic wake confirmation checks every 2 seconds after initial
  // delay
  if (wake_progress.confirmation_started && (elapsed_ms % 2000) < 100) {
    if (wake_progress.waking_console_index < console_cache.console_count) {
      UIConsoleInfo* console =
          &console_cache.consoles[wake_progress.waking_console_index];
      ConsoleState current_state;

      VitaRPS5Result result = ps5_discovery_wait_for_ready(
          console->ip_address, console->console_type, 500, &current_state);

      if (result == VITARPS5_SUCCESS && current_state == CONSOLE_STATE_READY) {
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console awakened successfully!");
        wake_progress.final_state = CONSOLE_STATE_READY;
        log_info("Console wake confirmed - console is ready");

        // Note: We'll let the timeout handle stopping the progress to show
        // success message The UI will show the success message until timeout
        return;
      } else if (current_state == CONSOLE_STATE_STANDBY) {
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console in standby - still waking...");
      } else if (result == VITARPS5_ERROR_NOT_CONNECTED) {
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Console unreachable - check network");
      } else if (result == VITARPS5_ERROR_NETWORK) {
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Network error during wake check");
      } else if (result == VITARPS5_ERROR_TIMEOUT) {
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Discovery timeout - console may be off");
      } else {
        snprintf(wake_progress.wake_status_message,
                 sizeof(wake_progress.wake_status_message),
                 "Waiting for console response...");
      }

      wake_progress.final_state = current_state;
    }
  }
}

static void stop_wake_progress(void) {
  wake_progress.is_waking = false;
  wake_progress.waking_console_index = 0;
  wake_progress.wake_start_time = 0;
  wake_progress.confirmation_started = false;
  wake_progress.final_state = CONSOLE_STATE_UNKNOWN;
  memset(wake_progress.wake_status_message, 0,
         sizeof(wake_progress.wake_status_message));
}

static bool is_console_waking(uint32_t console_index) {
  return wake_progress.is_waking &&
         wake_progress.waking_console_index == console_index;
}

static float get_wake_progress_percentage(void) {
  if (!wake_progress.is_waking) {
    return 0.0f;
  }

  uint64_t current_time = sceKernelGetSystemTimeWide() / 1000;  // Convert to ms
  uint64_t elapsed_ms = current_time - wake_progress.wake_start_time;
  uint64_t timeout_ms = wake_progress.wake_timeout_seconds * 1000;

  float percentage = (float)elapsed_ms / (float)timeout_ms;
  if (percentage > 1.0f) percentage = 1.0f;

  return percentage;
}

static void render_wake_progress_overlay(int card_x, int card_y,
                                         uint32_t console_index) {
  vita2d_pgf* font = ui_core_get_font();

  // Get wake progress information
  float progress = get_wake_progress_percentage();
  uint64_t current_time = sceKernelGetSystemTimeWide() / 1000;
  uint64_t elapsed_ms = current_time - wake_progress.wake_start_time;
  uint32_t remaining_seconds =
      wake_progress.wake_timeout_seconds - (elapsed_ms / 1000);

  // Ensure remaining seconds doesn't go negative
  if (remaining_seconds > wake_progress.wake_timeout_seconds) {
    remaining_seconds = 0;
  }

  // Semi-transparent overlay background
  const uint32_t overlay_color = 0x80000000;  // 50% transparent black
  vita2d_draw_rectangle(card_x, card_y, CONSOLE_CARD_WIDTH, 100, overlay_color);

  // Progress bar background
  int progress_bar_x = card_x + 20;
  int progress_bar_y = card_y + 30;
  int progress_bar_width = CONSOLE_CARD_WIDTH - 40;
  int progress_bar_height = 8;

  const uint32_t progress_bg_color = 0xFF333333;  // Dark gray
  vita2d_draw_rectangle(progress_bar_x, progress_bar_y, progress_bar_width,
                        progress_bar_height, progress_bg_color);

  // Progress bar fill - color based on state
  int fill_width = (int)(progress_bar_width * progress);
  uint32_t progress_fill_color = 0xFF00AA00;  // Default green

  // Change progress bar color for error states
  if (strstr(wake_progress.wake_status_message, "error") != NULL ||
      strstr(wake_progress.wake_status_message, "failed") != NULL ||
      strstr(wake_progress.wake_status_message, "timeout") != NULL ||
      strstr(wake_progress.wake_status_message, "unreachable") != NULL ||
      strstr(wake_progress.wake_status_message, "must be registered") != NULL ||
      strstr(wake_progress.wake_status_message, "cannot be woken") != NULL) {
    progress_fill_color = 0xFFAA0000;  // Red for errors
  } else if (wake_progress.final_state == CONSOLE_STATE_READY) {
    progress_fill_color = 0xFF00AA00;  // Bright green for success
  } else if (wake_progress.final_state == CONSOLE_STATE_STANDBY) {
    progress_fill_color = 0xFFAA8800;  // Orange for standby
  }

  vita2d_draw_rectangle(progress_bar_x, progress_bar_y, fill_width,
                        progress_bar_height, progress_fill_color);

  // Dynamic wake status text
  const char* wake_text = strlen(wake_progress.wake_status_message) > 0
                              ? wake_progress.wake_status_message
                              : "Waking PS5...";
  float text_scale = 0.9f;
  int text_width = vita2d_pgf_text_width(font, text_scale, wake_text);
  int text_x = card_x + (CONSOLE_CARD_WIDTH - text_width) / 2;
  int text_y = card_y + 20;

  // Choose text color based on final state and message content
  uint32_t text_color = UI_COLOR_TEXT_PRIMARY;
  if (wake_progress.final_state == CONSOLE_STATE_READY) {
    text_color = UI_COLOR_STATUS_AVAILABLE;  // Green for success
  } else if (wake_progress.final_state == CONSOLE_STATE_STANDBY) {
    text_color = UI_COLOR_STATUS_CONNECTING;  // Orange for standby
  } else if (strstr(wake_progress.wake_status_message, "error") != NULL ||
             strstr(wake_progress.wake_status_message, "failed") != NULL ||
             strstr(wake_progress.wake_status_message, "timeout") != NULL ||
             strstr(wake_progress.wake_status_message, "unreachable") != NULL ||
             strstr(wake_progress.wake_status_message, "must be registered") !=
                 NULL ||
             strstr(wake_progress.wake_status_message, "cannot be woken") !=
                 NULL) {
    text_color = UI_COLOR_STATUS_UNAVAILABLE;  // Red for errors
  } else if (strstr(wake_progress.wake_status_message, "already awake") !=
             NULL) {
    text_color = UI_COLOR_STATUS_AVAILABLE;  // Green for already awake
  }

  vita2d_pgf_draw_text(font, text_x, text_y, text_color, text_scale, wake_text);

  // Countdown timer (only show if not confirmed successful)
  if (wake_progress.final_state != CONSOLE_STATE_READY) {
    char countdown_text[32];
    snprintf(countdown_text, sizeof(countdown_text), "%d seconds remaining",
             remaining_seconds);

    float countdown_scale = 0.7f;
    int countdown_width =
        vita2d_pgf_text_width(font, countdown_scale, countdown_text);
    int countdown_x = card_x + (CONSOLE_CARD_WIDTH - countdown_width) / 2;
    int countdown_y = card_y + 55;

    vita2d_pgf_draw_text(font, countdown_x, countdown_y,
                         UI_COLOR_TEXT_SECONDARY, countdown_scale,
                         countdown_text);
  }
}

// Access function for session manager to pause/resume background threads
ConsoleStateThread* ui_dashboard_get_state_thread(void) {
  return background_state_thread;
}
