#include "ui_controller.h"

#include <stdio.h>
#include <string.h>

#include "../utils/logger.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "vita2d_ui.h"

// Controller mapping state
static ControllerMappingState mapping_state = {0};
static bool mapping_initialized = false;

// Screen and layout calculations
#define SCREEN_WIDTH 960
#define AVAILABLE_SPACE_FOR_IMAGES 580  // 960 - 320 (table) - 60 (margins)
#define VITA_SCALE 0.35f                // Scale to fit available space properly

// Mapping table positioning (improved layout with parent-child relationships)
#define MAPPING_TABLE_X 180
#define MAPPING_TABLE_Y 120
#define MAPPING_TABLE_WIDTH 320
#define MAPPING_TABLE_HEIGHT 400  // Increased from 300
#define MAPPING_TABLE_RADIUS 12

// Table structure constants (matching settings pattern)
#define TABLE_HEADER_HEIGHT 50    // Same as SETTINGS_HEADER_HEIGHT
#define TABLE_CONTENT_PADDING 20  // Body padding from settings
#define TABLE_ROW_HEIGHT 45       // Same as SETTINGS_ITEM_HEIGHT
#define TABLE_ROW_PADDING 8       // Padding within each row container
#define TABLE_TEXT_OFFSET_X 25    // Text offset from table edge
#define TABLE_COLUMN_2_X \
  295  // Second column position (properly spaced for composite header)
#define MAX_VISIBLE_ROWS 7  // Optimized to fill table space
#define SCROLLBAR_WIDTH 8   // VSCode-style thin scrollbar
#define SCROLLBAR_MARGIN 6  // Margin from table edge

// Controller diagram positioning (right-aligned)
#define IMAGES_RIGHT_EDGE (SCREEN_WIDTH - 40)  // 40px margin from screen edge
#define VITA_FRONT_Y 120  // Moved up since we removed the label
#define VITA_SPACING 5    // Spacing between front and back images
// VITA_BACK_Y will be calculated dynamically based on front image height +
// spacing

// Forward declarations
static void render_controller_diagrams(void);
static void render_mapping_table(void);
static void render_button_highlights(void);
static void init_default_mappings(void);
static void controller_mapping_reset_to_default(int row_index);
static void controller_mapping_reset_all_to_default(void);

void ui_controller_init(void) {
  log_info("Initializing controller mapping system...");

  if (!mapping_initialized) {
    // Initialize mapping state
    mapping_state.selected_row = 0;  // Start with first row selected
    mapping_state.in_assignment_mode = false;
    mapping_state.rebinding_button = -1;  // No button being rebound

    // Initialize controller preset with default mappings
    strcpy(mapping_state.current_preset.name, "Default");
    init_default_mappings();

    // Initialize button hit zones
    controller_mapping_init_hit_zones();

    mapping_initialized = true;
    log_info("Controller mapping system initialized");
  }
}

void ui_controller_cleanup(void) {
  // Nothing to cleanup for controller
}

void ui_controller_render(void) {
  vita2d_pgf* font = ui_core_get_font();

  // Header centered
  vita2d_pgf_draw_text(font, 350, 60, UI_COLOR_TEXT_PRIMARY, 1.2f,
                       "Controller Configuration");

  // Vita RPS5 logo in top right
  ui_core_render_logo();

  // Instructions and hints (centered with better size)
  const char* hint_text =
      "Navigate with D-Pad, X: Edit mapping, SELECT: Reset all mappings";
  int hint_text_width = vita2d_pgf_text_width(font, 1.0f, hint_text);
  int hint_x = (SCREEN_WIDTH - hint_text_width) / 2;  // Center horizontally
  vita2d_pgf_draw_text(font, hint_x, MAPPING_TABLE_Y - 25,
                       UI_COLOR_TEXT_SECONDARY, 1.0f,  // Increased from 0.8f
                       hint_text);

  // Render main UI components
  render_mapping_table();
  render_controller_diagrams();
  render_button_highlights();
}

void ui_controller_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  // Handle assignment mode first
  if (mapping_state.in_assignment_mode) {
    // PS5 button selection in assignment mode
    if ((pad->buttons & SCE_CTRL_CROSS) &&
        !(prev_pad->buttons & SCE_CTRL_CROSS)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_CROSS);
      return;
    }
    if ((pad->buttons & SCE_CTRL_CIRCLE) &&
        !(prev_pad->buttons & SCE_CTRL_CIRCLE)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_CIRCLE);
      return;
    }
    if ((pad->buttons & SCE_CTRL_SQUARE) &&
        !(prev_pad->buttons & SCE_CTRL_SQUARE)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_SQUARE);
      return;
    }
    if ((pad->buttons & SCE_CTRL_TRIANGLE) &&
        !(prev_pad->buttons & SCE_CTRL_TRIANGLE)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_TRIANGLE);
      return;
    }
    if ((pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_DPAD_UP);
      return;
    }
    if ((pad->buttons & SCE_CTRL_DOWN) &&
        !(prev_pad->buttons & SCE_CTRL_DOWN)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_DPAD_DOWN);
      return;
    }
    if ((pad->buttons & SCE_CTRL_LEFT) &&
        !(prev_pad->buttons & SCE_CTRL_LEFT)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_DPAD_LEFT);
      return;
    }
    if ((pad->buttons & SCE_CTRL_RIGHT) &&
        !(prev_pad->buttons & SCE_CTRL_RIGHT)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_DPAD_RIGHT);
      return;
    }
    if ((pad->buttons & SCE_CTRL_LTRIGGER) &&
        !(prev_pad->buttons & SCE_CTRL_LTRIGGER)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_L1);
      return;
    }
    if ((pad->buttons & SCE_CTRL_RTRIGGER) &&
        !(prev_pad->buttons & SCE_CTRL_RTRIGGER)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_R1);
      return;
    }
    if ((pad->buttons & SCE_CTRL_START) &&
        !(prev_pad->buttons & SCE_CTRL_START)) {
      controller_mapping_assign_ps5_button(PS5_BUTTON_OPTIONS);
      return;
    }
    if ((pad->buttons & SCE_CTRL_SELECT) &&
        !(prev_pad->buttons & SCE_CTRL_SELECT)) {
      // In assignment mode, SELECT exits instead of assigning
      mapping_state.in_assignment_mode = false;
      mapping_state.rebinding_button = -1;
      log_info("Cancelled button assignment");
      return;
    }
    return;  // Don't process other inputs in assignment mode
  }

  // Normal table navigation with scrolling
  if ((pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP)) {
    if (mapping_state.selected_row > 0) {
      // Move up within visible area
      mapping_state.selected_row--;
    } else if (mapping_state.scroll_offset > 0) {
      // Scroll up one row
      mapping_state.scroll_offset--;
    } else {
      // Wrap to bottom
      mapping_state.scroll_offset =
          (mapping_state.current_preset.mapping_count > MAX_VISIBLE_ROWS)
              ? mapping_state.current_preset.mapping_count - MAX_VISIBLE_ROWS
              : 0;
      mapping_state.selected_row =
          (mapping_state.current_preset.mapping_count > MAX_VISIBLE_ROWS)
              ? MAX_VISIBLE_ROWS - 1
              : mapping_state.current_preset.mapping_count - 1;
    }
  } else if ((pad->buttons & SCE_CTRL_DOWN) &&
             !(prev_pad->buttons & SCE_CTRL_DOWN)) {
    int visible_count = (mapping_state.current_preset.mapping_count -
                             mapping_state.scroll_offset <
                         MAX_VISIBLE_ROWS)
                            ? mapping_state.current_preset.mapping_count -
                                  mapping_state.scroll_offset
                            : MAX_VISIBLE_ROWS;

    if (mapping_state.selected_row < visible_count - 1) {
      // Move down within visible area
      mapping_state.selected_row++;
    } else if (mapping_state.scroll_offset + MAX_VISIBLE_ROWS <
               mapping_state.current_preset.mapping_count) {
      // Scroll down one row
      mapping_state.scroll_offset++;
    } else {
      // Wrap to top
      mapping_state.scroll_offset = 0;
      mapping_state.selected_row = 0;
    }
  } else if ((pad->buttons & SCE_CTRL_CROSS) &&
             !(prev_pad->buttons & SCE_CTRL_CROSS)) {
    // Start editing the selected mapping (account for scrolling)
    int actual_row = mapping_state.scroll_offset + mapping_state.selected_row;
    if (actual_row < mapping_state.current_preset.mapping_count) {
      mapping_state.in_assignment_mode = true;
      mapping_state.rebinding_button =
          mapping_state.current_preset.mappings[actual_row].vita_code;
      log_info("Started rebinding %s",
               vita_button_name(mapping_state.rebinding_button));
    }
  } else if ((pad->buttons & SCE_CTRL_SELECT) &&
             !(prev_pad->buttons & SCE_CTRL_SELECT)) {
    // Reset ALL mappings to default
    controller_mapping_reset_all_to_default();
    log_info("Reset all controller mappings to default");
  } else if ((pad->buttons & SCE_CTRL_CIRCLE) &&
             !(prev_pad->buttons & SCE_CTRL_CIRCLE)) {
    // Back to main dashboard
    ui_core_set_state(UI_STATE_MAIN_DASHBOARD);
    ui_navigation_set_selected_icon(3);  // Return to Play
    log_info("Returning to main dashboard");
  }
}

// Controller mapping implementation
void controller_mapping_init_hit_zones(void) {
  // Clear existing zones
  mapping_state.front_button_count = 0;
  mapping_state.rear_button_count = 0;

  // Calculate dynamic positions based on actual image placement
  // This needs to be done at runtime since image positions depend on texture
  // sizes For now, we'll use estimated positions - hit zones will be updated
  // when rendering starts

  // Note: Hit zones will be properly positioned in render_button_highlights
  // when we have access to the actual image positions
}

VitaButtonId controller_mapping_check_hit_zone(int x, int y, bool is_rear) {
  ButtonHitZone* zones =
      is_rear ? mapping_state.rear_buttons : mapping_state.front_buttons;
  int count = is_rear ? mapping_state.rear_button_count
                      : mapping_state.front_button_count;

  for (int i = 0; i < count; i++) {
    int zone_x1 = zones[i].x - zones[i].width / 2;
    int zone_y1 = zones[i].y - zones[i].height / 2;
    int zone_x2 = zones[i].x + zones[i].width / 2;
    int zone_y2 = zones[i].y + zones[i].height / 2;

    if (x >= zone_x1 && x <= zone_x2 && y >= zone_y1 && y <= zone_y2) {
      return zones[i].button_id;
    }
  }
  return -1;  // No hit
}

void controller_mapping_select_button(VitaButtonId button) {
  // This function is no longer used with the new table navigation system
  // Button selection is now handled by table row navigation
  log_info("Note: Direct button selection disabled - use table navigation");
}

void controller_mapping_assign_ps5_button(PS5ButtonId ps5_button) {
  if (mapping_state.in_assignment_mode && ps5_button < PS5_BUTTON_COUNT) {
    int actual_row = mapping_state.scroll_offset + mapping_state.selected_row;

    if (actual_row < mapping_state.current_preset.mapping_count) {
      // Check if this PS5 button is already mapped to another Vita button
      for (int i = 0; i < mapping_state.current_preset.mapping_count; i++) {
        if (mapping_state.current_preset.mappings[i].ps5_code == ps5_button) {
          // Remove the old mapping by setting it to "None"
          strcpy(mapping_state.current_preset.mappings[i].ps5_button, "None");
          mapping_state.current_preset.mappings[i].ps5_code =
              PS5_BUTTON_COUNT;  // Invalid value
          log_info("Removed duplicate mapping from %s",
                   mapping_state.current_preset.mappings[i].vita_button);
          break;
        }
      }

      // Update the selected mapping
      ControllerMapping* mapping =
          &mapping_state.current_preset.mappings[actual_row];
      mapping->ps5_code = ps5_button;
      strcpy(mapping->ps5_button, ps5_button_name(ps5_button));
      log_info("Mapped %s to %s", mapping->vita_button, mapping->ps5_button);

      // Exit assignment mode
      mapping_state.in_assignment_mode = false;
      mapping_state.rebinding_button = -1;
    }
  }
}

static void render_controller_diagrams(void) {
  UIAssets* assets = ui_core_get_assets();

  // Calculate right-aligned positions for both images
  int front_width =
      assets->vita_front
          ? (int)(vita2d_texture_get_width(assets->vita_front) * VITA_SCALE)
          : 300;
  int back_width =
      assets->vita_back
          ? (int)(vita2d_texture_get_width(assets->vita_back) * VITA_SCALE)
          : 300;

  int front_height =
      assets->vita_front
          ? (int)(vita2d_texture_get_height(assets->vita_front) * VITA_SCALE)
          : 200;

  int vita_front_x = IMAGES_RIGHT_EDGE - front_width;
  int vita_back_x = IMAGES_RIGHT_EDGE - back_width;

  // Calculate back image Y position with proper spacing
  int vita_back_y = VITA_FRONT_Y + front_height + VITA_SPACING;

  // Front view (scaled and right-aligned)
  if (assets->vita_front) {
    vita2d_draw_texture_scale(assets->vita_front, vita_front_x, VITA_FRONT_Y,
                              VITA_SCALE, VITA_SCALE);
  }

  // Back view (scaled and right-aligned with proper spacing)
  if (assets->vita_back) {
    vita2d_draw_texture_scale(assets->vita_back, vita_back_x, vita_back_y,
                              VITA_SCALE, VITA_SCALE);
  }
}

static void render_mapping_table(void) {
  vita2d_pgf* font = ui_core_get_font();

  // Main table background with settings panel style
  ui_core_render_rounded_rectangle(MAPPING_TABLE_X, MAPPING_TABLE_Y,
                                   MAPPING_TABLE_WIDTH, MAPPING_TABLE_HEIGHT,
                                   MAPPING_TABLE_RADIUS, UI_COLOR_CARD_BG);

  // Composite header with two different colors (muted turquoise and red)
  uint32_t teal_color = RGBA8(65, 180, 150, 255);  // Muted turquoise for Vita
  uint32_t red_color = RGBA8(205, 75, 75, 255);    // Muted red for PS5

  int header_split = MAPPING_TABLE_WIDTH / 2;  // Split header in half

  // Left half - PS Vita Control (turquoise)
  // Main rectangle
  vita2d_draw_rectangle(MAPPING_TABLE_X + MAPPING_TABLE_RADIUS, MAPPING_TABLE_Y,
                        header_split - MAPPING_TABLE_RADIUS,
                        TABLE_HEADER_HEIGHT, teal_color);
  // Left side
  vita2d_draw_rectangle(MAPPING_TABLE_X, MAPPING_TABLE_Y + MAPPING_TABLE_RADIUS,
                        MAPPING_TABLE_RADIUS,
                        TABLE_HEADER_HEIGHT - MAPPING_TABLE_RADIUS, teal_color);

  // Right half - PS5 Button (red)
  // Main rectangle
  vita2d_draw_rectangle(MAPPING_TABLE_X + header_split, MAPPING_TABLE_Y,
                        header_split - MAPPING_TABLE_RADIUS,
                        TABLE_HEADER_HEIGHT, red_color);
  // Right side
  vita2d_draw_rectangle(
      MAPPING_TABLE_X + MAPPING_TABLE_WIDTH - MAPPING_TABLE_RADIUS,
      MAPPING_TABLE_Y + MAPPING_TABLE_RADIUS, MAPPING_TABLE_RADIUS,
      TABLE_HEADER_HEIGHT - MAPPING_TABLE_RADIUS, red_color);

  // Draw rounded corners
  for (int y = 0; y < MAPPING_TABLE_RADIUS; y++) {
    for (int x = 0; x < MAPPING_TABLE_RADIUS; x++) {
      // Top left corner (turquoise)
      int dx_left = MAPPING_TABLE_RADIUS - x;
      int dy_left = MAPPING_TABLE_RADIUS - y;
      if (dx_left * dx_left + dy_left * dy_left <=
          MAPPING_TABLE_RADIUS * MAPPING_TABLE_RADIUS) {
        vita2d_draw_rectangle(MAPPING_TABLE_X + x, MAPPING_TABLE_Y + y, 1, 1,
                              teal_color);
      }

      // Top right corner (red)
      int dx_right = x;
      int dy_right = MAPPING_TABLE_RADIUS - y;
      if (dx_right * dx_right + dy_right * dy_right <=
          MAPPING_TABLE_RADIUS * MAPPING_TABLE_RADIUS) {
        vita2d_draw_rectangle(
            MAPPING_TABLE_X + MAPPING_TABLE_WIDTH - MAPPING_TABLE_RADIUS + x,
            MAPPING_TABLE_Y + y, 1, 1, red_color);
      }
    }
  }

  // Header text (composite headers with settings-style color)
  vita2d_pgf_draw_text(font, MAPPING_TABLE_X + 25, MAPPING_TABLE_Y + 35,
                       UI_COLOR_TEXT_PRIMARY, 1.0f, "Vita");
  vita2d_pgf_draw_text(font, MAPPING_TABLE_X + header_split + 25,
                       MAPPING_TABLE_Y + 35, UI_COLOR_TEXT_PRIMARY, 1.0f,
                       "PS5");

  // Content rows start below headers (no extra column labels needed)
  int rows_start_y = MAPPING_TABLE_Y + TABLE_HEADER_HEIGHT + 20;

  // VSCode-style scrollbar if needed
  if (mapping_state.current_preset.mapping_count > MAX_VISIBLE_ROWS) {
    int scroll_bar_x = MAPPING_TABLE_X + MAPPING_TABLE_WIDTH -
                       SCROLLBAR_MARGIN - SCROLLBAR_WIDTH;
    int scroll_bar_height = MAPPING_TABLE_HEIGHT - TABLE_HEADER_HEIGHT - 60;
    int scroll_bar_y = rows_start_y;

    // Scroll track (rounded, subtle)
    ui_core_render_rounded_rectangle(
        scroll_bar_x, scroll_bar_y, SCROLLBAR_WIDTH, scroll_bar_height,
        SCROLLBAR_WIDTH / 2, RGBA8(45, 45, 50, 180));

    // Scroll thumb (rounded, VSCode-style)
    float thumb_ratio =
        (float)MAX_VISIBLE_ROWS / mapping_state.current_preset.mapping_count;
    int thumb_height = (int)(scroll_bar_height * thumb_ratio);
    // Minimum thumb height for usability
    if (thumb_height < 20) thumb_height = 20;

    float scroll_ratio =
        (float)mapping_state.scroll_offset /
        (mapping_state.current_preset.mapping_count - MAX_VISIBLE_ROWS);
    int thumb_y =
        scroll_bar_y + (int)((scroll_bar_height - thumb_height) * scroll_ratio);

    ui_core_render_rounded_rectangle(scroll_bar_x, thumb_y, SCROLLBAR_WIDTH,
                                     thumb_height, SCROLLBAR_WIDTH / 2,
                                     RGBA8(120, 120, 125, 200));
  }

  // Render visible rows (up to MAX_VISIBLE_ROWS)
  int visible_count = (mapping_state.current_preset.mapping_count -
                           mapping_state.scroll_offset <
                       MAX_VISIBLE_ROWS)
                          ? mapping_state.current_preset.mapping_count -
                                mapping_state.scroll_offset
                          : MAX_VISIBLE_ROWS;

  for (int i = 0; i < visible_count; i++) {
    int mapping_index = mapping_state.scroll_offset + i;
    ControllerMapping* mapping =
        &mapping_state.current_preset.mappings[mapping_index];

    // Row container (parent for this row's elements)
    int row_container_y = rows_start_y + (i * TABLE_ROW_HEIGHT);
    int row_container_x = MAPPING_TABLE_X;

    // Text colors based on state
    uint32_t text_color = UI_COLOR_TEXT_PRIMARY;
    uint32_t ps5_text_color = UI_COLOR_TEXT_PRIMARY;

    // Settings-style row highlighting and selection
    if (i == mapping_state.selected_row) {
      // Selection highlight like settings (full row width with padding)
      int highlight_x = row_container_x + TABLE_CONTENT_PADDING;
      int highlight_width = MAPPING_TABLE_WIDTH - (2 * TABLE_CONTENT_PADDING) -
                            SCROLLBAR_WIDTH - SCROLLBAR_MARGIN;
      int highlight_y = row_container_y + TABLE_ROW_PADDING;
      int highlight_height = TABLE_ROW_HEIGHT - (2 * TABLE_ROW_PADDING);

      vita2d_draw_rectangle(highlight_x, highlight_y, highlight_width,
                            highlight_height, RGBA8(255, 255, 255, 20));

      text_color = UI_COLOR_TEXT_PRIMARY;  // White for selected
      ps5_text_color = UI_COLOR_TEXT_PRIMARY;
    } else {
      // Default colors for unselected rows
      text_color = UI_COLOR_TEXT_SECONDARY;
      ps5_text_color = UI_COLOR_TEXT_PRIMARY;
    }

    // Rebinding mode highlighting (orange like settings)
    if (mapping_state.in_assignment_mode && i == mapping_state.selected_row) {
      ps5_text_color = RGBA8(255, 165, 0, 255);  // Orange for rebinding
    }

    // Text positioning (ensure text stays within table boundaries)
    int text_y = row_container_y + (TABLE_ROW_HEIGHT / 2) +
                 6;  // Vertically centered in row

    // Calculate maximum text width to prevent overflow
    int left_column_max_width =
        TABLE_COLUMN_2_X - TABLE_TEXT_OFFSET_X - 10;  // 10px gap
    int right_column_max_width = MAPPING_TABLE_WIDTH - TABLE_COLUMN_2_X -
                                 SCROLLBAR_WIDTH - SCROLLBAR_MARGIN - 10;

    // Vita button name (left side) - truncated if too long
    char vita_text[32];
    strncpy(vita_text, mapping->vita_button, sizeof(vita_text) - 1);
    vita_text[sizeof(vita_text) - 1] = '\0';
    vita2d_pgf_draw_text(font, row_container_x + TABLE_TEXT_OFFSET_X, text_y,
                         text_color, 0.9f, vita_text);

    // PS5 button name (properly positioned within right column bounds)
    int ps5_column_start = row_container_x + (MAPPING_TABLE_WIDTH / 2) +
                           15;  // Start of PS5 column with padding
    int ps5_column_max_width = (MAPPING_TABLE_WIDTH / 2) - SCROLLBAR_WIDTH -
                               SCROLLBAR_MARGIN - 25;  // Available width

    if (mapping_state.in_assignment_mode && i == mapping_state.selected_row) {
      vita2d_pgf_draw_text(font, ps5_column_start, text_y, ps5_text_color, 0.9f,
                           "Press new...");
    } else {
      char ps5_text[32];
      strncpy(ps5_text, mapping->ps5_button, sizeof(ps5_text) - 1);
      ps5_text[sizeof(ps5_text) - 1] = '\0';
      vita2d_pgf_draw_text(font, ps5_column_start, text_y, ps5_text_color, 0.9f,
                           ps5_text);
    }
  }

  // No borders for cleaner look matching mockup
}

static void render_button_highlights(void) {
  if (!mapping_state.in_assignment_mode ||
      mapping_state.rebinding_button == (VitaButtonId)-1)
    return;

  // Find the button being rebound and highlight it on the controller diagrams
  VitaButtonId target_button = mapping_state.rebinding_button;

  // Check front buttons
  for (int i = 0; i < mapping_state.front_button_count; i++) {
    if (mapping_state.front_buttons[i].button_id == target_button) {
      ButtonHitZone* zone = &mapping_state.front_buttons[i];
      // Animated highlight
      vita2d_draw_rectangle(zone->x - zone->width / 2 - 3,
                            zone->y - zone->height / 2 - 3, zone->width + 6,
                            zone->height + 6,
                            RGBA8(255, 165, 0, 150));  // Orange highlight
      break;
    }
  }

  // Check rear buttons too
  for (int i = 0; i < mapping_state.rear_button_count; i++) {
    if (mapping_state.rear_buttons[i].button_id == target_button) {
      ButtonHitZone* zone = &mapping_state.rear_buttons[i];
      vita2d_draw_rectangle(zone->x - zone->width / 2 - 3,
                            zone->y - zone->height / 2 - 3, zone->width + 6,
                            zone->height + 6,
                            RGBA8(255, 165, 0, 150));  // Orange highlight
      break;
    }
  }
}

static void controller_mapping_reset_to_default(int row_index) {
  if (row_index < 0 || row_index >= mapping_state.current_preset.mapping_count)
    return;

  ControllerMapping* mapping =
      &mapping_state.current_preset.mappings[row_index];
  VitaButtonId vita_button = mapping->vita_code;

  // Default 1:1 mappings
  PS5ButtonId default_ps5_button;
  switch (vita_button) {
    case VITA_BUTTON_CROSS:
      default_ps5_button = PS5_BUTTON_CROSS;
      break;
    case VITA_BUTTON_CIRCLE:
      default_ps5_button = PS5_BUTTON_CIRCLE;
      break;
    case VITA_BUTTON_SQUARE:
      default_ps5_button = PS5_BUTTON_SQUARE;
      break;
    case VITA_BUTTON_TRIANGLE:
      default_ps5_button = PS5_BUTTON_TRIANGLE;
      break;
    case VITA_BUTTON_DPAD_UP:
      default_ps5_button = PS5_BUTTON_DPAD_UP;
      break;
    case VITA_BUTTON_DPAD_DOWN:
      default_ps5_button = PS5_BUTTON_DPAD_DOWN;
      break;
    case VITA_BUTTON_DPAD_LEFT:
      default_ps5_button = PS5_BUTTON_DPAD_LEFT;
      break;
    case VITA_BUTTON_DPAD_RIGHT:
      default_ps5_button = PS5_BUTTON_DPAD_RIGHT;
      break;
    case VITA_BUTTON_L:
      default_ps5_button = PS5_BUTTON_L1;
      break;
    case VITA_BUTTON_R:
      default_ps5_button = PS5_BUTTON_R1;
      break;
    case VITA_BUTTON_START:
      default_ps5_button = PS5_BUTTON_OPTIONS;
      break;
    case VITA_BUTTON_SELECT:
      default_ps5_button = PS5_BUTTON_SHARE;
      break;
    case VITA_BUTTON_LTRIGGER:
      default_ps5_button = PS5_BUTTON_L2;
      break;
    case VITA_BUTTON_RTRIGGER:
      default_ps5_button = PS5_BUTTON_R2;
      break;
    default:
      return;  // Unknown button
  }

  // Reset to default
  mapping->ps5_code = default_ps5_button;
  strcpy(mapping->ps5_button, ps5_button_name(default_ps5_button));

  log_info("Reset %s to default: %s", mapping->vita_button,
           mapping->ps5_button);
}

static void controller_mapping_reset_all_to_default(void) {
  // Reset all mappings to their 1:1 defaults
  for (int i = 0; i < mapping_state.current_preset.mapping_count; i++) {
    controller_mapping_reset_to_default(i);
  }

  // Reset scroll position and selection
  mapping_state.scroll_offset = 0;
  mapping_state.selected_row = 0;
  mapping_state.in_assignment_mode = false;
  mapping_state.rebinding_button = -1;
}

static void init_default_mappings(void) {
  mapping_state.current_preset.mapping_count = 0;

  // Default 1:1 mappings
  struct {
    VitaButtonId vita;
    PS5ButtonId ps5;
  } defaults[] = {{VITA_BUTTON_CROSS, PS5_BUTTON_CROSS},
                  {VITA_BUTTON_CIRCLE, PS5_BUTTON_CIRCLE},
                  {VITA_BUTTON_SQUARE, PS5_BUTTON_SQUARE},
                  {VITA_BUTTON_TRIANGLE, PS5_BUTTON_TRIANGLE},
                  {VITA_BUTTON_DPAD_UP, PS5_BUTTON_DPAD_UP},
                  {VITA_BUTTON_DPAD_DOWN, PS5_BUTTON_DPAD_DOWN},
                  {VITA_BUTTON_DPAD_LEFT, PS5_BUTTON_DPAD_LEFT},
                  {VITA_BUTTON_DPAD_RIGHT, PS5_BUTTON_DPAD_RIGHT},
                  {VITA_BUTTON_L, PS5_BUTTON_L1},
                  {VITA_BUTTON_R, PS5_BUTTON_R1},
                  {VITA_BUTTON_START, PS5_BUTTON_OPTIONS},
                  {VITA_BUTTON_SELECT, PS5_BUTTON_SHARE},
                  {VITA_BUTTON_LTRIGGER, PS5_BUTTON_L2},
                  {VITA_BUTTON_RTRIGGER, PS5_BUTTON_R2}};

  for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
    ControllerMapping* mapping =
        &mapping_state.current_preset
             .mappings[mapping_state.current_preset.mapping_count++];
    mapping->vita_code = defaults[i].vita;
    mapping->ps5_code = defaults[i].ps5;
    strcpy(mapping->vita_button, vita_button_name(defaults[i].vita));
    strcpy(mapping->ps5_button, ps5_button_name(defaults[i].ps5));
  }
}

const char* vita_button_name(VitaButtonId button) {
  switch (button) {
    case VITA_BUTTON_CROSS:
      return "Cross";
    case VITA_BUTTON_CIRCLE:
      return "Circle";
    case VITA_BUTTON_SQUARE:
      return "Square";
    case VITA_BUTTON_TRIANGLE:
      return "Triangle";
    case VITA_BUTTON_DPAD_UP:
      return "D-Pad Up";
    case VITA_BUTTON_DPAD_DOWN:
      return "D-Pad Down";
    case VITA_BUTTON_DPAD_LEFT:
      return "D-Pad Left";
    case VITA_BUTTON_DPAD_RIGHT:
      return "D-Pad Right";
    case VITA_BUTTON_L:
      return "L Button";
    case VITA_BUTTON_R:
      return "R Button";
    case VITA_BUTTON_START:
      return "Start";
    case VITA_BUTTON_SELECT:
      return "Select";
    case VITA_BUTTON_LTRIGGER:
      return "L2 (Rear)";
    case VITA_BUTTON_RTRIGGER:
      return "R2 (Rear)";
    case VITA_BUTTON_LSTICK:
      return "L3 (Stick)";
    case VITA_BUTTON_RSTICK:
      return "R3 (Stick)";
    default:
      return "Unknown";
  }
}

const char* ps5_button_name(PS5ButtonId button) {
  switch (button) {
    case PS5_BUTTON_CROSS:
      return "Cross";
    case PS5_BUTTON_CIRCLE:
      return "Circle";
    case PS5_BUTTON_SQUARE:
      return "Square";
    case PS5_BUTTON_TRIANGLE:
      return "Triangle";
    case PS5_BUTTON_DPAD_UP:
      return "D-Pad Up";
    case PS5_BUTTON_DPAD_DOWN:
      return "D-Pad Down";
    case PS5_BUTTON_DPAD_LEFT:
      return "D-Pad Left";
    case PS5_BUTTON_DPAD_RIGHT:
      return "D-Pad Right";
    case PS5_BUTTON_L1:
      return "L1";
    case PS5_BUTTON_R1:
      return "R1";
    case PS5_BUTTON_L2:
      return "L2";
    case PS5_BUTTON_R2:
      return "R2";
    case PS5_BUTTON_L3:
      return "L3";
    case PS5_BUTTON_R3:
      return "R3";
    case PS5_BUTTON_OPTIONS:
      return "Options";
    case PS5_BUTTON_SHARE:
      return "Share";
    case PS5_BUTTON_PS:
      return "PS";
    case PS5_BUTTON_TOUCHPAD:
      return "Touchpad";
    default:
      return "Unknown";
  }
}