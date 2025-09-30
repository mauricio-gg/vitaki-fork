#include "ui_settings.h"

#include <stdlib.h>
#include <string.h>

#include "../core/settings.h"
#include "../utils/logger.h"
#include "ui_components.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "vita2d_ui.h"

// Settings state
static SettingsScreen settings_screen = {0};
static SettingsState settings_state = {0};
static bool settings_initialized = false;

// Forward declarations
static void init_settings_panels(void);
static void render_settings_tab_bar(void);
static void render_current_settings_panel(void);
static uint32_t get_tab_color(SettingsTab tab);
static const char* get_tab_title(SettingsTab tab);
static void render_modern_setting_item(const SettingItem* item, int x, int y,
                                       bool selected);
static void render_modern_toggle_setting(const SettingItem* item, int x, int y);
static void render_modern_dropdown_setting(const SettingItem* item, int x,
                                           int y);
static void render_modern_slider_setting(const SettingItem* item, int x, int y);
static void render_dropdown_overlays(void);
static void handle_dropdown_setting_change(const char* label);
static void handle_slider_setting_change(const char* label, bool increase);

// Positioning helper functions for consistent alignment
static int get_text_y_position(int item_y);
static int get_control_y_position(int item_y, int control_height);

void ui_settings_init(void) {
  log_info("Initializing settings UI...");

  if (!settings_initialized) {
    // Initialize the settings backend first
    VitaRPS5Result result = vitarps5_settings_init();
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to initialize settings backend: %d", result);
    }

    init_settings_panels();
    settings_state.current_tab = SETTINGS_TAB_STREAMING;
    settings_state.selected_item = 0;
    settings_state.in_tab_navigation = false;
    settings_initialized = true;

    log_info("Settings UI initialized");
  }
}

void ui_settings_cleanup(void) {
  // Nothing to cleanup for settings
}

void ui_settings_render(void) {
  ui_core_render_logo();

  vita2d_pgf* font = ui_core_get_font();

  // L/R navigation hints - larger and better positioned
  vita2d_pgf_draw_text(font, 180, 86, UI_COLOR_TEXT_SECONDARY, 1.0f,
                       "L Previous");
  vita2d_pgf_draw_text(font, 750, 86, UI_COLOR_TEXT_SECONDARY, 1.0f, "Next R");

  // Render tab bar
  render_settings_tab_bar();

  // Render current panel
  render_current_settings_panel();

  // Version info centered below the panel - larger and cleaner
  int version_y = SETTINGS_PANEL_Y + SETTINGS_PANEL_HEIGHT + 20;
  const char* version_text = "VitaRPS5 v0.5.2 | Build: June 25, 2025";
  int version_width = vita2d_pgf_text_width(font, 0.9f, version_text);
  int version_x = SETTINGS_PANEL_X + (SETTINGS_PANEL_WIDTH - version_width) / 2;

  vita2d_pgf_draw_text(font, version_x, version_y, UI_COLOR_TEXT_SECONDARY,
                       0.9f, version_text);

  // Render dropdown lists on top of everything else for proper z-order
  render_dropdown_overlays();
}

void ui_settings_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  // Tab navigation with L/R buttons
  if ((pad->buttons & SCE_CTRL_LTRIGGER) &&
      !(prev_pad->buttons & SCE_CTRL_LTRIGGER)) {
    log_info("Settings: L trigger pressed - switching from tab %d",
             settings_state.current_tab);
    settings_state.current_tab =
        (settings_state.current_tab == 0) ? 3 : settings_state.current_tab - 1;
    settings_state.selected_item = 0;  // Reset selection when changing tabs
    log_info("Settings: Switched to tab %d", settings_state.current_tab);

    // Clear UI states when switching tabs
    UIElementStates* states = ui_components_get_states();
    for (int i = 0; i < 8; i++) {
      states->dropdown_open[i] = false;
      states->slider_dragging[i] = false;
    }

    log_info("Settings: Previous tab - switched to tab %d with %d items",
             settings_state.current_tab,
             settings_screen.panels[settings_state.current_tab].item_count);
  }

  if ((pad->buttons & SCE_CTRL_RTRIGGER) &&
      !(prev_pad->buttons & SCE_CTRL_RTRIGGER)) {
    log_info("Settings: R trigger pressed - switching from tab %d",
             settings_state.current_tab);
    settings_state.current_tab = (settings_state.current_tab + 1) % 4;
    settings_state.selected_item = 0;  // Reset selection when changing tabs
    log_info("Settings: Switched to tab %d", settings_state.current_tab);

    // Clear UI states when switching tabs
    UIElementStates* states = ui_components_get_states();
    for (int i = 0; i < 8; i++) {
      states->dropdown_open[i] = false;
      states->slider_dragging[i] = false;
    }

    log_info("Settings: Next tab - switched to tab %d with %d items",
             settings_state.current_tab,
             settings_screen.panels[settings_state.current_tab].item_count);
  }

  // Item navigation within current panel
  if ((pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP)) {
    int max_items =
        settings_screen.panels[settings_state.current_tab].item_count;
    settings_state.selected_item = (settings_state.selected_item == 0)
                                       ? max_items - 1
                                       : settings_state.selected_item - 1;
  }

  if ((pad->buttons & SCE_CTRL_DOWN) && !(prev_pad->buttons & SCE_CTRL_DOWN)) {
    int max_items =
        settings_screen.panels[settings_state.current_tab].item_count;
    settings_state.selected_item =
        (settings_state.selected_item + 1) % max_items;
  }

  // Left/Right button handling for sliders
  const SettingsPanel* current_panel =
      &settings_screen.panels[settings_state.current_tab];
  if (settings_state.selected_item >= 0 &&
      settings_state.selected_item < current_panel->item_count) {
    const SettingItem* item =
        &current_panel->items[settings_state.selected_item];

    if (item->type == SETTING_TYPE_SLIDER && item->enabled) {
      if ((pad->buttons & SCE_CTRL_LEFT) &&
          !(prev_pad->buttons & SCE_CTRL_LEFT)) {
        handle_slider_setting_change(item->label, false);  // decrease
        init_settings_panels();                            // refresh display
      }
      if ((pad->buttons & SCE_CTRL_RIGHT) &&
          !(prev_pad->buttons & SCE_CTRL_RIGHT)) {
        handle_slider_setting_change(item->label, true);  // increase
        init_settings_panels();                           // refresh display
      }
    }
  }

  // X button interaction with settings
  if ((pad->buttons & SCE_CTRL_CROSS) &&
      !(prev_pad->buttons & SCE_CTRL_CROSS)) {
    const SettingsPanel* current_panel =
        &settings_screen.panels[settings_state.current_tab];
    if (settings_state.selected_item >= 0 &&
        settings_state.selected_item < current_panel->item_count) {
      const SettingItem* item =
          &current_panel->items[settings_state.selected_item];
      if (item->enabled) {
        UIElementStates* states = ui_components_get_states();
        if (!states) return;

        // Use direct item indexing with modulo to ensure we stay within bounds
        int safe_index = settings_state.selected_item % 8;

        log_info(
            "Settings: X button pressed - tab=%d, item=%d, safe_index=%d, "
            "type=%d",
            settings_state.current_tab, settings_state.selected_item,
            safe_index, item->type);

        switch (item->type) {
          case SETTING_TYPE_TOGGLE:
            if (item->label) {
              // Actually toggle the setting via backend
              if (strcmp(item->label, "Hardware Decode") == 0) {
                vitarps5_settings_set_hardware_decode(
                    !vitarps5_settings_get_hardware_decode());
              } else if (strcmp(item->label, "Touch Controls") == 0) {
                vitarps5_settings_set_touch_controls(
                    !vitarps5_settings_get_touch_controls());
              } else if (strcmp(item->label, "Motion Controls") == 0) {
                vitarps5_settings_set_motion_controls(
                    !vitarps5_settings_get_motion_controls());
              } else if (strcmp(item->label, "HDR Support") == 0) {
                vitarps5_settings_set_hdr(!vitarps5_settings_get_hdr_support());
              } else if (strcmp(item->label, "V-Sync") == 0) {
                vitarps5_settings_set_vsync(!vitarps5_settings_get_vsync());
              } else if (strcmp(item->label, "Auto Connect") == 0) {
                vitarps5_settings_set_auto_connect(
                    !vitarps5_settings_get_auto_connect());
              } else if (strcmp(item->label, "Wake on LAN") == 0) {
                vitarps5_settings_set_wake_on_lan(
                    !vitarps5_settings_get_wake_on_lan());
              }
              // Save settings after any change
              vitarps5_settings_save();
              log_info("Toggled setting: %s", item->label);
            }
            break;
          case SETTING_TYPE_DROPDOWN:
            // For dropdowns, cycle through options instead of just opening
            if (item->label) {
              handle_dropdown_setting_change(item->label);
              // Also update display value immediately
              init_settings_panels();
              log_info("Changed dropdown: %s", item->label);
            }
            break;
          case SETTING_TYPE_SLIDER:
            states->slider_dragging[safe_index] =
                !states->slider_dragging[safe_index];
            if (item->label) {
              log_info("Toggled slider drag: %s", item->label);
            }
            break;
          default:
            break;
        }
      }
    }
  }

  // Back navigation
  if ((pad->buttons & SCE_CTRL_CIRCLE) &&
      !(prev_pad->buttons & SCE_CTRL_CIRCLE)) {
    ui_core_set_state(UI_STATE_MAIN_DASHBOARD);
    ui_navigation_set_selected_icon(3);
    log_info("Returning to main dashboard");
  }
}

SettingsTab ui_settings_get_current_tab(void) {
  return settings_state.current_tab;
}

void ui_settings_set_current_tab(SettingsTab tab) {
  if (tab < 4) {
    settings_state.current_tab = tab;
    settings_state.selected_item = 0;
  }
}

int ui_settings_get_selected_item(void) { return settings_state.selected_item; }

void ui_settings_init_panels(void) { init_settings_panels(); }

const SettingsPanel* ui_settings_get_panel(SettingsTab tab) {
  if (tab < 4) {
    return &settings_screen.panels[tab];
  }
  return NULL;
}

// Private functions
static void init_settings_panels(void) {
  // Zero out entire settings screen to prevent garbage memory access
  memset(&settings_screen, 0, sizeof(SettingsScreen));

  // NUCLEAR: Zero out all item arrays at once to guarantee clean slate
  for (int p = 0; p < 4; p++) {
    memset(&settings_screen.panels[p].items, 0,
           sizeof(settings_screen.panels[p].items));
  }

  // Initialize settings screen state
  settings_screen.selected_panel = 0;
  settings_screen.panel_navigation = true;

  // Panel 0: Streaming Quality (Blue) - Use real settings values
  strcpy(settings_screen.panels[0].title, "Streaming Quality");
  settings_screen.panels[0].color = SETTINGS_STREAMING_COLOR;
  settings_screen.panels[0].item_count = 5;
  settings_screen.panels[0].selected_item = -1;

  strcpy(settings_screen.panels[0].items[0].label, "Quality Preset");
  strcpy(settings_screen.panels[0].items[0].value,
         vitarps5_settings_get_quality_string());
  settings_screen.panels[0].items[0].type = SETTING_TYPE_DROPDOWN;
  settings_screen.panels[0].items[0].enabled = true;

  strcpy(settings_screen.panels[0].items[1].label, "Resolution");
  strcpy(settings_screen.panels[0].items[1].value,
         vitarps5_settings_get_resolution_string());
  settings_screen.panels[0].items[1].type = SETTING_TYPE_DROPDOWN;
  settings_screen.panels[0].items[1].enabled = true;

  strcpy(settings_screen.panels[0].items[2].label, "Frame Rate");
  strcpy(settings_screen.panels[0].items[2].value,
         vitarps5_settings_get_framerate_string());
  settings_screen.panels[0].items[2].type = SETTING_TYPE_DROPDOWN;
  settings_screen.panels[0].items[2].enabled = true;

  strcpy(settings_screen.panels[0].items[3].label, "Bitrate");
  strcpy(settings_screen.panels[0].items[3].value,
         vitarps5_settings_get_bitrate_string());
  settings_screen.panels[0].items[3].type = SETTING_TYPE_SLIDER;
  settings_screen.panels[0].items[3].enabled = true;

  strcpy(settings_screen.panels[0].items[4].label, "Hardware Decode");
  strcpy(settings_screen.panels[0].items[4].value, "");
  settings_screen.panels[0].items[4].type = SETTING_TYPE_TOGGLE;
  settings_screen.panels[0].items[4].enabled = true;

  // Ensure unused items are properly zeroed
  for (int i = settings_screen.panels[0].item_count; i < 8; i++) {
    memset(&settings_screen.panels[0].items[i], 0, sizeof(SettingItem));
  }

  // Panel 1: Video Settings (Green)
  strcpy(settings_screen.panels[1].title, "Video Settings");
  settings_screen.panels[1].color = SETTINGS_VIDEO_COLOR;
  settings_screen.panels[1].item_count = 5;
  settings_screen.panels[1].selected_item = -1;

  strcpy(settings_screen.panels[1].items[0].label, "Resolution");
  strcpy(settings_screen.panels[1].items[0].value,
         vitarps5_settings_get_resolution_string());
  settings_screen.panels[1].items[0].type = SETTING_TYPE_DROPDOWN;
  settings_screen.panels[1].items[0].enabled = true;

  strcpy(settings_screen.panels[1].items[1].label, "Frame Rate");
  strcpy(settings_screen.panels[1].items[1].value,
         vitarps5_settings_get_framerate_string());
  settings_screen.panels[1].items[1].type = SETTING_TYPE_DROPDOWN;
  settings_screen.panels[1].items[1].enabled = true;

  strcpy(settings_screen.panels[1].items[2].label, "Bitrate");
  strcpy(settings_screen.panels[1].items[2].value,
         vitarps5_settings_get_bitrate_string());
  settings_screen.panels[1].items[2].type = SETTING_TYPE_SLIDER;
  settings_screen.panels[1].items[2].enabled = true;

  strcpy(settings_screen.panels[1].items[3].label, "HDR Support");
  strcpy(settings_screen.panels[1].items[3].value, "");
  settings_screen.panels[1].items[3].type = SETTING_TYPE_TOGGLE;
  settings_screen.panels[1].items[3].enabled = true;

  strcpy(settings_screen.panels[1].items[4].label, "V-Sync");
  strcpy(settings_screen.panels[1].items[4].value, "");
  settings_screen.panels[1].items[4].type = SETTING_TYPE_TOGGLE;
  settings_screen.panels[1].items[4].enabled = true;

  // Ensure unused items are properly zeroed
  for (int i = settings_screen.panels[1].item_count; i < 8; i++) {
    memset(&settings_screen.panels[1].items[i], 0, sizeof(SettingItem));
  }

  // Panel 2: Network Settings (Orange)
  strcpy(settings_screen.panels[2].title, "Network Settings");
  settings_screen.panels[2].color = SETTINGS_NETWORK_COLOR;
  settings_screen.panels[2].item_count = 4;
  settings_screen.panels[2].selected_item = -1;

  strcpy(settings_screen.panels[2].items[0].label, "Connection Type");
  strcpy(settings_screen.panels[2].items[0].value, "WiFi");
  settings_screen.panels[2].items[0].type = SETTING_TYPE_DROPDOWN;
  settings_screen.panels[2].items[0].enabled = true;

  strcpy(settings_screen.panels[2].items[1].label, "Auto Connect");
  strcpy(settings_screen.panels[2].items[1].value, "");
  settings_screen.panels[2].items[1].type = SETTING_TYPE_TOGGLE;
  settings_screen.panels[2].items[1].enabled = true;

  strcpy(settings_screen.panels[2].items[2].label, "Wake on LAN");
  strcpy(settings_screen.panels[2].items[2].value, "");
  settings_screen.panels[2].items[2].type = SETTING_TYPE_TOGGLE;
  settings_screen.panels[2].items[2].enabled = true;

  strcpy(settings_screen.panels[2].items[3].label, "MTU Size");
  strcpy(settings_screen.panels[2].items[3].value,
         vitarps5_settings_get_mtu_string());
  settings_screen.panels[2].items[3].type = SETTING_TYPE_SLIDER;
  settings_screen.panels[2].items[3].enabled = true;

  // Ensure unused items are properly zeroed
  for (int i = settings_screen.panels[2].item_count; i < 8; i++) {
    memset(&settings_screen.panels[2].items[i], 0, sizeof(SettingItem));
  }

  // Panel 3: Controller Settings (Purple)
  strcpy(settings_screen.panels[3].title, "Controller Settings");
  settings_screen.panels[3].color = SETTINGS_CONTROLLER_COLOR;
  settings_screen.panels[3].item_count = 5;
  settings_screen.panels[3].selected_item = -1;

  strcpy(settings_screen.panels[3].items[0].label, "Motion Controls");
  strcpy(settings_screen.panels[3].items[0].value, "");
  settings_screen.panels[3].items[0].type = SETTING_TYPE_TOGGLE;
  settings_screen.panels[3].items[0].enabled = true;

  strcpy(settings_screen.panels[3].items[1].label, "Touch Controls");
  strcpy(settings_screen.panels[3].items[1].value, "");
  settings_screen.panels[3].items[1].type = SETTING_TYPE_TOGGLE;
  settings_screen.panels[3].items[1].enabled = true;

  strcpy(settings_screen.panels[3].items[2].label, "Deadzone");
  strcpy(settings_screen.panels[3].items[2].value,
         vitarps5_settings_get_deadzone_string());
  settings_screen.panels[3].items[2].type = SETTING_TYPE_SLIDER;
  settings_screen.panels[3].items[2].enabled = true;

  strcpy(settings_screen.panels[3].items[3].label, "Sensitivity");
  strcpy(settings_screen.panels[3].items[3].value,
         vitarps5_settings_get_sensitivity_string());
  settings_screen.panels[3].items[3].type = SETTING_TYPE_SLIDER;
  settings_screen.panels[3].items[3].enabled = true;

  strcpy(settings_screen.panels[3].items[4].label, "Button Mapping");
  strcpy(settings_screen.panels[3].items[4].value, "Default");
  settings_screen.panels[3].items[4].type = SETTING_TYPE_DROPDOWN;
  settings_screen.panels[3].items[4].enabled = true;

  // CRITICAL: Ensure unused items are properly zeroed (items 6-7)
  for (int i = settings_screen.panels[3].item_count; i < 8; i++) {
    memset(&settings_screen.panels[3].items[i], 0, sizeof(SettingItem));
  }

  log_info("Settings panels initialized");
}

static void render_settings_tab_bar(void) {
  const char* tab_names[] = {"Streaming", "Video", "Network", "Controller"};
  uint32_t tab_colors[] = {SETTINGS_TAB_COLOR_STREAMING,
                           SETTINGS_TAB_COLOR_VIDEO, SETTINGS_TAB_COLOR_NETWORK,
                           SETTINGS_TAB_COLOR_CONTROLLER};

  // Center tabs over the panel
  int total_tabs_width = (4 * SETTINGS_TAB_WIDTH) + (3 * SETTINGS_TAB_SPACING);
  int tab_start_x =
      SETTINGS_PANEL_X + (SETTINGS_PANEL_WIDTH - total_tabs_width) / 2;

  vita2d_pgf* font = ui_core_get_font();

  for (int i = 0; i < 4; i++) {
    int tab_x = tab_start_x + i * (SETTINGS_TAB_WIDTH + SETTINGS_TAB_SPACING);
    bool is_active = (settings_state.current_tab == i);

    // Tab background
    uint32_t bg_color = is_active ? tab_colors[i] : RGBA8(70, 70, 70, 255);
    ui_core_render_rounded_rectangle(tab_x, SETTINGS_TAB_BAR_Y,
                                     SETTINGS_TAB_WIDTH, SETTINGS_TAB_HEIGHT, 8,
                                     bg_color);

    // Tab text centered
    uint32_t text_color =
        is_active ? UI_COLOR_TEXT_PRIMARY : RGBA8(200, 200, 200, 255);
    int text_width = vita2d_pgf_text_width(font, 0.9f, tab_names[i]);
    int text_x = tab_x + (SETTINGS_TAB_WIDTH - text_width) / 2;
    int text_y = SETTINGS_TAB_BAR_Y + (SETTINGS_TAB_HEIGHT / 2) + 5;

    vita2d_pgf_draw_text(font, text_x, text_y, text_color, 0.9f, tab_names[i]);
  }
}

static void render_current_settings_panel(void) {
  if (settings_state.current_tab >= 4) return;

  const SettingsPanel* panel =
      &settings_screen.panels[settings_state.current_tab];
  if (!panel) return;

  // Draw the full panel background in charcoal
  ui_core_render_rounded_rectangle(SETTINGS_PANEL_X, SETTINGS_PANEL_Y,
                                   SETTINGS_PANEL_WIDTH, SETTINGS_PANEL_HEIGHT,
                                   SETTINGS_PANEL_RADIUS, UI_COLOR_CARD_BG);

  // Draw colored header with proper rounded top corners
  uint32_t header_color = get_tab_color(settings_state.current_tab);

  // Main header rectangle (excluding corner areas)
  vita2d_draw_rectangle(SETTINGS_PANEL_X + SETTINGS_PANEL_RADIUS,
                        SETTINGS_PANEL_Y,
                        SETTINGS_PANEL_WIDTH - (2 * SETTINGS_PANEL_RADIUS),
                        SETTINGS_HEADER_HEIGHT, header_color);

  // Left and right sides of header
  vita2d_draw_rectangle(
      SETTINGS_PANEL_X, SETTINGS_PANEL_Y + SETTINGS_PANEL_RADIUS,
      SETTINGS_PANEL_RADIUS, SETTINGS_HEADER_HEIGHT - SETTINGS_PANEL_RADIUS,
      header_color);
  vita2d_draw_rectangle(
      SETTINGS_PANEL_X + SETTINGS_PANEL_WIDTH - SETTINGS_PANEL_RADIUS,
      SETTINGS_PANEL_Y + SETTINGS_PANEL_RADIUS, SETTINGS_PANEL_RADIUS,
      SETTINGS_HEADER_HEIGHT - SETTINGS_PANEL_RADIUS, header_color);

  // Draw rounded top corners
  for (int y = 0; y < SETTINGS_PANEL_RADIUS; y++) {
    for (int x = 0; x < SETTINGS_PANEL_RADIUS; x++) {
      // Top left corner
      int dx_left = SETTINGS_PANEL_RADIUS - x;
      int dy_left = SETTINGS_PANEL_RADIUS - y;
      if (dx_left * dx_left + dy_left * dy_left <=
          SETTINGS_PANEL_RADIUS * SETTINGS_PANEL_RADIUS) {
        vita2d_draw_rectangle(SETTINGS_PANEL_X + x, SETTINGS_PANEL_Y + y, 1, 1,
                              header_color);
      }

      // Top right corner
      int dx_right = x;
      int dy_right = SETTINGS_PANEL_RADIUS - y;
      if (dx_right * dx_right + dy_right * dy_right <=
          SETTINGS_PANEL_RADIUS * SETTINGS_PANEL_RADIUS) {
        vita2d_draw_rectangle(
            SETTINGS_PANEL_X + SETTINGS_PANEL_WIDTH - SETTINGS_PANEL_RADIUS + x,
            SETTINGS_PANEL_Y + y, 1, 1, header_color);
      }
    }
  }

  // Panel title in header
  vita2d_pgf* font = ui_core_get_font();
  vita2d_pgf_draw_text(font, SETTINGS_PANEL_X + SETTINGS_LABEL_X_OFFSET,
                       SETTINGS_PANEL_Y + 35, UI_COLOR_TEXT_PRIMARY, 1.1f,
                       get_tab_title(settings_state.current_tab));

  // Ensure selected_item is within bounds for current panel
  if (settings_state.selected_item >= panel->item_count) {
    settings_state.selected_item = 0;
  }

  // Render settings items in body area
  for (int i = 0; i < panel->item_count && i < 8;
       i++) {  // Additional bounds check
    // Validate item before rendering
    if (!panel->items[i].label || !panel->items[i].label[0]) {
      continue;
    }

    // Debug value pointers for slider/dropdown items
    if (panel->items[i].type == SETTING_TYPE_SLIDER ||
        panel->items[i].type == SETTING_TYPE_DROPDOWN) {
      if (!panel->items[i].value) {
        continue;
      }
    }

    bool selected = (settings_state.selected_item == i);
    int item_y = SETTINGS_PANEL_Y + SETTINGS_HEADER_HEIGHT + 20 +
                 (i * SETTINGS_ITEM_HEIGHT);
    render_modern_setting_item(&panel->items[i], SETTINGS_PANEL_X, item_y,
                               selected);
  }
}

static uint32_t get_tab_color(SettingsTab tab) {
  switch (tab) {
    case SETTINGS_TAB_STREAMING:
      return SETTINGS_TAB_COLOR_STREAMING;
    case SETTINGS_TAB_VIDEO:
      return SETTINGS_TAB_COLOR_VIDEO;
    case SETTINGS_TAB_NETWORK:
      return SETTINGS_TAB_COLOR_NETWORK;
    case SETTINGS_TAB_CONTROLLER:
      return SETTINGS_TAB_COLOR_CONTROLLER;
    default:
      return SETTINGS_TAB_COLOR_STREAMING;
  }
}

static const char* get_tab_title(SettingsTab tab) {
  switch (tab) {
    case SETTINGS_TAB_STREAMING:
      return "Streaming Quality";
    case SETTINGS_TAB_VIDEO:
      return "Video Settings";
    case SETTINGS_TAB_NETWORK:
      return "Network Settings";
    case SETTINGS_TAB_CONTROLLER:
      return "Controller Settings";
    default:
      return "Settings";
  }
}

static void render_modern_setting_item(const SettingItem* item, int x, int y,
                                       bool selected) {
  if (!item || !item->label || !item->label[0]) return;

  vita2d_pgf* font = ui_core_get_font();
  uint32_t label_color =
      item->enabled ? UI_COLOR_TEXT_SECONDARY : RGBA8(128, 128, 128, 255);
  uint32_t value_color =
      item->enabled ? UI_COLOR_TEXT_PRIMARY : RGBA8(128, 128, 128, 255);

  // Highlight if selected - using constants for consistent alignment
  if (selected && item->enabled) {
    int highlight_x = x + SETTINGS_BODY_PADDING;
    int highlight_width = SETTINGS_PANEL_WIDTH - (2 * SETTINGS_BODY_PADDING);
    int highlight_y = y + SELECTION_HIGHLIGHT_PADDING;
    int highlight_height = SELECTION_HIGHLIGHT_HEIGHT;

    // Ensure highlight doesn't overlap with header
    int min_highlight_y =
        SETTINGS_PANEL_Y + SETTINGS_HEADER_HEIGHT + SELECTION_HIGHLIGHT_PADDING;
    if (highlight_y < min_highlight_y) {
      highlight_y = min_highlight_y;
    }

    vita2d_draw_rectangle(highlight_x, highlight_y, highlight_width,
                          highlight_height, RGBA8(255, 255, 255, 20));
  }

  // Render label on the left - using consistent positioning
  int label_y = get_text_y_position(y);
  vita2d_pgf_draw_text(font, x + SETTINGS_LABEL_X_OFFSET, label_y, label_color,
                       0.9f, item->label);

  // Render value based on type
  switch (item->type) {
    case SETTING_TYPE_TOGGLE:
      render_modern_toggle_setting(item, x, y);
      break;
    case SETTING_TYPE_DROPDOWN:
      render_modern_dropdown_setting(item, x, y);
      break;
    case SETTING_TYPE_SLIDER:
      render_modern_slider_setting(item, x, y);
      break;
    case SETTING_TYPE_TEXT: {
      if (item->value && strlen(item->value) > 0) {
        int text_width = strlen(item->value) * 8;
        int value_y = get_text_y_position(y);
        vita2d_pgf_draw_text(
            font,
            x + SETTINGS_PANEL_WIDTH - SETTINGS_VALUE_RIGHT_MARGIN - text_width,
            value_y, value_color, 0.9f, item->value);
      }
      break;
    }
  }
}

static void render_modern_toggle_setting(const SettingItem* item, int x,
                                         int y) {
  if (!item || !item->label || !item->label[0]) return;

  if (!item->value) return;

  // Determine toggle state from settings backend
  bool toggle_on = false;
  if (strcmp(item->label, "Hardware Decode") == 0) {
    toggle_on = vitarps5_settings_get_hardware_decode();
  } else if (strcmp(item->label, "Touch Controls") == 0) {
    toggle_on = vitarps5_settings_get_touch_controls();
  } else if (strcmp(item->label, "Motion Controls") == 0) {
    toggle_on = vitarps5_settings_get_motion_controls();
  } else if (strcmp(item->label, "HDR Support") == 0) {
    toggle_on = vitarps5_settings_get_hdr_support();
  } else if (strcmp(item->label, "V-Sync") == 0) {
    toggle_on = vitarps5_settings_get_vsync();
  } else if (strcmp(item->label, "Auto Connect") == 0) {
    toggle_on = vitarps5_settings_get_auto_connect();
  } else if (strcmp(item->label, "Wake on LAN") == 0) {
    toggle_on = vitarps5_settings_get_wake_on_lan();
  }

  // Calculate toggle position - using consistent helper function
  int toggle_x =
      x + SETTINGS_PANEL_WIDTH - SETTINGS_VALUE_RIGHT_MARGIN - TOGGLE_WIDTH;
  int toggle_y = get_control_y_position(y, TOGGLE_HEIGHT);

  if (toggle_x < x) toggle_x = x + SETTINGS_BODY_PADDING;

  // Draw toggle with animation (simplified for now)
  float animation = toggle_on ? 1.0f : 0.0f;
  ui_components_draw_toggle_switch(toggle_x, toggle_y, toggle_on, animation,
                                   item->enabled);
}

static void render_modern_dropdown_setting(const SettingItem* item, int x,
                                           int y) {
  if (!item || !item->value || !item->value[0]) return;

  int dropdown_x = x + SETTINGS_PANEL_WIDTH - SETTINGS_VALUE_RIGHT_MARGIN -
                   SETTINGS_DROPDOWN_WIDTH;
  if (dropdown_x < x + SETTINGS_BODY_PADDING) {
    dropdown_x = x + SETTINGS_BODY_PADDING;
  }

  UIElementStates* states = ui_components_get_states();
  if (!states) return;

  // Use direct item indexing with modulo to ensure we stay within bounds
  int safe_index = settings_state.selected_item % 8;

  bool is_open = states->dropdown_open[safe_index];
  int dropdown_y = get_control_y_position(y, DROPDOWN_HEIGHT);

  ui_components_draw_dropdown_menu(dropdown_x, dropdown_y,
                                   SETTINGS_DROPDOWN_WIDTH, item->value,
                                   is_open, item->enabled);
}

static void render_modern_slider_setting(const SettingItem* item, int x,
                                         int y) {
  if (!item || !item->value || !item->value[0]) return;

  // Safe strlen call - we already validated item->value above
  int value_len = strlen(item->value);
  int value_text_width = value_len * 8 + 10;
  int slider_x = x + SETTINGS_PANEL_WIDTH - SETTINGS_VALUE_RIGHT_MARGIN -
                 SETTINGS_SLIDER_WIDTH - value_text_width;

  int min_x = x + SETTINGS_LABEL_X_OFFSET + 150;
  if (slider_x < min_x) slider_x = min_x;

  // Calculate value based on item value - with comprehensive null safety
  float percentage = 0.5f;
  // item->value is guaranteed non-null and non-empty by validation above
  if (strstr(item->value, "%")) {
    percentage = atoi(item->value) / 100.0f;
  } else if (strcmp(item->value, "8000") == 0 ||
             strcmp(item->value, "8000 Kbps") == 0) {
    percentage = 0.8f;
  } else if (strcmp(item->value, "1500") == 0) {
    percentage = 0.75f;  // MTU size
  }

  UIElementStates* states = ui_components_get_states();
  if (!states) return;

  // Use direct item indexing with modulo to ensure we stay within bounds
  int safe_index = settings_state.selected_item % 8;

  bool is_dragging = states->slider_dragging[safe_index];
  int slider_y = get_control_y_position(y, SLIDER_THUMB_RADIUS * 2);

  ui_components_draw_slider(slider_x, slider_y, SETTINGS_SLIDER_WIDTH,
                            percentage, is_dragging, item->enabled);

  // Value text to the right of slider - using consistent text positioning
  if (value_len > 0) {
    vita2d_pgf* font = ui_core_get_font();
    int text_x = slider_x + SETTINGS_SLIDER_WIDTH + 10;
    if (text_x + value_text_width >
        x + SETTINGS_PANEL_WIDTH - SETTINGS_VALUE_RIGHT_MARGIN) {
      text_x = x + SETTINGS_PANEL_WIDTH - SETTINGS_VALUE_RIGHT_MARGIN -
               value_text_width;
    }

    int value_text_y = get_text_y_position(y);
    vita2d_pgf_draw_text(font, text_x, value_text_y, UI_COLOR_TEXT_PRIMARY,
                         0.8f, item->value);
  }
}

static void render_dropdown_overlays(void) {
  UIElementStates* states = ui_components_get_states();
  if (!states) return;

  // Safety check for current_tab
  if (settings_state.current_tab >= 4) return;

  const SettingsPanel* panel =
      &settings_screen.panels[settings_state.current_tab];
  if (!panel) return;

  // Render dropdown lists for all open dropdowns in the current panel
  for (int i = 0; i < panel->item_count && i < 8; i++) {
    // Validate item before processing
    if (!panel->items[i].label || !panel->items[i].label[0]) {
      continue;
    }

    // Use direct item indexing with modulo to ensure we stay within bounds
    int safe_index = i % 8;

    if (panel->items[i].type == SETTING_TYPE_DROPDOWN &&
        panel->items[i].value && panel->items[i].value[0] &&
        states->dropdown_open[safe_index]) {
      // Calculate the same position as in render_modern_dropdown_setting
      int item_y = SETTINGS_PANEL_Y + SETTINGS_HEADER_HEIGHT + 20 +
                   (i * SETTINGS_ITEM_HEIGHT);
      int dropdown_x = SETTINGS_PANEL_X + SETTINGS_PANEL_WIDTH -
                       SETTINGS_VALUE_RIGHT_MARGIN - SETTINGS_DROPDOWN_WIDTH;

      if (dropdown_x < SETTINGS_PANEL_X + SETTINGS_BODY_PADDING) {
        dropdown_x = SETTINGS_PANEL_X + SETTINGS_BODY_PADDING;
      }

      int dropdown_y = get_control_y_position(item_y, DROPDOWN_HEIGHT);

      ui_components_draw_dropdown_list(
          dropdown_x, dropdown_y, SETTINGS_DROPDOWN_WIDTH,
          panel->items[i].value, panel->items[i].enabled);
    }
  }
}

// Helper function to handle dropdown setting changes
static void handle_dropdown_setting_change(const char* label) {
  if (!label) return;

  if (strcmp(label, "Quality Preset") == 0) {
    const VitaRPS5Settings* current = vitarps5_settings_get();
    VitaRPS5Quality new_preset = (current->quality_preset + 1) % 4;
    vitarps5_settings_set_quality_preset(new_preset);
  } else if (strcmp(label, "Resolution") == 0) {
    const VitaRPS5Settings* current = vitarps5_settings_get();
    if (current->resolution_width == 720) {
      vitarps5_settings_set_resolution(540, 360);
    } else {
      vitarps5_settings_set_resolution(720, 480);
    }
  } else if (strcmp(label, "Frame Rate") == 0) {
    const VitaRPS5Settings* current = vitarps5_settings_get();
    uint32_t new_fps = (current->target_fps == 30) ? 60 : 30;
    vitarps5_settings_set_framerate(new_fps);
  } else if (strcmp(label, "Connection Type") == 0) {
    // For now, just log - connection type detection could be implemented
    log_info("Connection type cycling not implemented yet");
  } else if (strcmp(label, "Button Mapping") == 0) {
    log_info("Button mapping cycling not implemented yet");
  }

  // Save settings after change
  vitarps5_settings_save();
}

// Helper function to handle slider setting changes
static void handle_slider_setting_change(const char* label, bool increase) {
  if (!label) return;

  if (strcmp(label, "Bitrate") == 0) {
    const VitaRPS5Settings* current = vitarps5_settings_get();
    uint32_t step = 1000;  // 1 Mbps steps
    uint32_t new_bitrate = current->target_bitrate;

    if (increase && new_bitrate < 15000) {
      new_bitrate += step;
    } else if (!increase && new_bitrate > 1000) {
      new_bitrate -= step;
    }
    vitarps5_settings_set_bitrate(new_bitrate);
  } else if (strcmp(label, "Deadzone") == 0) {
    const VitaRPS5Settings* current = vitarps5_settings_get();
    float step = 5.0f;  // 5% steps
    float new_deadzone = current->deadzone_percent;

    if (increase && new_deadzone < 50.0f) {
      new_deadzone += step;
    } else if (!increase && new_deadzone > 0.0f) {
      new_deadzone -= step;
    }
    vitarps5_settings_set_deadzone(new_deadzone);
  } else if (strcmp(label, "Sensitivity") == 0) {
    const VitaRPS5Settings* current = vitarps5_settings_get();
    float step = 10.0f;  // 10% steps
    float new_sensitivity = current->sensitivity_percent;

    if (increase && new_sensitivity < 150.0f) {
      new_sensitivity += step;
    } else if (!increase && new_sensitivity > 50.0f) {
      new_sensitivity -= step;
    }
    vitarps5_settings_set_sensitivity(new_sensitivity);
  } else if (strcmp(label, "MTU Size") == 0) {
    const VitaRPS5Settings* current = vitarps5_settings_get();
    uint32_t step = 100;  // 100 byte steps
    uint32_t new_mtu = current->mtu_size;

    if (increase && new_mtu < 1500) {
      new_mtu += step;
    } else if (!increase && new_mtu > 1200) {
      new_mtu -= step;
    }
    vitarps5_settings_set_mtu_size(new_mtu);
  }

  // Save settings after change
  vitarps5_settings_save();
}

// Positioning helper functions for consistent alignment
static int get_text_y_position(int item_y) {
  // Use constant-based calculation with empirically determined offset
  return item_y + TEXT_VERTICAL_CENTER_OFFSET;
}

static int get_control_y_position(int item_y, int control_height) {
  return item_y + CONTROL_VERTICAL_CENTER_OFFSET - (control_height / 2);
}