#include "ui_profile.h"

#include <math.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <string.h>

#include "../core/profile_storage.h"
#include "../discovery/ps5_discovery.h"
#include "../psn/psn_account.h"
#include "../psn/psn_id_utils.h"
#include "../system/vita_system_info.h"
#include "../utils/logger.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "vita2d_ui.h"

// Profile layout constants (sized for Vita 960x544 screen)
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define PROFILE_CARD_RADIUS 8
#define PROFILE_CARD_MARGIN 15
#define PROFILE_CARD_PADDING 15

// Left panel (User Profile & Authentication) - Positioned to match Controller
// Config table
#define LEFT_PANEL_X 180  // Match Controller Configuration table positioning
#define LEFT_PANEL_Y 120
#define LEFT_PANEL_WIDTH 340  // Reduced width to make room for repositioning
#define USER_CARD_HEIGHT 150
#define AUTH_CARD_HEIGHT \
  220  // Expanded with extra 20px bottom padding for button
#define CARD_SPACING 20

// Right panel (Connection Information) - Positioned with more right margin
#define RIGHT_PANEL_WIDTH 380  // Narrower table with better right margin
#define RIGHT_PANEL_X \
  (960 - RIGHT_PANEL_WIDTH - 25)  // 25px margin from right edge
#define RIGHT_PANEL_Y 120
#define RIGHT_PANEL_HEIGHT 400  // Match Controller Config table height (400px)

// Profile state
static char psn_username[64] = "";
static char psn_password[64] = "";
static char psn_id_base64[32] = "";  // PSN ID base64 input
static int auth_input_field = 0;     // 0=username, 1=password, 2=account_number
static bool psn_authenticated = false;
static bool psn_id_valid = false;

// PSN button interaction state
static bool psn_button_selected = false;
static bool psn_button_pressed = false;
static float psn_button_press_time = 0.0f;

// System information cache
static VitaSystemInfo system_info = {0};
static VitaNetworkInfo network_info = {0};
static bool system_info_loaded = false;

// Profile storage
static ProfileData current_profile = {0};
static bool profile_loaded = false;

// PSN account for discovery integration
static PSNAccount* global_psn_account = NULL;

// Touch input state
static bool touch_active = false;
static float last_touch_x = 0.0f;
static float last_touch_y = 0.0f;

// User data loading state
static bool user_data_loaded = false;
static bool user_data_loading = false;
static VitaRPS5Result user_data_load_error = VITARPS5_SUCCESS;
static bool profile_card_selected = false;

// Forward declarations for helper functions
static void update_animations(float delta_time);
static void handle_touch_input(void);
static bool is_point_in_button(float x, float y, int button_x, int button_y,
                               int button_w, int button_h);
static void trigger_psn_button_action(void);
static void trigger_profile_card_action(void);
static void load_user_data(void);

void ui_profile_init(void) {
  log_info("Initializing profile...");

  // Initialize PSN account system
  VitaRPS5Result result = psn_account_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize PSN account system: %s",
              vitarps5_result_string(result));
  }

  // Create global PSN account instance FIRST
  PSNAccountConfig psn_config = {0};
  psn_config.enable_auto_refresh = true;
  psn_config.refresh_interval_ms = 60000;
  psn_config.cache_account_info = true;

  result = psn_account_create(&psn_config, &global_psn_account);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create PSN account instance: %s",
              vitarps5_result_string(result));
    global_psn_account = NULL;
  }

  // NOW attempt to automatically load PSN account from system registry
  log_info("=== PSN AUTHENTICATION INIT DEBUG ===");
  log_info("Checking for PSN account in system registry...");

  // System PSN ID check will be done AFTER loading profile preferences

  // Initialize profile storage
  result = profile_storage_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize profile storage: %s",
              vitarps5_result_string(result));
    return;
  }

  // Load profile data
  log_info("Loading profile data from storage...");
  result = profile_storage_load(&current_profile);
  if (result == VITARPS5_SUCCESS) {
    profile_loaded = true;
    log_info("Profile loaded successfully - PSN ID: %.8s...",
             current_profile.psn_id_base64[0] ? current_profile.psn_id_base64
                                              : "(empty)");

    // Load PSN credentials from profile
    char username[64], email[128];
    bool authenticated;
    result =
        profile_storage_get_psn_credentials(username, email, &authenticated);
    if (result == VITARPS5_SUCCESS) {
      strncpy(psn_username, username, sizeof(psn_username) - 1);
      psn_username[sizeof(psn_username) - 1] = '\0';  // Ensure null termination
      psn_authenticated = authenticated;
    }

    // Load PSN ID from profile - prioritize saved ID over system ID
    if (strlen(current_profile.psn_id_base64) > 0) {
      char saved_psn_id[32];
      strncpy(saved_psn_id, current_profile.psn_id_base64,
              sizeof(saved_psn_id) - 1);
      saved_psn_id[sizeof(saved_psn_id) - 1] = '\0';

      bool saved_id_valid = psn_id_validate_base64_format(saved_psn_id);
      if (saved_id_valid) {
        log_info(
            "Loading saved PSN ID from profile: %s (overriding system PSN ID)",
            saved_psn_id);
        strncpy(psn_id_base64, saved_psn_id, sizeof(psn_id_base64) - 1);
        psn_id_base64[sizeof(psn_id_base64) - 1] = '\0';
        psn_id_valid = true;
        // Update PSN account system and discovery
        ui_profile_set_psn_id_base64(psn_id_base64);
      } else {
        log_warning("Saved PSN ID in profile is invalid: %s", saved_psn_id);
      }
    }

    log_info("Profile loaded successfully");
  } else {
    log_warning("Failed to load profile: %s, creating default profile",
                vitarps5_result_string(result));

    // CRITICAL FIX: Create default profile when loading fails
    VitaRPS5Result create_result =
        profile_storage_create_default(&current_profile);
    if (create_result == VITARPS5_SUCCESS) {
      profile_loaded = true;
      log_info("Default profile created successfully");

      // Save the default profile to create the file
      VitaRPS5Result save_result = profile_storage_save(&current_profile);
      if (save_result == VITARPS5_SUCCESS) {
        log_info("Default profile saved to storage");
      } else {
        log_error("Failed to save default profile: %s",
                  vitarps5_result_string(save_result));
      }
    } else {
      log_error("Failed to create default profile: %s",
                vitarps5_result_string(create_result));
    }
  }

  // Now check system PSN account as fallback if no saved PSN ID was loaded
  if (!psn_id_valid) {
    log_info("No saved PSN ID found - checking system PSN account as fallback");
    bool has_system_account = psn_id_system_has_account();
    log_info("psn_id_system_has_account() returned: %s",
             has_system_account ? "true" : "false");

    if (has_system_account) {
      char system_psn_id[32];
      VitaRPS5Result sys_result =
          psn_id_read_from_system(system_psn_id, sizeof(system_psn_id));
      log_info("psn_id_read_from_system() result: %s",
               vitarps5_result_string(sys_result));

      if (sys_result == VITARPS5_SUCCESS) {
        log_info("Using system PSN account as fallback: %.8s...",
                 system_psn_id);

        // Mark as authenticated with system PSN account
        psn_authenticated = true;
        strncpy(psn_username, "PlayStation User", sizeof(psn_username) - 1);
        psn_username[sizeof(psn_username) - 1] = '\0';

        // Set the PSN ID
        strncpy(psn_id_base64, system_psn_id, sizeof(psn_id_base64) - 1);
        psn_id_base64[sizeof(psn_id_base64) - 1] = '\0';
        psn_id_valid = true;

        log_info("System PSN account configured for discovery");
      } else {
        log_warning("Failed to read system PSN account: %s",
                    vitarps5_result_string(sys_result));
      }
    } else {
      log_info("No system PSN account found - manual configuration required");
    }
  } else {
    log_info("Using saved PSN ID from profile (system PSN account ignored)");
  }

  log_info("Final PSN state: authenticated=%s, username='%s', account_valid=%s",
           psn_authenticated ? "true" : "false", psn_username,
           psn_id_valid ? "true" : "false");

  // Initialize system information gathering
  result = vita_system_info_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize system information: %s",
              vitarps5_result_string(result));
    return;
  }

  // Load user data first
  load_user_data();

  // Load initial system and network information
  result = vita_system_info_get_system(&system_info);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to get system information: %s",
              vitarps5_result_string(result));
    user_data_load_error = result;
  } else {
    user_data_loaded = true;
  }

  result = vita_system_info_get_network(&network_info);
  if (result != VITARPS5_SUCCESS) {
    log_warning("Failed to get network information: %s",
                vitarps5_result_string(result));
  }

  // Update profile with fresh system info
  if (profile_loaded) {
    profile_storage_update_system_info(&system_info);
  }

  system_info_loaded = true;
  log_info("Profile initialized with user data status: %s",
           user_data_loaded ? "loaded" : "failed");
}

void ui_profile_cleanup(void) {
  log_info("Cleaning up profile...");

  // Save profile before cleanup
  if (profile_loaded) {
    VitaRPS5Result result = profile_storage_save(&current_profile);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to save profile during cleanup: %s",
                vitarps5_result_string(result));
    }
  }

  // Cleanup PSN account instance
  if (global_psn_account) {
    psn_account_destroy(global_psn_account);
    global_psn_account = NULL;
  }

  // Cleanup PSN account system
  psn_account_cleanup();

  profile_storage_cleanup();
  vita_system_info_cleanup();
  system_info_loaded = false;
  profile_loaded = false;
}

void ui_profile_render(void) {
  vita2d_pgf* font = ui_core_get_font();
  UIAssets* assets = ui_core_get_assets();

  // Update animations with fixed delta time (60fps)
  update_animations(0.016f);

  // Header (centered and consistent with other pages)
  const char* header_text = "Profile & Authentication";
  int header_width = vita2d_pgf_text_width(font, 1.2f, header_text);
  int header_x = (SCREEN_WIDTH - header_width) / 2;
  vita2d_pgf_draw_text(font, header_x, 60, UI_COLOR_TEXT_PRIMARY, 1.2f,
                       header_text);

  // Vita RPS5 logo in top right
  ui_core_render_logo();

  // Left card - User Profile (with shadow, no header) - Interactive
  uint32_t card_bg_color = UI_COLOR_CARD_BG;

  // Add selection highlight if profile card is selected
  if (profile_card_selected) {
    // Draw thick, bright highlight border with rounded corners
    ui_core_render_card_with_shadow(
        LEFT_PANEL_X - 8, LEFT_PANEL_Y - 8, LEFT_PANEL_WIDTH + 16,
        USER_CARD_HEIGHT + 16, PROFILE_CARD_RADIUS, UI_COLOR_PRIMARY_BLUE);
    // Add inner glow effect
    ui_core_render_card_with_shadow(
        LEFT_PANEL_X - 4, LEFT_PANEL_Y - 4, LEFT_PANEL_WIDTH + 8,
        USER_CARD_HEIGHT + 8, 6, 0x80FF9034);  // Semi-transparent blue glow
  }

  ui_core_render_card_with_shadow(LEFT_PANEL_X, LEFT_PANEL_Y, LEFT_PANEL_WIDTH,
                                  USER_CARD_HEIGHT, PROFILE_CARD_RADIUS,
                                  card_bg_color);

  // User avatar circle (positioned for vertical centering, blue background)
  int avatar_x = LEFT_PANEL_X + 20;
  int avatar_y = LEFT_PANEL_Y + (USER_CARD_HEIGHT - 60) / 2;
  ui_core_render_rounded_rectangle(avatar_x, avatar_y, 60, 60, 30,
                                   UI_COLOR_PRIMARY_BLUE);

  // Avatar icon - Use real profile image if available
  if (assets->has_user_profile_image && assets->user_profile_image) {
    // Use the user's actual profile image
    int img_width = vita2d_texture_get_width(assets->user_profile_image);
    int img_height = vita2d_texture_get_height(assets->user_profile_image);

    // Scale to fit in 60x60 circle
    float scale = 60.0f / fmaxf(img_width, img_height);
    int scaled_width = (int)(img_width * scale);
    int scaled_height = (int)(img_height * scale);
    int img_x = avatar_x + (60 - scaled_width) / 2;
    int img_y = avatar_y + (60 - scaled_height) / 2;

    // Draw circular mask (simplified - just draw the image scaled)
    vita2d_draw_texture_scale(assets->user_profile_image, img_x, img_y, scale,
                              scale);
  } else if (assets->profile_icon) {
    // Fallback to default profile icon
    int icon_size = (int)(40 * 1.15f);  // 1.15x larger = 46px
    int icon_x = avatar_x + (60 - icon_size) / 2;
    int icon_y = avatar_y + (60 - icon_size) / 2;
    vita2d_draw_texture_scale(
        assets->profile_icon, icon_x, icon_y,
        (float)icon_size / vita2d_texture_get_width(assets->profile_icon),
        (float)icon_size / vita2d_texture_get_height(assets->profile_icon));
  } else {
    // Fallback: draw a simple person silhouette using shapes (1.15x larger)
    int head_size = (int)(12 * 1.15f);  // 1.15x larger = 14px
    int head_x = avatar_x + 30 - head_size / 2;
    int head_y = avatar_y + 18;  // Adjusted for larger size
    ui_core_render_rounded_rectangle(head_x, head_y, head_size, head_size,
                                     head_size / 2, UI_COLOR_TEXT_PRIMARY);

    // Body (1.15x larger)
    int body_width = (int)(20 * 1.15f);   // 1.15x larger = 23px
    int body_height = (int)(16 * 1.15f);  // 1.15x larger = 18px
    int body_x = avatar_x + 30 - body_width / 2;
    int body_y = avatar_y + 30;  // Adjusted for larger head
    ui_core_render_rounded_rectangle(body_x, body_y, body_width, body_height, 4,
                                     UI_COLOR_TEXT_PRIMARY);
  }

  // User info (positioned next to avatar, vertically centered) - Smart user
  // data display
  int info_x = LEFT_PANEL_X + 100;
  int info_y = LEFT_PANEL_Y + (USER_CARD_HEIGHT - 65) / 2 +
               15;  // Center 3 lines of text

  if (user_data_loading) {
    // Show loading state
    vita2d_pgf_draw_text(font, info_x, info_y, UI_COLOR_TEXT_SECONDARY, 1.2f,
                         "Loading user data...");
    vita2d_pgf_draw_text(font, info_x, info_y + 25, UI_COLOR_TEXT_TERTIARY,
                         0.75f, "Please wait");
  } else if (!user_data_loaded && user_data_load_error != VITARPS5_SUCCESS) {
    // Show error state with tap to reload
    vita2d_pgf_draw_text(font, info_x, info_y, UI_COLOR_STATUS_UNAVAILABLE,
                         1.2f, "No User Found");
    vita2d_pgf_draw_text(font, info_x, info_y + 25, UI_COLOR_TEXT_SECONDARY,
                         0.75f, "Tap to reload user data");

    // Show error details on third line
    char error_line[64];
    snprintf(error_line, sizeof(error_line), "Error: %s",
             vitarps5_result_string(user_data_load_error));
    vita2d_pgf_draw_text(font, info_x, info_y + 45, UI_COLOR_TEXT_TERTIARY,
                         0.7f, error_line);
  } else {
    // Show user data (either loaded or fallback)
    const char* display_name;
    if (user_data_loaded && system_info_loaded &&
        strlen(system_info.user_name) > 0) {
      display_name = system_info.user_name;
    } else if (profile_loaded && strlen(current_profile.display_name) > 0) {
      display_name = current_profile.display_name;
    } else {
      display_name = "Vita User";
    }

    vita2d_pgf_draw_text(font, info_x, info_y, UI_COLOR_TEXT_PRIMARY, 1.2f,
                         display_name);

    // Show PSN ID if it's been entered (for local remote play)
    if (psn_id_valid && strlen(psn_id_base64) > 0) {
      // Show first 8 characters of PSN ID
      char psn_id_display[32];
      strncpy(psn_id_display, psn_id_base64, 8);
      psn_id_display[8] = '\0';

      char second_line[64];
      snprintf(second_line, sizeof(second_line), "PSN ID: %s...",
               psn_id_display);
      vita2d_pgf_draw_text(font, info_x, info_y + 25, UI_COLOR_TEXT_SECONDARY,
                           0.75f, second_line);

      log_info("Displaying PSN ID on profile card: '%s'", psn_id_display);
    } else {
      // PSN ID not entered yet - show hint
      vita2d_pgf_draw_text(font, info_x, info_y + 25, UI_COLOR_TEXT_TERTIARY,
                           0.75f, "Tap to enter PSN ID");
    }
  }

  // PSN Authentication section (positioned below user profile with spacing)
  int auth_y = LEFT_PANEL_Y + USER_CARD_HEIGHT + CARD_SPACING;
  ui_core_render_card_with_shadow(LEFT_PANEL_X, auth_y, LEFT_PANEL_WIDTH,
                                  AUTH_CARD_HEIGHT, PROFILE_CARD_RADIUS,
                                  UI_COLOR_CARD_BG);

  // PSN Authentication header text (green colored, with top margin like
  // Connection Info)
  vita2d_pgf_draw_text(font, LEFT_PANEL_X + 20, auth_y + 35,
                       UI_COLOR_STATUS_AVAILABLE, 1.2f, "PSN Authentication");
  // Multi-line text for PSN description (equidistant spacing)
  vita2d_pgf_draw_text(font, LEFT_PANEL_X + 20, auth_y + 75,
                       UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "Required for remote play via");
  vita2d_pgf_draw_text(font, LEFT_PANEL_X + 20, auth_y + 90,
                       UI_COLOR_TEXT_SECONDARY, 0.95f, "local network.");

  // Status indicator (positioned below description text, equidistant spacing)
  int status_y = auth_y + 125;
  if (psn_authenticated) {
    ui_core_render_status(LEFT_PANEL_X + 20, status_y, STATUS_TYPE_AVAILABLE,
                          "PSN Authenticated", 0.95f);
  } else {
    ui_core_render_status(LEFT_PANEL_X + 20, status_y, STATUS_TYPE_UNAVAILABLE,
                          "Not authenticated", 0.95f);
  }

  // Interactive PSN button with proper feedback (wider and taller, with bottom
  // padding)
  int button_y = auth_y + 155;
  int button_width = 150;  // Wider for "Sign In to PSN" text
  int button_height = 42;
  int button_x = LEFT_PANEL_X + 20;

  // Draw selection highlight if PSN button is selected - like dashboard button
  if (psn_button_selected) {
    // Draw thick, bright highlight border with rounded corners
    ui_core_render_rounded_rectangle(button_x - 8, button_y - 8,
                                     button_width + 16, button_height + 16, 8,
                                     UI_COLOR_PRIMARY_BLUE);
    // Add inner glow effect
    ui_core_render_rounded_rectangle(button_x - 4, button_y - 4,
                                     button_width + 8, button_height + 8, 6,
                                     0x80FF9034);  // Semi-transparent blue glow
  }

  // Button shadow (only when not selected to avoid double outline)
  if (!psn_button_selected) {
    ui_core_render_rounded_rectangle(button_x + 2, button_y + 2, button_width,
                                     button_height, 8, RGBA8(0, 0, 0, 40));
  }

  // Button background - change color based on auth status and press state
  uint32_t button_color =
      psn_authenticated ? UI_COLOR_STATUS_UNAVAILABLE : UI_COLOR_PRIMARY_BLUE;

  // Apply press animation
  if (psn_button_pressed && psn_button_press_time < 0.2f) {
    // Brighten button when pressed (simple color shift)
    button_color =
        psn_authenticated ? 0xFFFF6060 : 0xFFFFB050;  // Brighter versions
  }

  ui_core_render_rounded_rectangle(button_x, button_y, button_width,
                                   button_height, 8, button_color);

  // Add press animation overlay
  if (psn_button_pressed && psn_button_press_time < 0.2f) {
    ui_core_render_rounded_rectangle(button_x, button_y, button_width,
                                     button_height, 8, 0x40FFFFFF);
  }

  // Button text - change based on auth status and whether system PSN was used
  const char* button_text;
  if (psn_authenticated) {
    if (strcmp(psn_username, "PlayStation User") == 0) {
      button_text =
          "Override PSN";  // System PSN is active, allow manual override
    } else {
      button_text = "Sign Out";  // Manual PSN is active
    }
  } else {
    button_text = "Sign In to PSN";  // No PSN configured
  }

  int text_width = vita2d_pgf_text_width(font, 0.8f, button_text);
  int text_x = button_x + (button_width - text_width) / 2;
  int text_y = button_y + (button_height / 2) + 6;  // Vertically centered
  vita2d_pgf_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, 0.8f,
                       button_text);

  // Right panel - Connection Information (with shadow and darker background)
  uint32_t darker_bg =
      RGBA8(35, 40, 45, 255);  // Darker than regular card background
  ui_core_render_card_with_shadow(RIGHT_PANEL_X, RIGHT_PANEL_Y,
                                  RIGHT_PANEL_WIDTH, RIGHT_PANEL_HEIGHT,
                                  PROFILE_CARD_RADIUS, darker_bg);

  // Connection Information header text (orange colored, with more margin)
  vita2d_pgf_draw_text(font, RIGHT_PANEL_X + 20, RIGHT_PANEL_Y + 35,
                       UI_COLOR_STATUS_CONNECTING, 1.2f,
                       "Connection Information");

  // Connection details in professional table layout (positioned below header) -
  // NOW WITH REAL DATA
  int label_x = RIGHT_PANEL_X + 20;
  int y = RIGHT_PANEL_Y + 75;  // More space below header

  // Clean table layout without separators (tighter spacing for better fit)
  const int row_spacing = 34;  // 1pt shorter rows as requested
  const int value_column_x = RIGHT_PANEL_X + (RIGHT_PANEL_WIDTH / 2) +
                             20;  // Center of table + slight offset

  // Network Type - Real connection type
  vita2d_pgf_draw_text(font, label_x, y, UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "Network Type");
  const char* connection_type =
      system_info_loaded ? vita_system_info_get_connection_type_string(
                               network_info.connection_type)
                         : "Unknown";
  vita2d_pgf_draw_text(font, value_column_x, y, UI_COLOR_TEXT_PRIMARY, 0.95f,
                       connection_type);

  // Device Model
  y += row_spacing;
  vita2d_pgf_draw_text(font, label_x, y, UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "Device Model");
  const char* device_model =
      system_info_loaded ? system_info.model_name : "PS Vita";
  vita2d_pgf_draw_text(font, value_column_x, y, UI_COLOR_TEXT_PRIMARY, 0.95f,
                       device_model);

  // Device IP - Real IP address
  y += row_spacing;
  vita2d_pgf_draw_text(font, label_x, y, UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "Device IP");
  const char* device_ip =
      system_info_loaded ? network_info.ip_address : "Not Connected";
  vita2d_pgf_draw_text(font, value_column_x, y, UI_COLOR_TEXT_PRIMARY, 0.95f,
                       device_ip);

  // Firmware Version
  y += row_spacing;
  vita2d_pgf_draw_text(font, label_x, y, UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "Firmware");
  const char* firmware =
      system_info_loaded ? system_info.firmware_version : "Unknown";
  vita2d_pgf_draw_text(font, value_column_x, y, UI_COLOR_TEXT_PRIMARY, 0.95f,
                       firmware);

  // Memory Status
  y += row_spacing;
  vita2d_pgf_draw_text(font, label_x, y, UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "Memory");
  char memory_str[32];
  if (system_info_loaded) {
    snprintf(memory_str, sizeof(memory_str), "%d/%d MB",
             system_info.free_memory_mb, system_info.total_memory_mb);
  } else {
    snprintf(memory_str, sizeof(memory_str), "Unknown");
  }
  vita2d_pgf_draw_text(font, value_column_x, y, UI_COLOR_TEXT_PRIMARY, 0.95f,
                       memory_str);

  // Battery Status (if available)
  y += row_spacing;
  vita2d_pgf_draw_text(font, label_x, y, UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "Battery");
  char battery_str[32];
  if (system_info_loaded && system_info.battery_percent >= 0) {
    snprintf(battery_str, sizeof(battery_str), "%d%% %s",
             system_info.battery_percent,
             system_info.is_charging ? "(Charging)" : "");
  } else {
    snprintf(battery_str, sizeof(battery_str), "N/A (PS TV)");
  }
  vita2d_pgf_draw_text(font, value_column_x, y, UI_COLOR_TEXT_PRIMARY, 0.95f,
                       battery_str);

  // PSN Status
  y += row_spacing;
  vita2d_pgf_draw_text(font, label_x, y, UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "PSN Status");
  const char* psn_status =
      psn_authenticated ? "Authenticated" : "Not Signed In";
  vita2d_pgf_draw_text(font, value_column_x, y, UI_COLOR_TEXT_PRIMARY, 0.95f,
                       psn_status);

  // Connection Name
  y += row_spacing;
  vita2d_pgf_draw_text(font, label_x, y, UI_COLOR_TEXT_SECONDARY, 0.95f,
                       "Connection");
  const char* connection_name =
      system_info_loaded ? network_info.connection_name : "Unknown";
  vita2d_pgf_draw_text(font, value_column_x, y, UI_COLOR_TEXT_PRIMARY, 0.95f,
                       connection_name);

  // Status indicators at bottom (positioned after all table rows with more
  // spacing) - REAL STATUS
  y += 35;  // More space after the Connection row to avoid cramping

  // Network Connection Status - Real status
  StatusType network_status = STATUS_TYPE_UNAVAILABLE;
  const char* network_status_text = "Network Disconnected";

  if (system_info_loaded && network_info.is_connected) {
    network_status = STATUS_TYPE_AVAILABLE;
    if (network_info.connection_type == 1) {
      network_status_text = "WiFi Connected";
    } else if (network_info.connection_type == 3) {
      network_status_text = "Ethernet Connected";
    } else {
      network_status_text = "Network Connected";
    }
  }

  ui_core_render_status(label_x, y, network_status, network_status_text, 0.95f);

  // Remote Play Status (positioned below like separate rows)
  y += 25;
  StatusType remote_status =
      psn_authenticated ? STATUS_TYPE_AVAILABLE : STATUS_TYPE_UNAVAILABLE;
  const char* remote_status_text =
      psn_authenticated ? "Remote Play Ready" : "PSN Required";
  ui_core_render_status(label_x, y, remote_status, remote_status_text, 0.95f);
}

void ui_profile_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  // Handle touch input first
  handle_touch_input();

  // Handle button presses
  bool x_pressed =
      (pad->buttons & SCE_CTRL_CROSS) && !(prev_pad->buttons & SCE_CTRL_CROSS);
  bool circle_pressed = (pad->buttons & SCE_CTRL_CIRCLE) &&
                        !(prev_pad->buttons & SCE_CTRL_CIRCLE);
  bool start_pressed =
      (pad->buttons & SCE_CTRL_START) && !(prev_pad->buttons & SCE_CTRL_START);
  bool up_pressed =
      (pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP);
  bool down_pressed =
      (pad->buttons & SCE_CTRL_DOWN) && !(prev_pad->buttons & SCE_CTRL_DOWN);

  // Handle back navigation
  if (circle_pressed) {
    ui_core_set_state(UI_STATE_MAIN_DASHBOARD);
    ui_navigation_set_selected_icon(3);  // Return to Play
    log_info("Returning to main dashboard");
    return;
  }

  // Handle START button - refresh system information
  if (start_pressed) {
    log_info("Refreshing system information");
    if (system_info_loaded) {
      vita_system_info_refresh_network();
      vita_system_info_refresh_memory();
      vita_system_info_refresh_battery();
      // Reload the data
      vita_system_info_get_system(&system_info);
      vita_system_info_get_network(&network_info);
    }
    return;
  }

  // Handle navigation between interactive elements (up/down arrows)
  if (up_pressed || down_pressed) {
    // Toggle between profile card and PSN button selection
    // Profile card is always interactive now (for PSN ID input)
    if (profile_card_selected) {
      profile_card_selected = false;
      psn_button_selected = true;
    } else {
      profile_card_selected = true;
      psn_button_selected = false;
    }
    return;
  }

  // Handle X button - Profile card PSN ID input or PSN button action
  if (x_pressed) {
    if (profile_card_selected) {
      // Profile card is selected - open PSN ID input for local remote play
      trigger_profile_card_action();
    } else {
      // PSN button action for online remote play authentication
      trigger_psn_button_action();
    }
    return;
  }

  // Handle PSN login input when in PSN_LOGIN state
  if (ui_core_get_state() == UI_STATE_PSN_LOGIN) {
    bool up_pressed =
        (pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP);
    bool down_pressed =
        (pad->buttons & SCE_CTRL_DOWN) && !(prev_pad->buttons & SCE_CTRL_DOWN);

    if (circle_pressed) {
      // Cancel login
      ui_core_set_state(UI_STATE_PROFILE);
      log_info("PSN login cancelled");
    } else if (up_pressed && auth_input_field > 0) {
      auth_input_field--;
    } else if (down_pressed && auth_input_field < 2) {
      auth_input_field++;
    } else if (x_pressed) {
      if (auth_input_field == 0) {
        ui_core_ime_open(0, "Enter PSN Username", psn_username, false);
      } else if (auth_input_field == 1) {
        ui_core_ime_open(1, "Enter PSN Password", "", true);
      } else if (auth_input_field == 2) {
        ui_core_ime_open(2, "Enter PSN ID (Base64)", psn_id_base64, false);
      }
    }
  }
}

void ui_profile_get_psn_credentials(char* username, char* password) {
  if (username) {
    strncpy(username, psn_username, 63);
    username[63] = '\0';
  }
  if (password) {
    strncpy(password, psn_password, 63);
    password[63] = '\0';
  }
}

void ui_profile_set_psn_credentials(const char* username,
                                    const char* password) {
  if (username) {
    strncpy(psn_username, username, sizeof(psn_username) - 1);
    psn_username[sizeof(psn_username) - 1] = '\0';
  }
  if (password) {
    strncpy(psn_password, password, sizeof(psn_password) - 1);
    psn_password[sizeof(psn_password) - 1] = '\0';
  }

  // For now, simulate successful authentication when credentials are set
  // TODO: Implement real PSN authentication in future phase
  if (username && strlen(username) > 0 && password && strlen(password) > 0) {
    psn_authenticated = true;
    log_info("PSN authentication simulated for user: %s", username);

    // Save credentials to persistent storage (remember=true for now)
    VitaRPS5Result result =
        profile_storage_set_psn_credentials(username, NULL, true);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to save PSN credentials: %s",
                vitarps5_result_string(result));
    }

    ui_core_set_state(UI_STATE_PROFILE);  // Return to profile view
  }
}

bool ui_profile_is_psn_authenticated(void) { return psn_authenticated; }

void ui_profile_set_psn_authenticated(bool authenticated) {
  psn_authenticated = authenticated;
}

void ui_profile_handle_ime_result(int field, const char* text) {
  if (!text) return;

  log_info("Processing IME result for field %d: '%s'", field, text);

  if (field == 0) {
    // PSN Username
    strncpy(psn_username, text, sizeof(psn_username) - 1);
    psn_username[sizeof(psn_username) - 1] = '\0';
    log_info("PSN username updated: '%s'", psn_username);
  } else if (field == 1) {
    // PSN Password
    strncpy(psn_password, text, sizeof(psn_password) - 1);
    psn_password[sizeof(psn_password) - 1] = '\0';
    log_info("PSN password updated (length: %d)", (int)strlen(psn_password));

    // Auto-authenticate when both username and password are provided
    if (strlen(psn_username) > 0 && strlen(psn_password) > 0) {
      ui_profile_set_psn_credentials(psn_username, psn_password);
    }
  } else if (field == 2) {
    // PSN Account Number
    strncpy(psn_id_base64, text, sizeof(psn_id_base64) - 1);
    psn_id_base64[sizeof(psn_id_base64) - 1] = '\0';

    // Validate PSN ID format
    psn_id_valid = psn_id_validate_base64_format(psn_id_base64);

    if (psn_id_valid) {
      log_info("PSN ID base64 updated: '%s' (valid)", psn_id_base64);

      // Update PSN account system with real PSN ID
      // This will enable proper PS5 discovery with authentication
      ui_profile_set_psn_id_base64(psn_id_base64);
    } else {
      log_warning("PSN ID invalid: '%s' (length: %d)", psn_id_base64,
                  (int)strlen(psn_id_base64));
    }
  } else if (field == 10) {
    // PSN ID input from profile card (for local remote play)
    strncpy(psn_id_base64, text, sizeof(psn_id_base64) - 1);
    psn_id_base64[sizeof(psn_id_base64) - 1] = '\0';

    // Validate PSN ID format
    psn_id_valid = psn_id_validate_base64_format(psn_id_base64);

    log_info("PSN ID for local remote play updated: '%s' (valid: %s)",
             psn_id_base64, psn_id_valid ? "yes" : "no");

    if (psn_id_valid) {
      // Update PSN account system and save to profile
      ui_profile_set_psn_id_base64(psn_id_base64);
      log_info("PSN ID set successfully for local remote play");

      // Clear profile card selection after successful entry
      profile_card_selected = false;
    } else {
      log_warning("PSN ID validation failed - invalid format");
      // Keep profile card selected for retry
    }
  }
}

void ui_profile_set_psn_id_base64(const char* new_psn_id) {
  if (!new_psn_id) {
    log_error("Cannot set PSN ID: invalid parameters");
    return;
  }

  // Validate the PSN ID format first
  if (!psn_id_validate_base64_format(new_psn_id)) {
    log_error("Cannot set PSN ID: invalid base64 format: %s", new_psn_id);
    return;
  }

  log_info("PSN ID base64 successfully set for discovery: '%s'", new_psn_id);

  // Update local state for immediate UI update
  strncpy(psn_id_base64, new_psn_id, sizeof(psn_id_base64) - 1);
  psn_id_base64[sizeof(psn_id_base64) - 1] = '\0';
  psn_id_valid = true;

  // Update PSN account system for discovery
  if (global_psn_account) {
    VitaRPS5Result psn_result =
        psn_account_set_psn_id_base64(global_psn_account, new_psn_id);
    if (psn_result == VITARPS5_SUCCESS) {
      log_info("PSN account system updated with base64 PSN ID");

      // CRITICAL FIX: Freeze PSN ID refresh to prevent overwriting during
      // session
      VitaRPS5Result freeze_result =
          psn_account_freeze_refresh(global_psn_account);
      if (freeze_result == VITARPS5_SUCCESS) {
        log_info("PSN ID frozen - will not be overwritten by registry refresh");
      } else {
        log_error("Failed to freeze PSN ID refresh: %s",
                  vitarps5_result_string(freeze_result));
      }
    } else {
      log_error("Failed to set PSN ID in account system: %s",
                vitarps5_result_string(psn_result));
    }
  } else {
    log_warning("global_psn_account is NULL, cannot update PSN account system");
  }

  // Also update the discovery system using the UI core interface
  log_info("Updating discovery system with new PSN ID via ui_core interface");
  VitaRPS5Result discovery_result = ui_core_set_discovery_psn_id(new_psn_id);
  if (discovery_result == VITARPS5_SUCCESS) {
    log_info("Discovery system PSN account updated successfully via ui_core");
  } else {
    log_error("Failed to update discovery PSN account via ui_core: %s",
              vitarps5_result_string(discovery_result));
  }

  // Update profile storage
  strncpy(current_profile.psn_id_base64, new_psn_id,
          sizeof(current_profile.psn_id_base64) - 1);
  current_profile.psn_id_base64[sizeof(current_profile.psn_id_base64) - 1] =
      '\0';

  // Save profile
  log_info("Saving profile with PSN ID: %.8s...",
           current_profile.psn_id_base64);
  VitaRPS5Result save_result = profile_storage_save(&current_profile);
  if (save_result != VITARPS5_SUCCESS) {
    log_error("Failed to save PSN ID to profile: %s",
              vitarps5_result_string(save_result));
  } else {
    log_info("Profile saved successfully with PSN ID");
  }
}

void ui_profile_get_psn_id_base64(char* psn_id_base64, size_t size) {
  if (!psn_id_base64 || size == 0) {
    return;
  }

  // Get from profile storage
  strncpy(psn_id_base64, current_profile.psn_id_base64, size - 1);
  psn_id_base64[size - 1] = '\0';
}

bool ui_profile_get_psn_id_valid(void) { return psn_id_valid; }

uint64_t ui_profile_get_psn_account_number(void) {
  if (!psn_id_valid || strlen(current_profile.psn_id_base64) == 0) {
    log_warning("No valid PSN ID available for account number conversion");
    log_debug("PSN ID validation state: valid=%d, base64_length=%zu",
              psn_id_valid, strlen(current_profile.psn_id_base64));
    return 0;
  }

  log_debug("Converting PSN ID base64 '%s' to account number",
            current_profile.psn_id_base64);
  uint64_t account_number = 0;
  VitaRPS5Result result = psn_id_base64_to_account_number(
      current_profile.psn_id_base64, &account_number);

  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to convert PSN ID base64 to account number: %s",
              vitarps5_result_string(result));
    return 0;
  }

  log_debug("Converted PSN ID '%s' to account number: %llu",
            current_profile.psn_id_base64, account_number);

  // Validate the account number is reasonable
  if (account_number == 0) {
    log_error(
        "PSN account number conversion resulted in 0 - this will cause "
        "registration failures");
    return 0;
  }

  log_debug("PSN account number validation passed: %llu", account_number);
  return account_number;
}

VitaRPS5Result ui_profile_get_psn_account_id_binary(uint8_t* account_id) {
  if (!account_id) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!psn_id_valid || strlen(current_profile.psn_id_base64) == 0) {
    log_warning("No valid PSN ID available - cannot provide binary account ID");
    return VITARPS5_ERROR_NO_DATA;
  }

  // Convert base64 PSN ID to binary format (8 bytes)
  VitaRPS5Result result =
      psn_id_base64_to_binary(current_profile.psn_id_base64, account_id, false);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to convert PSN ID base64 to binary format: %s",
              vitarps5_result_string(result));
    return result;
  }

  log_debug("Converted PSN ID '%s' to binary format for session authentication",
            current_profile.psn_id_base64);
  return VITARPS5_SUCCESS;
}

PSNAccount* ui_profile_get_psn_account(void) { return global_psn_account; }

// Helper function implementations

static void update_animations(float delta_time) {
  // Update PSN button press animation
  if (psn_button_pressed) {
    psn_button_press_time += delta_time;
    if (psn_button_press_time >= 0.2f) {  // 200ms press animation
      psn_button_pressed = false;
      psn_button_press_time = 0.0f;
    }
  }
}

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

  // Check profile card area (always available for PSN ID input)
  int card_x = LEFT_PANEL_X;
  int card_y = LEFT_PANEL_Y;
  int card_width = LEFT_PANEL_WIDTH;
  int card_height = USER_CARD_HEIGHT;

  if (is_point_in_button(touch_x, touch_y, card_x, card_y, card_width,
                         card_height)) {
    log_info("Profile card touched - triggering PSN ID input");
    trigger_profile_card_action();
    return;
  }

  // Check PSN button area
  int button_x = LEFT_PANEL_X + 20;
  int button_y = LEFT_PANEL_Y + USER_CARD_HEIGHT + CARD_SPACING +
                 155;  // Same as render position
  int button_width = 150;
  int button_height = 42;

  if (is_point_in_button(touch_x, touch_y, button_x, button_y, button_width,
                         button_height)) {
    log_info("PSN button touched");
    trigger_psn_button_action();
    return;
  }
}

static bool is_point_in_button(float x, float y, int button_x, int button_y,
                               int button_w, int button_h) {
  return (x >= button_x && x <= button_x + button_w && y >= button_y &&
          y <= button_y + button_h);
}

static void trigger_psn_button_action(void) {
  log_info("=== PSN BUTTON PRESSED ===");

  // Set PSN button animation
  psn_button_pressed = true;
  psn_button_press_time = 0.0f;
  psn_button_selected = true;

  if (psn_authenticated) {
    if (strcmp(psn_username, "PlayStation User") == 0) {
      // System PSN is active - allow manual override
      log_info("Overriding system PSN with manual configuration");
      ui_core_set_state(UI_STATE_PSN_LOGIN);
      auth_input_field = 0;
    } else {
      // Manual PSN is active - sign out
      log_info("Signing out of manually configured PSN");
      psn_authenticated = false;
      memset(psn_username, 0, sizeof(psn_username));
      memset(psn_password, 0, sizeof(psn_password));
      memset(psn_id_base64, 0, sizeof(psn_id_base64));
      psn_id_valid = false;

      // Clear credentials from persistent storage
      profile_storage_clear_psn_credentials();

      // Try to re-load system PSN account
      if (psn_id_system_has_account()) {
        char system_psn_id[32];
        VitaRPS5Result result =
            psn_id_read_from_system(system_psn_id, sizeof(system_psn_id));
        if (result == VITARPS5_SUCCESS) {
          log_info("Restored system PSN account after manual sign-out");
          psn_authenticated = true;
          strncpy(psn_username, "PlayStation User", sizeof(psn_username) - 1);
          psn_username[sizeof(psn_username) - 1] = '\0';
          strncpy(psn_id_base64, system_psn_id, sizeof(psn_id_base64) - 1);
          psn_id_base64[sizeof(psn_id_base64) - 1] = '\0';
          psn_id_valid = true;
        }
      }
    }
  } else {
    // No PSN configured - start manual sign in process
    log_info("Starting manual PSN sign in process");
    ui_core_set_state(UI_STATE_PSN_LOGIN);
    auth_input_field = 0;
  }
}

static void trigger_profile_card_action(void) {
  log_info("=== PROFILE CARD PSN ID INPUT PRESSED ===");

  // Set profile card selection
  profile_card_selected = true;

  // Create help message with instructions on how to find PSN ID
  const char* help_title =
      "Enter PSN ID for Local Remote Play\n\n"
      "How to find your PSN ID:\n"
      "1. PlayStation App: Profile → Settings → Account Info → Account ID\n"
      "2. PS5 Console: Settings → Users & Accounts → Account → View Account\n"
      "3. Convert 16-digit Account ID to Base64 using online converter\n\n"
      "Example format: nD1Ho0mY7wY=";

  // Open PSN ID input dialog for local remote play
  log_info("Opening PSN ID input for local remote play");
  ui_core_ime_open(10, help_title, psn_id_base64[0] ? psn_id_base64 : "",
                   false);
}

static void load_user_data(void) {
  log_info("Loading user data from system");

  user_data_loading = true;
  user_data_loaded = false;
  user_data_load_error = VITARPS5_SUCCESS;

  // Force reload system information
  VitaRPS5Result result = vita_system_info_get_system(&system_info);

  user_data_loading = false;

  if (result == VITARPS5_SUCCESS) {
    user_data_loaded = true;
    user_data_load_error = VITARPS5_SUCCESS;
    system_info_loaded = true;
    log_info("User data loaded successfully: '%s'", system_info.user_name);

    // Update profile with fresh data
    if (profile_loaded) {
      profile_storage_update_system_info(&system_info);
    }
  } else {
    user_data_loaded = false;
    user_data_load_error = result;
    log_error("Failed to load user data: %s", vitarps5_result_string(result));
  }
}