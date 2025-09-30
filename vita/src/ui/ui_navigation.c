#include "ui_navigation.h"

#include <math.h>
#include <psp2/touch.h>
#include <stdlib.h>

#include "../utils/logger.h"
#include "ui_core.h"
#include "vita2d_ui.h"

// Navigation state
static Particle particles[PARTICLE_COUNT];
static WaveNavigation wave_nav = {0};
static float animation_time = 0.0f;

// Wave navigation items (bottom to top as per specification)
static WaveNavItem wave_nav_items[] = {
    {"Profile", NULL, 0, true, 0.0f, UI_COLOR_PRIMARY_BLUE},
    {"Controller", NULL, 1, true, 0.0f, UI_COLOR_STATUS_AVAILABLE},
    {"Settings", NULL, 2, true, 0.0f, UI_COLOR_STATUS_CONNECTING},
    {"Play", NULL, 3, true, 0.0f, UI_COLOR_ACCENT_PURPLE}};
static const int wave_nav_count = sizeof(wave_nav_items) / sizeof(WaveNavItem);

// Forward declarations
static void draw_scaled_icon(vita2d_texture* texture, int x, int y);
static void draw_play_icon(vita2d_texture* texture, int x, int y);
static vita2d_texture* get_particle_texture(int symbol_type);

void ui_navigation_init(void) {
  log_info("Initializing navigation system...");

  UIAssets* assets = ui_core_get_assets();

  // Set wave navigation icons
  wave_nav_items[0].icon = assets->profile_icon;
  wave_nav_items[1].icon = assets->controller_icon;
  wave_nav_items[2].icon = assets->settings_icon;
  wave_nav_items[3].icon = assets->play_icon;

  // Initialize particle system
  for (int i = 0; i < PARTICLE_COUNT; i++) {
    particles[i].x = (float)(rand() % 960);
    particles[i].y = (float)(rand() % 544);
    particles[i].vx = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f;
    particles[i].vy = ((float)(rand() % 100) / 100.0f + 0.5f) * 0.8f;
    particles[i].scale = 0.15f + ((float)(rand() % 100) / 100.0f) * 0.25f;
    particles[i].rotation = (float)(rand() % 360);
    particles[i].rotation_speed =
        ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;
    particles[i].symbol_type = rand() % 4;

    // Assign colors based on symbol type
    switch (particles[i].symbol_type) {
      case 0:
        particles[i].color = PARTICLE_COLOR_RED;
        break;
      case 1:
        particles[i].color = PARTICLE_COLOR_BLUE;
        break;
      case 2:
        particles[i].color = PARTICLE_COLOR_GREEN;
        break;
      case 3:
        particles[i].color = PARTICLE_COLOR_ORANGE;
        break;
    }
    particles[i].active = true;
  }

  // Initialize wave navigation
  wave_nav.wave_offset = 0.0f;
  wave_nav.wave_direction = 1.0f;
  wave_nav.selected_icon = 3;  // Start with Play (main dashboard)
  wave_nav.selection_animation = 1.0f;

  log_info("Navigation system initialized");
}

void ui_navigation_cleanup(void) {
  // Nothing to cleanup for navigation
}

void ui_navigation_update(float delta_time) {
  animation_time += delta_time;
  if (animation_time > 2.0f * M_PI) animation_time -= 2.0f * M_PI;

  // Update particles first
  ui_navigation_update_particles();

  // Update wave animation
  wave_nav.wave_offset +=
      WAVE_AMPLITUDE * wave_nav.wave_direction * delta_time * 60.0f;

  // Reverse direction at extremes
  if (wave_nav.wave_offset > WAVE_AMPLITUDE) {
    wave_nav.wave_direction = -1.0f;
  } else if (wave_nav.wave_offset < -WAVE_AMPLITUDE) {
    wave_nav.wave_direction = 1.0f;
  }

  // Update icon positions with wave offset
  for (int i = 0; i < wave_nav_count; i++) {
    wave_nav_items[i].y_offset =
        sinf(animation_time + i * 0.5f) * WAVE_AMPLITUDE;
  }

  // Update selection animation
  if (wave_nav.selected_icon >= 0 && wave_nav.selected_icon < wave_nav_count) {
    wave_nav.selection_animation += delta_time * 8.0f;
    if (wave_nav.selection_animation > 1.0f)
      wave_nav.selection_animation = 1.0f;
  }
}

void ui_navigation_render(void) {
  UIAssets* assets = ui_core_get_assets();

  // Draw waves
  if (assets->wave_bottom) {
    vita2d_draw_texture(assets->wave_bottom, 0, 0);
  }

  if (assets->wave_top) {
    vita2d_draw_texture(assets->wave_top, 0, 0);
  }

  // Positioning - adjusted to show Play button at top
  int icon_x = 20;
  int icon_spacing = 60;
  int icon_start_y = 180;  // Moved up to accommodate Play button at top

  // Draw all navigation icons from top to bottom (matching array order)
  // Play (top) - index 3 - use special rotated white triangle
  draw_play_icon(assets->play_icon, icon_x, icon_start_y);
  // Settings - index 2
  draw_scaled_icon(assets->settings_icon, icon_x, icon_start_y + icon_spacing);
  // Controller - index 1
  draw_scaled_icon(assets->controller_icon, icon_x,
                   icon_start_y + icon_spacing * 2);
  // Profile (bottom) - index 0 - Use real profile image if available
  if (assets->has_user_profile_image && assets->user_profile_image) {
    draw_scaled_icon(assets->user_profile_image, icon_x,
                     icon_start_y + icon_spacing * 3);
  } else {
    draw_scaled_icon(assets->profile_icon, icon_x,
                     icon_start_y + icon_spacing * 3);
  }
}

void ui_navigation_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  bool up_pressed =
      (pad->buttons & SCE_CTRL_UP) && !(prev_pad->buttons & SCE_CTRL_UP);
  bool down_pressed =
      (pad->buttons & SCE_CTRL_DOWN) && !(prev_pad->buttons & SCE_CTRL_DOWN);
  bool x_pressed =
      (pad->buttons & SCE_CTRL_CROSS) && !(prev_pad->buttons & SCE_CTRL_CROSS);

  // Navigate wave icons
  if (up_pressed && wave_nav.selected_icon < wave_nav_count - 1) {
    wave_nav.selected_icon++;
    wave_nav.selection_animation = 0.0f;
  } else if (down_pressed && wave_nav.selected_icon > 0) {
    wave_nav.selected_icon--;
    wave_nav.selection_animation = 0.0f;
  }

  // Handle selection - but only if not already in a view that needs X button
  // interaction
  UIState current_state = ui_core_get_state();
  if (x_pressed && current_state != UI_STATE_SETTINGS &&
      current_state != UI_STATE_PSN_LOGIN &&
      current_state != UI_STATE_CONTROLLER_MAPPING) {
    switch (wave_nav.selected_icon) {
      case 0:  // Profile
        ui_core_set_state(UI_STATE_PROFILE);
        log_info("Opening profile view");
        break;
      case 1:  // Controller
        ui_core_set_state(UI_STATE_CONTROLLER_MAPPING);
        log_info("Opening controller mapping");
        break;
      case 2:  // Settings
        ui_core_set_state(UI_STATE_SETTINGS);
        log_info("Opening settings");
        break;
      case 3:  // Play
        ui_core_set_state(UI_STATE_MAIN_DASHBOARD);
        log_info("Opening main dashboard");
        break;
    }
  }
}

void ui_navigation_handle_touch(void) {
  SceTouchData touch_data;

  // Check front touch panel
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch_data, 1);

  if (touch_data.reportNum > 0) {
    // Convert touch coordinates to screen coordinates
    float touch_x = (float)touch_data.report[0].x * 960.0f / 1920.0f;
    float touch_y = (float)touch_data.report[0].y * 544.0f / 1088.0f;

    // Handle wave navigation touch
    if (touch_x <= WAVE_NAV_WIDTH) {
      int icon_x = 20;
      int icon_spacing = 60;
      int icon_start_y = 180;  // Match the render positioning
      int icon_size = 36;

      for (int i = 0; i < wave_nav_count; i++) {
        // Icons are drawn in reverse order (3,2,1,0 from top to bottom)
        float icon_y = icon_start_y + ((3 - i) * icon_spacing);

        if (touch_x >= icon_x - 10 && touch_x <= icon_x + icon_size + 10 &&
            touch_y >= icon_y - 10 && touch_y <= icon_y + icon_size + 10) {
          wave_nav.selected_icon = i;
          wave_nav.selection_animation = 0.0f;

          // Switch view based on selected icon
          switch (i) {
            case 0:
              ui_core_set_state(UI_STATE_PROFILE);
              break;
            case 1:
              ui_core_set_state(UI_STATE_CONTROLLER_MAPPING);
              break;
            case 2:
              ui_core_set_state(UI_STATE_SETTINGS);
              break;
            case 3:
              ui_core_set_state(UI_STATE_MAIN_DASHBOARD);
              break;
          }
          log_info("Touch navigation to: %s", wave_nav_items[i].name);
          break;
        }
      }
    }
  }
}

int ui_navigation_get_selected_icon(void) { return wave_nav.selected_icon; }

void ui_navigation_set_selected_icon(int icon) {
  if (icon >= 0 && icon < wave_nav_count) {
    wave_nav.selected_icon = icon;
    wave_nav.selection_animation = 0.0f;
  }
}

void ui_navigation_update_particles(void) {
  for (int i = 0; i < PARTICLE_COUNT; i++) {
    if (!particles[i].active) continue;

    // Update position
    particles[i].x += particles[i].vx;
    particles[i].y += particles[i].vy;

    // Update rotation
    particles[i].rotation += particles[i].rotation_speed;
    if (particles[i].rotation > 360.0f) particles[i].rotation -= 360.0f;
    if (particles[i].rotation < 0.0f) particles[i].rotation += 360.0f;

    // Respawn particles that go off screen
    if (particles[i].y > 544 + 50 || particles[i].x < -50 ||
        particles[i].x > 960 + 50) {
      particles[i].x = (float)(rand() % 960);
      particles[i].y = -50.0f;
      particles[i].vx = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f;
      particles[i].vy = ((float)(rand() % 100) / 100.0f + 0.5f) * 0.8f;
      particles[i].scale = 0.15f + ((float)(rand() % 100) / 100.0f) * 0.25f;
      particles[i].rotation_speed =
          ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;
    }
  }
}

void ui_navigation_render_particles(void) {
  UIAssets* assets = ui_core_get_assets();

  for (int i = 0; i < PARTICLE_COUNT; i++) {
    if (!particles[i].active) continue;

    vita2d_texture* texture = get_particle_texture(particles[i].symbol_type);
    if (!texture) continue;

    // Calculate position with rotation
    float center_x = particles[i].x;
    float center_y = particles[i].y;

    // Draw particle with tint color and scaling
    vita2d_draw_texture_tint_scale_rotate(
        texture, center_x, center_y, particles[i].scale, particles[i].scale,
        particles[i].rotation, particles[i].color);
  }
}

// Private functions
static void draw_scaled_icon(vita2d_texture* texture, int x, int y) {
  if (!texture) return;

  int original_width = vita2d_texture_get_width(texture);
  if (original_width == 0) return;

  float scale = 32.0f / (float)original_width;  // ICON_MAX_WIDTH = 32
  vita2d_draw_texture_scale(texture, x, y, scale, scale);
}

static void draw_play_icon(vita2d_texture* texture, int x, int y) {
  // Draw a filled white right-pointing triangle (play symbol)
  // Icon size is 32x32 to match other icons
  const int icon_size = 32;
  const uint32_t white = RGBA8(255, 255, 255, 255);

  // Triangle dimensions (right-pointing)
  const int triangle_width = 20;   // Width of triangle base
  const int triangle_height = 24;  // Height of triangle

  // Center the triangle within the 32x32 icon space
  int start_x = x + (icon_size - triangle_width) / 2;
  int start_y = y + (icon_size - triangle_height) / 2;

  // Draw filled right-pointing triangle using horizontal lines
  // Triangle points: left-top, left-bottom, right-center
  for (int row = 0; row < triangle_height; row++) {
    // Calculate the width of this row based on triangle shape
    int half_height = triangle_height / 2;
    int distance_from_center = abs(row - half_height);
    int row_width =
        triangle_width - (distance_from_center * triangle_width / half_height);

    if (row_width > 0) {
      vita2d_draw_rectangle(start_x, start_y + row, row_width, 1, white);
    }
  }
}

static vita2d_texture* get_particle_texture(int symbol_type) {
  UIAssets* assets = ui_core_get_assets();

  switch (symbol_type) {
    case 0:
      return assets->symbol_triangle;
    case 1:
      return assets->symbol_circle;
    case 2:
      return assets->symbol_ex;
    case 3:
      return assets->symbol_square;
    default:
      return assets->symbol_circle;
  }
}