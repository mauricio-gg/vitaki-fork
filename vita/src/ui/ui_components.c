#include "ui_components.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/console_registration.h"
#include "../discovery/ps5_discovery.h"
#include "../utils/logger.h"
#include "ui_core.h"
#include "vita2d_ui.h"

// UI element states
static UIElementStates ui_element_states = {0};

void ui_components_init(void) {
  // Initialize all animation states to 0
  memset(&ui_element_states, 0, sizeof(UIElementStates));
}

void ui_components_draw_toggle_switch(int x, int y, bool is_on,
                                      float animation_progress, bool enabled) {
  // Calculate colors based on state
  uint32_t track_color;
  if (!enabled) {
    track_color = RGBA8(60, 60, 60, 255);
  } else if (is_on) {
    // Interpolate between off and on colors based on animation
    uint8_t green = 80 + (uint8_t)(95 * animation_progress);
    uint8_t red = 80 - (uint8_t)(4 * animation_progress);
    track_color = RGBA8(red, green, 80, 255);
  } else {
    track_color = RGBA8(80, 80, 80, 255);
  }

  // Draw track (pill shape)
  int track_radius = TOGGLE_HEIGHT / 2;

  // Middle rectangle
  vita2d_draw_rectangle(x + track_radius, y, TOGGLE_WIDTH - TOGGLE_HEIGHT,
                        TOGGLE_HEIGHT, track_color);

  // Left semicircle
  for (int dy = 0; dy < TOGGLE_HEIGHT; dy++) {
    int width = (int)sqrt(track_radius * track_radius -
                          (dy - track_radius) * (dy - track_radius));
    if (width > 0) {
      vita2d_draw_rectangle(x + track_radius - width, y + dy, width, 1,
                            track_color);
    }
  }

  // Right semicircle
  for (int dy = 0; dy < TOGGLE_HEIGHT; dy++) {
    int width = (int)sqrt(track_radius * track_radius -
                          (dy - track_radius) * (dy - track_radius));
    if (width > 0) {
      vita2d_draw_rectangle(x + TOGGLE_WIDTH - track_radius, y + dy, width, 1,
                            track_color);
    }
  }

  // Calculate thumb position
  float thumb_x = x + TOGGLE_THUMB_RADIUS + 2 +
                  (animation_progress * (TOGGLE_WIDTH - TOGGLE_HEIGHT - 4));
  int thumb_y = y + TOGGLE_HEIGHT / 2;

  // Draw thumb shadow
  for (int r = TOGGLE_THUMB_RADIUS; r > 0; r--) {
    uint8_t alpha = 60 * r / TOGGLE_THUMB_RADIUS;
    ui_components_draw_circle((int)thumb_x + 1, thumb_y + 1, r,
                              RGBA8(0, 0, 0, alpha));
  }

  // Draw thumb
  uint32_t thumb_color =
      enabled ? RGBA8(255, 255, 255, 255) : RGBA8(180, 180, 180, 255);
  for (int r = TOGGLE_THUMB_RADIUS; r > 0; r--) {
    ui_components_draw_circle((int)thumb_x, thumb_y, r, thumb_color);
  }
}

void ui_components_draw_dropdown_menu(int x, int y, int width,
                                      const char* value, bool is_open,
                                      bool enabled) {
  uint32_t bg_color = enabled ? RGBA8(60, 60, 60, 255) : RGBA8(50, 50, 50, 255);
  uint32_t text_color =
      enabled ? UI_COLOR_TEXT_PRIMARY : RGBA8(128, 128, 128, 255);
  uint32_t border_color =
      enabled ? RGBA8(80, 80, 80, 255) : RGBA8(60, 60, 60, 255);

  // Main dropdown box
  ui_core_render_rounded_rectangle(x, y, width, DROPDOWN_HEIGHT, 4, bg_color);

  // Border
  ui_core_render_rounded_rectangle(x, y, width, DROPDOWN_HEIGHT, 4,
                                   border_color);
  vita2d_draw_rectangle(x + 1, y + 1, width - 2, DROPDOWN_HEIGHT - 2, bg_color);

  // Text
  vita2d_pgf* font = ui_core_get_font();
  vita2d_pgf_draw_text(font, x + 10, y + 20, text_color, 0.8f, value);

  // Arrow
  int arrow_x = x + width - 20;
  int arrow_y = y + DROPDOWN_HEIGHT / 2 - 2;
  ui_components_draw_dropdown_arrow(arrow_x, arrow_y, text_color);

  // Note: Dropdown list rendering moved to separate function for proper z-order
}

void ui_components_draw_dropdown_list(int x, int y, int width,
                                      const char* value, bool enabled) {
  if (!enabled) return;

  int list_y = y + DROPDOWN_HEIGHT + 2;
  int list_height = DROPDOWN_ITEM_HEIGHT * 4;  // Show 4 items

  // Shadow
  vita2d_draw_rectangle(x + 2, list_y + 2, width, list_height,
                        RGBA8(0, 0, 0, 80));

  // List background
  vita2d_draw_rectangle(x, list_y, width, list_height, RGBA8(50, 50, 50, 255));

  // Sample items (in real implementation, these would come from data)
  const char* items[] = {"Balanced", "Performance", "Quality", "Custom"};
  for (int i = 0; i < 4; i++) {
    int item_y = list_y + i * DROPDOWN_ITEM_HEIGHT;

    // Hover effect on second item
    if (i == 1) {
      vita2d_draw_rectangle(x, item_y, width, DROPDOWN_ITEM_HEIGHT,
                            RGBA8(80, 80, 80, 255));
    }

    vita2d_pgf* font = ui_core_get_font();
    vita2d_pgf_draw_text(font, x + 10, item_y + 22, UI_COLOR_TEXT_PRIMARY, 0.8f,
                         items[i]);
  }
}

void ui_components_draw_slider(int x, int y, int width, float value,
                               bool is_dragging, bool enabled) {
  // Ensure value is in range
  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;

  uint32_t track_color =
      enabled ? RGBA8(60, 60, 60, 255) : RGBA8(50, 50, 50, 255);

  int track_y = y + (SLIDER_THUMB_RADIUS - SLIDER_TRACK_HEIGHT / 2);

  // Track shadow
  vita2d_draw_rectangle(x, track_y + 1, width, SLIDER_TRACK_HEIGHT,
                        RGBA8(0, 0, 0, 60));

  // Track background with rounded ends
  vita2d_draw_rectangle(x + 2, track_y, width - 4, SLIDER_TRACK_HEIGHT,
                        track_color);

  // Rounded ends
  for (int dy = 0; dy < SLIDER_TRACK_HEIGHT; dy++) {
    int r = SLIDER_TRACK_HEIGHT / 2;
    int offset = (int)sqrt(r * r - (dy - r) * (dy - r));
    if (offset > 0) {
      vita2d_draw_rectangle(x + r - offset, track_y + dy, offset, 1,
                            track_color);
      vita2d_draw_rectangle(x + width - r, track_y + dy, offset, 1,
                            track_color);
    }
  }

  // Filled portion
  int fill_width = (int)(width * value);
  if (fill_width > 4) {
    // Gradient effect
    for (int i = 0; i < fill_width - 2; i++) {
      float t = (float)i / fill_width;
      uint8_t blue = 255 - (t * 30);  // Slight gradient
      vita2d_draw_line(x + 2 + i, track_y, x + 2 + i,
                       track_y + SLIDER_TRACK_HEIGHT,
                       RGBA8(52, 144, blue, 255));
    }
  }

  // Thumb position
  int thumb_x = x + fill_width;
  int thumb_y = y + SLIDER_THUMB_RADIUS;

  // Thumb shadow (larger when dragging)
  int shadow_radius =
      is_dragging ? SLIDER_THUMB_RADIUS + 2 : SLIDER_THUMB_RADIUS;
  for (int r = shadow_radius; r > 0; r--) {
    uint8_t alpha =
        is_dragging ? 100 * r / shadow_radius : 60 * r / shadow_radius;
    ui_components_draw_circle(thumb_x + 1, thumb_y + 1, r,
                              RGBA8(0, 0, 0, alpha));
  }

  // Thumb with gradient effect
  int thumb_radius =
      is_dragging ? SLIDER_THUMB_RADIUS + 1 : SLIDER_THUMB_RADIUS;

  for (int r = thumb_radius; r > 0; r--) {
    uint8_t brightness = 255 - ((thumb_radius - r) * 15);
    ui_components_draw_circle(thumb_x, thumb_y, r,
                              RGBA8(brightness, brightness, brightness, 255));
  }
}

void ui_components_draw_circle(int cx, int cy, int radius, uint32_t color) {
  // Sanity checks for circle parameters
  if (cx < -100 || cx > 1060 || cy < -100 || cy > 644 || radius <= 0 ||
      radius > 1000) {
    log_error("💥 draw_circle sanity fail: x=%d y=%d r=%d", cx, cy, radius);
    return;
  }

  // Fix problematic color values that cause crashes
  if (color == 0xFFFFFFFF) {
    // Replace pure white (0xFFFFFFFF) with slightly off-white to prevent
    // crashes
    color = RGBA8(254, 254, 254,
                  255);  // Very close to white but not exactly 0xFFFFFFFF
  }

  // Additional color validation - ensure alpha channel is set
  if ((color & 0xFF000000) == 0) {
    color |= 0xFF000000;  // Force alpha to 255 if it's 0
  }

  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        int draw_x = cx + x;
        int draw_y = cy + y;

        // Add bounds checking to prevent crashes
        if (draw_x < 0 || draw_x >= 960 || draw_y < 0 || draw_y >= 544) {
          continue;  // Skip out-of-bounds pixels silently
        }

        vita2d_draw_rectangle(draw_x, draw_y, 1, 1, color);
      }
    }
  }
}

void ui_components_draw_dropdown_arrow(int x, int y, uint32_t color) {
  // Draw downward pointing triangle
  for (int i = 0; i < DROPDOWN_ARROW_SIZE / 2; i++) {
    vita2d_draw_line(x - i, y + i, x + i, y + i, color);
  }
}

UIElementStates* ui_components_get_states(void) { return &ui_element_states; }

void ui_components_update_animations(float delta_time) {
  // Update toggle animations based on actual toggle states
  // This would need to be coordinated with the settings system
  for (int i = 0; i < 8; i++) {
    // For now, just maintain the current animation values
    // In a real implementation, this would check actual setting values
  }
}

void ui_components_render_console_card(const ConsoleInfo* console, int x, int y,
                                       bool is_authenticated) {
  if (!console) return;

  UIAssets* assets = ui_core_get_assets();
  vita2d_pgf* font = ui_core_get_font();

  // Use the actual console card asset - center it properly based on actual
  // texture size
  if (assets->console_card) {
    int actual_card_width = vita2d_texture_get_width(assets->console_card);
    int actual_card_height = vita2d_texture_get_height(assets->console_card);

    // Center the card based on its actual texture size, not the constant
    int screen_center_x = 960 / 2;
    int centered_x = screen_center_x - (actual_card_width / 2);

    // Draw console state glow/border BEFORE the card
    // 0 = Unknown, 1 = Ready, 2 = Standby
    if (console->console_state == 1) {  // CONSOLE_STATE_READY
      // Blue glow for ready state - draw larger rectangle behind card
      int glow_padding = 4;
      vita2d_draw_rectangle(centered_x - glow_padding, y - glow_padding,
                            actual_card_width + (glow_padding * 2),
                            actual_card_height + (glow_padding * 2),
                            RGBA8(0, 162, 232, 180));  // Blue with transparency
    } else if (console->console_state == 2) {          // CONSOLE_STATE_STANDBY
      // Yellow/orange glow for standby state
      int glow_padding = 4;
      vita2d_draw_rectangle(
          centered_x - glow_padding, y - glow_padding,
          actual_card_width + (glow_padding * 2),
          actual_card_height + (glow_padding * 2),
          RGBA8(255, 193, 7, 180));  // Yellow with transparency
    }

    vita2d_draw_texture(assets->console_card, centered_x, y);

    // Update x position for child elements to use the centered position
    x = centered_x;
  }

  // Get actual card dimensions for console name bar and logo positioning
  int actual_card_width = assets->console_card
                              ? vita2d_texture_get_width(assets->console_card)
                              : CONSOLE_CARD_WIDTH;
  int actual_card_height = assets->console_card
                               ? vita2d_texture_get_height(assets->console_card)
                               : CONSOLE_CARD_HEIGHT;

  // Position PS5 logo 1/3 from top of card
  if (assets->ps5_logo) {
    // Get logo dimensions for centering calculation
    int logo_width = (int)(vita2d_texture_get_width(assets->ps5_logo) * 0.8f);
    int logo_height = (int)(vita2d_texture_get_height(assets->ps5_logo) * 0.8f);
    int logo_x = x + (actual_card_width - logo_width) /
                         2;  // Horizontally centered on actual card
    int logo_y =
        y + (actual_card_height / 3) -
        (logo_height / 2);  // 1/3 from top, vertically centered at that point
    vita2d_draw_texture_scale(assets->ps5_logo, logo_x, logo_y, 0.8f, 0.8f);
  }

  // Position console name bar 1/3 from bottom of card
  int name_bar_height = 40;
  int name_bar_y = y + actual_card_height - (actual_card_height / 3) -
                   (name_bar_height /
                    2);  // 1/3 from bottom, vertically centered at that point
  int name_bar_x = x + 15;                      // 15px padding from edges
  int name_bar_width = actual_card_width - 30;  // 15px padding on each side

  // Draw lighter colored name bar background
  ui_core_render_rounded_rectangle(name_bar_x, name_bar_y, name_bar_width,
                                   name_bar_height, 8, RGBA8(70, 75, 80, 255));

  // Console name text in the bar - horizontally centered with spaced dash
  char formatted_name[32];
  snprintf(formatted_name, sizeof(formatted_name), "PS5 - 024");

  int text_width =
      vita2d_pgf_text_width(font, 1.2f, formatted_name);  // Same scale as title
  int text_x = name_bar_x + (name_bar_width - text_width) /
                                2;  // Horizontally centered in bar
  int text_y = name_bar_y + (name_bar_height / 2) +
               8;  // Vertically centered (adjusted for 1.2f scale)
  vita2d_pgf_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, 1.2f,
                       formatted_name);

  // Status indicator using actual ellipse assets (network status)
  vita2d_texture* status_ellipse = NULL;
  switch (console->status) {
    case CONSOLE_STATUS_AVAILABLE:
      status_ellipse = assets->ellipse_green;
      break;
    case CONSOLE_STATUS_UNAVAILABLE:
      status_ellipse = assets->ellipse_red;
      break;
    case CONSOLE_STATUS_CONNECTING:
      status_ellipse = assets->ellipse_yellow;
      break;
    default:
      break;
  }

  if (status_ellipse) {
    // Use actual card width for status indicator positioning
    int actual_card_width = assets->console_card
                                ? vita2d_texture_get_width(assets->console_card)
                                : CONSOLE_CARD_WIDTH;
    int status_x = x + actual_card_width - 30;  // Top-right corner
    int status_y = y + 10;
    vita2d_draw_texture(status_ellipse, status_x, status_y);
  }

  // Console state text indicator and action buttons (Ready/Standby)
  const char* state_text = NULL;
  uint32_t state_color = UI_COLOR_TEXT_SECONDARY;
  bool show_wake_button = false;
  bool show_register_button = false;

  if (console->console_state == 1) {  // CONSOLE_STATE_READY
    state_text = "Ready";
    state_color = RGBA8(0, 162, 232, 255);   // Blue
  } else if (console->console_state == 2) {  // CONSOLE_STATE_STANDBY
    state_text = "Standby";
    state_color = RGBA8(255, 193, 7, 255);  // Yellow

    // Check registration status to determine which button to show
    // Using clean slate implementation - no registered consoles initially
    bool is_registered = ui_core_is_console_registered(console->ip_address);
    if (is_registered) {
      show_wake_button =
          true;  // Show wake button for registered standby consoles
      // Note: Removed frequent debug logging to prevent log spam during
      // rendering
    } else {
      show_register_button =
          true;  // Show register button for unregistered consoles
      // Note: Removed frequent debug logging to prevent log spam during
      // rendering
    }
  }

  if (state_text) {
    // Position state text below the console name bar
    float state_scale = 0.8f;
    int state_text_width = vita2d_pgf_text_width(font, state_scale, state_text);
    int state_x = x + (actual_card_width - state_text_width) / 2;
    int state_y = name_bar_y + name_bar_height + 15;
    vita2d_pgf_draw_text(font, state_x, state_y, state_color, state_scale,
                         state_text);
  }

  // Wake button for standby consoles
  if (show_wake_button) {
    // Draw compact Wake button below state text
    const char* wake_text = "Wake";
    float wake_scale = 0.7f;
    int wake_text_width = vita2d_pgf_text_width(font, wake_scale, wake_text);

    // Button dimensions - compact size
    int wake_button_width = wake_text_width + 20;  // 10px padding each side
    int wake_button_height = 25;                   // Compact height

    // Center button horizontally on card
    int wake_button_x = x + (actual_card_width - wake_button_width) / 2;
    int wake_button_y = name_bar_y + name_bar_height + 40;  // Below state text

    // Draw rounded button background with standby color theme
    ui_core_render_rounded_rectangle(
        wake_button_x, wake_button_y, wake_button_width, wake_button_height, 6,
        RGBA8(255, 193, 7, 200));  // Semi-transparent yellow

    // Center wake text in button
    int wake_text_x = wake_button_x + (wake_button_width - wake_text_width) / 2;
    int wake_text_y = wake_button_y + (wake_button_height / 2) +
                      5;  // Adjust for font baseline

    // Draw black text on yellow button for good contrast
    vita2d_pgf_draw_text(font, wake_text_x, wake_text_y, RGBA8(0, 0, 0, 255),
                         wake_scale, wake_text);

    log_debug("Wake button rendered for console at (%d,%d) size %dx%d",
              wake_button_x, wake_button_y, wake_button_width,
              wake_button_height);
  }

  // Register button for unregistered consoles
  if (show_register_button) {
    // Draw compact Register button below state text
    const char* register_text = "Register";
    float register_scale = 0.65f;  // Slightly smaller to fit longer text
    int register_text_width =
        vita2d_pgf_text_width(font, register_scale, register_text);

    // Button dimensions - compact size
    int register_button_width =
        register_text_width + 20;     // 10px padding each side
    int register_button_height = 25;  // Compact height

    // Center button horizontally on card
    int register_button_x = x + (actual_card_width - register_button_width) / 2;
    int register_button_y =
        name_bar_y + name_bar_height + 40;  // Below state text

    // Draw rounded button background with blue theme for registration
    ui_core_render_rounded_rectangle(
        register_button_x, register_button_y, register_button_width,
        register_button_height, 6,
        RGBA8(0, 162, 232, 200));  // Semi-transparent blue

    // Center register text in button
    int register_text_x =
        register_button_x + (register_button_width - register_text_width) / 2;
    int register_text_y = register_button_y + (register_button_height / 2) +
                          5;  // Adjust for font baseline

    // Draw white text on blue button for good contrast
    vita2d_pgf_draw_text(font, register_text_x, register_text_y,
                         RGBA8(255, 255, 255, 255), register_scale,
                         register_text);

    log_debug(
        "Register button rendered for console: card_x=%d, "
        "actual_card_width=%d, button_pos=(%d,%d), button_size=%dx%d",
        x, actual_card_width, register_button_x, register_button_y,
        register_button_width, register_button_height);
  }

  // Authentication status overlay
  if (!is_authenticated) {
    // Get actual card dimensions for overlay
    int actual_card_width = assets->console_card
                                ? vita2d_texture_get_width(assets->console_card)
                                : CONSOLE_CARD_WIDTH;
    int actual_card_height =
        assets->console_card ? vita2d_texture_get_height(assets->console_card)
                             : CONSOLE_CARD_HEIGHT;

    // Apply semi-transparent overlay to entire card (50% opacity)
    vita2d_draw_rectangle(x, y, actual_card_width, actual_card_height,
                          UI_COLOR_SEMI_TRANSPARENT);

    // Draw "Unauthenticated" tag in top-right corner
    const char* auth_tag_text = "Unauthenticated";
    float auth_text_scale = 0.7f;
    int tag_text_width =
        vita2d_pgf_text_width(font, auth_text_scale, auth_tag_text);

    // Tag dimensions - compact size
    int tag_width = tag_text_width + 12;  // 6px padding on each side
    int tag_height = 20;                  // Compact height

    // Position tag in top-right corner with 5px margin
    int tag_x = x + actual_card_width - tag_width - 5;
    int tag_y = y + 5;

    // Draw rounded tag background (yellow)
    ui_core_render_rounded_rectangle(tag_x, tag_y, tag_width, tag_height, 6,
                                     UI_COLOR_AUTH_TAG_BG);

    // Center text within tag
    int text_x = tag_x + (tag_width - tag_text_width) / 2;
    int text_y = tag_y + (tag_height / 2) + 5;  // Adjust for font baseline

    // Draw white text on yellow background
    vita2d_pgf_draw_text(font, text_x, text_y, UI_COLOR_AUTH_TAG_TEXT,
                         auth_text_scale, auth_tag_text);
  }
}

bool ui_components_check_wake_button_hit(const ConsoleInfo* console, int card_x,
                                         int card_y, float touch_x,
                                         float touch_y) {
  if (!console || console->console_state != 2) {  // Only for STANDBY consoles
    return false;
  }

  // Check if console has valid registration for wake functionality
  // Using clean slate implementation - no registered consoles initially
  bool is_registered = ui_core_is_console_registered(console->ip_address);
  if (!is_registered) {
    log_debug("Wake button hit ignored for unregistered console %s",
              console->ip_address);
    return false;  // Don't process wake button hit for unregistered consoles
  }

  UIAssets* assets = ui_core_get_assets();
  vita2d_pgf* font = ui_core_get_font();

  // Get actual card dimensions for wake button positioning
  int actual_card_width = assets->console_card
                              ? vita2d_texture_get_width(assets->console_card)
                              : CONSOLE_CARD_WIDTH;
  int actual_card_height = assets->console_card
                               ? vita2d_texture_get_height(assets->console_card)
                               : CONSOLE_CARD_HEIGHT;

  // Calculate wake button position (must match rendering code)
  int name_bar_height = 40;
  int name_bar_y = card_y + actual_card_height - (actual_card_height / 3) -
                   (name_bar_height / 2);

  const char* wake_text = "Wake";
  float wake_scale = 0.7f;
  int wake_text_width = vita2d_pgf_text_width(font, wake_scale, wake_text);

  // Button dimensions (must match rendering code)
  int wake_button_width = wake_text_width + 20;  // 10px padding each side
  int wake_button_height = 25;                   // Compact height

  // Center button horizontally on card (must match rendering code)
  int screen_center_x = 960 / 2;
  int centered_card_x = screen_center_x - (actual_card_width / 2);
  int wake_button_x =
      centered_card_x + (actual_card_width - wake_button_width) / 2;
  int wake_button_y = name_bar_y + name_bar_height + 40;  // Below state text

  // Check if touch point is within wake button bounds
  bool is_hit = (touch_x >= wake_button_x &&
                 touch_x <= wake_button_x + wake_button_width &&
                 touch_y >= wake_button_y &&
                 touch_y <= wake_button_y + wake_button_height);

  if (is_hit) {
    log_info(
        "Wake button hit detected at touch (%.1f,%.1f) for button at (%d,%d) "
        "size %dx%d",
        touch_x, touch_y, wake_button_x, wake_button_y, wake_button_width,
        wake_button_height);
  }

  return is_hit;
}

bool ui_components_check_register_button_hit(const ConsoleInfo* console,
                                             int card_x, int card_y,
                                             float touch_x, float touch_y) {
  if (!console || console->console_state != 2) {  // Only for STANDBY consoles
    return false;
  }

  // Check if console is NOT registered (opposite of wake button logic)
  // Using clean slate implementation - no registered consoles initially
  bool is_registered = ui_core_is_console_registered(console->ip_address);
  if (is_registered) {
    log_debug("Register button hit ignored for already registered console %s",
              console->ip_address);
    return false;  // Don't process register button hit for already registered
                   // consoles
  }

  UIAssets* assets = ui_core_get_assets();
  vita2d_pgf* font = ui_core_get_font();

  // Get actual card dimensions for register button positioning
  int actual_card_width = assets->console_card
                              ? vita2d_texture_get_width(assets->console_card)
                              : CONSOLE_CARD_WIDTH;
  int actual_card_height = assets->console_card
                               ? vita2d_texture_get_height(assets->console_card)
                               : CONSOLE_CARD_HEIGHT;

  // Calculate register button position (must match rendering code)
  int name_bar_height = 40;
  int name_bar_y = card_y + actual_card_height - (actual_card_height / 3) -
                   (name_bar_height / 2);

  const char* register_text = "Register";
  float register_scale = 0.65f;
  int register_text_width =
      vita2d_pgf_text_width(font, register_scale, register_text);

  // Button dimensions (must match rendering code)
  int register_button_width =
      register_text_width + 20;     // 10px padding each side
  int register_button_height = 25;  // Compact height

  // Center button horizontally on card (must match rendering code)
  // The rendering code uses: x + (actual_card_width - register_button_width) /
  // 2 We need to use the same card_x that was passed to rendering
  int screen_center_x = 960 / 2;
  int centered_card_x = screen_center_x - (actual_card_width / 2);
  int register_button_x =
      centered_card_x + (actual_card_width - register_button_width) / 2;
  int register_button_y =
      name_bar_y + name_bar_height + 40;  // Below state text

  // Debug logging for coordinate troubleshooting
  log_debug(
      "Register button hit detection: card_x=%d, centered_card_x=%d, "
      "button_pos=(%d,%d), button_size=%dx%d, touch=(%.1f,%.1f)",
      card_x, centered_card_x, register_button_x, register_button_y,
      register_button_width, register_button_height, touch_x, touch_y);

  // Check if touch point is within register button bounds
  bool is_hit = (touch_x >= register_button_x &&
                 touch_x <= register_button_x + register_button_width &&
                 touch_y >= register_button_y &&
                 touch_y <= register_button_y + register_button_height);

  if (is_hit) {
    log_info(
        "Register button hit detected at touch (%.1f,%.1f) for button at "
        "(%d,%d) size %dx%d",
        touch_x, touch_y, register_button_x, register_button_y,
        register_button_width, register_button_height);
  }

  return is_hit;
}