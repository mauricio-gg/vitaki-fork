# VitaRPS5 Complete UI Implementation Specification

## Overview

This document provides a complete specification for implementing the VitaRPS5 UI in vitaki-fork while keeping the proven vitaki backend untouched.

**Goal**: Full VitaRPS5 visual experience (wave navigation, animated background, console cards, touch interactions) powered by vitaki-fork's working backend.

**Approach**: Rewrite UI layer only - no backend changes.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    VitaRPS5 UI Layer                     │
│  (ui.c - Complete rewrite of rendering/input)            │
├─────────────────────────────────────────────────────────┤
│              vitaki-fork Backend (Unchanged)             │
│  discovery.c, host.c, config.c, video.c, etc.           │
└─────────────────────────────────────────────────────────┘
```

## Visual Components

### 1. Wave Navigation Sidebar (130px left)

**Layout:**
```
┌─────────┬────────────────────────────┐
│  Wave   │                            │
│  Nav    │     Main Content Area      │
│ (130px) │        (830px)             │
│         │                            │
│  [🎮]   │    Console Cards           │
│  [⚙️]   │    & Content               │
│  [🎮]   │                            │
│  [👤]   │                            │
└─────────┴────────────────────────────┘
```

**Components:**
- Background: `wave_top.png` and `wave_bottom.png` textures
- 4 navigation icons (top to bottom):
  1. Play (🎮) - Main dashboard
  2. Settings (⚙️) - Settings screen
  3. Controller (🎮) - Controller mapping
  4. Profile (👤) - Profile/authentication
- Selection highlight: PlayStation Blue glow around selected icon
- Wave animation: Subtle vertical wave motion

**Implementation Details:**
```c
// Sidebar dimensions
#define WAVE_NAV_WIDTH 130
#define WAVE_NAV_ICON_SIZE 48
#define WAVE_NAV_ICON_X 41  // Center of 130px width
#define WAVE_NAV_ICON_START_Y 180
#define WAVE_NAV_ICON_SPACING 60

// Navigation state
static int selected_nav_icon = 0;  // 0=Play, 1=Settings, 2=Controller, 3=Profile
static float wave_animation_time = 0.0f;

// Rendering function
void render_wave_navigation() {
    // Draw wave textures as background
    vita2d_draw_texture(wave_top, 0, 0);
    vita2d_draw_texture(wave_bottom, 0, 0);

    // Draw navigation icons with wave animation
    for (int i = 0; i < 4; i++) {
        int y = WAVE_NAV_ICON_START_Y + (i * WAVE_NAV_ICON_SPACING);
        float wave_offset = sinf(wave_animation_time + i * 0.5f) * 3.0f;

        // Selection highlight
        if (i == selected_nav_icon) {
            draw_circle(WAVE_NAV_ICON_X, y + wave_offset, 28, UI_COLOR_PRIMARY_BLUE);
        }

        // Icon
        vita2d_draw_texture_scale(nav_icons[i],
            WAVE_NAV_ICON_X - 24, y + wave_offset - 24, 1.0f, 1.0f);
    }
}
```

### 2. Animated Background - PlayStation Symbols

**Particle System:**
- 12 particles floating upward
- Symbol types: Triangle, Circle, X, Square
- Colors: Red, Green, Blue, Orange (semi-transparent)
- Physics: Upward drift with rotation

**Particle Structure:**
```c
typedef struct {
    float x, y;              // Position
    float vx, vy;            // Velocity (vy is upward)
    float scale;             // Size (0.15 - 0.4)
    float rotation;          // Current rotation angle
    float rotation_speed;    // Rotation velocity
    int symbol_type;         // 0=triangle, 1=circle, 2=x, 3=square
    uint32_t color;          // PARTICLE_COLOR_RED/GREEN/BLUE/ORANGE
    bool active;             // Is particle visible
} Particle;

#define PARTICLE_COUNT 12
static Particle particles[PARTICLE_COUNT];
```

**Particle Colors (ABGR with alpha):**
```c
#define PARTICLE_COLOR_RED    0x80FF5555  // Semi-transparent red
#define PARTICLE_COLOR_GREEN  0x8055FF55  // Semi-transparent green
#define PARTICLE_COLOR_BLUE   0x805555FF  // Semi-transparent blue
#define PARTICLE_COLOR_ORANGE 0x8055AAFF  // Semi-transparent orange
```

**Initialization:**
```c
void init_particles() {
    srand(time(NULL));
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        particles[i].x = (float)(rand() % 960);
        particles[i].y = (float)(rand() % 544);
        particles[i].vx = ((rand() % 100) / 100.0f - 0.5f) * 0.5f;  // Slight horizontal drift
        particles[i].vy = -((rand() % 100) / 100.0f + 0.5f) * 0.8f; // Upward (negative Y)
        particles[i].scale = 0.15f + ((rand() % 100) / 100.0f) * 0.25f;
        particles[i].rotation = (float)(rand() % 360);
        particles[i].rotation_speed = ((rand() % 100) / 100.0f - 0.5f) * 2.0f;
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
}
```

**Update Logic (60 FPS):**
```c
void update_particles(float delta_time) {
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        if (!particles[i].active) continue;

        // Update position
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].rotation += particles[i].rotation_speed;

        // Wrap around screen edges
        if (particles[i].y < -50) {
            particles[i].y = 594;  // Respawn at bottom
            particles[i].x = (float)(rand() % 960);
        }
        if (particles[i].x < -50) particles[i].x = 1010;
        if (particles[i].x > 1010) particles[i].x = -50;
    }
}
```

**Rendering:**
```c
void render_particles() {
    vita2d_texture* symbol_textures[4] = {
        symbol_triangle, symbol_circle, symbol_ex, symbol_square
    };

    for (int i = 0; i < PARTICLE_COUNT; i++) {
        if (!particles[i].active) continue;

        vita2d_texture* tex = symbol_textures[particles[i].symbol_type];

        // Draw with rotation and scale
        vita2d_draw_texture_scale_rotate(tex,
            particles[i].x, particles[i].y,
            particles[i].scale, particles[i].scale,
            particles[i].rotation);

        // Apply color tint (may require custom shader or color modulation)
    }
}
```

### 3. Console Card Display (Center Area)

**Layout:**
```
Content Area (830px wide, starts at x=130):
┌─────────────────────────────────────────┐
│  "Which do you want to connect?"        │
│                                          │
│    ┌────────────────────────┐           │
│    │   PS5 Logo             │           │
│    │                        │           │
│    │   ┌──────────────┐     │           │
│    │   │ PS5 - 024    │     │           │
│    │   └──────────────┘     │ [●]       │
│    │                        │           │
│    │     Ready / Standby    │           │
│    └────────────────────────┘           │
│                                          │
│          [+ Add New]                    │
└─────────────────────────────────────────┘
```

**Console Card Specifications:**
- Size: 400x200 pixels
- Position: Centered horizontally in content area
- Spacing: 120px between cards (vertically)
- Background: `console_card.png` texture (or rounded rect with shadow)
- Components:
  1. PS5 logo (centered, 1/3 from top)
  2. Console name bar (1/3 from bottom, dark background)
  3. Status indicator (top-right corner)
  4. State text ("Ready" / "Standby")
  5. Action button (Wake/Register) if applicable

**Console Card Structure:**
```c
typedef struct {
    char name[32];           // "PS5 - 024"
    char ip_address[16];     // "192.168.1.100"
    int status;              // 0=Available, 1=Unavailable, 2=Connecting
    int state;               // 0=Unknown, 1=Ready, 2=Standby
    bool is_registered;      // Has valid credentials
    bool is_discovered;      // From network discovery
} ConsoleCardInfo;
```

**Rendering Console Card:**
```c
void render_console_card(ConsoleCardInfo* console, int x, int y, bool selected) {
    // Selection highlight
    if (selected) {
        draw_rounded_rectangle(x - 8, y - 8, 416, 216, 8, UI_COLOR_PRIMARY_BLUE);
    }

    // Card background with shadow
    draw_card_with_shadow(x, y, 400, 200, 12, UI_COLOR_CARD_BG);

    // Console state glow (if Ready or Standby)
    if (console->state == 1) {  // Ready
        draw_rounded_rectangle(x - 4, y - 4, 408, 208, 10,
            RGBA8(0, 162, 232, 180));  // Blue glow
    } else if (console->state == 2) {  // Standby
        draw_rounded_rectangle(x - 4, y - 4, 408, 208, 10,
            RGBA8(255, 193, 7, 180));  // Yellow glow
    }

    // PS5 logo (centered, 1/3 from top)
    int logo_x = x + 200 - (ps5_logo_width / 2);
    int logo_y = y + (200 / 3) - (ps5_logo_height / 2);
    vita2d_draw_texture(ps5_logo, logo_x, logo_y);

    // Console name bar (1/3 from bottom)
    int name_bar_y = y + 200 - (200 / 3) - 20;
    draw_rounded_rectangle(x + 15, name_bar_y, 370, 40, 8,
        RGBA8(70, 75, 80, 255));

    // Console name text (centered in bar)
    int text_width = vita2d_font_text_width(font, 20, console->name);
    int text_x = x + 200 - (text_width / 2);
    vita2d_font_draw_text(font, text_x, name_bar_y + 25,
        UI_COLOR_TEXT_PRIMARY, 20, console->name);

    // Status indicator (top-right)
    vita2d_texture* status_tex = NULL;
    if (console->status == 0) status_tex = ellipse_green;
    else if (console->status == 1) status_tex = ellipse_red;
    else if (console->status == 2) status_tex = ellipse_yellow;

    if (status_tex) {
        vita2d_draw_texture(status_tex, x + 370, y + 10);
    }

    // State text ("Ready" / "Standby")
    const char* state_text = NULL;
    uint32_t state_color = UI_COLOR_TEXT_SECONDARY;
    if (console->state == 1) {
        state_text = "Ready";
        state_color = RGBA8(0, 162, 232, 255);  // Blue
    } else if (console->state == 2) {
        state_text = "Standby";
        state_color = RGBA8(255, 193, 7, 255);  // Yellow
    }

    if (state_text) {
        int state_text_width = vita2d_font_text_width(font, 18, state_text);
        int state_x = x + 200 - (state_text_width / 2);
        vita2d_font_draw_text(font, state_x, name_bar_y + 60,
            state_color, 18, state_text);
    }
}
```

**Card Grid Layout:**
```c
void render_console_grid() {
    int screen_center_x = VITA_WIDTH / 2;
    int card_spacing = 120;
    int start_y = 150;  // After header text

    int num_consoles = get_console_count();

    for (int i = 0; i < num_consoles; i++) {
        ConsoleCardInfo* console = get_console_at_index(i);

        int card_x = screen_center_x - (CONSOLE_CARD_WIDTH / 2);
        int card_y = start_y + (i * card_spacing);

        bool selected = (i == selected_console_index);
        render_console_card(console, card_x, card_y, selected);
    }
}
```

### 4. Discovery and Action Buttons

**"Add New" Button:**
- Position: Bottom center of content area
- Size: From `button_add_new.png` texture
- States: Normal, Hover (PlayStation Blue tint), Pressed (scale down)
- Action: Triggers discovery or manual add flow

```c
void render_add_new_button(bool selected, bool pressed) {
    int button_width = vita2d_texture_get_width(button_add_new);
    int button_height = vita2d_texture_get_height(button_add_new);

    int button_x = (VITA_WIDTH - button_width) / 2;
    int button_y = VITA_HEIGHT - 100 - button_height;

    // Selection highlight
    if (selected) {
        draw_rounded_rectangle(button_x - 8, button_y - 8,
            button_width + 16, button_height + 16, 8, UI_COLOR_PRIMARY_BLUE);
    }

    // Press animation (scale down)
    float scale = pressed ? 0.95f : 1.0f;
    int offset = pressed ? 2 : 0;

    vita2d_draw_texture_scale(button_add_new,
        button_x + offset, button_y + offset, scale, scale);
}
```

### 5. Touch Screen Interactions

**Touch Areas:**
1. Wave navigation icons (4 circular hitboxes)
2. Console cards (rectangular hitboxes)
3. Add New button (rectangular hitbox)
4. Console action buttons (Wake/Register when visible)

**Touch Input Handling:**
```c
void handle_touch_input() {
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

    if (touch.reportNum > 0) {
        // Convert touch coordinates to screen coordinates
        float touch_x = (touch.report[0].x / 1920.0f) * 960.0f;
        float touch_y = (touch.report[0].y / 1088.0f) * 544.0f;

        // Check wave navigation icons
        for (int i = 0; i < 4; i++) {
            int icon_x = WAVE_NAV_ICON_X;
            int icon_y = WAVE_NAV_ICON_START_Y + (i * WAVE_NAV_ICON_SPACING);

            if (is_point_in_circle(touch_x, touch_y, icon_x, icon_y, 30)) {
                selected_nav_icon = i;
                switch_screen(i);  // 0=Dashboard, 1=Settings, 2=Controller, 3=Profile
                break;
            }
        }

        // Check console cards
        int num_consoles = get_console_count();
        for (int i = 0; i < num_consoles; i++) {
            int card_x = (VITA_WIDTH / 2) - (CONSOLE_CARD_WIDTH / 2);
            int card_y = 150 + (i * 120);

            if (is_point_in_rect(touch_x, touch_y, card_x, card_y,
                CONSOLE_CARD_WIDTH, CONSOLE_CARD_HEIGHT)) {
                selected_console_index = i;
                // Double tap to connect
                break;
            }
        }

        // Check Add New button
        int button_x = (VITA_WIDTH - button_add_new_width) / 2;
        int button_y = VITA_HEIGHT - 100 - button_add_new_height;

        if (is_point_in_rect(touch_x, touch_y, button_x, button_y,
            button_add_new_width, button_add_new_height)) {
            trigger_add_console_action();
        }
    }
}

bool is_point_in_circle(float px, float py, int cx, int cy, int radius) {
    float dx = px - cx;
    float dy = py - cy;
    return (dx*dx + dy*dy) <= (radius*radius);
}

bool is_point_in_rect(float px, float py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}
```

## Backend Integration

All UI components call existing vitaki-fork backend functions:

**Discovery:**
```c
// UI calls when user taps "Add New"
void trigger_add_console_action() {
    start_discovery(NULL, NULL);  // vitaki-fork function
}
```

**Console Connection:**
```c
// UI calls when user selects console and presses X or double-taps
void trigger_console_connect(int console_index) {
    VitaChiakiHost* host = context.hosts[console_index];
    if (host) {
        context.active_host = host;
        // vitaki streaming starts automatically via host_stream()
    }
}
```

**Wake Console:**
```c
// UI calls when user taps "Wake" button on standby console
void trigger_console_wake(int console_index) {
    VitaChiakiHost* host = context.hosts[console_index];
    if (host) {
        host_wakeup(host);  // vitaki-fork function
    }
}
```

**Registration:**
```c
// UI calls when user taps "Register" button on unregistered console
void trigger_console_register(int console_index) {
    VitaChiakiHost* host = context.hosts[console_index];
    if (host) {
        context.active_host = host;
        // Switch to registration screen (existing vitaki flow)
        next_screen = UI_SCREEN_TYPE_REGISTER_HOST;
    }
}
```

## Asset Loading

**New Textures to Load:**
```c
// PlayStation symbols for particles
vita2d_texture *symbol_triangle, *symbol_circle, *symbol_ex, *symbol_square;

// Wave navigation
vita2d_texture *wave_top, *wave_bottom;
vita2d_texture *nav_icon_play, *nav_icon_settings, *nav_icon_controller, *nav_icon_profile;

// Console cards
vita2d_texture *console_card_bg;
vita2d_texture *ps5_logo;
vita2d_texture *ellipse_green, *ellipse_yellow, *ellipse_red;

// Buttons
vita2d_texture *button_add_new;

void load_vitarps5_textures() {
    // Symbols
    symbol_triangle = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/symbol_triangle.png");
    symbol_circle = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/symbol_circle.png");
    symbol_ex = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/symbol_ex.png");
    symbol_square = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/symbol_square.png");

    // Waves
    wave_top = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/wave_top.png");
    wave_bottom = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/wave_bottom.png");

    // Status indicators
    ellipse_green = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/ellipse_green.png");
    ellipse_yellow = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/ellipse_yellow.png");
    ellipse_red = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/ellipse_red.png");

    // Buttons
    button_add_new = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/button_add_new.png");
    console_card_bg = vita2d_load_PNG_file("app0:/assets/vitarps5/user_provided/modern_assets/console_card.png");

    // PS5 logo (reuse existing)
    ps5_logo = img_ps5;  // Already loaded
}
```

**Add to CMakeLists.txt:**
```cmake
FILE res/assets/vitarps5/user_provided/modern_assets/symbol_triangle.png assets/vitarps5/symbol_triangle.png
FILE res/assets/vitarps5/user_provided/modern_assets/symbol_circle.png assets/vitarps5/symbol_circle.png
FILE res/assets/vitarps5/user_provided/modern_assets/symbol_ex.png assets/vitarps5/symbol_ex.png
FILE res/assets/vitarps5/user_provided/modern_assets/symbol_square.png assets/vitarps5/symbol_square.png
FILE res/assets/vitarps5/user_provided/modern_assets/wave_top.png assets/vitarps5/wave_top.png
FILE res/assets/vitarps5/user_provided/modern_assets/wave_bottom.png assets/vitarps5/wave_bottom.png
FILE res/assets/vitarps5/user_provided/modern_assets/ellipse_green.png assets/vitarps5/ellipse_green.png
FILE res/assets/vitarps5/user_provided/modern_assets/ellipse_yellow.png assets/vitarps5/ellipse_yellow.png
FILE res/assets/vitarps5/user_provided/modern_assets/ellipse_red.png assets/vitarps5/ellipse_red.png
FILE res/assets/vitarps5/user_provided/modern_assets/button_add_new.png assets/vitarps5/button_add_new.png
FILE res/assets/vitarps5/user_provided/modern_assets/console_card.png assets/vitarps5/console_card.png
```

## Main Rendering Flow

**Complete `draw_main_menu()` Rewrite:**
```c
UIScreenType draw_main_menu() {
    // Update animations
    static uint64_t last_frame_time = 0;
    uint64_t current_time = sceKernelGetProcessTimeWide();
    float delta_time = (current_time - last_frame_time) / 1000000.0f;
    last_frame_time = current_time;

    wave_animation_time += delta_time;
    update_particles(delta_time);

    // Render layers (back to front)

    // 1. Animated background particles
    render_particles();

    // 2. Wave navigation sidebar
    render_wave_navigation();

    // 3. Main content area
    // Header text
    const char* header = "Which do you want to connect?";
    int header_width = vita2d_font_text_width(font, 24, header);
    int header_x = VITA_WIDTH / 2 - header_width / 2;
    vita2d_font_draw_text(font, header_x, 80, UI_COLOR_TEXT_PRIMARY, 24, header);

    // Console cards
    int num_consoles = count_hosts();
    if (num_consoles > 0) {
        render_console_grid();
        render_add_new_button(add_button_selected, add_button_pressed);
    } else {
        render_empty_state();  // "No consoles found. Tap Add New to discover."
    }

    // Handle input
    handle_touch_input();
    handle_controller_input();

    // Check for screen transitions based on selected_nav_icon
    UIScreenType next_screen = UI_SCREEN_TYPE_MAIN;
    if (selected_nav_icon == 1) next_screen = UI_SCREEN_TYPE_SETTINGS;
    // ... other nav transitions

    return next_screen;
}
```

## Implementation Phases

### Phase 1: Foundation (Est: 2-3 hours)
- Load all VitaRPS5 textures
- Add texture paths to CMakeLists.txt
- Initialize particle system
- Test particle rendering on black background

**Deliverable:** Animated PlayStation symbols floating on screen

### Phase 2: Wave Navigation (Est: 2-3 hours)
- Render wave textures
- Add 4 navigation icons
- Implement icon selection
- Add wave animation
- Wire to screen switching

**Deliverable:** Working wave sidebar with navigation

### Phase 3: Console Cards (Est: 3-4 hours)
- Map vitaki hosts to console card structure
- Implement console card rendering
- Add selection highlight
- Add status indicators
- Position cards in grid

**Deliverable:** Console cards displaying vitaki hosts

### Phase 4: Touch Interactions (Est: 2-3 hours)
- Implement touch input handling
- Add hitbox detection (circles, rects)
- Wire touch to selections
- Add visual feedback

**Deliverable:** Full touch screen control

### Phase 5: Backend Integration (Est: 1-2 hours)
- Wire console selection to vitaki host streaming
- Wire Add New to vitaki discovery
- Wire Wake/Register buttons to vitaki backend
- Test complete flow

**Deliverable:** Full VitaRPS5 UI with vitaki backend

### Phase 6: Polish & Testing (Est: 2-3 hours)
- Add animations and transitions
- Optimize rendering performance
- Test on hardware
- Fix bugs and edge cases

**Deliverable:** Production-ready VitaRPS5 UI

## Estimated Total Time

**15-20 hours of focused development**

Split across multiple sessions for quality and testing.

## Success Criteria

1. ✅ Animated PlayStation symbol background
2. ✅ Wave navigation sidebar with 4 icons
3. ✅ Console cards displaying vitaki hosts
4. ✅ Touch screen interactions working
5. ✅ All vitaki backend functions operational
6. ✅ Smooth animations (60 FPS)
7. ✅ No regressions in vitaki functionality

## Notes

- This is purely UI work - vitaki backend remains untouched
- All rendering happens in `ui.c` - no other files modified (except CMakeLists.txt for assets)
- Existing vitaki functionality (discovery, registration, streaming) unchanged
- Touch and controller input both supported
- Can phase implementation to test incrementally