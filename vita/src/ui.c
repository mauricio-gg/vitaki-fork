// Very very simple homegrown immediate mode GUI
#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/message_dialog.h>
#include <psp2/registrymgr.h>
#include <psp2/ime_dialog.h>
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

// Particle colors (ABGR with alpha for transparency)
#define PARTICLE_COLOR_RED    0x80FF5555  // Semi-transparent red
#define PARTICLE_COLOR_GREEN  0x8055FF55  // Semi-transparent green
#define PARTICLE_COLOR_BLUE   0x805555FF  // Semi-transparent blue
#define PARTICLE_COLOR_ORANGE 0x8055AAFF  // Semi-transparent orange

#define VITA_WIDTH 960
#define VITA_HEIGHT 544

// VitaRPS5 UI Layout Constants
#define WAVE_NAV_WIDTH 130
#define CONTENT_AREA_X WAVE_NAV_WIDTH
#define CONTENT_AREA_WIDTH (VITA_WIDTH - WAVE_NAV_WIDTH)
#define CONSOLE_CARD_WIDTH 400
#define CONSOLE_CARD_HEIGHT 200
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
#define BTN_REGISTER_PATH TEXTURE_PATH "btn_register.png"
#define BTN_REGISTER_ACTIVE_PATH TEXTURE_PATH "btn_register_active.png"
#define BTN_ADD_PATH TEXTURE_PATH "btn_add.png"
#define BTN_ADD_ACTIVE_PATH TEXTURE_PATH "btn_add_active.png"
#define BTN_DISCOVERY_PATH TEXTURE_PATH "btn_discovery.png"
#define BTN_DISCOVERY_ACTIVE_PATH TEXTURE_PATH "btn_discovery_active.png"
#define BTN_DISCOVERY_OFF_PATH TEXTURE_PATH "btn_discovery_off.png"
#define BTN_DISCOVERY_OFF_ACTIVE_PATH \
  TEXTURE_PATH "btn_discovery_off_active.png"
#define BTN_SETTINGS_PATH TEXTURE_PATH "btn_settings.png"
#define BTN_SETTINGS_ACTIVE_PATH TEXTURE_PATH "btn_settings_active.png"
#define BTN_MESSAGES_PATH TEXTURE_PATH "btn_messages.png"
#define BTN_MESSAGES_ACTIVE_PATH TEXTURE_PATH "btn_messages_active.png"
#define IMG_PS4_PATH TEXTURE_PATH "ps4.png"
#define IMG_PS4_OFF_PATH TEXTURE_PATH "ps4_off.png"
#define IMG_PS4_REST_PATH TEXTURE_PATH "ps4_rest.png"
#define IMG_PS5_PATH TEXTURE_PATH "ps5.png"
#define IMG_PS5_OFF_PATH TEXTURE_PATH "ps5_off.png"
#define IMG_PS5_REST_PATH TEXTURE_PATH "ps5_rest.png"
#define IMG_DISCOVERY_HOST TEXTURE_PATH "discovered_host.png"
#define IMG_HEADER_LOGO_PATH TEXTURE_PATH "header_logo.png"

vita2d_font* font;
vita2d_font* font_mono;
vita2d_texture *btn_register, *btn_register_active, *btn_add, *btn_add_active,
    *btn_discovery, *btn_discovery_active, *btn_discovery_off,
    *btn_discovery_off_active, *btn_settings, *btn_settings_active,
    *btn_messages, *btn_messages_active, *img_ps4,
    *img_ps4_off, *img_ps4_rest, *img_ps5, *img_ps5_off, *img_ps5_rest,
    *img_header, *img_discovery_host;

// VitaRPS5 UI textures
vita2d_texture *symbol_triangle, *symbol_circle, *symbol_ex, *symbol_square;
vita2d_texture *wave_top, *wave_bottom;
vita2d_texture *ellipse_green, *ellipse_yellow, *ellipse_red;
vita2d_texture *button_add_new, *console_card_bg;
vita2d_texture *icon_play, *icon_settings, *icon_controller, *icon_profile;

// Particle system state
static Particle particles[PARTICLE_COUNT];
static bool particles_initialized = false;

// Wave navigation state
#define WAVE_NAV_WIDTH 130
#define WAVE_NAV_ICON_SIZE 48
#define WAVE_NAV_ICON_X 41
#define WAVE_NAV_ICON_START_Y 180
#define WAVE_NAV_ICON_SPACING 60

static int selected_nav_icon = 0;  // 0=Play, 1=Settings, 2=Controller, 3=Profile
static float wave_animation_time = 0.0f;

// Console card system
#define CONSOLE_CARD_WIDTH 400
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
  UI_SCREEN_TYPE_ADD_HOST,
  UI_SCREEN_TYPE_EDIT_HOST,
  UI_SCREEN_TYPE_STREAM,
  UI_SCREEN_TYPE_SETTINGS,
  UI_SCREEN_TYPE_MESSAGES,
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

// Particle system functions

/// Initialize particle system with random positions and velocities
void init_particles() {
  if (particles_initialized) return;

  srand((unsigned int)sceKernelGetProcessTimeWide());

  for (int i = 0; i < PARTICLE_COUNT; i++) {
    particles[i].x = (float)(rand() % VITA_WIDTH);
    particles[i].y = (float)(rand() % VITA_HEIGHT);
    particles[i].vx = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f;  // Slight horizontal drift
    particles[i].vy = -((float)(rand() % 100) / 100.0f + 0.5f) * 0.8f; // Upward (negative Y)
    particles[i].scale = 0.15f + ((float)(rand() % 100) / 100.0f) * 0.25f;
    particles[i].rotation = (float)(rand() % 360);
    particles[i].rotation_speed = ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;
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

    // Wrap around screen edges
    if (particles[i].y < -50) {
      particles[i].y = VITA_HEIGHT + 50;  // Respawn at bottom
      particles[i].x = (float)(rand() % VITA_WIDTH);
    }
    if (particles[i].x < -50) particles[i].x = VITA_WIDTH + 50;
    if (particles[i].x > VITA_WIDTH + 50) particles[i].x = -50;
  }
}

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

/// Render VitaRPS5 wave navigation sidebar
void render_wave_navigation() {
  // Draw wave background textures
  if (wave_top) {
    vita2d_draw_texture(wave_top, 0, 0);
  }
  if (wave_bottom) {
    vita2d_draw_texture(wave_bottom, 0, VITA_HEIGHT - vita2d_texture_get_height(wave_bottom));
  }

  // Navigation icons array
  vita2d_texture* nav_icons[4] = {
    icon_play, icon_settings, icon_controller, icon_profile
  };

  // Draw navigation icons with wave animation
  wave_animation_time += 0.05f;  // Update animation

  for (int i = 0; i < 4; i++) {
    if (!nav_icons[i]) continue;

    int y = WAVE_NAV_ICON_START_Y + (i * WAVE_NAV_ICON_SPACING);
    float wave_offset = sinf(wave_animation_time + i * 0.5f) * 3.0f;

    // Selection highlight (PlayStation Blue circle)
    if (i == selected_nav_icon) {
      draw_circle(WAVE_NAV_ICON_X, y + wave_offset, 28, UI_COLOR_PRIMARY_BLUE);
    }

    // Draw icon (centered at WAVE_NAV_ICON_X, y + wave_offset)
    int icon_w = vita2d_texture_get_width(nav_icons[i]);
    int icon_h = vita2d_texture_get_height(nav_icons[i]);
    float scale = (float)WAVE_NAV_ICON_SIZE / (float)(icon_w > icon_h ? icon_w : icon_h);

    vita2d_draw_texture_scale(nav_icons[i],
      WAVE_NAV_ICON_X - (icon_w * scale / 2.0f),
      y + wave_offset - (icon_h * scale / 2.0f),
      scale, scale);
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

  // Selection highlight (PlayStation Blue border)
  if (selected) {
    draw_rounded_rectangle(x - 4, y - 4, CONSOLE_CARD_WIDTH + 8, CONSOLE_CARD_HEIGHT + 8, 12, UI_COLOR_PRIMARY_BLUE);
  }

  // Card background with shadow
  draw_card_with_shadow(x, y, CONSOLE_CARD_WIDTH, CONSOLE_CARD_HEIGHT, 12, UI_COLOR_CARD_BG);

  // Console state glow (if Ready or Standby)
  if (console->state == 1) {  // Ready - Blue glow
    draw_rounded_rectangle(x - 2, y - 2, CONSOLE_CARD_WIDTH + 4, CONSOLE_CARD_HEIGHT + 4, 10,
      RGBA8(52, 144, 255, 120));  // PlayStation Blue with transparency
  } else if (console->state == 2) {  // Standby - Yellow glow
    draw_rounded_rectangle(x - 2, y - 2, CONSOLE_CARD_WIDTH + 4, CONSOLE_CARD_HEIGHT + 4, 10,
      RGBA8(255, 193, 7, 120));  // Yellow with transparency
  }

  // PS5 logo (centered, 1/3 from top)
  bool is_ps5 = console->host && chiaki_target_is_ps5(console->host->target);
  vita2d_texture* logo = is_ps5 ? img_ps5 : img_ps4;
  if (logo) {
    int logo_w = vita2d_texture_get_width(logo);
    int logo_h = vita2d_texture_get_height(logo);
    int logo_x = x + (CONSOLE_CARD_WIDTH / 2) - (logo_w / 2);
    int logo_y = y + (CONSOLE_CARD_HEIGHT / 3) - (logo_h / 2);
    vita2d_draw_texture(logo, logo_x, logo_y);
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

  // State text ("Ready" / "Standby")
  const char* state_text = NULL;
  uint32_t state_color = UI_COLOR_TEXT_SECONDARY;
  if (console->state == 1) {
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

/// Render console cards in grid layout
void render_console_grid() {
  int screen_center_x = VITA_WIDTH / 2;
  int content_area_x = WAVE_NAV_WIDTH + ((VITA_WIDTH - WAVE_NAV_WIDTH) / 2);

  // Header text
  vita2d_font_draw_text(font, content_area_x - 150, 100, UI_COLOR_TEXT_PRIMARY, 24,
    "Which do you want to connect?");

  int num_hosts = 0;
  ConsoleCardInfo cards[MAX_NUM_HOSTS];

  // Map all vitaki hosts to console cards
  for (int i = 0; i < MAX_NUM_HOSTS; i++) {
    if (context.hosts[i]) {
      map_host_to_console_card(context.hosts[i], &cards[num_hosts]);
      num_hosts++;
    }
  }

  // Render console cards
  for (int i = 0; i < num_hosts; i++) {
    int card_x = content_area_x - (CONSOLE_CARD_WIDTH / 2);
    int card_y = CONSOLE_CARD_START_Y + (i * CONSOLE_CARD_SPACING);

    bool selected = (i == selected_console_index);
    render_console_card(&cards[i], card_x, card_y, selected);
  }

  // "Add New" button at bottom
  if (button_add_new) {
    int btn_w = vita2d_texture_get_width(button_add_new);
    int btn_x = content_area_x - (btn_w / 2);
    int btn_y = CONSOLE_CARD_START_Y + (num_hosts * CONSOLE_CARD_SPACING) + 20;
    vita2d_draw_texture(button_add_new, btn_x, btn_y);
  }
}

/// Load all textures required for rendering the UI
void load_textures() {
  btn_add = vita2d_load_PNG_file(BTN_ADD_PATH);
  btn_add_active = vita2d_load_PNG_file(BTN_ADD_ACTIVE_PATH);
  btn_discovery = vita2d_load_PNG_file(BTN_DISCOVERY_PATH);
  btn_discovery_active = vita2d_load_PNG_file(BTN_DISCOVERY_ACTIVE_PATH);
  btn_discovery_off = vita2d_load_PNG_file(BTN_DISCOVERY_OFF_PATH);
  btn_discovery_off_active =
      vita2d_load_PNG_file(BTN_DISCOVERY_OFF_ACTIVE_PATH);
  btn_register = vita2d_load_PNG_file(BTN_REGISTER_PATH);
  btn_register_active = vita2d_load_PNG_file(BTN_REGISTER_ACTIVE_PATH);
  btn_settings = vita2d_load_PNG_file(BTN_SETTINGS_PATH);
  btn_settings_active = vita2d_load_PNG_file(BTN_SETTINGS_ACTIVE_PATH);
  btn_messages = vita2d_load_PNG_file(BTN_MESSAGES_PATH);
  btn_messages_active = vita2d_load_PNG_file(BTN_MESSAGES_ACTIVE_PATH);
  img_header = vita2d_load_PNG_file(IMG_HEADER_LOGO_PATH);
  img_ps4 = vita2d_load_PNG_file(IMG_PS4_PATH);
  img_ps4_off = vita2d_load_PNG_file(IMG_PS4_OFF_PATH);
  img_ps4_rest = vita2d_load_PNG_file(IMG_PS4_REST_PATH);
  img_ps5 = vita2d_load_PNG_file(IMG_PS5_PATH);
  img_ps5_off = vita2d_load_PNG_file(IMG_PS5_OFF_PATH);
  img_ps5_rest = vita2d_load_PNG_file(IMG_PS5_REST_PATH);
  img_discovery_host = vita2d_load_PNG_file(IMG_DISCOVERY_HOST);

  // Load VitaRPS5 UI assets
  symbol_triangle = vita2d_load_PNG_file("app0:/assets/vitarps5/symbol_triangle.png");
  symbol_circle = vita2d_load_PNG_file("app0:/assets/vitarps5/symbol_circle.png");
  symbol_ex = vita2d_load_PNG_file("app0:/assets/vitarps5/symbol_ex.png");
  symbol_square = vita2d_load_PNG_file("app0:/assets/vitarps5/symbol_square.png");
  wave_top = vita2d_load_PNG_file("app0:/assets/vitarps5/wave_top.png");
  wave_bottom = vita2d_load_PNG_file("app0:/assets/vitarps5/wave_bottom.png");
  ellipse_green = vita2d_load_PNG_file("app0:/assets/vitarps5/ellipse_green.png");
  ellipse_yellow = vita2d_load_PNG_file("app0:/assets/vitarps5/ellipse_yellow.png");
  ellipse_red = vita2d_load_PNG_file("app0:/assets/vitarps5/ellipse_red.png");
  button_add_new = vita2d_load_PNG_file("app0:/assets/vitarps5/button_add_new.png");
  console_card_bg = vita2d_load_PNG_file("app0:/assets/vitarps5/console_card.png");

  // Load navigation icons
  icon_play = vita2d_load_PNG_file("app0:/assets/vitarps5/icon_play.png");
  icon_settings = vita2d_load_PNG_file("app0:/assets/vitarps5/icon_settings.png");
  icon_controller = vita2d_load_PNG_file("app0:/assets/vitarps5/icon_controller.png");
  icon_profile = vita2d_load_PNG_file("app0:/assets/vitarps5/icon_profile.png");
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

/// Draw a button into the header bar
/// @param id           Numerical identifier of the button
/// @param x_offset     Offset on the X axis in pixels from the start of the
///                     header bar
/// @param default_img  Image to render when the button is not active
/// @param active_img   Image to render when the button is active
/// @return whether the button is pressed or not
bool header_button(MainWidgetId id, int x_offset, vita2d_texture* default_img,
                   vita2d_texture* active_img) {
  int active_id = context.ui_state.active_item;
  bool is_active = active_id == id;

  // Draw button
  vita2d_texture* img = is_active ? active_img : default_img;
  int w = vita2d_texture_get_width(img);
  int h = vita2d_texture_get_height(img);
  int x = HEADER_BAR_X + x_offset;
  // Buttons are bottom-aligned to the header bar
  int y = HEADER_BAR_Y + HEADER_BAR_H - h;
  vita2d_draw_texture(img, x, y);

  // Navigation handling
  int btn = context.ui_state.button_state;
  if (is_active) {
    if (btn_pressed(SCE_CTRL_DOWN) && context.num_hosts > 0) {
      // Select first host tile
      context.ui_state.next_active_item = UI_MAIN_WIDGET_HOST_TILE;
    } else if (btn_pressed(SCE_CTRL_LEFT)) {
      // Select button to the left
      context.ui_state.next_active_item = MAX(0, active_id - 1);
    } else if (btn_pressed(SCE_CTRL_RIGHT)) {
      // Select button to the right
      context.ui_state.next_active_item =
          MIN(UI_MAIN_WIDGET_SETTINGS_BTN, active_id + 1);
    }
  }

  if (is_touched(x, y, w, h)) {
    // Focus the button if it's touched, no matter the button state
    context.ui_state.next_active_item = id;
    return true;
  }
  return is_active && btn_pressed(SCE_CTRL_CONFIRM);
}
uint16_t IMEInput[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
bool showingIME = false;
char* text_input(MainWidgetId id, int x, int y, int w, int h, char* label,
                char* value, int max_len) {
  bool is_active = context.ui_state.active_item == id;
  if (is_active) {
    vita2d_draw_rectangle(x + 300 - 3, y - 3, w + 6, h + 6, COLOR_ACTIVE);
    if (btn_pressed(SCE_CTRL_CONFIRM)) {
      SceImeDialogParam param;

      sceImeDialogParamInit(&param);
			param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
			param.languagesForced = SCE_TRUE;
			param.type = SCE_IME_TYPE_DEFAULT;
			param.option = SCE_IME_OPTION_NO_ASSISTANCE;
			param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
      uint16_t IMELabel[label != NULL ? sizeof(label) + 1 : sizeof("Text")];
      utf8_to_utf16(label != NULL ? label : "Text", IMELabel);
			param.title = IMELabel;
			param.maxTextLength = SCE_IME_DIALOG_MAX_TEXT_LENGTH;
      if (value != NULL) {
        uint16_t IMEValue[sizeof(value)];
        utf8_to_utf16(value, IMEValue);
			  param.initialText = IMEValue;
      }
			param.inputTextBuffer = IMEInput;

      showingIME = true;
      sceImeDialogInit(&param);
    }
  }
  vita2d_draw_rectangle(x + 300, y, w, h, COLOR_BLACK);
  // vita2d_draw_texture(icon, x + 20, y + h / 2);
  if (label != NULL) vita2d_font_draw_text(font, x, y + h / 1.5, COLOR_WHITE, 40, label);
  if (value != NULL) vita2d_font_draw_text(font, x + 300, y + h / 1.5, COLOR_WHITE, 40, value);
  
  if (is_active && showingIME) {
    if (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED) {
      showingIME = false;
      SceImeDialogResult result={};
      sceImeDialogGetResult(&result);
      sceImeDialogTerm();
      if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {

        uint16_t*last_input = (result.button == SCE_IME_DIALOG_BUTTON_ENTER) ? IMEInput:u"";
        char IMEResult[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
        utf16_to_utf8(IMEInput, IMEResult);
        LOGD("IME returned %s", IMEResult);
        return strdup(IMEResult);
      }
    }
  }
  return NULL;

  // TODO: Render label + icon
  // TODO: Render input border
  // TODO: Render value
  // TODO: If touched or X pressed, open up IME dialogue and update value
}

long int number_input(MainWidgetId id, int x, int y, int w, int h, char* label, long int value) {
  // -1 => blank

  // int to str
  char value_str[100];
  if (value == -1) {
    value_str[0] = 0; // empty string
  } else {
    snprintf(value_str, 100, "%d", value);
  }

  bool is_active = context.ui_state.active_item == id;
  if (is_active) {
    vita2d_draw_rectangle(x + 300 - 3, y - 3, w + 6, h + 6, COLOR_ACTIVE);
    if (btn_pressed(SCE_CTRL_CONFIRM)) {
      SceImeDialogParam param;

      sceImeDialogParamInit(&param);
			param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
			param.languagesForced = SCE_TRUE;
			param.type = SCE_IME_TYPE_NUMBER;
			param.option = SCE_IME_OPTION_NO_ASSISTANCE;
			param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
      uint16_t IMELabel[label != NULL ? sizeof(label) + 1 : sizeof("Text")];
      utf8_to_utf16(label != NULL ? label : "Text", IMELabel);
			param.title = IMELabel;
			param.maxTextLength = SCE_IME_DIALOG_MAX_TEXT_LENGTH;
      uint16_t IMEValue[sizeof(value_str)];
      utf8_to_utf16(value_str, IMEValue);
      param.initialText = IMEValue;
			param.inputTextBuffer = IMEInput;

      showingIME = true;
      sceImeDialogInit(&param);
    }
  }
  vita2d_draw_rectangle(x + 300, y, w, h, COLOR_BLACK);
  // vita2d_draw_texture(icon, x + 20, y + h / 2);
  if (label != NULL) vita2d_font_draw_text(font, x, y + h / 1.5, COLOR_WHITE, 40, label);
  vita2d_font_draw_text(font, x + 300, y + h / 1.5, COLOR_WHITE, 40, value_str);

  if (is_active && showingIME) {
    if (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED) {
      showingIME = false;
      SceImeDialogResult result={};
      sceImeDialogGetResult(&result);
      sceImeDialogTerm();
      if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {

        uint16_t*last_input = (result.button == SCE_IME_DIALOG_BUTTON_ENTER) ? IMEInput:u"";
        char IMEResult[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
        utf16_to_utf8(IMEInput, IMEResult);
        long int num = strtol(strdup(IMEResult), NULL, 10);
        LOGD("IME returned %s -> %d", IMEResult, num);
        return num;
      }
    }
  }

  // TODO: Render label + icon
  // TODO: Render input border
  // TODO: Render value
  // TODO: If touched or X pressed, open up IME dialogue and return value
  return -1;
}

int choice_input(int x, int y, int w, int h, char* label, vita2d_texture* icon,
                 char** choice_labels, size_t num_choices, uint8_t cur_choice) {
  // TODO: Render label + icon
  // TODO: Render input border
  // TODO: Render value
  // TODO: Button/touch handling to update choice
  return -1;
}

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

/// Draw the header bar for the main menu screen
/// @return the screen to draw during the next cycle
UIScreenType draw_header_bar() {
  // Modern header with rounded corners and shadow
  int w = vita2d_texture_get_width(img_header);
  int h = vita2d_texture_get_height(img_header);
  vita2d_draw_texture(img_header, HEADER_BAR_X - w,
                      HEADER_BAR_Y - (h - HEADER_BAR_H) / 2);

  // Draw header bar with modern styling
  draw_card_with_shadow(HEADER_BAR_X, HEADER_BAR_Y, HEADER_BAR_W, HEADER_BAR_H, 6, UI_COLOR_CARD_BG);

  // Header buttons
  UIScreenType next_screen = UI_SCREEN_TYPE_MAIN;
  if (header_button(UI_MAIN_WIDGET_ADD_HOST_BTN, 315, btn_add,
                    btn_add_active)) {
    next_screen = UI_SCREEN_TYPE_ADD_HOST;
  }
  if (header_button(UI_MAIN_WIDGET_REGISTER_BTN, 475, btn_register,
                    btn_register_active)) {
    // TODO what was this button supposed to do??
    //next_screen = UI_SCREEN_TYPE_REGISTER;
  }
  bool discovery_on = context.discovery_enabled;
  if (header_button(
          UI_MAIN_WIDGET_DISCOVERY_BTN, 639,
          discovery_on ? btn_discovery : btn_discovery_off,
          discovery_on ? btn_discovery_active : btn_discovery_off_active)) {
    if (discovery_on) {
      stop_discovery();
    } else {
      start_discovery(NULL, NULL);
    }
  }
  if (header_button(UI_MAIN_WIDGET_MESSAGES_BTN, 684, btn_messages,
                    btn_messages_active)) {
    next_screen = UI_SCREEN_TYPE_MESSAGES;
  }
  if (header_button(UI_MAIN_WIDGET_SETTINGS_BTN, 729, btn_settings,
                    btn_settings_active)) {
    next_screen = UI_SCREEN_TYPE_SETTINGS;
  }
  return next_screen;
}

/// Draw the main menu screen with the list of hosts and header bar
/// @return the screen to draw during the next cycle
UIScreenType draw_main_menu() {
  // Update and render VitaRPS5 particle background
  update_particles();
  render_particles();

  // Render VitaRPS5 wave navigation sidebar
  render_wave_navigation();

  // Handle wave navigation input (L1/R1 to cycle through nav items)
  if (btn_pressed(SCE_CTRL_LTRIGGER)) {
    selected_nav_icon = (selected_nav_icon - 1 + 4) % 4;
  } else if (btn_pressed(SCE_CTRL_RTRIGGER)) {
    selected_nav_icon = (selected_nav_icon + 1) % 4;
  }

  // Handle navigation selection with Triangle
  UIScreenType next_screen = UI_SCREEN_TYPE_MAIN;
  if (btn_pressed(SCE_CTRL_TRIANGLE)) {
    switch (selected_nav_icon) {
      case 0: next_screen = UI_SCREEN_TYPE_MAIN; break;       // Play
      case 1: next_screen = UI_SCREEN_TYPE_SETTINGS; break;   // Settings
      case 2: next_screen = UI_SCREEN_TYPE_REGISTER_HOST; break;  // Controller (placeholder)
      case 3: next_screen = UI_SCREEN_TYPE_REGISTER_HOST; break;  // Profile (placeholder)
    }
  }

  if (next_screen != UI_SCREEN_TYPE_MAIN) {
    return next_screen;
  }

  // Render VitaRPS5 console cards instead of host tiles
  render_console_grid();

  // Handle console card navigation (Up/Down to select cards)
  int num_hosts = 0;
  for (int i = 0; i < MAX_NUM_HOSTS; i++) {
    if (context.hosts[i]) num_hosts++;
  }

  if (num_hosts > 0) {
    if (btn_pressed(SCE_CTRL_UP)) {
      selected_console_index = (selected_console_index - 1 + num_hosts) % num_hosts;
    } else if (btn_pressed(SCE_CTRL_DOWN)) {
      selected_console_index = (selected_console_index + 1) % num_hosts;
    }

    // Handle card actions (Cross to connect, Square to wake)
    if (btn_pressed(SCE_CTRL_CROSS) || btn_pressed(SCE_CTRL_CIRCLE)) {
      // Set active host from selected card
      int host_idx = 0;
      for (int i = 0; i < MAX_NUM_HOSTS; i++) {
        if (context.hosts[i]) {
          if (host_idx == selected_console_index) {
            context.active_host = context.hosts[i];

            bool discovered = (context.active_host->type & DISCOVERED) && (context.active_host->discovery_state);
            bool registered = context.active_host->type & REGISTERED;
            bool at_rest = discovered && context.active_host->discovery_state &&
                           context.active_host->discovery_state->state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY;

            if (discovered && !at_rest && registered) {
              next_screen = UI_SCREEN_TYPE_MESSAGES;
              host_stream(context.active_host);
            } else if (at_rest) {
              host_wakeup(context.active_host);
            } else if (!registered) {
              next_screen = UI_SCREEN_TYPE_REGISTER_HOST;
            }
            break;
          }
          host_idx++;
        }
      }
    } else if (btn_pressed(SCE_CTRL_SQUARE)) {
      // Wake selected console
      int host_idx = 0;
      for (int i = 0; i < MAX_NUM_HOSTS; i++) {
        if (context.hosts[i]) {
          if (host_idx == selected_console_index) {
            host_wakeup(context.hosts[i]);
            break;
          }
          host_idx++;
        }
      }
    }
  } else {
    // No hosts - show empty state message
    int msg_x = WAVE_NAV_WIDTH + ((VITA_WIDTH - WAVE_NAV_WIDTH) / 2) - 200;
    vita2d_font_draw_text(font, msg_x, VITA_HEIGHT / 2, UI_COLOR_TEXT_SECONDARY, 20,
      "No consoles found. Press Start to discover.");
  }

  // Handle "Add New" button (Start button to trigger discovery)
  if (btn_pressed(SCE_CTRL_START)) {
    if (!context.discovery_enabled) {
      start_discovery(NULL, NULL);
    }
  }

  // VitaRPS5 UI control hints at bottom
  int hint_y = VITA_HEIGHT - 25;
  int hint_x = WAVE_NAV_WIDTH + 20;
  vita2d_font_draw_text(font, hint_x, hint_y, UI_COLOR_TEXT_TERTIARY, 16,
    "L1/R1: Nav | Up/Down: Select | Cross: Connect | Square: Wake | Start: Discover");


  return next_screen;
}
char* PSNID_LABEL = "PSN ID";
char* CONTROLLER_MAP_ID_LABEL = "Controller map";
/// Draw the settings form
/// @return whether the dialog should keep rendering
bool draw_settings() {
  // Modern settings card
  int card_x = 40;
  int card_y = 40;
  int card_w = 880;
  int card_h = 460;
  draw_card_with_shadow(card_x, card_y, card_w, card_h, 12, UI_COLOR_CARD_BG);

  // Title
  vita2d_font_draw_text(font, card_x + 30, card_y + 45, UI_COLOR_TEXT_PRIMARY, 26,
                        "Settings");

  int font_size = 18;

  char* psntext = text_input(UI_MAIN_WIDGET_TEXT_INPUT | 1, card_x + 30, card_y + 80, 600, 70, PSNID_LABEL, context.config.psn_account_id, 20);
  if (psntext != NULL) {
    free(context.config.psn_account_id);
    context.config.psn_account_id = psntext;
    load_psn_id_if_needed();
    config_serialize(&context.config);
  }
  vita2d_font_draw_text(font, card_x + 650, card_y + 100, UI_COLOR_TEXT_TERTIARY, 16,
                        "Press Start to reset"
                        );
  vita2d_font_draw_text(font, card_x + 650, card_y + 118, UI_COLOR_TEXT_TERTIARY, 16,
                        "from device account"
                        );

  int ctrlmap_id = number_input(UI_MAIN_WIDGET_TEXT_INPUT | 2, card_x + 30, card_y + 165, 600, 70, CONTROLLER_MAP_ID_LABEL, context.config.controller_map_id);
  if (ctrlmap_id != -1) {
    context.config.controller_map_id = ctrlmap_id;
    config_serialize(&context.config);
  }

  // Controller map info in condensed format
  int info_x = card_x + 30;
  int info_y = card_y + 260;
  int info_y_delta = 18;
  int help_size = 15;

  vita2d_font_draw_text(font, info_x, info_y, UI_COLOR_TEXT_SECONDARY, help_size,
                        "Controller Maps: 0-7,25,99 (official remote play layouts)"
                        );
  vita2d_font_draw_text(font, info_x, info_y + 1*info_y_delta, UI_COLOR_TEXT_TERTIARY, help_size,
                        "0: L2/R2 rear upper, L3/R3 rear lower  |  1: L2/R2 front upper, L3/R3 front lower"
                        );
  vita2d_font_draw_text(font, info_x, info_y + 2*info_y_delta, UI_COLOR_TEXT_TERTIARY, help_size,
                        "2-3: Various combinations  |  4-5: Reduced button layouts"
                        );
  vita2d_font_draw_text(font, info_x, info_y + 3*info_y_delta, UI_COLOR_TEXT_TERTIARY, help_size,
                        "6-7: Front corners only  |  25: No touchpad  |  99: Custom vitaki layout"
                        );
  vita2d_font_draw_text(font, info_x, info_y + 4*info_y_delta, UI_COLOR_TEXT_TERTIARY, help_size,
                        "Add 100 to swap L2<->L3 and R2<->R3"
                        );
  vita2d_font_draw_text(font, info_x, info_y + 6*info_y_delta, UI_COLOR_PRIMARY_BLUE, help_size,
                        "Tip: Start + Select = PS button in all maps"
                        );

  if (btn_pressed(SCE_CTRL_DOWN)) {
    context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 2);
  }
  if (btn_pressed(SCE_CTRL_UP)) {
    context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 1);
  }

  if (btn_pressed(SCE_CTRL_START)) {
    if (context.config.psn_account_id) {
      free(context.config.psn_account_id);
    }
    context.config.psn_account_id = NULL;
    load_psn_id_if_needed();
    config_serialize(&context.config);
  }

  // Bottom tooltip
  vita2d_font_draw_textf(font, 10, VITA_HEIGHT - 18, UI_COLOR_TEXT_TERTIARY, 16,
                         "%s: Back to main menu", cancel_btn_str);

  if (btn_pressed(SCE_CTRL_CANCEL)) {
    context.ui_state.next_active_item = UI_MAIN_WIDGET_SETTINGS_BTN;
    return false;
  }
  return true;
}

long int LINK_CODE = -1;
char* LINK_CODE_LABEL = "Registration code";

/// Draw the form to register a host
/// @return whether the dialog should keep rendering
bool draw_registration_dialog() {
  // Modern registration card
  int card_x = 80;
  int card_y = 80;
  int card_w = 800;
  int card_h = 380;
  draw_card_with_shadow(card_x, card_y, card_w, card_h, 12, UI_COLOR_CARD_BG);

  // Title
  vita2d_font_draw_text(font, card_x + 30, card_y + 50, UI_COLOR_TEXT_PRIMARY, 28,
                        "Register Console");

  // Draw instructions with modern styling
  int info_font_size = 20;
  int info_x = card_x + 30;
  int info_y = card_y + 120;
  int info_y_delta = 28;
  vita2d_font_draw_text(font, info_x, info_y, UI_COLOR_TEXT_SECONDARY, info_font_size,
                        "On your PS console, go to Settings > System > Remote Play and select Pair Device,"
                        );
  vita2d_font_draw_text(font, info_x, info_y + info_y_delta, UI_COLOR_TEXT_SECONDARY, info_font_size,
                        "then enter the corresponding 8-digit code here (no spaces)."
                        );

  // Instructions at bottom
  int font_size = 18;
  int tooltip_x = 10;
  int tooltip_y = VITA_HEIGHT - font_size;
  vita2d_font_draw_textf(font, tooltip_x, tooltip_y, UI_COLOR_TEXT_TERTIARY, font_size,
                         "Triangle: Register (clear any current registration);  %s: Exit without registering.", cancel_btn_str
                        );

  long int link_code = number_input(UI_MAIN_WIDGET_TEXT_INPUT | 0, 30, 30, 600, 80, LINK_CODE_LABEL, LINK_CODE);
  if (link_code >= 0) {
    LINK_CODE = link_code;
  }

  if (btn_pressed(SCE_CTRL_TRIANGLE)) {
    if (LINK_CODE >= 0) {
      LOGD("User input link code: %d", LINK_CODE);
      host_register(context.active_host, LINK_CODE);
    } else {
      LOGD("User exited registration screen without inputting link code");
    }
  }
  if (btn_pressed(SCE_CTRL_CANCEL) || btn_pressed(SCE_CTRL_TRIANGLE)) {
    LINK_CODE = -1;
    context.ui_state.next_active_item = UI_MAIN_WIDGET_SETTINGS_BTN;
    return false;
  }
  return true;
}

char* REMOTEIP_LABEL = "Remote IP";
char* REGISTERED_CONSOLE_LABEL = "Console No.";
char* REMOTEIP;
int CONSOLENUM = -1;

/// Draw the form to manually add a new host
/// @return whether the dialog should keep rendering
bool draw_add_host_dialog() {

  // check if any registered host exists. If not, display a message and no UI.
  bool registered_host_exists = false;
  for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
    VitaChiakiHost* rhost = context.config.registered_hosts[rhost_idx];
    if (rhost) {
      registered_host_exists = true;
      break;
    }
  }

  if (!registered_host_exists) {
    int font_size = 24;
    int info_y_delta = 31;
    int info_x = 30;
    int info_y = 40;

    vita2d_font_draw_text(font, info_x, info_y, COLOR_WHITE, font_size,
                          "No registered hosts found."
                          );
    vita2d_font_draw_text(font, info_x, info_y + info_y_delta, COLOR_WHITE, font_size,
                          "Pair to a console on a local network first."
                          );

    if (btn_pressed(SCE_CTRL_CANCEL) | btn_pressed(SCE_CTRL_CONFIRM)) {
      context.ui_state.next_active_item = UI_MAIN_WIDGET_ADD_HOST_BTN;
      REMOTEIP = "";
      CONSOLENUM = -1;
      return false;
    }
    return true;
  }

  // at least one registered host exists, so draw ui

  char* remoteip_text = text_input(UI_MAIN_WIDGET_TEXT_INPUT | 1, 30, 30, 600, 80, REMOTEIP_LABEL, REMOTEIP, 20);
  if (remoteip_text != NULL) {
    //if (REMOTEIP != NULL) free(REMOTEIP);
    REMOTEIP = remoteip_text;
    // LOGD("remoteip_text is %s", remoteip_text);
    //free(context.config.psn_account_id);
    //context.config.psn_account_id = remoteip_text;
    //load_psn_id_if_needed();
    //config_serialize(&context.config);
  }

  int console_num = number_input(UI_MAIN_WIDGET_TEXT_INPUT | 2, 30, 140, 600, 80, REGISTERED_CONSOLE_LABEL, CONSOLENUM);
  if ((console_num >= 0) && (console_num < context.config.num_registered_hosts)) {
    VitaChiakiHost* rhost = context.config.registered_hosts[console_num];
    if (rhost) {
      CONSOLENUM = console_num;
      // LOGD("console_num is %d", console_num);
      //context.config.controller_map_id = console_num;
      //config_serialize(&context.config);
    }
  }

  // Draw list of consoles
  int font_size = 18;
  int info_x = 30;
  int info_y = 250;
  int info_y_delta = 21;

  // write host list if possible
  int host_exists = false;
  int j = 0;
  for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
    VitaChiakiHost* rhost = context.config.registered_hosts[rhost_idx];
    if (!rhost) {
      continue;
    }

    // If a host is found then there is at least 1 host, so write instructions line.
    if (!host_exists) {
      vita2d_font_draw_text(font, info_x, info_y, COLOR_WHITE, font_size,
                            "Select number (0, 1, etc) from registered consoles below:"
                            );
    }
    host_exists = true;


    bool is_ps5 = chiaki_target_is_ps5(rhost->target);
    char this_host_info[100];
    char* nickname = rhost->registered_state->server_nickname;
    if (!nickname) nickname = "";
    snprintf(this_host_info, 100, "%d: %s [%X%X%X%X%X%X] (%s)", rhost_idx,
             nickname, rhost->server_mac[0], rhost->server_mac[1],
             rhost->server_mac[2], rhost->server_mac[3], rhost->server_mac[4], rhost->server_mac[5],
             is_ps5 ? "PS5" : "PS4");


    vita2d_font_draw_text(font, info_x, info_y + 2*(j+1)*info_y_delta, COLOR_WHITE, font_size,
                          this_host_info
                          );

    j++;
  }

  if (!host_exists) {
    // this should never be shown
    vita2d_font_draw_text(font, info_x, info_y, COLOR_WHITE, font_size,
                          "No registered hosts found. Pair to a console on a local network first."
                          );
  }

  int tooltip_x = 10;
  int tooltip_y = VITA_HEIGHT - font_size;
  vita2d_font_draw_textf(font, tooltip_x, tooltip_y, COLOR_WHITE, font_size,
                         "Triangle: save and add host;  %s: Exit without saving.", cancel_btn_str
                        );

  if (btn_pressed(SCE_CTRL_DOWN)) {
    context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 2);
  }
  if (btn_pressed(SCE_CTRL_UP)) {
    context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 1);
  }

  // reset CONSOLENUM if invalid
  if (CONSOLENUM >= context.config.num_registered_hosts) {
    CONSOLENUM = -1;
  }

  // reset REMOTEIP if invalid
  // TODO trim whitespace? too hard in c....
  if (!REMOTEIP) {
      REMOTEIP = "";
  }

  // cancel
  if (btn_pressed(SCE_CTRL_CANCEL)) {
    context.ui_state.next_active_item = UI_MAIN_WIDGET_ADD_HOST_BTN;
    REMOTEIP = "";
    CONSOLENUM = -1;
    return false;
  }

  // save (if pos)
  if (btn_pressed(SCE_CTRL_TRIANGLE)) {
    if ((REMOTEIP != NULL) && (strlen(REMOTEIP) != 0)) {
      if ((CONSOLENUM >= 0) && (CONSOLENUM < context.config.num_registered_hosts)) {
        VitaChiakiHost* rhost = context.config.registered_hosts[CONSOLENUM];
        if (rhost) {
          // save new host
          save_manual_host(rhost, REMOTEIP);

          // exit
          context.ui_state.next_active_item = UI_MAIN_WIDGET_ADD_HOST_BTN;
          REMOTEIP = "";
          CONSOLENUM = -1;
          return false;
        }
      }
    }
  }

  return true;
}

/// Draw the form to edit an existing host
/// @return whether the dialog should keep rendering
bool draw_edit_host_dialog() { 

  return false;
}
/// Render the current frame of an active stream
/// @return whether the stream should keep rendering
bool draw_stream() { 
  if (context.stream.is_streaming) context.stream.is_streaming = false;
  
  return false;
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
    // if (!context.stream.is_streaming) {
      // sceKernelDelayThread(1000 * 10);
      // Get current controller state
      if (!sceCtrlReadBufferPositive(0, &ctrl, 1)) {
        // Try again...
        LOGE("Failed to get controller state");
        continue;
      }
      context.ui_state.old_button_state = context.ui_state.button_state;
      context.ui_state.button_state = ctrl.buttons;

      // Get current touch state
      sceTouchPeek(SCE_TOUCH_PORT_FRONT, &(context.ui_state.touch_state_front),
                  1);


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
      if (!context.stream.is_streaming) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        // Draw modern charcoal background
        vita2d_draw_rectangle(0, 0, VITA_WIDTH, VITA_HEIGHT, UI_COLOR_BACKGROUND);

        // Render the current screen
        if (screen == UI_SCREEN_TYPE_MAIN) {
          screen = draw_main_menu();
        } else if (screen == UI_SCREEN_TYPE_REGISTER_HOST) {
          context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 0);
          if (!draw_registration_dialog()) {
            screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_ADD_HOST) {
          if (context.ui_state.next_active_item != (UI_MAIN_WIDGET_TEXT_INPUT | 2)) {
            if (context.ui_state.active_item != (UI_MAIN_WIDGET_TEXT_INPUT | 2)) {
              context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 1);
            }
          }
          if (!draw_add_host_dialog()) {
            screen = UI_SCREEN_TYPE_MAIN;
          }
        } else if (screen == UI_SCREEN_TYPE_EDIT_HOST) {
          if (!draw_edit_host_dialog()) {
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
        } else if (screen == UI_SCREEN_TYPE_SETTINGS) {
          if (context.ui_state.active_item != (UI_MAIN_WIDGET_TEXT_INPUT | 2)) {
            context.ui_state.next_active_item = (UI_MAIN_WIDGET_TEXT_INPUT | 1);
          }
          if (!draw_settings()) {
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
