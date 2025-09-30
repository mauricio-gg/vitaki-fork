#ifndef VITARPS5_VITA2D_UI_H
#define VITARPS5_VITA2D_UI_H

#include <psp2/ctrl.h>
#include <vita2d.h>

#include "../core/vitarps5.h"

// UI initialization and cleanup
int ui_init(void);
void ui_cleanup(void);

// Main UI functions
void ui_update(SceCtrlData* pad);
void ui_render(void);

// Touch input handling (internal only)
bool is_point_in_rect(float x, float y, float rx, float ry, float rw, float rh);

// IME Dialog functions
bool ui_is_ime_active(void);

// Modern UI state
typedef enum {
  UI_STATE_MAIN_DASHBOARD = 0,  // Play view - Main screen with consoles
  UI_STATE_PROFILE,             // Profile & Authentication view
  UI_STATE_CONTROLLER_MAPPING,  // Controller configuration view
  UI_STATE_SETTINGS,            // Settings with tabbed interface
  UI_STATE_ADD_CONNECTION,      // Console discovery/pairing flow
  UI_STATE_PSN_LOGIN,           // PSN login (preserved)
  UI_STATE_CONSOLE_PAIRING,     // Console pairing
  UI_STATE_REGISTRATION,        // PS5 console registration flow
  UI_STATE_STREAMING,           // Active streaming view
  UI_STATE_CONNECTION_DETAILS   // Console management screen
} UIState;

// Settings tab enumeration
typedef enum {
  SETTINGS_TAB_STREAMING = 0,  // Streaming Quality (Blue)
  SETTINGS_TAB_VIDEO,          // Video Settings (Green)
  SETTINGS_TAB_NETWORK,        // Network Settings (Orange)
  SETTINGS_TAB_CONTROLLER      // Controller Settings (Purple)
} SettingsTab;

// Settings screen state
typedef struct {
  SettingsTab current_tab;     // Currently active tab
  int selected_item;           // Selected item within current panel
  bool in_tab_navigation;      // True when navigating tabs with L/R
  float tab_transition_alpha;  // Animation alpha for tab transitions
} SettingsState;

// Console status
typedef enum {
  CONSOLE_STATUS_AVAILABLE = 0,
  CONSOLE_STATUS_UNAVAILABLE,
  CONSOLE_STATUS_CONNECTING,
  CONSOLE_STATUS_UNKNOWN
} ConsoleStatus;

// Console card data structure
typedef struct {
  char console_name[32];  // e.g., "PS5-024"
  char console_type[16];  // "PS5", "PS4 Pro", etc.
  char ip_address[16];    // Network address
  ConsoleStatus status;   // Available, Unavailable, Connecting
  int console_state;    // Ready(1), Standby(2), Unknown(0) - from error_codes.h
  int signal_strength;  // 0-4 for WiFi bars
  uint32_t last_connected;  // Timestamp
  bool is_paired;           // Authentication status
} ConsoleInfo;

// Wave navigation items (bottom to top)
typedef struct {
  char name[32];          // "Profile", "Controller", etc.
  vita2d_texture* icon;   // Icon texture
  int id;                 // Unique identifier
  bool enabled;           // Whether item is accessible
  float y_offset;         // Y position with wave animation
  uint32_t accent_color;  // Accent color for this view
} WaveNavItem;

// Controller mapping entry
typedef struct {
  char vita_button[32];  // PS Vita control name
  char ps5_button[32];   // Mapped PS5 button
  int vita_code;         // Vita button code
  int ps5_code;          // PS5 button code
} ControllerMapping;

// Controller preset
typedef struct {
  char name[32];                   // Preset name
  ControllerMapping mappings[20];  // Button mappings
  int mapping_count;               // Number of mappings
} ControllerPreset;

// Rear touchpad quadrant
typedef enum {
  TOUCH_QUAD_TOP_LEFT = 0,
  TOUCH_QUAD_TOP_RIGHT,
  TOUCH_QUAD_BOTTOM_LEFT,
  TOUCH_QUAD_BOTTOM_RIGHT
} TouchQuadrant;

// Particle structure for PlayStation symbol animation
typedef struct {
  float x, y;            // Position
  float vx, vy;          // Velocity
  float scale;           // Size scale (0.5-1.2)
  float rotation;        // Rotation angle
  float rotation_speed;  // Rotation velocity
  int symbol_type;       // 0=triangle, 1=circle, 2=x, 3=square
  uint32_t color;        // Particle color with transparency
  bool active;           // Whether particle is active
} Particle;

// Wave animation structure
typedef struct {
  float wave_offset;          // Current wave animation offset
  float wave_direction;       // Wave movement direction
  int selected_icon;          // Currently selected navigation icon
  float selection_animation;  // Selection highlight animation
} WaveNavigation;

// Modern UI Assets structure
typedef struct {
  // Background and particles
  vita2d_texture* background;
  vita2d_texture* wave_top;
  vita2d_texture* wave_bottom;

  // PlayStation symbol particles
  vita2d_texture* symbol_triangle;
  vita2d_texture* symbol_circle;
  vita2d_texture* symbol_ex;
  vita2d_texture* symbol_square;

  // Status and control elements
  vita2d_texture* ellipse_green;
  vita2d_texture* ellipse_red;
  vita2d_texture* ellipse_yellow;
  vita2d_texture* toggle_on;
  vita2d_texture* toggle_off;
  vita2d_texture* dropdown_indicator;

  // Buttons and cards
  vita2d_texture* button_add_new;
  vita2d_texture* charcoal_button;
  vita2d_texture* console_card;

  // Branding and logos
  vita2d_texture* ps5_logo;
  vita2d_texture* vita_rps5_logo;

  // Navigation icons
  vita2d_texture* profile_icon;
  vita2d_texture* controller_icon;
  vita2d_texture* settings_icon;
  vita2d_texture* play_icon;

  // User profile image (loaded from system)
  vita2d_texture* user_profile_image;
  bool has_user_profile_image;

  // Controller diagrams
  vita2d_texture* vita_front;
  vita2d_texture* vita_back;
  vita2d_texture* ps5_controller;
} UIAssets;

// Modern UI Colors (ABGR format for vita2d)
#define UI_COLOR_PRIMARY_BLUE 0xFFFF9034     // PlayStation Blue #3490FF
#define UI_COLOR_BACKGROUND 0xFF1A1614       // Animated charcoal gradient base
#define UI_COLOR_CARD_BG 0xFF37322D          // Dark charcoal (45,50,55)
#define UI_COLOR_TEXT_PRIMARY 0xFFFFFFFF     // White
#define UI_COLOR_TEXT_SECONDARY 0xFFB4B4B4   // Light Gray
#define UI_COLOR_TEXT_TERTIARY 0xFFA0A0A0    // Medium Gray
#define UI_COLOR_CONSOLE_NAME_BG 0xFF000000  // Black bar
#define UI_COLOR_CONSOLE_NAME_TEXT 0xFFFFFFFF   // White text
#define UI_COLOR_STATUS_AVAILABLE 0xFF50AF4C    // Success Green #4CAF50
#define UI_COLOR_STATUS_CONNECTING 0xFF0098FF   // Warning Orange #FF9800
#define UI_COLOR_STATUS_UNAVAILABLE 0xFF3643F4  // Error Red #F44336
#define UI_COLOR_ACCENT_PURPLE 0xFFB0279C       // Accent Purple #9C27B0

// Authentication status colors
#define UI_COLOR_AUTH_TAG_BG 0xFFFFD700    // Yellow background for auth tag
#define UI_COLOR_AUTH_TAG_TEXT 0xFFFFFFFF  // White text for auth tag
#define UI_COLOR_SEMI_TRANSPARENT \
  0x80FFFFFF  // 50% white overlay for unauthenticated cards

// Settings tab colors
#define SETTINGS_TAB_COLOR_STREAMING 0xFFFF9034   // Blue
#define SETTINGS_TAB_COLOR_VIDEO 0xFF50AF4C       // Green
#define SETTINGS_TAB_COLOR_NETWORK 0xFF0098FF     // Orange
#define SETTINGS_TAB_COLOR_CONTROLLER 0xFFB0279C  // Purple

// Modern settings layout constants
#define SETTINGS_TAB_BAR_Y 105
#define SETTINGS_TAB_WIDTH 120
#define SETTINGS_TAB_HEIGHT 35
#define SETTINGS_TAB_SPACING 10
#define SETTINGS_PANEL_X 265  // Start after sidebar
#define SETTINGS_PANEL_Y 145
#define SETTINGS_PANEL_WIDTH 540  // Much wider panel
#define SETTINGS_PANEL_HEIGHT 320
#define SETTINGS_PANEL_RADIUS 12
#define SETTINGS_HEADER_HEIGHT 50
#define SETTINGS_BODY_PADDING 25
#define SETTINGS_LABEL_X_OFFSET 30
#define SETTINGS_VALUE_RIGHT_MARGIN 30
#define SETTINGS_ITEM_HEIGHT 45
#define SETTINGS_ITEM_Y_START 70  // Start below header
#define SETTINGS_TOGGLE_WIDTH 64
#define SETTINGS_TOGGLE_HEIGHT 32
#define SETTINGS_SLIDER_WIDTH 120
#define SETTINGS_DROPDOWN_WIDTH 110

// Particle Colors (with transparency)
#define PARTICLE_COLOR_RED 0x80FF5555     // Semi-transparent red
#define PARTICLE_COLOR_GREEN 0x8055FF55   // Semi-transparent green
#define PARTICLE_COLOR_BLUE 0x805555FF    // Semi-transparent blue
#define PARTICLE_COLOR_ORANGE 0x8055AAFF  // Semi-transparent orange

// Settings item types
typedef enum {
  SETTING_TYPE_TOGGLE = 0,
  SETTING_TYPE_DROPDOWN,
  SETTING_TYPE_SLIDER,
  SETTING_TYPE_TEXT
} SettingType;

// Individual setting item
typedef struct {
  char label[32];    // "Quality Preset", "Hardware Decode", etc.
  char value[32];    // Current value display
  SettingType type;  // TOGGLE, DROPDOWN, SLIDER, TEXT
  bool enabled;      // Whether user can interact
  void* data;        // Pointer to actual setting value
} SettingItem;

// Settings panel
typedef struct {
  char title[32];        // "Streaming Quality", "Video Settings", etc.
  uint32_t color;        // Panel background color
  SettingItem items[8];  // Max 8 items per panel
  int item_count;        // Actual number of items
  int selected_item;     // Currently selected item (-1 = none)
} SettingsPanel;

// Complete settings screen state
typedef struct {
  SettingsPanel panels[4];  // 4 panels as shown in mockup
  int selected_panel;       // Currently selected panel (0-3)
  bool panel_navigation;  // True = navigating panels, False = navigating items
} SettingsScreen;

// Configuration UI state
typedef struct {
  char ps_ip[16];
  int quality_index;
  bool hardware_decode;
  bool motion_controls;
} UIConfig;

// Modern UI Layout Constants
#define WAVE_NAV_WIDTH 130      // Wave navigation width
#define CONTENT_AREA_WIDTH 830  // Content area width
#define CONSOLE_CARD_WIDTH 300
#define CONSOLE_CARD_HEIGHT 250  // Taller cards
#define CARD_PADDING 20          // Modern spacing
#define CARD_RADIUS 12           // Rounded corners
#define ICON_SIZE 48
#define WAVE_ICON_SPACING 80  // Spacing between wave icons
#define ELEMENT_MARGIN 30     // Margin between elements

// Legacy Settings Layout Constants (kept for compatibility)
#define SETTINGS_HEADER_Y 40
#define SETTINGS_PANELS_START_Y 80
#define SETTINGS_PANELS_START_X 170
#define SETTINGS_PANEL_PADDING 15
#define SETTINGS_PANEL_SPACING 10  // Re-add for legacy functions
#define SETTINGS_ITEM_SPACING 5

// Settings Panel Colors (matching mockup exactly)
#define SETTINGS_STREAMING_COLOR 0xFF8B4B20   // Blue panel
#define SETTINGS_VIDEO_COLOR 0xFF507850       // Green panel
#define SETTINGS_NETWORK_COLOR 0xFF6E4B14     // Orange panel
#define SETTINGS_CONTROLLER_COLOR 0xFF6E3278  // Purple panel

// Animation Constants
#define PARTICLE_COUNT 12       // Optimal particle count
#define WAVE_AMPLITUDE 3.0f     // Wave movement amplitude
#define ANIMATION_SPEED 0.016f  // 60fps frame time

// UI Element Drawing Constants
#define TOGGLE_WIDTH 64
#define TOGGLE_HEIGHT 32
#define TOGGLE_THUMB_RADIUS 12
#define TOGGLE_ANIMATION_TIME 0.2f  // 200ms animation

#define DROPDOWN_HEIGHT 30
#define DROPDOWN_ARROW_SIZE 8
#define DROPDOWN_ITEM_HEIGHT 35
#define DROPDOWN_MAX_VISIBLE_ITEMS 5

#define SLIDER_TRACK_HEIGHT 4
#define SLIDER_THUMB_RADIUS 8

// Font and text positioning constants (relative to parent)
#define FONT_BASELINE_OFFSET 8  // Empirically determined for 0.9f font scale
#define TEXT_VERTICAL_CENTER_OFFSET \
  (SETTINGS_ITEM_HEIGHT / 2 + FONT_BASELINE_OFFSET)  // Add instead of subtract

// Selection highlight constants (relative to item)
#define SELECTION_HIGHLIGHT_PADDING 2  // Padding around highlight
#define SELECTION_HIGHLIGHT_HEIGHT \
  (SETTINGS_ITEM_HEIGHT - 2 * SELECTION_HIGHLIGHT_PADDING)

// Control element positioning (relative to item center)
#define CONTROL_VERTICAL_CENTER_OFFSET (SETTINGS_ITEM_HEIGHT / 2)

// Value text positioning (relative to item)
#define VALUE_TEXT_VERTICAL_OFFSET TEXT_VERTICAL_CENTER_OFFSET

// UI Element States
typedef struct {
  float toggle_animations[8];  // Animation progress for each toggle (0.0-1.0)
  bool dropdown_open[8];       // Which dropdowns are open
  int dropdown_selected[8];    // Selected item in each dropdown
  float slider_drag_value[8];  // Current drag value for sliders
  bool slider_dragging[8];     // Whether slider is being dragged
} UIElementStates;

// Font sizes
#define FONT_SIZE_HEADER 24
#define FONT_SIZE_BODY 16
#define FONT_SIZE_CONSOLE_NAME 18
#define FONT_SIZE_BUTTON 16
#define FONT_SIZE_SMALL 14

#endif  // VITARPS5_VITA2D_UI_H