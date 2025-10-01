// Very very simple homegrown immediate mode GUI
#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/message_dialog.h>
#include <psp2/registrymgr.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <chiaki/base64.h>

#include "context.h"
#include "host.h"
#include "ui.h"
#include "util.h"

// Legacy colors (kept for compatibility)
#define COLOR_WHITE RGBA8(255, 255, 255, 255)
#define COLOR_GRAY50 RGBA8(129, 129, 129, 255)
#define COLOR_BLACK RGBA8(0, 0, 0, 255)
#define COLOR_ACTIVE RGBA8(255, 170, 238, 255)
#define COLOR_TILE_BG RGBA8(51, 51, 51, 255)
#define COLOR_BANNER RGBA8(22, 45, 80, 255)

// Modern VitaRPS5 colors (ABGR format for vita2d)
#define UI_COLOR_PRIMARY_BLUE 0xFFFF9034     // PlayStation Blue #3490FF
#define UI_COLOR_BACKGROUND 0xFF1A1614       // Animated charcoal gradient base
#define UI_COLOR_CARD_BG 0xFF37322D          // Dark charcoal (45,50,55)
#define UI_COLOR_TEXT_PRIMARY 0xFFFFFFFF     // White
#define UI_COLOR_TEXT_SECONDARY 0xFFB4B4B4   // Light Gray
#define UI_COLOR_TEXT_TERTIARY 0xFFA0A0A0    // Medium Gray
#define UI_COLOR_STATUS_AVAILABLE 0xFF50AF4C    // Success Green #4CAF50
#define UI_COLOR_STATUS_CONNECTING 0xFF0098FF   // Warning Orange #FF9800
#define UI_COLOR_STATUS_UNAVAILABLE 0xFF3643F4  // Error Red #F44336
#define UI_COLOR_ACCENT_PURPLE 0xFFB0279C       // Accent Purple #9C27B0
#define UI_COLOR_SHADOW 0x3C000000           // Semi-transparent black for shadows

// Particle colors (ABGR with alpha for transparency - 80% opacity = 0xCC)
#define PARTICLE_COLOR_RED    0xCCFF5555  // 80% transparent red
#define PARTICLE_COLOR_GREEN  0xCC55FF55  // 80% transparent green
#define PARTICLE_COLOR_BLUE   0xCC5555FF  // 80% transparent blue
#define PARTICLE_COLOR_ORANGE 0xCC55AAFF  // 80% transparent orange

#define VITA_WIDTH 960
#define VITA_HEIGHT 544

// Font Size Hierarchy (Phase 2 Standards)
#define FONT_SIZE_HEADER 24      // Screen titles, main headers
#define FONT_SIZE_SUBHEADER 18   // Section titles, tab labels
#define FONT_SIZE_BODY 16        // Primary content text
#define FONT_SIZE_SMALL 16       // Secondary text, hints (MINIMUM - 16pt)

// VitaRPS5 UI Layout Constants
#define WAVE_NAV_WIDTH 104  // 20% thinner than original 130px
#define CONTENT_AREA_X WAVE_NAV_WIDTH
#define CONTENT_AREA_WIDTH (VITA_WIDTH - WAVE_NAV_WIDTH)
#define PARTICLE_COUNT 12

// Legacy layout (will be phased out)
#define HEADER_BAR_X 136
#define HEADER_BAR_Y 43
#define HEADER_BAR_H 26
#define HEADER_BAR_W 774
#define HOST_SLOTS_X HEADER_BAR_X - 86
#define HOST_SLOTS_Y HEADER_BAR_Y + HEADER_BAR_H + 43
#define HOST_SLOT_W 400
#define HOST_SLOT_H 190

// Particle structure for background animation
typedef struct {
  float x, y;
  float vx, vy;
  float scale;
  float rotation;
  float rotation_speed;
  int symbol_type;  // 0=triangle, 1=circle, 2=x, 3=square
  uint32_t color;
  bool active;
} Particle;

#define TEXTURE_PATH "app0:/assets/"
#define IMG_PS4_PATH TEXTURE_PATH "ps4.png"
#define IMG_PS4_OFF_PATH TEXTURE_PATH "ps4_off.png"
#define IMG_PS4_REST_PATH TEXTURE_PATH "ps4_rest.png"
#define IMG_PS5_PATH TEXTURE_PATH "ps5.png"
#define IMG_PS5_OFF_PATH TEXTURE_PATH "ps5_off.png"
#define IMG_PS5_REST_PATH TEXTURE_PATH "ps5_rest.png"
#define IMG_DISCOVERY_HOST TEXTURE_PATH "discovered_host.png"

vita2d_font* font;
vita2d_font* font_mono;
vita2d_texture *img_ps4, *img_ps4_off, *img_ps4_rest,
    *img_ps5, *img_ps5_off, *img_ps5_rest, *img_discovery_host;

// VitaRPS5 UI textures
vita2d_texture *symbol_triangle, *symbol_circle, *symbol_ex, *symbol_square;
vita2d_texture *wave_top, *wave_bottom;
vita2d_texture *ellipse_green, *ellipse_yellow, *ellipse_red;
vita2d_texture *button_add_new;
vita2d_texture *icon_play, *icon_settings, *icon_controller, *icon_profile;
vita2d_texture *background_gradient, *vita_rps5_logo;
vita2d_texture *vita_front, *ps5_logo;

// Particle system state
static Particle particles[PARTICLE_COUNT];
static bool particles_initialized = false;

// Wave navigation state
#define WAVE_NAV_ICON_SIZE 48
#define WAVE_NAV_ICON_X 33  // Adjusted for thinner bar (centered at 104/2 = 52, minus icon_size/2)
#define WAVE_NAV_ICON_START_Y 180
#define WAVE_NAV_ICON_SPACING 80  // Increased from 60 for better separation

static int selected_nav_icon = 0;  // 0=Play, 1=Settings, 2=Controller, 3=Profile
static float wave_animation_time = 0.0f;

// Console card system
#define CONSOLE_CARD_WIDTH 200  // Half width for more compact cards
#define CONSOLE_CARD_HEIGHT 200
#define CONSOLE_CARD_SPACING 120
#define CONSOLE_CARD_START_Y 150

typedef struct {
  char name[32];           // "PS5 - 024"
  char ip_address[16];     // "192.168.1.100"
  int status;              // 0=Available, 1=Unavailable, 2=Connecting
  int state;               // 0=Unknown, 1=Ready, 2=Standby
  bool is_registered;      // Has valid credentials
  bool is_discovered;      // From network discovery
  VitaChiakiHost* host;    // Original vitaki host reference
} ConsoleCardInfo;

static int selected_console_index = 0;

// Console card cache to prevent flickering during discovery updates
typedef struct {
  ConsoleCardInfo cards[MAX_NUM_HOSTS];
  int num_cards;
  uint64_t last_update_time;  // Microseconds since epoch
} ConsoleCardCache;

static ConsoleCardCache card_cache = {0};
#define CARD_CACHE_UPDATE_INTERVAL_US (10 * 1000000)  // 10 seconds in microseconds

// Wave navigation sidebar uses simple colored bar (no animation)

// PIN entry state for VitaRPS5-style registration
typedef struct {
  uint32_t pin_digits[8];  // Each digit 0-9, or 10 for empty
  int current_digit;        // Which digit cursor is on (0-7)
  bool pin_complete;        // All 8 digits entered
  uint32_t complete_pin;    // Final 8-digit number
} PinEntryState;

static PinEntryState pin_entry_state = {0};
static bool show_cursor = false;
static uint32_t cursor_blink_timer = 0;
static bool pin_entry_initialized = false;

// Focus system for D-pad navigation
typedef enum {
  FOCUS_NAV_BAR = 0,      // Wave navigation sidebar
  FOCUS_CONSOLE_CARDS = 1  // Console cards area (includes discovery card when empty)
} FocusArea;

static FocusArea current_focus = FOCUS_CONSOLE_CARDS;
static int last_console_selection = 0;  // Remember last selected console when moving away

#define MAX_TOOLTIP_CHARS 200
char active_tile_tooltip_msg[MAX_TOOLTIP_CHARS] = {0};

/// Types of actions that can be performed on hosts
typedef enum ui_host_action_t {
  UI_HOST_ACTION_NONE = 0,
  UI_HOST_ACTION_WAKEUP,  // Only for at-rest hosts
  UI_HOST_ACTION_STREAM,  // Only for online hosts
  UI_HOST_ACTION_DELETE,  // Only for manually added hosts
  UI_HOST_ACTION_EDIT,    // Only for registered/manually added hosts
  UI_HOST_ACTION_REGISTER,    // Only for discovered hosts
} UIHostAction;

/// Types of screens that can be rendered
typedef enum ui_screen_type_t {
  UI_SCREEN_TYPE_MAIN = 0,
  UI_SCREEN_TYPE_REGISTER,
  UI_SCREEN_TYPE_REGISTER_HOST,
  UI_SCREEN_TYPE_STREAM,
  UI_SCREEN_TYPE_WAKING,         // Waking up console screen
  UI_SCREEN_TYPE_SETTINGS,
  UI_SCREEN_TYPE_MESSAGES,
  UI_SCREEN_TYPE_PROFILE,        // Phase 2: Profile & Registration screen
  UI_SCREEN_TYPE_CONTROLLER,     // Phase 2: Controller Configuration screen
} UIScreenType;

// Initialize Yes and No button from settings (will be updated in init_ui)
int SCE_CTRL_CONFIRM = SCE_CTRL_CROSS;
int SCE_CTRL_CANCEL  = SCE_CTRL_CIRCLE;
char* confirm_btn_str = "Cross";
char* cancel_btn_str  = "Circle";

/// Check if a button has been newly pressed
bool btn_pressed(SceCtrlButtons btn) {
  return (context.ui_state.button_state & btn) &&
         !(context.ui_state.old_button_state & btn);
}

// Modern rendering helpers (extracted from VitaRPS5)

/// Draw a circle at the given position with the given radius and color
static void draw_circle(int cx, int cy, int radius, uint32_t color) {
  // Bounds checking
  if (cx < -100 || cx > 1060 || cy < -100 || cy > 644 || radius <= 0 || radius > 1000) {
    return;
  }

  // Fix problematic color values
  if (color == 0xFFFFFFFF) {
    color = RGBA8(254, 254, 254, 255);
  }

  // Ensure alpha channel is set
  if ((color & 0xFF000000) == 0) {
    color |= 0xFF000000;
  }

  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        int draw_x = cx + x;
        int draw_y = cy + y;

        if (draw_x < 0 || draw_x >= 960 || draw_y < 0 || draw_y >= 544) {
          continue;
        }

        vita2d_draw_rectangle(draw_x, draw_y, 1, 1, color);
      }
    }
  }
}

/// Draw a rounded rectangle with the given parameters
static void draw_rounded_rectangle(int x, int y, int width, int height, int radius, uint32_t color) {
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
        vita2d_draw_rectangle(x + width - radius + i - 1, y + radius - j, 1, 1, color);
        // Bottom-left corner
        vita2d_draw_rectangle(x + radius - i, y + height - radius + j - 1, 1, 1, color);
        // Bottom-right corner
        vita2d_draw_rectangle(x + width - radius + i - 1, y + height - radius + j - 1, 1, 1, color);
      }
    }
  }
}

/// Draw a card with a shadow effect
static void draw_card_with_shadow(int x, int y, int width, int height, int radius, uint32_t color) {
  // Render shadow first (offset by a few pixels)
  int shadow_offset = 4;
  uint32_t shadow_color = UI_COLOR_SHADOW;
  draw_rounded_rectangle(x + shadow_offset, y + shadow_offset, width, height, radius, shadow_color);

  // Render the actual card on top
  draw_rounded_rectangle(x, y, width, height, radius, color);
}

// ============================================================================
// PHASE 2: REUSABLE UI COMPONENTS
// ============================================================================

/// Draw a toggle switch (iOS-style)
/// @param x X position
/// @param y Y position
/// @param width Total width of switch
/// @param height Total height of switch
/// @param state true = ON, false = OFF
/// @param selected true if this control is currently selected
static void draw_toggle_switch(int x, int y, int width, int height, bool state, bool selected) {
  uint32_t track_color = state ? RGBA8(0x00, 0x70, 0xCC, 200) : RGBA8(0x60, 0x60, 0x60, 200);
  uint32_t knob_color = RGBA8(0xFF, 0xFF, 0xFF, 255);

  // Selection highlight
  if (selected) {
    draw_rounded_rectangle(x - 2, y - 2, width + 4, height + 4, height/2 + 1, UI_COLOR_PRIMARY_BLUE);
  }

  // Track (background)
  draw_rounded_rectangle(x, y, width, height, height/2, track_color);

  // Knob (circular button)
  int knob_radius = (height - 4) / 2;
  int knob_x = state ? (x + width - knob_radius - 2) : (x + knob_radius + 2);
  int knob_y = y + height/2;

  draw_circle(knob_x, knob_y, knob_radius, knob_color);
}

/// Draw a dropdown control
/// @param x X position
/// @param y Y position
/// @param width Width of dropdown
/// @param height Height of dropdown
/// @param label Label text (left side)
/// @param value Current value text (right side)
/// @param expanded true if dropdown is expanded
/// @param selected true if this control is currently selected
static void draw_dropdown(int x, int y, int width, int height, const char* label,
                          const char* value, bool expanded, bool selected) {
  uint32_t bg_color = selected ? RGBA8(0x40, 0x40, 0x50, 255) : RGBA8(0x30, 0x30, 0x38, 255);

  // Background
  draw_rounded_rectangle(x, y, width, height, 8, bg_color);

  // Selection highlight
  if (selected && !expanded) {
    draw_rounded_rectangle(x - 2, y - 2, width + 4, height + 4, 10, UI_COLOR_PRIMARY_BLUE);
    draw_rounded_rectangle(x, y, width, height, 8, bg_color);
  }

  // Label text (left)
  vita2d_font_draw_text(font, x + 15, y + height/2 + 6, UI_COLOR_TEXT_PRIMARY, 16, label);

  // Value text (right)
  int value_width = vita2d_font_text_width(font, 16, value);
  vita2d_font_draw_text(font, x + width - value_width - 30, y + height/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, 16, value);

  // Down arrow indicator (simple triangle)
  int arrow_x = x + width - 18;
  int arrow_y = y + height/2;
  int arrow_size = 6;

  // Draw downward pointing triangle
  for (int i = 0; i < arrow_size; i++) {
    vita2d_draw_rectangle(arrow_x - i, arrow_y + i, 1 + i*2, 1, UI_COLOR_TEXT_SECONDARY);
  }
}

/// Draw a tab bar with colored sections
/// @param x X position
/// @param y Y position
/// @param width Total width
/// @param height Height of tab bar
/// @param tabs Array of tab label strings
/// @param colors Array of colors for each tab
/// @param num_tabs Number of tabs
/// @param selected Index of currently selected tab
static void draw_tab_bar(int x, int y, int width, int height,
                         const char* tabs[], uint32_t colors[], int num_tabs, int selected) {
  int tab_width = width / num_tabs;

  for (int i = 0; i < num_tabs; i++) {
    int tab_x = x + (i * tab_width);

    // Tab background - flat color, no dimming
    draw_rounded_rectangle(tab_x, y, tab_width - 4, height, 8, colors[i]);

    // Tab text (centered) - use subheader font size
    int text_width = vita2d_font_text_width(font, FONT_SIZE_SUBHEADER, tabs[i]);
    int text_x = tab_x + (tab_width - text_width) / 2;
    int text_y = y + height/2 + 6;

    vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, tabs[i]);

    // Selection indicator (bottom bar) - only visual difference
    if (i == selected) {
      vita2d_draw_rectangle(tab_x + 2, y + height - 3, tab_width - 8, 3, UI_COLOR_PRIMARY_BLUE);
    }
  }
}

/// Status dot colors
typedef enum {
  STATUS_ACTIVE = 0,    // Green
  STATUS_STANDBY = 1,   // Yellow
  STATUS_ERROR = 2      // Red
} StatusType;

/// Draw a status indicator dot
/// @param x X position (center)
/// @param y Y position (center)
/// @param radius Radius of dot
/// @param status Status type (determines color)
static void draw_status_dot(int x, int y, int radius, StatusType status) {
  uint32_t color;
  switch (status) {
    case STATUS_ACTIVE:
      color = RGBA8(0x2D, 0x8A, 0x3E, 255); // Green
      break;
    case STATUS_STANDBY:
      color = RGBA8(0xD9, 0x77, 0x06, 255); // Orange/Yellow
      break;
    case STATUS_ERROR:
      color = RGBA8(0xDC, 0x26, 0x26, 255); // Red
      break;
    default:
      color = RGBA8(0x80, 0x80, 0x80, 255); // Gray
  }

  draw_circle(x, y, radius, color);
}

// Particle system functions

/// Initialize particle system with random positions and velocities
void init_particles() {
  if (particles_initialized) return;

  srand((unsigned int)sceKernelGetProcessTimeWide());

  for (int i = 0; i < PARTICLE_COUNT; i++) {
    particles[i].x = (float)(rand() % VITA_WIDTH);
    particles[i].y = -(float)(rand() % 200);  // Start above screen (0 to -200)
    particles[i].vx = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f;  // Slight horizontal drift
    particles[i].vy = ((float)(rand() % 100) / 100.0f + 0.3f) * 1.2f;  // Downward (positive Y, gravity)
    particles[i].scale = 0.30f + ((float)(rand() % 100) / 100.0f) * 0.50f;  // 2x bigger: 0.30 to 0.80
    particles[i].rotation = (float)(rand() % 360);
    particles[i].rotation_speed = ((float)(rand() % 100) / 100.0f - 0.5f) * 1.0f;  // Half speed: -0.5 to +0.5
    particles[i].symbol_type = rand() % 4;

    // Assign color based on symbol
    switch (particles[i].symbol_type) {
      case 0: particles[i].color = PARTICLE_COLOR_RED; break;
      case 1: particles[i].color = PARTICLE_COLOR_BLUE; break;
      case 2: particles[i].color = PARTICLE_COLOR_GREEN; break;
      case 3: particles[i].color = PARTICLE_COLOR_ORANGE; break;
    }
    particles[i].active = true;
  }

  particles_initialized = true;
}

/// Update particle positions and rotation
void update_particles() {
  if (!particles_initialized) return;

  for (int i = 0; i < PARTICLE_COUNT; i++) {
    if (!particles[i].active) continue;

    // Update position
    particles[i].x += particles[i].vx;
    particles[i].y += particles[i].vy;
    particles[i].rotation += particles[i].rotation_speed;

    // Wrap around screen edges (respawn at top when falling off bottom)
    if (particles[i].y > VITA_HEIGHT + 50) {
      particles[i].y = -(float)(rand() % 100);  // Respawn at top
      particles[i].x = (float)(rand() % VITA_WIDTH);
    }
    if (particles[i].x < -50) particles[i].x = VITA_WIDTH + 50;
    if (particles[i].x > VITA_WIDTH + 50) particles[i].x = -50;
  }
}

// Forward declarations
void draw_play_icon(int center_x, int center_y, int size);

/// Render all active particles
void render_particles() {
  if (!particles_initialized) return;

  vita2d_texture* symbol_textures[4] = {
    symbol_triangle, symbol_circle, symbol_ex, symbol_square
  };

  for (int i = 0; i < PARTICLE_COUNT; i++) {
    if (!particles[i].active) continue;

    vita2d_texture* tex = symbol_textures[particles[i].symbol_type];
    if (!tex) continue;

    // Draw with scale and rotation
    vita2d_draw_texture_scale_rotate(tex,
      particles[i].x, particles[i].y,
      particles[i].scale, particles[i].scale,
      particles[i].rotation);

    // Note: Color tinting would require custom shader
    // For now particles use texture colors
  }
}

/// Render VitaRPS5 navigation sidebar with simple colored bar
void render_wave_navigation() {
  // Draw simple teal/cyan colored bar for navigation sidebar
  // Color matches the original wave texture (teal/cyan)
  uint32_t nav_bar_color = RGBA8(78, 133, 139, 255);  // Teal color from wave texture
  vita2d_draw_rectangle(0, 0, WAVE_NAV_WIDTH, VITA_HEIGHT, nav_bar_color);

  // Navigation icons array
  vita2d_texture* nav_icons[4] = {
    icon_play, icon_settings, icon_controller, icon_profile
  };

  // Draw navigation icons (static, no animation)
  // TODO: Add wave background animation in future update

  for (int i = 0; i < 4; i++) {
    int y = WAVE_NAV_ICON_START_Y + (i * WAVE_NAV_ICON_SPACING);

    // Selection highlight (PlayStation Blue circle) - only show if nav bar has focus
    if (i == selected_nav_icon && current_focus == FOCUS_NAV_BAR) {
      draw_circle(WAVE_NAV_ICON_X, y, 28, UI_COLOR_PRIMARY_BLUE);
    }

    // Draw icon (centered at WAVE_NAV_ICON_X, y) - STATIC position
    if (i == 0) {
      // First icon: Draw white triangle play icon instead of texture
      draw_play_icon(WAVE_NAV_ICON_X, y, WAVE_NAV_ICON_SIZE);
    } else {
      // Other icons: Draw from textures
      if (!nav_icons[i]) continue;

      int icon_w = vita2d_texture_get_width(nav_icons[i]);
      int icon_h = vita2d_texture_get_height(nav_icons[i]);
      float scale = (float)WAVE_NAV_ICON_SIZE / (float)(icon_w > icon_h ? icon_w : icon_h);

      vita2d_draw_texture_scale(nav_icons[i],
        WAVE_NAV_ICON_X - (icon_w * scale / 2.0f),
        y - (icon_h * scale / 2.0f),
        scale, scale);
    }
  }
}

/// Map VitaChiakiHost to ConsoleCardInfo
void map_host_to_console_card(VitaChiakiHost* host, ConsoleCardInfo* card) {
  if (!host || !card) return;

  bool discovered = (host->type & DISCOVERED) && (host->discovery_state);
  bool registered = host->type & REGISTERED;
  bool at_rest = discovered && host->discovery_state &&
                 host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

  // Copy host name
  if (discovered && host->discovery_state) {
    snprintf(card->name, sizeof(card->name), "%s", host->discovery_state->host_name);
    snprintf(card->ip_address, sizeof(card->ip_address), "%s", host->discovery_state->host_addr);
  } else if (registered && host->registered_state) {
    snprintf(card->name, sizeof(card->name), "%s", host->registered_state->server_nickname);
    snprintf(card->ip_address, sizeof(card->ip_address), "%s", host->hostname);
  } else if (host->hostname) {
    snprintf(card->name, sizeof(card->name), "%s", host->hostname);
    snprintf(card->ip_address, sizeof(card->ip_address), "%s", host->hostname);
  }

  // Map host state to console state
  if (discovered && !at_rest) {
    card->status = 0;  // Available
    card->state = 1;   // Ready
  } else if (at_rest) {
    card->status = 2;  // Connecting/Standby
    card->state = 2;   // Standby
  } else {
    card->status = 1;  // Unavailable
    card->state = 0;   // Unknown
  }

  card->is_registered = registered;
  card->is_discovered = discovered;
  card->host = host;
}

/// Render a single console card
void render_console_card(ConsoleCardInfo* console, int x, int y, bool selected) {
  if (!console) return;

  bool is_registered = console->is_registered;
  bool is_discovered = console->is_discovered;
  bool is_unpaired = is_discovered && !is_registered;

  // Status border color (awake=light blue, asleep=yellow, unpaired=grey)
  uint32_t border_color = UI_COLOR_PRIMARY_BLUE;  // Default selection blue
  if (!selected && is_unpaired) {
    border_color = RGBA8(120, 120, 120, 255);  // Grey for unpaired
  } else if (!selected && console->state == 1) {  // Ready/Awake
    border_color = RGBA8(52, 144, 255, 255);  // Light blue
  } else if (!selected && console->state == 2) {  // Standby/Asleep
    border_color = RGBA8(255, 193, 7, 255);  // Yellow
  }

  // Draw status border
  if (!selected || is_unpaired) {
    draw_rounded_rectangle(x - 3, y - 3, CONSOLE_CARD_WIDTH + 6, CONSOLE_CARD_HEIGHT + 6, 12, border_color);
  }

  // Selection highlight (PlayStation Blue border, only for paired consoles)
  if (selected && !is_unpaired) {
    draw_rounded_rectangle(x - 4, y - 4, CONSOLE_CARD_WIDTH + 8, CONSOLE_CARD_HEIGHT + 8, 12, UI_COLOR_PRIMARY_BLUE);
  }

  // Card background (greyed out for unpaired consoles)
  uint32_t card_bg = is_unpaired ? RGBA8(0x25, 0x25, 0x28, 255) : UI_COLOR_CARD_BG;
  draw_card_with_shadow(x, y, CONSOLE_CARD_WIDTH, CONSOLE_CARD_HEIGHT, 12, card_bg);

  // PS5 logo (centered, properly scaled for card)
  bool is_ps5 = console->host && chiaki_target_is_ps5(console->host->target);

  if (is_ps5 && ps5_logo) {
    int logo_w = vita2d_texture_get_width(ps5_logo);
    int logo_h = vita2d_texture_get_height(ps5_logo);

    // Scale logo to fit card width (max 60% of card width)
    float max_width = CONSOLE_CARD_WIDTH * 0.6f;
    float scale = max_width / logo_w;

    int scaled_w = (int)(logo_w * scale);
    int scaled_h = (int)(logo_h * scale);
    int logo_x = x + (CONSOLE_CARD_WIDTH / 2) - (scaled_w / 2);
    int logo_y = y + 50;  // Fixed position from top

    // Dimmed for unpaired consoles
    if (is_unpaired) {
      vita2d_draw_texture_tint_scale(ps5_logo, logo_x, logo_y, scale, scale,
                                     RGBA8(255, 255, 255, 100));
    } else {
      vita2d_draw_texture_scale(ps5_logo, logo_x, logo_y, scale, scale);
    }
  } else if (!is_ps5) {
    // Fallback to PS4 icon for PS4 consoles
    vita2d_texture* logo = img_ps4;
    if (logo) {
      int logo_w = vita2d_texture_get_width(logo);
      int logo_h = vita2d_texture_get_height(logo);
      int logo_x = x + (CONSOLE_CARD_WIDTH / 2) - (logo_w / 2);
      int logo_y = y + (CONSOLE_CARD_HEIGHT / 3) - (logo_h / 2);
      vita2d_draw_texture(logo, logo_x, logo_y);
    }
  }

  // Console name bar (1/3 from bottom)
  int name_bar_y = y + CONSOLE_CARD_HEIGHT - (CONSOLE_CARD_HEIGHT / 3) - 20;
  draw_rounded_rectangle(x + 15, name_bar_y, CONSOLE_CARD_WIDTH - 30, 40, 8,
    RGBA8(70, 75, 80, 255));

  // Console name text (centered in bar)
  int text_width = vita2d_font_text_width(font, 20, console->name);
  int text_x = x + (CONSOLE_CARD_WIDTH / 2) - (text_width / 2);
  vita2d_font_draw_text(font, text_x, name_bar_y + 27, UI_COLOR_TEXT_PRIMARY, 20, console->name);

  // Status indicator (top-right)
  vita2d_texture* status_tex = NULL;
  if (console->status == 0) status_tex = ellipse_green;
  else if (console->status == 1) status_tex = ellipse_red;
  else if (console->status == 2) status_tex = ellipse_yellow;

  if (status_tex) {
    vita2d_draw_texture(status_tex, x + CONSOLE_CARD_WIDTH - 35, y + 10);
  }

  // State text ("Ready" / "Standby" / "Unpaired")
  const char* state_text = NULL;
  uint32_t state_color = UI_COLOR_TEXT_SECONDARY;

  if (is_unpaired) {
    state_text = "Unpaired";
    state_color = RGBA8(180, 180, 180, 255);  // Light grey
  } else if (console->state == 1) {
    state_text = "Ready";
    state_color = RGBA8(52, 144, 255, 255);  // PlayStation Blue
  } else if (console->state == 2) {
    state_text = "Standby";
    state_color = RGBA8(255, 193, 7, 255);  // Yellow
  }

  if (state_text) {
    int state_text_width = vita2d_font_text_width(font, 18, state_text);
    int state_x = x + (CONSOLE_CARD_WIDTH / 2) - (state_text_width / 2);
    vita2d_font_draw_text(font, state_x, name_bar_y + 55, state_color, 18, state_text);
  }
}

/// Update console card cache to prevent flickering during discovery updates
void update_console_card_cache(bool force_update) {
  uint64_t current_time = sceKernelGetProcessTimeWide();

  // Only update cache if enough time has passed or if forced
  if (!force_update &&
      (current_time - card_cache.last_update_time) < CARD_CACHE_UPDATE_INTERVAL_US) {
    return;
  }

  // Count current valid hosts
  int num_hosts = 0;
  ConsoleCardInfo temp_cards[MAX_NUM_HOSTS];

  for (int i = 0; i < MAX_NUM_HOSTS; i++) {
    if (context.hosts[i]) {
      map_host_to_console_card(context.hosts[i], &temp_cards[num_hosts]);
      num_hosts++;
    }
  }

  // Only update cache if we have valid hosts (prevents storing empty state during discovery updates)
  if (num_hosts > 0) {
    card_cache.num_cards = num_hosts;
    memcpy(card_cache.cards, temp_cards, sizeof(ConsoleCardInfo) * num_hosts);
    card_cache.last_update_time = current_time;
  }
}

/// Render console cards in grid layout
void render_console_grid() {
  // Center based on FULL screen width (not just content area)
  int screen_center_x = VITA_WIDTH / 2;
  int screen_center_y = VITA_HEIGHT / 2;

  // Update cache (respects 10-second interval)
  update_console_card_cache(false);

  // Calculate card position - centered on full screen
  int card_y = screen_center_y - (CONSOLE_CARD_HEIGHT / 2);
  int card_x = screen_center_x - (CONSOLE_CARD_WIDTH / 2);

  // Header text - centered horizontally on full screen above the card
  const char* header_text = "Which do you want to connect?";
  int text_width = vita2d_font_text_width(font, 24, header_text);
  int text_x = screen_center_x - (text_width / 2);
  int text_y = card_y - 50;  // Position text 50px above card

  vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, 24, header_text);

  // Use cached cards to prevent flickering
  if (card_cache.num_cards > 0) {
    for (int i = 0; i < card_cache.num_cards; i++) {
      // For multiple cards, stack them vertically centered around screen center
      int this_card_y = card_y + (i * CONSOLE_CARD_SPACING);

      // Only show selection highlight if console cards have focus
      bool selected = (i == selected_console_index && current_focus == FOCUS_CONSOLE_CARDS);
      render_console_card(&card_cache.cards[i], card_x, this_card_y, selected);
    }
  }
}

/// Draw a simple white filled triangle play icon (pointing right)
void draw_play_icon(int center_x, int center_y, int size) {
  uint32_t white = RGBA8(255, 255, 255, 255);
  int half_size = size / 2;

  // Draw filled triangle using horizontal lines
  // Triangle points: left (center_x - half_size, center_y),
  //                  top-right (center_x + half_size, center_y - half_size)
  //                  bottom-right (center_x + half_size, center_y + half_size)
  for (int y = -half_size; y <= half_size; y++) {
    int x_start = center_x - half_size + abs(y);  // Left edge moves right as we go away from center
    int x_end = center_x + half_size;              // Right edge is fixed
    int width = x_end - x_start;
    if (width > 0) {
      vita2d_draw_rectangle(x_start, center_y + y, width, 1, white);
    }
  }
}

/// Load all textures required for rendering the UI
void load_textures() {
  img_ps4 = vita2d_load_PNG_file(IMG_PS4_PATH);
  img_ps4_off = vita2d_load_PNG_file(IMG_PS4_OFF_PATH);
  img_ps4_rest = vita2d_load_PNG_file(IMG_PS4_REST_PATH);
  img_ps5 = vita2d_load_PNG_file(IMG_PS5_PATH);
  img_ps5_off = vita2d_load_PNG_file(IMG_PS5_OFF_PATH);
  img_ps5_rest = vita2d_load_PNG_file(IMG_PS5_REST_PATH);
  img_discovery_host = vita2d_load_PNG_file(IMG_DISCOVERY_HOST);

  // Load VitaRPS5 UI assets
  symbol_triangle = vita2d_load_PNG_file("app0:/assets/symbol_triangle.png");
  symbol_circle = vita2d_load_PNG_file("app0:/assets/symbol_circle.png");
  symbol_ex = vita2d_load_PNG_file("app0:/assets/symbol_ex.png");
  symbol_square = vita2d_load_PNG_file("app0:/assets/symbol_square.png");
  wave_top = vita2d_load_PNG_file("app0:/assets/wave_top.png");
  wave_bottom = vita2d_load_PNG_file("app0:/assets/wave_bottom.png");
  ellipse_green = vita2d_load_PNG_file("app0:/assets/ellipse_green.png");
  ellipse_yellow = vita2d_load_PNG_file("app0:/assets/ellipse_yellow.png");
  ellipse_red = vita2d_load_PNG_file("app0:/assets/ellipse_red.png");
  button_add_new = vita2d_load_PNG_file("app0:/assets/button_add_new.png");

  // Load navigation icons
  icon_play = vita2d_load_PNG_file("app0:/assets/icon_play.png");
  icon_settings = vita2d_load_PNG_file("app0:/assets/icon_settings.png");
  icon_controller = vita2d_load_PNG_file("app0:/assets/icon_controller.png");
  icon_profile = vita2d_load_PNG_file("app0:/assets/icon_profile.png");

  // Load new professional assets
  background_gradient = vita2d_load_PNG_file("app0:/assets/background.png");
  vita_rps5_logo = vita2d_load_PNG_file("app0:/assets/Vita_RPS5_Logo.png");
  vita_front = vita2d_load_PNG_file("app0:/assets/Vita_Front.png");
  ps5_logo = vita2d_load_PNG_file("app0:/assets/PS5_logo.png");
}

/// Check if a given region is touched on the front touch screen
bool is_touched(int x, int y, int width, int height) {
  SceTouchData* tdf = &(context.ui_state.touch_state_front);
  if (!tdf) {
    return false;
  }
  // TODO: Do the coordinate systems really match?
  return tdf->report->x > x && tdf->report->x <= x + width &&
         tdf->report->y > y && tdf->report->y <= y + height;
}

/// Check if a point is inside a circle (for wave navigation icons)
bool is_point_in_circle(float px, float py, int cx, int cy, int radius) {
  float dx = px - cx;
  float dy = py - cy;
  return (dx*dx + dy*dy) <= (radius*radius);
}

/// Check if a point is inside a rectangle (for cards and buttons)
bool is_point_in_rect(float px, float py, int rx, int ry, int rw, int rh) {
  return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

/// Handle VitaRPS5 touch screen input
UIScreenType handle_vitarps5_touch_input(int num_hosts) {
  SceTouchData touch;
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

  if (touch.reportNum > 0) {
    // Convert touch coordinates to screen coordinates
    // Vita touch resolution: 1920x1088, screen: 960x544
    float touch_x = (touch.report[0].x / 1920.0f) * 960.0f;
    float touch_y = (touch.report[0].y / 1088.0f) * 544.0f;

    // Check wave navigation icons (circular hitboxes - static positions)
    for (int i = 0; i < 4; i++) {
      int icon_x = WAVE_NAV_ICON_X;
      int icon_y = WAVE_NAV_ICON_START_Y + (i * WAVE_NAV_ICON_SPACING);

      if (is_point_in_circle(touch_x, touch_y, icon_x, icon_y, 30)) {
        selected_nav_icon = i;
        // Navigate to screen based on icon
        switch (i) {
          case 0: return UI_SCREEN_TYPE_MAIN;
          case 1: return UI_SCREEN_TYPE_SETTINGS;
          case 2: return UI_SCREEN_TYPE_CONTROLLER;  // Controller Configuration
          case 3: return UI_SCREEN_TYPE_PROFILE;     // Profile & Authentication
        }
      }
    }

    // Check console cards (rectangular hitboxes)
    if (num_hosts > 0) {
      int content_area_x = WAVE_NAV_WIDTH + ((VITA_WIDTH - WAVE_NAV_WIDTH) / 2);
      for (int i = 0; i < num_hosts; i++) {
        int card_x = content_area_x - (CONSOLE_CARD_WIDTH / 2);
        int card_y = CONSOLE_CARD_START_Y + (i * CONSOLE_CARD_SPACING);

        if (is_point_in_rect(touch_x, touch_y, card_x, card_y,
            CONSOLE_CARD_WIDTH, CONSOLE_CARD_HEIGHT)) {
          // Select card and trigger connect action
          selected_console_index = i;

          // Find and connect to selected host
          int host_idx = 0;
          for (int j = 0; j < MAX_NUM_HOSTS; j++) {
            if (context.hosts[j]) {
              if (host_idx == selected_console_index) {
                context.active_host = context.hosts[j];

                bool discovered = (context.active_host->type & DISCOVERED) && (context.active_host->discovery_state);
                bool registered = context.active_host->type & REGISTERED;
                bool at_rest = discovered && context.active_host->discovery_state &&
                               context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

                if (discovered && !at_rest && registered) {
                  host_stream(context.active_host);
                  return UI_SCREEN_TYPE_STREAM;
                } else if (at_rest) {
                  host_wakeup(context.active_host);
                } else if (!registered) {
                  return UI_SCREEN_TYPE_REGISTER_HOST;
                }
                break;
              }
              host_idx++;
            }
          }
          break;
        }
      }

      // Check "Add New" button
      if (button_add_new) {
        int btn_w = vita2d_texture_get_width(button_add_new);
        int btn_x = content_area_x - (btn_w / 2);
        int btn_y = CONSOLE_CARD_START_Y + (num_hosts * CONSOLE_CARD_SPACING) + 20;
        int btn_h = vita2d_texture_get_height(button_add_new);

        if (is_point_in_rect(touch_x, touch_y, btn_x, btn_y, btn_w, btn_h)) {
          if (!context.discovery_enabled) {
            start_discovery(NULL, NULL);
          }
        }
      }
    }
  }

  return UI_SCREEN_TYPE_MAIN;
}

/// Draw the tile for a host
/// @return The action to take for the host
UIHostAction host_tile(int host_slot, VitaChiakiHost* host) {
  int active_id = context.ui_state.active_item;
  bool is_active = active_id == (UI_MAIN_WIDGET_HOST_TILE | host_slot);
  bool discovered = (host->type & DISCOVERED) && (host->discovery_state);
  bool registered = host->type & REGISTERED;
  bool added = host->type & MANUALLY_ADDED;
  bool mutable = (added || registered);
  bool at_rest = discovered && host->discovery_state->state ==
                                   CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

  int x = HOST_SLOTS_X + (host_slot % 2) * (HOST_SLOT_W + 58);
  int y = HOST_SLOTS_Y;
  if (host_slot > 1) {
    y += HOST_SLOT_H + 11;
  }
  // Draw card with shadow for modern look
  if (is_active) {
    // Active selection border with glow effect
    draw_rounded_rectangle(x - 3, y - 3, HOST_SLOT_W + 6, HOST_SLOT_H + 6, 8, UI_COLOR_PRIMARY_BLUE);
  }
  draw_card_with_shadow(x, y, HOST_SLOT_W, HOST_SLOT_H, 8, UI_COLOR_CARD_BG);

  // Draw host name (nickname) and host id (mac)
  if (discovered) {
    vita2d_draw_texture(img_discovery_host, x, y);
    vita2d_font_draw_text(font, x + 68, y + 40, COLOR_WHITE, 40,
                         host->discovery_state->host_name);
    vita2d_font_draw_text(font, x + 255, y + 23, COLOR_WHITE, 20,
                         host->discovery_state->host_id);
  } else if (registered) {
    char* nickname = host->registered_state->server_nickname;
    if (!nickname) nickname = "";
    uint8_t* host_mac = host->server_mac;
    vita2d_font_draw_text(font, x + 68, y + 40, COLOR_WHITE, 40,
                          nickname);
    vita2d_font_draw_textf(font, x + 255, y + 23, COLOR_WHITE, 20,
                          "%X%X%X%X%X%X", host_mac[0], host_mac[1], host_mac[2],
                          host_mac[3], host_mac[4], host_mac[5]);
  }

  // Draw how many manually added instances of this console exist
  if (discovered && registered) {
    int num_mhosts = count_manual_hosts_of_console(host);
    if (num_mhosts == 1) {
      vita2d_font_draw_text(font, x + 10, y + HOST_SLOT_H - 10, COLOR_WHITE, 20, "(1 manual remote host)");
    } else if (num_mhosts > 1) {
      vita2d_font_draw_textf(font, x + 10, y + HOST_SLOT_H - 10, COLOR_WHITE, 20, "(%d manual remote hosts)", num_mhosts);
    } else {
      vita2d_font_draw_textf(font, x + 10, y + HOST_SLOT_H - 10, COLOR_WHITE, 20, "[%d manual remote hosts]", num_mhosts);
    }
  }

  // Draw host address
  vita2d_font_draw_text(font, x + 260, y + HOST_SLOT_H - 10, COLOR_WHITE, 20, host->hostname);

  vita2d_texture* console_img;
  bool is_ps5 = chiaki_target_is_ps5(host->target);
  // TODO: Don't use separate textures for off/on/rest, use tinting instead
  if (added) {// && !discovered) {
    console_img = is_ps5 ? img_ps5_off : img_ps4_off;
  } else if (at_rest) {
    console_img = is_ps5 ? img_ps5_rest : img_ps4_rest;
  } else {
    console_img = is_ps5 ? img_ps5 : img_ps4;
  }
  vita2d_draw_texture(console_img, x + 64, y + 64);
  if (discovered && !at_rest) {
    const char* app_name = host->discovery_state->running_app_name;
    const char* app_id = host->discovery_state->running_app_titleid;
    // printf("%s", app_name);
    // printf("%s", app_id);
    if (app_name && app_id) {
      vita2d_font_draw_text(font, x + 32, y + 16, COLOR_WHITE, 16, app_name);
      vita2d_font_draw_text(font, x + 300, y + 170, COLOR_WHITE, 16, app_id);
    }
  }

  // set tooltip
  if (is_active) {
    if (at_rest) {
      if (registered) {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "%s: send wake signal (note: console may be temporarily undetected during wakeup)", confirm_btn_str);
      } else {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "Cannot send wake signal to unregistered console.");
      }
    } else {
      if (discovered && !registered) {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "%s: begin pairing process", confirm_btn_str);
      } else if (discovered && registered) {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "%s: start remote play;  Square: re-pair", confirm_btn_str);
      } else if (added) {
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "%s: send wake signal and/or start remote play (wakeup takes time);  SELECT button: delete host (no confirmation)", confirm_btn_str);
      } else {
        // there should never be tiles that are neither discovered nor added
        snprintf(active_tile_tooltip_msg, MAX_TOOLTIP_CHARS, "");
      }
    }
  }

  // Handle navigation
  int btn = context.ui_state.button_state;
  int old_btn = context.ui_state.old_button_state;
  int last_slot = context.num_hosts - 1;
  if (is_active) {
    if (context.active_host != host) {
      context.active_host = host;
    }
    if (btn_pressed(SCE_CTRL_UP)) {
      if (host_slot < 2) {
        // Set focus on the last button of the header bar
        context.ui_state.next_active_item = UI_MAIN_WIDGET_SETTINGS_BTN;
      } else {
        // Set focus on the host tile directly above
        context.ui_state.next_active_item =
            UI_MAIN_WIDGET_HOST_TILE | (host_slot - 2);
      }
    } else if (btn_pressed(SCE_CTRL_RIGHT)) {
      if (host_slot != last_slot && (host_slot == 0 || host_slot == 2)) {
        // Set focus on the host tile to the right
        context.ui_state.next_active_item =
            UI_MAIN_WIDGET_HOST_TILE | (host_slot + 1);
      }
    } else if (btn_pressed(SCE_CTRL_DOWN)) {
      if (last_slot >= host_slot + 2 && host_slot < 2) {
        // Set focus on the host tile directly below
        context.ui_state.next_active_item =
            UI_MAIN_WIDGET_HOST_TILE | (host_slot + 2);
      }
    } else if (btn_pressed(SCE_CTRL_LEFT)) {
      if (host_slot == 1 || host_slot == 3) {
        context.ui_state.next_active_item =
            UI_MAIN_WIDGET_HOST_TILE | (host_slot - 1);
      }
    }

    if (btn_pressed(SCE_CTRL_SELECT) && added) {
      delete_manual_host(host);
      // TODO delete from manual hosts

      // refresh tiles
      update_context_hosts();
    }
    // Determine action to perform
    // if (btn_pressed(SCE_CTRL_CONFIRM) && !registered) {
    //   for (int i = 0; i < context.config.num_registered_hosts; i++) {
    //     printf("0x%x", context.config.registered_hosts[i]->registered_state);
    //     if (context.config.registered_hosts[i] != NULL && strcmp(context.active_host->hostname, context.config.registered_hosts[i]->hostname) == 0) {
    //       context.active_host->registered_state = context.config.registered_hosts[i]->registered_state;
    //       context.active_host->type |= REGISTERED;
    //       registered = true;
    //       break;
    //     }
    //   }
    // }
    // if (btn_pressed(SCE_CTRL_CONFIRM) && !registered && !added) {
    //   for (int i = 0; i < context.config.num_manual_hosts; i++) {
    //     if (context.config.manual_hosts[i] != NULL && strcmp(context.active_host->hostname, context.config.manual_hosts[i]->hostname) == 0) {
    //       context.active_host->registered_state = context.config.manual_hosts[i]->registered_state;
    //       context.active_host->type |= MANUALLY_ADDED;
    //       context.active_host->type |= REGISTERED;
    //       added = true;
    //       break;
    //     }
    //   }
    // }
    if (registered && btn_pressed(SCE_CTRL_CONFIRM)) {
      if (at_rest) {
        return UI_HOST_ACTION_WAKEUP;
      } else {
        // since we don't know if the remote host is awake, send wakeup signal
        if (added) host_wakeup(context.active_host);
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
        int err = host_stream(context.active_host);
        return UI_HOST_ACTION_STREAM;
      }
    } else if (!registered && !added && discovered && btn_pressed(SCE_CTRL_CONFIRM)){
      if (at_rest) {
        LOGD("Cannot wake unregistered console.");
        return UI_HOST_ACTION_NONE;
      }
      return UI_HOST_ACTION_REGISTER;
    } else if (discovered && btn_pressed(SCE_CTRL_SQUARE)) {
      return UI_HOST_ACTION_REGISTER;
    }
  }
  if (is_touched(x, y, HOST_SLOT_W, HOST_SLOT_H)) {
    context.ui_state.next_active_item = UI_MAIN_WIDGET_HOST_TILE | host_slot;
  }
  return UI_HOST_ACTION_NONE;
}

uint16_t IMEInput[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
bool showingIME = false;



void load_psn_id_if_needed() {
  if (context.config.psn_account_id == NULL || strlen(context.config.psn_account_id) < 1) {
    char accIDBuf[8];
    memset(accIDBuf, 0, sizeof(accIDBuf));
    if (context.config.psn_account_id) {
      free(context.config.psn_account_id);
    }
    sceRegMgrGetKeyBin("/CONFIG/NP/", "account_id", accIDBuf, sizeof(accIDBuf));

    int b64_strlen = get_base64_size(sizeof(accIDBuf));
    context.config.psn_account_id = (char*)malloc(b64_strlen+1); // + 1 for null termination
    context.config.psn_account_id[b64_strlen] = 0; // null terminate
    chiaki_base64_encode(accIDBuf, sizeof(accIDBuf), context.config.psn_account_id, get_base64_size(sizeof(accIDBuf)));
    LOGD("size of id %d", strlen(context.config.psn_account_id));
  }
}

/// Draw the main menu screen with the list of hosts and header bar
/// @return the screen to draw during the next cycle
UIScreenType draw_main_menu() {
  // Update and render VitaRPS5 particle background
  update_particles();
  render_particles();

  // Render VitaRPS5 navigation sidebar
  render_wave_navigation();

  // Render VitaRPS5 console cards instead of host tiles
  render_console_grid();

  // Count hosts
  int num_hosts = 0;
  for (int i = 0; i < MAX_NUM_HOSTS; i++) {
    if (context.hosts[i]) num_hosts++;
  }

  UIScreenType next_screen = UI_SCREEN_TYPE_MAIN;

  // === D-PAD NAVIGATION (moves between ALL UI elements) ===

  if (btn_pressed(SCE_CTRL_UP)) {
    if (current_focus == FOCUS_NAV_BAR) {
      // Move up within nav bar
      selected_nav_icon = (selected_nav_icon - 1 + 4) % 4;
    } else if (current_focus == FOCUS_CONSOLE_CARDS && num_hosts > 0) {
      // Move up within console cards (cycle through)
      selected_console_index = (selected_console_index - 1 + num_hosts) % num_hosts;
    }
  } else if (btn_pressed(SCE_CTRL_DOWN)) {
    if (current_focus == FOCUS_NAV_BAR) {
      // Move down within nav bar
      selected_nav_icon = (selected_nav_icon + 1) % 4;
    } else if (current_focus == FOCUS_CONSOLE_CARDS && num_hosts > 0) {
      // Move down within console cards (cycle through)
      selected_console_index = (selected_console_index + 1) % num_hosts;
    }
  } else if (btn_pressed(SCE_CTRL_LEFT)) {
    if (current_focus == FOCUS_CONSOLE_CARDS) {
      // Move left to nav bar
      last_console_selection = selected_console_index;
      current_focus = FOCUS_NAV_BAR;
    }
  } else if (btn_pressed(SCE_CTRL_RIGHT)) {
    if (current_focus == FOCUS_NAV_BAR) {
      // Move right from nav bar to console cards/discovery card
      current_focus = FOCUS_CONSOLE_CARDS;
      if (num_hosts > 0) {
        selected_console_index = last_console_selection;
      }
    }
  }

  // === X BUTTON (Activate/Select highlighted element) ===

  if (btn_pressed(SCE_CTRL_CROSS)) {
    if (current_focus == FOCUS_NAV_BAR) {
      // Activate nav bar icon - switch screen
      switch (selected_nav_icon) {
        case 0: next_screen = UI_SCREEN_TYPE_MAIN; break;       // Play (console list)
        case 1: next_screen = UI_SCREEN_TYPE_SETTINGS; break;   // Settings
        case 2: next_screen = UI_SCREEN_TYPE_CONTROLLER; break; // Controller Configuration
        case 3: next_screen = UI_SCREEN_TYPE_PROFILE; break;    // Profile & Registration
      }
    } else if (current_focus == FOCUS_CONSOLE_CARDS && num_hosts > 0) {
      // Connect to selected console
      int host_idx = 0;
      for (int i = 0; i < MAX_NUM_HOSTS; i++) {
        if (context.hosts[i]) {
          if (host_idx == selected_console_index) {
            context.active_host = context.hosts[i];

            bool discovered = (context.active_host->type & DISCOVERED) && (context.active_host->discovery_state);
            bool registered = context.active_host->type & REGISTERED;
            bool at_rest = discovered && context.active_host->discovery_state &&
                           context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

            if (!registered) {
              // Unregistered console - start registration
              next_screen = UI_SCREEN_TYPE_REGISTER_HOST;
            } else if (at_rest) {
              // Dormant console - wake and show waking screen
              LOGD("Waking dormant console...");
              host_wakeup(context.active_host);
              next_screen = UI_SCREEN_TYPE_WAKING;
            } else if (registered) {
              // Ready console - start streaming
              next_screen = UI_SCREEN_TYPE_STREAM;
              host_stream(context.active_host);
            }
            break;
          }
          host_idx++;
        }
      }
    }
  }

  // === OTHER BUTTONS ===

  // Square: Re-pair selected console (unregister + register again)
  if (btn_pressed(SCE_CTRL_SQUARE) && current_focus == FOCUS_CONSOLE_CARDS && num_hosts > 0) {
    int host_idx = 0;
    for (int i = 0; i < MAX_NUM_HOSTS; i++) {
      if (context.hosts[i]) {
        if (host_idx == selected_console_index) {
          VitaChiakiHost* host = context.hosts[i];
          bool registered = host->type & REGISTERED;

          if (registered) {
            // Remove registration and trigger re-pairing
            LOGD("Re-pairing console: %s", host->hostname);

            // Free registered state memory
            if (host->registered_state) {
              free(host->registered_state);
              host->registered_state = NULL;
            }

            // Remove from config.registered_hosts array
            for (int j = 0; j < context.config.num_registered_hosts; j++) {
              if (context.config.registered_hosts[j] == host) {
                // Shift remaining elements left
                for (int k = j; k < context.config.num_registered_hosts - 1; k++) {
                  context.config.registered_hosts[k] = context.config.registered_hosts[k + 1];
                }
                context.config.registered_hosts[context.config.num_registered_hosts - 1] = NULL;
                context.config.num_registered_hosts--;
                break;
              }
            }

            // Clear registered flag
            host->type &= ~REGISTERED;

            // Save config to persist changes
            config_serialize(&context.config);

            LOGD("Registration data deleted for console: %s", host->hostname);

            // Trigger registration screen
            context.active_host = host;
            next_screen = UI_SCREEN_TYPE_REGISTER_HOST;
          }
          break;
        }
        host_idx++;
      }
    }
  }

  // Handle touch screen input for VitaRPS5 UI
  UIScreenType touch_screen = handle_vitarps5_touch_input(num_hosts);
  if (touch_screen != UI_SCREEN_TYPE_MAIN) {
    return touch_screen;
  }

  // VitaRPS5 UI control hints at bottom
  int hint_y = VITA_HEIGHT - 25;
  int hint_x = WAVE_NAV_WIDTH + 20;
  vita2d_font_draw_text(font, hint_x, hint_y, UI_COLOR_TEXT_TERTIARY, 16,
    "D-Pad: Navigate | Cross: Connect/Wake | Square: Re-pair");


  return next_screen;
}

// ============================================================================
// PHASE 2: SETTINGS SCREEN
// ============================================================================

typedef enum {
  SETTINGS_TAB_STREAMING = 0,
  SETTINGS_TAB_COUNT = 1  // Only Streaming tab (removed Video/Network - no backend support)
} SettingsTab;

typedef struct {
  SettingsTab current_tab;
  int selected_item;
  bool dropdown_expanded;
  int dropdown_selected_option;
} SettingsState;

static SettingsState settings_state = {0};

// Tab color (Blue) - Only Streaming settings, Video/Network removed (no backend support)
static uint32_t settings_tab_colors[SETTINGS_TAB_COUNT] = {
  RGBA8(0x00, 0x70, 0xCC, 255), // Blue - Streaming
};

static const char* settings_tab_names[SETTINGS_TAB_COUNT] = {
  "Streaming Quality"
};

// Resolution/FPS option strings for dropdowns
static const char* resolution_options[] = {"720p", "1080p"};
static const char* fps_options[] = {"30 FPS", "60 FPS"};

/// Get resolution string from ChiakiVideoResolutionPreset
static const char* get_resolution_string(ChiakiVideoResolutionPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p: return "360p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_540p: return "540p";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_720p: return "720p (Experimental)";
    case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p: return "1080p (Experimental)";
    default: return "540p";
  }
}

/// Get FPS string from ChiakiVideoFPSPreset
static const char* get_fps_string(ChiakiVideoFPSPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_FPS_PRESET_30: return "30 FPS";
    case CHIAKI_VIDEO_FPS_PRESET_60: return "60 FPS";
    default: return "60 FPS";
  }
}

/// Draw Streaming Quality tab content
static void draw_settings_streaming_tab(int content_x, int content_y, int content_w) {
  int item_h = 50;
  int item_spacing = 10;
  int y = content_y;

  // Quality Preset dropdown
  draw_dropdown(content_x, y, content_w, item_h, "Quality Preset",
                get_resolution_string(context.config.resolution),
                false, settings_state.selected_item == 0);
  y += item_h + item_spacing;

  // FPS Target dropdown
  draw_dropdown(content_x, y, content_w, item_h, "FPS Target",
                get_fps_string(context.config.fps),
                false, settings_state.selected_item == 1);
  y += item_h + item_spacing;

  // Auto Discovery toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     context.config.auto_discovery, settings_state.selected_item == 2);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Auto Discovery");
  y += item_h + item_spacing;

  // Show Latency toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     context.config.show_latency, settings_state.selected_item == 3);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Show Latency");
}

/// Draw Controller Settings tab content
static void draw_settings_controller_tab(int content_x, int content_y, int content_w) {
  int item_h = 50;
  int item_spacing = 10;
  int y = content_y;

  // Controller Map ID dropdown
  char map_id_str[32];
  snprintf(map_id_str, sizeof(map_id_str), "Map %d", context.config.controller_map_id);
  draw_dropdown(content_x, y, content_w, item_h, "Controller Map", map_id_str,
                false, settings_state.selected_item == 0);
  y += item_h + item_spacing;

  // Button layout toggle (Circle vs Cross confirm)
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     context.config.circle_btn_confirm, settings_state.selected_item == 1);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, 16, "Circle Button Confirm");
  y += item_h + item_spacing;

  // TODO(PHASE2-STUB): Motion Controls - Not implemented
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     false, settings_state.selected_item == 2);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_SECONDARY, 16, "Motion Controls (Stub)");
}

/// Main Settings screen rendering function
/// @return whether the dialog should keep rendering
bool draw_settings() {
  // Render particle background and navigation sidebar
  update_particles();
  render_particles();
  render_wave_navigation();

  // Main content area (avoiding wave nav sidebar)
  int content_x = WAVE_NAV_WIDTH + 40;
  int content_y = 100;
  int content_w = VITA_WIDTH - WAVE_NAV_WIDTH - 80;

  // Settings title (streaming settings only now)
  vita2d_font_draw_text(font, content_x, 50, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, "Streaming Settings");

  // Content area (no tabs needed - only one section)
  int tab_content_y = 90;
  int tab_content_w = content_w - 40;
  int tab_content_x = content_x + 20;

  // Draw streaming settings content
  draw_settings_streaming_tab(tab_content_x, tab_content_y, tab_content_w);

  // Control hints at bottom
  int hint_y = VITA_HEIGHT - 25;
  int hint_x = WAVE_NAV_WIDTH + 20;
  vita2d_font_draw_text(font, hint_x, hint_y, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL,
    "Up/Down: Navigate | X: Toggle/Select | Circle: Back");

  // === INPUT HANDLING ===

  // No tab switching needed - only one section
  int max_items = 4; // Streaming tab: Resolution, FPS, Auto Discovery, Show Latency

  // Up/Down: Navigate items
  if (btn_pressed(SCE_CTRL_UP)) {
    settings_state.selected_item = (settings_state.selected_item - 1 + max_items) % max_items;
  } else if (btn_pressed(SCE_CTRL_DOWN)) {
    settings_state.selected_item = (settings_state.selected_item + 1) % max_items;
  }

  // X: Activate selected item (toggle or cycle dropdown)
  if (btn_pressed(SCE_CTRL_CROSS)) {
    if (settings_state.selected_item == 0) {
          // Cycle resolution: 360p → 540p → 720p → 1080p → 360p
          switch (context.config.resolution) {
            case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
              context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
              break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
              context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
              break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
              context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
              break;
            case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
            default:
              context.config.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
              break;
          }
          config_serialize(&context.config);
        } else if (settings_state.selected_item == 1) {
          // Cycle FPS
          context.config.fps = (context.config.fps == CHIAKI_VIDEO_FPS_PRESET_30) ?
            CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30;
          config_serialize(&context.config);
        } else if (settings_state.selected_item == 2) {
          // Auto discovery toggle
          context.config.auto_discovery = !context.config.auto_discovery;
          config_serialize(&context.config);
    } else if (settings_state.selected_item == 3) {
      // Show latency toggle
      context.config.show_latency = !context.config.show_latency;
      config_serialize(&context.config);
    }
  }

  // Circle: Back to main menu
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    return false;
  }

  return true;
}

// ============================================================================
// PHASE 2: PROFILE & REGISTRATION SCREEN
// ============================================================================

typedef enum {
  PROFILE_SECTION_INFO = 0,
  PROFILE_SECTION_CONNECTION = 1,
  PROFILE_SECTION_COUNT = 2
} ProfileSection;

typedef struct {
  ProfileSection current_section;
  bool editing_psn_id;
} ProfileState;

static ProfileState profile_state = {0};

/// Draw profile card (left side)
static void draw_profile_card(int x, int y, int width, int height, bool selected) {
  uint32_t card_color = UI_COLOR_CARD_BG;
  draw_card_with_shadow(x, y, width, height, 12, card_color);

  if (selected) {
    draw_rounded_rectangle(x - 2, y - 2, width + 4, height + 4, 14, UI_COLOR_PRIMARY_BLUE);
    draw_rounded_rectangle(x, y, width, height, 12, card_color);
  }

  int content_x = x + 20;
  int content_y = y + 30;

  // User icon (blue circular background with profile.png icon as placeholder)
  int icon_size = 50;
  int icon_x = content_x;
  int icon_y = content_y;
  draw_circle(icon_x + icon_size/2, icon_y + icon_size/2, icon_size/2, UI_COLOR_PRIMARY_BLUE);

  // Profile icon (placeholder until PSN login retrieves actual user avatar)
  if (icon_profile) {
    int img_w = vita2d_texture_get_width(icon_profile);
    int img_h = vita2d_texture_get_height(icon_profile);
    float scale = (icon_size * 0.6f) / img_w;  // 60% of circle size
    int scaled_w = (int)(img_w * scale);
    int scaled_h = (int)(img_h * scale);
    int img_x = icon_x + (icon_size - scaled_w) / 2;
    int img_y = icon_y + (icon_size - scaled_h) / 2;
    vita2d_draw_texture_scale(icon_profile, img_x, img_y, scale, scale);
  }

  // PSN Account ID
  const char* psn_id = context.config.psn_account_id ? context.config.psn_account_id : "Not Set";
  vita2d_font_draw_text(font, content_x + icon_size + 20, content_y + 20,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, psn_id);

  // PlayStation Network label
  vita2d_font_draw_text(font, content_x + icon_size + 20, content_y + 42,
                        UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, "PlayStation Network");

  // Divider line
  vita2d_draw_rectangle(content_x, content_y + 70, width - 40, 1,
                        RGBA8(0x50, 0x50, 0x50, 255));

  // "Account ID: xxxx" label at bottom
  vita2d_font_draw_text(font, content_x, y + height - 30,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, "Account ID");
  vita2d_font_draw_text(font, content_x, y + height - 12,
                        UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        psn_id);
}

/// Draw connection info card (right side) - two-column layout
static void draw_connection_info_card(int x, int y, int width, int height, bool selected) {
  uint32_t card_color = UI_COLOR_CARD_BG;
  draw_card_with_shadow(x, y, width, height, 12, card_color);

  if (selected) {
    draw_rounded_rectangle(x - 2, y - 2, width + 4, height + 4, 14, UI_COLOR_PRIMARY_BLUE);
    draw_rounded_rectangle(x, y, width, height, 12, card_color);
  }

  int content_x = x + 15;
  int content_y = y + 25;
  int line_h = 20;
  int col2_x = content_x + 120;  // Value column

  // Title
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER,
                        "Connection Information");
  content_y += 30;

  // Network Type
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Network Type");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        "Local WiFi");
  content_y += line_h;

  // Console IP
  const char* console_ip = "Not Connected";
  if (context.active_host && context.active_host->discovery_state &&
      context.active_host->discovery_state->host_addr) {
    console_ip = context.active_host->discovery_state->host_addr;
  }
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Console IP");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        console_ip);
  content_y += line_h;

  // Latency (if enabled)
  if (context.config.show_latency) {
    char latency_text[32] = "N/A";
    uint32_t latency_color = UI_COLOR_TEXT_PRIMARY;

    if (context.stream.session_init && context.stream.session.rtt_us > 0) {
      uint32_t latency_ms = (uint32_t)(context.stream.session.rtt_us / 1000);
      snprintf(latency_text, sizeof(latency_text), "%u ms", latency_ms);

      // Color code
      if (latency_ms < 30) {
        latency_color = RGBA8(0x4C, 0xAF, 0x50, 255);  // Green
      } else if (latency_ms < 60) {
        latency_color = RGBA8(0xFF, 0xB7, 0x4D, 255);  // Yellow
      } else {
        latency_color = RGBA8(0xF4, 0x43, 0x36, 255);  // Red
      }
    }

    vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                          "Latency");
    vita2d_font_draw_text(font, col2_x, content_y, latency_color, FONT_SIZE_SMALL,
                          latency_text);
    content_y += line_h;
  }

  // Connection status
  bool is_connected = context.active_host != NULL;
  const char* connection_text = is_connected ? "Direct" : "None";
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Connection");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        connection_text);
  content_y += line_h;

  // Remote Play status
  const char* remote_play = is_connected ? "Available" : "Unavailable";
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Remote Play");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        remote_play);
  content_y += line_h;

  // Quality Setting
  const char* quality_text = "Auto";
  if (context.config.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_720p) {
    quality_text = "720p";
  } else if (context.config.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p) {
    quality_text = "1080p";
  }
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Quality Setting");
  vita2d_font_draw_text(font, col2_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL,
                        quality_text);
}

/// Draw PSN Authentication section (bottom) - modern design with status indicators
static void draw_registration_section(int x, int y, int width, int height, bool selected) {
  uint32_t card_color = UI_COLOR_CARD_BG;
  draw_card_with_shadow(x, y, width, height, 12, card_color);

  if (selected) {
    draw_rounded_rectangle(x - 2, y - 2, width + 4, height + 4, 14, UI_COLOR_PRIMARY_BLUE);
    draw_rounded_rectangle(x, y, width, height, 12, card_color);
  }

  int content_x = x + 15;
  int content_y = y + 25;

  // Title
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER,
                        "PSN Authentication");
  content_y += 30;

  // Description text
  vita2d_font_draw_text(font, content_x, content_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL,
                        "Required for remote play on PS5 over local net.");
  content_y += 25;

  // Authentication status indicators
  int num_registered = context.config.num_registered_hosts;
  bool authenticated = num_registered > 0;

  // Status indicator 1: Not authenticated (red X) or authenticated (green checkmark)
  draw_status_dot(content_x, content_y - 3, 6, authenticated ? STATUS_ACTIVE : STATUS_ERROR);
  vita2d_font_draw_text(font, content_x + 15, content_y,
                        authenticated ? RGBA8(0x4C, 0xAF, 0x50, 255) : RGBA8(0xF4, 0x43, 0x36, 255),
                        FONT_SIZE_SMALL, authenticated ? "Authenticated" : "Not authenticated");
  content_y += 22;

  // "Add New" button
  int btn_w = 80;
  int btn_h = 30;
  int btn_x = content_x;
  int btn_y = content_y;

  uint32_t btn_color = selected ? UI_COLOR_PRIMARY_BLUE : RGBA8(0x50, 0x70, 0xA0, 255);
  draw_rounded_rectangle(btn_x, btn_y, btn_w, btn_h, 6, btn_color);

  int text_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, "Add New");
  vita2d_font_draw_text(font, btn_x + (btn_w - text_w) / 2, btn_y + btn_h / 2 + 5,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL, "Add New");

  // Hint if selected
  if (selected) {
    vita2d_font_draw_text(font, btn_x + btn_w + 15, btn_y + btn_h / 2 + 5,
                          UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL,
                          "Press X to register");
  }
}

/// Main Profile & Registration screen
/// @return next screen type to display
UIScreenType draw_profile_screen() {
  // Render particle background and navigation sidebar
  update_particles();
  render_particles();
  render_wave_navigation();

  // Main content area
  int content_x = WAVE_NAV_WIDTH + 40;
  int content_y = 60;
  int content_w = VITA_WIDTH - WAVE_NAV_WIDTH - 80;

  // Title
  vita2d_font_draw_text(font, content_x, 50, UI_COLOR_TEXT_PRIMARY, 26, "Profile & Connection");

  // Layout: Profile card (left), Connection info (right) - registration removed
  int card_spacing = 15;
  int card_w = (content_w - card_spacing) / 2;
  int card_h = 250;  // Taller cards since no bottom section

  // Profile card (left)
  draw_profile_card(content_x, content_y, card_w, card_h,
                    profile_state.current_section == PROFILE_SECTION_INFO);

  // Connection info card (right)
  draw_connection_info_card(content_x + card_w + card_spacing, content_y, card_w, card_h,
                             profile_state.current_section == PROFILE_SECTION_CONNECTION);

  // Control hints at bottom
  int hint_y = VITA_HEIGHT - 25;
  int hint_x = WAVE_NAV_WIDTH + 20;
  vita2d_font_draw_text(font, hint_x, hint_y, UI_COLOR_TEXT_TERTIARY, 16,
    "Left/Right: Switch Card | Circle: Back");

  UIScreenType next_screen = UI_SCREEN_TYPE_PROFILE;

  // === INPUT HANDLING ===

  // Left/Right: Navigate between Profile and Connection cards
  if (btn_pressed(SCE_CTRL_LEFT)) {
    profile_state.current_section = PROFILE_SECTION_INFO;
  } else if (btn_pressed(SCE_CTRL_RIGHT)) {
    profile_state.current_section = PROFILE_SECTION_CONNECTION;
  }

  // Circle: Back to main menu
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    next_screen = UI_SCREEN_TYPE_MAIN;
  }

  return next_screen;
}

// ============================================================================
// PHASE 2: CONTROLLER CONFIGURATION SCREEN (REDESIGNED WITH TABS)
// ============================================================================

typedef enum {
  CONTROLLER_TAB_MAPPINGS = 0,
  CONTROLLER_TAB_SETTINGS = 1,
  CONTROLLER_TAB_COUNT = 2
} ControllerTab;

typedef struct {
  ControllerTab current_tab;
  int selected_item;
} ControllerState;

static ControllerState controller_state = {0};

// Tab colors (Purple for both tabs - controller theme)
static uint32_t controller_tab_colors[CONTROLLER_TAB_COUNT] = {
  RGBA8(0x7C, 0x3A, 0xED, 255), // Purple - Mappings
  RGBA8(0x9C, 0x27, 0xB0, 255), // Accent Purple - Settings
};

static const char* controller_tab_names[CONTROLLER_TAB_COUNT] = {
  "Button Mappings",
  "Controller Settings"
};

// Button mapping data structure
typedef struct {
  const char* vita_button;
  const char* ps5_button;
} ButtonMapping;

/// Get scheme name from controller map ID
static const char* get_scheme_name(int map_id) {
  if (map_id >= 0 && map_id <= 7) {
    return "Official Layout";
  } else if (map_id == 25) {
    return "No Touchpad";
  } else if (map_id == 99) {
    return "Vitaki Custom";
  } else if (map_id >= 100) {
    return "L2/L3 R2/R3 Swap";
  }
  return "Unknown";
}

/// Get button mappings based on controller map ID (dynamically generated from controller.c logic)
static void get_button_mappings(int map_id, ButtonMapping* mappings, int* count) {
  int idx = 0;

  // Common mappings for ALL schemes
  mappings[idx++] = (ButtonMapping){"D-Pad", "D-Pad"};
  mappings[idx++] = (ButtonMapping){"Face Buttons", "Face Buttons"};
  mappings[idx++] = (ButtonMapping){"L1", "L1"};
  mappings[idx++] = (ButtonMapping){"R1", "R1"};
  mappings[idx++] = (ButtonMapping){"Select+Start", "PS Button"};

  // Map-specific L2/R2/L3/R3/Touchpad assignments
  if (map_id == 99) {
    // Map 99: Vitaki Custom (rear touch L2/R2, physical buttons L3/R3)
    mappings[idx++] = (ButtonMapping){"Rear L (near L1)", "L2"};
    mappings[idx++] = (ButtonMapping){"Rear R (near R1)", "R2"};
    mappings[idx++] = (ButtonMapping){"Left + Square", "L3"};
    mappings[idx++] = (ButtonMapping){"Right + Circle", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Touch", "Touchpad"};
  } else if (map_id == 1 || map_id == 101) {
    // Map 1/101: Front touch arcs for all triggers
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 2 || map_id == 102) {
    // Map 2/102: Rear touch left/right for L3/R3, front arcs for L2/R2
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Rear Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Rear Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 3 || map_id == 103) {
    // Map 3/103: Rear touch for L3/R3, front upper arcs for L2/R2
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Rear Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Rear Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 4 || map_id == 104) {
    // Map 4/104: Touchpad only, no L2/R2/L3/R3
    mappings[idx++] = (ButtonMapping){"Front Touch", "Touchpad"};
    mappings[idx++] = (ButtonMapping){"L2/R2/L3/R3", "Not Mapped"};
  } else if (map_id == 5 || map_id == 105) {
    // Map 5/105: No triggers or touchpad at all
    mappings[idx++] = (ButtonMapping){"All Extras", "Not Mapped"};
  } else if (map_id == 6 || map_id == 106) {
    // Map 6/106: No L3/R3, front arcs for L2/R2
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"L3/R3", "Not Mapped"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 7 || map_id == 107) {
    // Map 7/107: No L3/R3, front upper arcs for L2/R2
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"L3/R3", "Not Mapped"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  } else if (map_id == 25 || map_id == 125) {
    // Map 25/125: No touchpad, front arcs for all triggers
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Touchpad", "Not Mapped"};
  } else if (map_id == 100) {
    // Map 100: Rear touch quadrants
    mappings[idx++] = (ButtonMapping){"Rear Lower-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Rear Lower-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Rear Upper-Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Rear Upper-Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Touch", "Touchpad"};
  } else {
    // Map 0 (default): Front touch arcs (same as map 1)
    mappings[idx++] = (ButtonMapping){"Front Upper-Left", "L2"};
    mappings[idx++] = (ButtonMapping){"Front Upper-Right", "R2"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Left", "L3"};
    mappings[idx++] = (ButtonMapping){"Front Lower-Right", "R3"};
    mappings[idx++] = (ButtonMapping){"Front Center", "Touchpad"};
  }

  *count = idx;
}

/// Draw Mappings Tab content (scheme selector + button table + vita diagram)
static void draw_controller_mappings_tab(int content_x, int content_y, int content_w) {
  // Scheme selector at top with Left/Right arrows
  int selector_y = content_y;
  int selector_h = 50;

  // Draw scheme selector card
  draw_card_with_shadow(content_x, selector_y, content_w, selector_h, 8, UI_COLOR_CARD_BG);

  // Left arrow
  vita2d_font_draw_text(font, content_x + 30, selector_y + selector_h/2 + 8,
                        UI_COLOR_PRIMARY_BLUE, FONT_SIZE_HEADER, "<");

  // Scheme text (centered)
  char scheme_text[64];
  snprintf(scheme_text, sizeof(scheme_text), "Scheme %d: %s",
           context.config.controller_map_id, get_scheme_name(context.config.controller_map_id));
  int text_w = vita2d_font_text_width(font, FONT_SIZE_SUBHEADER, scheme_text);
  vita2d_font_draw_text(font, content_x + (content_w - text_w)/2, selector_y + selector_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, scheme_text);

  // Right arrow
  vita2d_font_draw_text(font, content_x + content_w - 50, selector_y + selector_h/2 + 8,
                        UI_COLOR_PRIMARY_BLUE, FONT_SIZE_HEADER, ">");

  // Hint below selector
  const char* hint = "Press Left/Right to cycle schemes";
  int hint_w = vita2d_font_text_width(font, FONT_SIZE_SMALL, hint);
  vita2d_font_draw_text(font, content_x + (content_w - hint_w)/2, selector_y + selector_h + 18,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, hint);

  // Layout for mapping table and diagram (side by side)
  int panel_y = selector_y + selector_h + 35;
  int panel_h = 300;
  int panel_spacing = 20;
  int panel_w = (content_w - panel_spacing) / 2;

  // Button mapping table (left panel)
  int table_x = content_x;
  draw_card_with_shadow(table_x, panel_y, panel_w, panel_h, 8, UI_COLOR_CARD_BG);

  vita2d_font_draw_text(font, table_x + 15, panel_y + 30,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SUBHEADER, "Button Mappings");

  // Table headers
  int row_y = panel_y + 55;
  int col1_x = table_x + 15;
  int col2_x = table_x + panel_w/2 + 10;

  vita2d_font_draw_text(font, col1_x, row_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, "Vita");
  vita2d_font_draw_text(font, col2_x, row_y, UI_COLOR_TEXT_SECONDARY, FONT_SIZE_SMALL, "PS5");
  row_y += 25;

  // Get mappings
  ButtonMapping mappings[20];
  int mapping_count = 0;
  get_button_mappings(context.config.controller_map_id, mappings, &mapping_count);

  // Draw first 8 mappings
  int row_spacing = 24;
  for (int i = 0; i < mapping_count && i < 8; i++) {
    vita2d_font_draw_text(font, col1_x, row_y + (i * row_spacing),
                          UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL, mappings[i].vita_button);
    vita2d_font_draw_text(font, col2_x, row_y + (i * row_spacing),
                          UI_COLOR_TEXT_PRIMARY, FONT_SIZE_SMALL, mappings[i].ps5_button);
  }

  // Vita diagram (right panel) - professional assets with white background
  int diagram_x = content_x + panel_w + panel_spacing;
  draw_card_with_shadow(diagram_x, panel_y, panel_w, panel_h, 8, RGBA8(255, 255, 255, 255));

  vita2d_font_draw_text(font, diagram_x + 15, panel_y + 30,
                        RGBA8(0, 0, 0, 255), FONT_SIZE_SUBHEADER, "Vita Layout");

  // Draw Vita Front diagram (centered in card)
  if (vita_front) {
    int img_w = vita2d_texture_get_width(vita_front);
    int img_h = vita2d_texture_get_height(vita_front);

    // Scale to fit panel height while maintaining aspect ratio
    float max_h = panel_h - 60;  // Leave space for title
    float max_w = panel_w - 30;  // Leave margins
    float scale = 1.0f;

    if (img_h > max_h || img_w > max_w) {
      float scale_h = max_h / img_h;
      float scale_w = max_w / img_w;
      scale = (scale_h < scale_w) ? scale_h : scale_w;
    }

    int scaled_w = (int)(img_w * scale);
    int scaled_h = (int)(img_h * scale);
    int img_x = diagram_x + (panel_w - scaled_w) / 2;
    int img_y = panel_y + 50 + (panel_h - 50 - scaled_h) / 2;

    vita2d_draw_texture_scale(vita_front, img_x, img_y, scale, scale);
  }
}

/// Draw Settings Tab content (controller-related settings)
static void draw_controller_settings_tab(int content_x, int content_y, int content_w) {
  int item_h = 50;
  int item_spacing = 10;
  int y = content_y;

  // Circle Button Confirm toggle
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     context.config.circle_btn_confirm, controller_state.selected_item == 0);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_PRIMARY, FONT_SIZE_BODY, "Circle Button Confirm");
  y += item_h + item_spacing;

  // Motion Controls - requires gyro backend integration (Phase 4)
  draw_toggle_switch(content_x + content_w - 70, y + (item_h - 30)/2, 60, 30,
                     false, controller_state.selected_item == 1);
  vita2d_font_draw_text(font, content_x + 15, y + item_h/2 + 6,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_BODY, "Motion Controls");
  vita2d_font_draw_text(font, content_x + content_w - 165, y + item_h/2 + 6,
                        UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, "(Coming Soon)");
}

/// Main Controller Configuration screen with tabs
bool draw_controller_config_screen() {
  // Render particle background and navigation sidebar
  update_particles();
  render_particles();
  render_wave_navigation();

  // Main content area (avoiding wave nav sidebar)
  int content_x = WAVE_NAV_WIDTH + 40;
  int content_y = 100;
  int content_w = VITA_WIDTH - WAVE_NAV_WIDTH - 80;

  // Controller title
  vita2d_font_draw_text(font, content_x, 50, UI_COLOR_TEXT_PRIMARY, FONT_SIZE_HEADER, "Controller Configuration");

  // Tab bar
  int tab_bar_y = 70;
  int tab_bar_h = 40;
  draw_tab_bar(content_x, tab_bar_y, content_w, tab_bar_h,
               controller_tab_names, controller_tab_colors,
               CONTROLLER_TAB_COUNT, controller_state.current_tab);

  // Tab content area
  int tab_content_y = tab_bar_y + tab_bar_h + 20;
  int tab_content_w = content_w - 40;
  int tab_content_x = content_x + 20;

  // Draw current tab content
  switch (controller_state.current_tab) {
    case CONTROLLER_TAB_MAPPINGS:
      draw_controller_mappings_tab(tab_content_x, tab_content_y, tab_content_w);
      break;
    case CONTROLLER_TAB_SETTINGS:
      draw_controller_settings_tab(tab_content_x, tab_content_y, tab_content_w);
      break;
  }

  // Control hints at bottom
  int hint_y = VITA_HEIGHT - 25;
  int hint_x = WAVE_NAV_WIDTH + 20;
  const char* hints = (controller_state.current_tab == CONTROLLER_TAB_MAPPINGS) ?
    "L1/R1: Switch Tab | Left/Right: Change Scheme | Circle: Back" :
    "L1/R1: Switch Tab | Up/Down: Navigate | X: Toggle | Circle: Back";
  vita2d_font_draw_text(font, hint_x, hint_y, UI_COLOR_TEXT_TERTIARY, FONT_SIZE_SMALL, hints);

  // === INPUT HANDLING ===

  // L1/R1: Switch tabs
  if (btn_pressed(SCE_CTRL_LTRIGGER)) {
    controller_state.current_tab = (controller_state.current_tab - 1 + CONTROLLER_TAB_COUNT) % CONTROLLER_TAB_COUNT;
    controller_state.selected_item = 0;
  } else if (btn_pressed(SCE_CTRL_RTRIGGER)) {
    controller_state.current_tab = (controller_state.current_tab + 1) % CONTROLLER_TAB_COUNT;
    controller_state.selected_item = 0;
  }

  // Tab-specific navigation
  if (controller_state.current_tab == CONTROLLER_TAB_MAPPINGS) {
    // Left/Right: Cycle through schemes
    if (btn_pressed(SCE_CTRL_LEFT)) {
      int current_id = context.config.controller_map_id;
      if (current_id == 0) {
        context.config.controller_map_id = 99;
      } else if (current_id == 25) {
        context.config.controller_map_id = 7;
      } else if (current_id == 99) {
        context.config.controller_map_id = 25;
      } else if (current_id > 0 && current_id <= 7) {
        context.config.controller_map_id = current_id - 1;
      }
      config_serialize(&context.config);
    } else if (btn_pressed(SCE_CTRL_RIGHT)) {
      int current_id = context.config.controller_map_id;
      if (current_id < 7) {
        context.config.controller_map_id = current_id + 1;
      } else if (current_id == 7) {
        context.config.controller_map_id = 25;
      } else if (current_id == 25) {
        context.config.controller_map_id = 99;
      } else {
        context.config.controller_map_id = 0;
      }
      config_serialize(&context.config);
    }
  } else if (controller_state.current_tab == CONTROLLER_TAB_SETTINGS) {
    // Up/Down: Navigate items (Circle Button Confirm and Motion Controls)
    int max_items = 2;
    if (btn_pressed(SCE_CTRL_UP)) {
      controller_state.selected_item = (controller_state.selected_item - 1 + max_items) % max_items;
    } else if (btn_pressed(SCE_CTRL_DOWN)) {
      controller_state.selected_item = (controller_state.selected_item + 1) % max_items;
    }

    // X: Toggle selected item
    if (btn_pressed(SCE_CTRL_CROSS)) {
      if (controller_state.selected_item == 0) {
        // Circle button confirm toggle
        context.config.circle_btn_confirm = !context.config.circle_btn_confirm;
        config_serialize(&context.config);
      }
      // Item 1 (Motion Controls) is not yet implemented - do nothing
    }
  }

  // Circle: Back to main menu
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    return false;
  }

  return true;
}

// VitaRPS5-style PIN entry constants
#define PIN_DIGIT_COUNT 8
#define PIN_DIGIT_WIDTH 60
#define PIN_DIGIT_HEIGHT 70
#define PIN_DIGIT_SPACING 10
#define PIN_CARD_WIDTH 700
#define PIN_CARD_HEIGHT 450

/// Helper: Reset PIN entry state
void reset_pin_entry() {
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    pin_entry_state.pin_digits[i] = 10;  // 10 = empty
  }
  pin_entry_state.current_digit = 0;
  pin_entry_state.pin_complete = false;
  pin_entry_state.complete_pin = 0;
  show_cursor = true;
  cursor_blink_timer = 0;
  pin_entry_initialized = true;
}

/// Helper: Update cursor blink animation
void update_cursor_blink() {
  cursor_blink_timer++;
  if (cursor_blink_timer >= 30) {  // ~0.5 second at 60fps
    show_cursor = !show_cursor;
    cursor_blink_timer = 0;
  }
}

/// Helper: Check if PIN is complete
bool is_pin_complete() {
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    if (pin_entry_state.pin_digits[i] > 9) {
      return false;
    }
  }
  return true;
}

/// Helper: Convert PIN digits to number
uint32_t pin_to_number() {
  uint32_t pin = 0;
  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    pin = pin * 10 + pin_entry_state.pin_digits[i];
  }
  return pin;
}

/// Helper: Render single PIN digit box
void render_pin_digit(int x, int y, uint32_t digit, bool is_current, bool has_value) {
  // Digit box background
  uint32_t box_color = is_current ? UI_COLOR_PRIMARY_BLUE : RGBA8(0x2C, 0x2C, 0x2E, 255);
  draw_rounded_rectangle(x, y, PIN_DIGIT_WIDTH, PIN_DIGIT_HEIGHT, 4, box_color);

  // Digit text or cursor
  if (has_value && digit <= 9) {
    char digit_text[2] = {'0' + digit, '\0'};
    int text_w = vita2d_font_text_width(font, 40, digit_text);
    int text_x = x + (PIN_DIGIT_WIDTH / 2) - (text_w / 2);
    int text_y = y + (PIN_DIGIT_HEIGHT / 2) + 15;
    vita2d_font_draw_text(font, text_x, text_y, UI_COLOR_TEXT_PRIMARY, 40, digit_text);
  } else if (is_current && show_cursor) {
    // Blinking cursor
    int cursor_x = x + PIN_DIGIT_WIDTH / 2;
    int cursor_y1 = y + 15;
    int cursor_y2 = y + PIN_DIGIT_HEIGHT - 15;
    vita2d_draw_line(cursor_x, cursor_y1, cursor_x, cursor_y2, UI_COLOR_TEXT_PRIMARY);
  }
}

/// Draw VitaRPS5-style PIN entry registration screen
/// @return whether the dialog should keep rendering
bool draw_registration_dialog() {
  // Initialize PIN entry on first render
  if (!pin_entry_initialized) {
    reset_pin_entry();
  }

  // Update cursor blink
  update_cursor_blink();

  // Card centered on screen
  int card_x = (VITA_WIDTH - PIN_CARD_WIDTH) / 2;
  int card_y = (VITA_HEIGHT - PIN_CARD_HEIGHT) / 2;

  draw_card_with_shadow(card_x, card_y, PIN_CARD_WIDTH, PIN_CARD_HEIGHT, 12, UI_COLOR_CARD_BG);

  // Title
  vita2d_font_draw_text(font, card_x + 20, card_y + 50, UI_COLOR_TEXT_PRIMARY, 28,
                        "PS5 Console Registration");

  // Console info (name and IP)
  if (context.active_host) {
    char console_info[128];
    const char* console_name = "Unknown Console";
    const char* host_ip = NULL;

    // Get console name from discovery or registered state
    if (context.active_host->discovery_state && context.active_host->discovery_state->host_name) {
      console_name = context.active_host->discovery_state->host_name;
    } else if (context.active_host->registered_state && context.active_host->registered_state->server_nickname) {
      console_name = context.active_host->registered_state->server_nickname;
    } else if (context.active_host->hostname) {
      console_name = context.active_host->hostname;
    }

    // Get IP from discovery or registered state
    if (context.active_host->discovery_state && context.active_host->discovery_state->host_addr) {
      host_ip = context.active_host->discovery_state->host_addr;
    } else if (context.active_host->registered_state && context.active_host->registered_state->ap_ssid) {
      host_ip = context.active_host->registered_state->ap_ssid;
    }

    if (host_ip) {
      snprintf(console_info, sizeof(console_info), "%s (%s)", console_name, host_ip);
    } else {
      snprintf(console_info, sizeof(console_info), "%s", console_name);
    }
    vita2d_font_draw_text(font, card_x + 20, card_y + 100, UI_COLOR_TEXT_SECONDARY, 20, console_info);
  }

  // Instructions
  vita2d_font_draw_text(font, card_x + 20, card_y + 150, UI_COLOR_TEXT_PRIMARY, 20,
                        "Enter the 8-digit session PIN displayed on your PS5:");

  // PIN digit boxes (centered in card)
  int pin_total_width = (PIN_DIGIT_WIDTH * PIN_DIGIT_COUNT) + (PIN_DIGIT_SPACING * (PIN_DIGIT_COUNT - 1));
  int pin_start_x = card_x + (PIN_CARD_WIDTH - pin_total_width) / 2;
  int pin_y = card_y + 220;

  for (int i = 0; i < PIN_DIGIT_COUNT; i++) {
    int x = pin_start_x + i * (PIN_DIGIT_WIDTH + PIN_DIGIT_SPACING);
    bool is_current = (pin_entry_state.current_digit == i);
    bool has_value = (pin_entry_state.pin_digits[i] <= 9);
    render_pin_digit(x, pin_y, pin_entry_state.pin_digits[i], is_current, has_value);
  }

  // Navigation hints
  vita2d_font_draw_text(font, card_x + 20, card_y + PIN_CARD_HEIGHT - 50, UI_COLOR_TEXT_SECONDARY, 18,
                        "Left/Right: Move   Up/Down: Change digit   Cross: Confirm   Circle: Cancel");

  // Input handling
  if (btn_pressed(SCE_CTRL_LEFT)) {
    if (pin_entry_state.current_digit > 0) {
      pin_entry_state.current_digit--;
    }
  } else if (btn_pressed(SCE_CTRL_RIGHT)) {
    if (pin_entry_state.current_digit < PIN_DIGIT_COUNT - 1) {
      pin_entry_state.current_digit++;
    }
  } else if (btn_pressed(SCE_CTRL_UP)) {
    uint32_t* digit = &pin_entry_state.pin_digits[pin_entry_state.current_digit];
    if (*digit > 9) *digit = 0;
    else *digit = (*digit + 1) % 10;
  } else if (btn_pressed(SCE_CTRL_DOWN)) {
    uint32_t* digit = &pin_entry_state.pin_digits[pin_entry_state.current_digit];
    if (*digit > 9) *digit = 9;
    else *digit = (*digit + 9) % 10;
  } else if (btn_pressed(SCE_CTRL_SQUARE)) {
    // Clear current digit
    pin_entry_state.pin_digits[pin_entry_state.current_digit] = 10;
  } else if (btn_pressed(SCE_CTRL_CROSS)) {
    // Confirm PIN if complete
    if (is_pin_complete()) {
      uint32_t pin = pin_to_number();
      LOGD("User entered PIN: %08u", pin);
      host_register(context.active_host, pin);
      pin_entry_initialized = false;  // Reset for next time
      return false;
    }
  } else if (btn_pressed(SCE_CTRL_CIRCLE)) {
    // Cancel
    pin_entry_initialized = false;  // Reset for next time
    return false;
  }

  return true;
}

char* REMOTEIP_LABEL = "Remote IP";
char* REGISTERED_CONSOLE_LABEL = "Console No.";
char* REMOTEIP;
int CONSOLENUM = -1;
/// Render the current frame of an active stream
/// @return whether the stream should keep rendering
bool draw_stream() {
  // Match ywnico: immediately return false, let video callback handle everything
  // UI loop will skip rendering when is_streaming is true
  if (context.stream.is_streaming) context.stream.is_streaming = false;
  return false;
}

/// Waking screen state
static uint32_t waking_start_time = 0;
static int waking_animation_frame = 0;
static UIScreenType waking_next_screen = UI_SCREEN_TYPE_MAIN;
static const uint32_t WAKING_TIMEOUT_MS = 30000;  // 30 seconds timeout

/// Draw the "Waking up console..." screen with animation
/// @return the next screen to show
UIScreenType draw_waking_screen() {
  // Initialize timer on first call
  if (waking_start_time == 0) {
    waking_start_time = sceKernelGetProcessTimeLow() / 1000;  // Convert to milliseconds
    waking_animation_frame = 0;
    waking_next_screen = UI_SCREEN_TYPE_WAKING;
  }

  // Check timeout
  uint32_t current_time = sceKernelGetProcessTimeLow() / 1000;
  uint32_t elapsed = current_time - waking_start_time;

  if (elapsed > WAKING_TIMEOUT_MS) {
    // Timeout - reset and go back to main
    waking_start_time = 0;
    return UI_SCREEN_TYPE_MAIN;
  }

  // Check if console woke up (became ready for streaming)
  if (context.active_host) {
    bool ready = (context.active_host->type & REGISTERED) &&
                 !(context.active_host->discovery_state &&
                   context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY);

    if (ready) {
      // Console woke up! Reset state and start streaming
      waking_start_time = 0;
      host_stream(context.active_host);
      return UI_SCREEN_TYPE_STREAM;
    }
  }

  // Draw waking screen
  vita2d_set_clear_color(RGBA8(0x1A, 0x1A, 0x2E, 0xFF));

  // Card dimensions
  int card_w = 600;
  int card_h = 300;
  int card_x = (VITA_WIDTH - card_w) / 2;
  int card_y = (VITA_HEIGHT - card_h) / 2;

  // Draw card background
  vita2d_draw_rectangle(card_x, card_y, card_w, card_h, RGBA8(0x2A, 0x2A, 0x3E, 0xFF));

  // Draw card border
  vita2d_draw_rectangle(card_x, card_y, card_w, 2, UI_COLOR_PRIMARY_BLUE);  // Top
  vita2d_draw_rectangle(card_x, card_y + card_h - 2, card_w, 2, UI_COLOR_PRIMARY_BLUE);  // Bottom

  // Draw title
  const char* title = "Waking Up Console";
  vita2d_font_draw_text(font, card_x + 30, card_y + 60, UI_COLOR_TEXT_PRIMARY, 28, title);

  // Draw console name if available
  if (context.active_host && context.active_host->hostname) {
    char console_text[128];
    snprintf(console_text, sizeof(console_text), "Console: %s", context.active_host->hostname);
    vita2d_font_draw_text(font, card_x + 30, card_y + 100, UI_COLOR_TEXT_SECONDARY, 20, console_text);
  }

  // Animate dots (simple animation: 0, 1, 2, 3 dots cycling)
  waking_animation_frame = (current_time / 500) % 4;  // Change every 500ms
  char dots[5] = "";
  for (int i = 0; i < waking_animation_frame; i++) {
    dots[i] = '.';
  }
  dots[waking_animation_frame] = '\0';

  char status_text[64];
  snprintf(status_text, sizeof(status_text), "Please wait%s", dots);
  vita2d_font_draw_text(font, card_x + 30, card_y + 150, UI_COLOR_TEXT_PRIMARY, 22, status_text);

  // Draw timeout progress bar
  int progress_w = card_w - 60;
  int progress_h = 6;
  int progress_x = card_x + 30;
  int progress_y = card_y + card_h - 60;

  // Background
  vita2d_draw_rectangle(progress_x, progress_y, progress_w, progress_h, RGBA8(0x40, 0x40, 0x50, 0xFF));

  // Progress
  float progress_ratio = (float)elapsed / (float)WAKING_TIMEOUT_MS;
  int filled_w = (int)(progress_w * progress_ratio);
  vita2d_draw_rectangle(progress_x, progress_y, filled_w, progress_h, UI_COLOR_PRIMARY_BLUE);

  // Timeout text
  int remaining_sec = (WAKING_TIMEOUT_MS - elapsed) / 1000;
  char timeout_text[32];
  snprintf(timeout_text, sizeof(timeout_text), "Timeout in %d seconds", remaining_sec);
  vita2d_font_draw_text(font, card_x + 30, card_y + card_h - 30, UI_COLOR_TEXT_SECONDARY, 18, timeout_text);

  // Circle to cancel
  if (btn_pressed(SCE_CTRL_CIRCLE)) {
    waking_start_time = 0;
    return UI_SCREEN_TYPE_MAIN;
  }

  return UI_SCREEN_TYPE_WAKING;  // Keep showing waking screen
}

/// Draw the debug messages screen
/// @return whether the dialog should keep rendering
bool draw_messages() {
  vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));
  context.ui_state.next_active_item = -1;

  // initialize mlog_line_offset
  if (!context.ui_state.mlog_last_update) context.ui_state.mlog_line_offset = -1;
  if (context.ui_state.mlog_last_update != context.mlog->last_update) {
    context.ui_state.mlog_last_update = context.mlog->last_update;
    context.ui_state.mlog_line_offset = -1;
  }


  int w = VITA_WIDTH;
  int h = VITA_HEIGHT;

  int left_margin = 12;
  int top_margin = 20;
  int bottom_margin = 20;
  int font_size = 18;
  int line_height = font_size + 2;

  // compute lines to print
  // TODO enable scrolling etc
  int max_lines = (h - top_margin - bottom_margin) / line_height;
  bool overflow = (context.mlog->lines > max_lines);

  int max_line_offset = 0;
  if (overflow) {
    max_line_offset = context.mlog->lines - max_lines + 1;
  } else {
    max_line_offset = 0;
    context.ui_state.mlog_line_offset = -1;
  }
  int line_offset = max_line_offset;

  // update line offset according to mlog_line_offset
  if (context.ui_state.mlog_line_offset >= 0) {
    if (context.ui_state.mlog_line_offset <= max_line_offset) {
      line_offset = context.ui_state.mlog_line_offset;
    }
  }

  int y = top_margin;
  int i_y = 0;
  if (overflow && (line_offset > 0)) {
    char note[100];
    if (line_offset == 1) {
      snprintf(note, 100, "<%d line above>", line_offset);
    } else {
      snprintf(note, 100, "<%d lines above>", line_offset);
    }
    vita2d_font_draw_text(font_mono, left_margin, y,
                          COLOR_GRAY50, font_size,
                          note
                          );
    y += line_height;
    i_y ++;
  }

  int j;
  for (j = line_offset; j < context.mlog->lines; j++) {
    if (i_y > max_lines - 1) break;
    if (overflow && (i_y == max_lines - 1)) {
      if (j < context.mlog->lines - 1) break;
    }
    vita2d_font_draw_text(font_mono, left_margin, y,
                          COLOR_WHITE, font_size,
                          get_message_log_line(context.mlog, j)
                          );
    y += line_height;
    i_y ++;
  }
  if (overflow && (j < context.mlog->lines - 1)) {
    char note[100];
    int lines_below = context.mlog->lines - j - 1;
    if (lines_below == 1) {
      snprintf(note, 100, "<%d line below>", lines_below);
    } else {
      snprintf(note, 100, "<%d lines below>", lines_below);
    }
    vita2d_font_draw_text(font_mono, left_margin, y,
                          COLOR_GRAY50, font_size,
                          note
                          );
    y += line_height;
    i_y ++;
  }

  if (btn_pressed(SCE_CTRL_UP)) {
    if (overflow) {
      int next_offset = line_offset - 1;

      if (next_offset == 1) next_offset = 0;
      if (next_offset == max_line_offset-1) next_offset = max_line_offset-2;

      if (next_offset < 0) next_offset = line_offset;
      context.ui_state.mlog_line_offset = next_offset;
    }
  }
  if (btn_pressed(SCE_CTRL_DOWN)) {
    if (overflow) {
      int next_offset = line_offset + 1;

      if (next_offset == max_line_offset - 1) next_offset = max_line_offset;
      if (next_offset == 1) next_offset = 2;

      if (next_offset > max_line_offset) next_offset = max_line_offset;
      context.ui_state.mlog_line_offset = next_offset;
    }
  }

  if (btn_pressed(SCE_CTRL_CANCEL)) {
    // TODO abort connection if connecting
    vita2d_set_clear_color(RGBA8(0x40, 0x40, 0x40, 0xFF));
    context.ui_state.next_active_item = UI_MAIN_WIDGET_MESSAGES_BTN;
    return false;
  }
  return true;
}

void init_ui() {
  vita2d_init();
  vita2d_set_clear_color(RGBA8(0x40, 0x40, 0x40, 0xFF));
  load_textures();
  init_particles();  // Initialize VitaRPS5 particle background
  font = vita2d_load_font_file("app0:/assets/fonts/Roboto-Regular.ttf");
  font_mono = vita2d_load_font_file("app0:/assets/fonts/RobotoMono-Regular.ttf");
  vita2d_set_vblank_wait(true);

  // Initialize touch screen
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);

  // Set yes/no buttons (circle = yes on Japanese vitas, typically)
  SCE_CTRL_CONFIRM = context.config.circle_btn_confirm ? SCE_CTRL_CIRCLE : SCE_CTRL_CROSS;
  SCE_CTRL_CANCEL  = context.config.circle_btn_confirm ? SCE_CTRL_CROSS : SCE_CTRL_CIRCLE;
  confirm_btn_str = context.config.circle_btn_confirm ? "Circle" : "Cross";
  cancel_btn_str  = context.config.circle_btn_confirm ? "Cross" : "Circle";
}

/// Main UI loop
void draw_ui() {
  init_ui();
  SceCtrlData ctrl;
  memset(&ctrl, 0, sizeof(ctrl));


  UIScreenType screen = UI_SCREEN_TYPE_MAIN;

  load_psn_id_if_needed();

  while (true) {
    // Always read controller input - input thread uses Ext2 variant to access controller independently
    if (!sceCtrlReadBufferPositive(0, &ctrl, 1)) {
      // Try again...
      LOGE("Failed to get controller state");
      continue;
    }
    context.ui_state.old_button_state = context.ui_state.button_state;
    context.ui_state.button_state = ctrl.buttons;

    // Get current touch state
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &(context.ui_state.touch_state_front), 1);


      // handle invalid items
      int this_active_item = context.ui_state.next_active_item;
      if (this_active_item == -1) {
        this_active_item = context.ui_state.active_item;
      }
      if (this_active_item > -1) {
        if (this_active_item & UI_MAIN_WIDGET_HOST_TILE) {
          if (context.num_hosts == 0) {
            // return to toolbar
            context.ui_state.next_active_item = UI_MAIN_WIDGET_SETTINGS_BTN;
          } else {
            int host_j = this_active_item - UI_MAIN_WIDGET_HOST_TILE;
            if (host_j >= context.num_hosts) {
              context.ui_state.next_active_item = UI_MAIN_WIDGET_HOST_TILE | (context.num_hosts-1);
            }
          }
        }
      }

      if (context.ui_state.next_active_item >= 0) {
        context.ui_state.active_item = context.ui_state.next_active_item;
        context.ui_state.next_active_item = -1;
      }

      // Skip ALL rendering when streaming - match ywnico pattern
      if (!context.stream.is_streaming) {
        vita2d_start_drawing();
        vita2d_clear_screen();

        // Draw gradient background if available, otherwise fallback to solid color
        if (background_gradient) {
          vita2d_draw_texture(background_gradient, 0, 0);
        } else {
          vita2d_draw_rectangle(0, 0, VITA_WIDTH, VITA_HEIGHT, UI_COLOR_BACKGROUND);
        }

        // Draw Vita RPS5 logo in top-right corner for professional branding (small with transparency)
        if (vita_rps5_logo) {
          int logo_w = vita2d_texture_get_width(vita_rps5_logo);
          int logo_h = vita2d_texture_get_height(vita_rps5_logo);
          float logo_scale = 0.1f;  // 10% of original size
          int scaled_w = (int)(logo_w * logo_scale);
          int scaled_h = (int)(logo_h * logo_scale);
          int logo_x = VITA_WIDTH - scaled_w - 20;  // 20px margin from right
          int logo_y = 20;  // 20px margin from top

          // Draw with 50% transparency (alpha = 128)
          vita2d_draw_texture_tint_scale(vita_rps5_logo, logo_x, logo_y,
                                         logo_scale, logo_scale,
                                         RGBA8(255, 255, 255, 128));
        }

        // Render the current screen
        if (screen == UI_SCREEN_TYPE_MAIN) {
          screen = draw_main_menu();
        } else if (screen == UI_SCREEN_TYPE_REGISTER_HOST) {
          context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 0);
          if (!draw_registration_dialog()) {
            screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_MESSAGES) {
          if (!draw_messages()) {
            screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_STREAM) {
          if (!draw_stream()) {
            screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_WAKING) {
          screen = draw_waking_screen();
        } else if (screen == UI_SCREEN_TYPE_SETTINGS) {
          if (context.ui_state.active_item != (UI_MAIN_WIDGET_TEXT_INPUT | 2)) {
            context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 1);
          }
          if (!draw_settings()) {
            screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_PROFILE) {
          // Phase 2: Profile & Registration screen
          screen = draw_profile_screen();
        } else if (screen == UI_SCREEN_TYPE_CONTROLLER) {
          // Phase 2: Controller Configuration screen
          if (!draw_controller_config_screen()) {
            screen = UI_SCREEN_TYPE_MAIN;
          }
        }
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
      }
    // }
  }
}
