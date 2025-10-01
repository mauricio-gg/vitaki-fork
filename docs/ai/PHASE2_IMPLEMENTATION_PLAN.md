# Phase 2 Implementation Plan - Detailed Tasks

**Date**: September 30, 2025
**Goal**: Implement Settings, Profile/Registration, and Controller Configuration screens

---

## Part 1: Reusable UI Components

### 1.1 Rounded Rectangle Drawing
```c
void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
```
- Uses vita2d_draw_rectangle for now (TODO: Add proper rounded corners later)
- Will be used for all cards and panels

### 1.2 Toggle Switch Component
```c
void draw_toggle_switch(int x, int y, int width, int height, bool state, bool selected);
```
- Background track (gray when off, blue when on)
- Circular knob that slides left/right
- Highlight border when selected

### 1.3 Dropdown Component
```c
void draw_dropdown(int x, int y, int width, int height, const char* label,
                   const char* value, bool expanded, bool selected);
```
- Label on left, value on right
- Down arrow indicator
- Expands to show options when activated
- Highlight when selected

### 1.4 Tab Bar Component
```c
void draw_tab_bar(int x, int y, int width, int height,
                  const char* tabs[], uint32_t colors[], int num_tabs, int selected);
```
- Colored sections for each tab
- Selected tab has brighter color
- Text centered in each tab

### 1.5 Status Dot
```c
void draw_status_dot(int x, int y, int radius, StatusType status);
```
- Green: Active/Connected
- Yellow: Standby/Warning
- Red: Error/Disconnected

---

## Part 2: Settings Screen Implementation

### 2.1 Settings State Structure
```c
typedef enum {
    SETTINGS_TAB_STREAMING = 0,
    SETTINGS_TAB_VIDEO = 1,
    SETTINGS_TAB_NETWORK = 2,
    SETTINGS_TAB_CONTROLLER = 3,
    SETTINGS_TAB_COUNT = 4
} SettingsTab;

typedef struct {
    SettingsTab current_tab;
    int selected_item;
    bool dropdown_expanded;
    int dropdown_selected_option;
} SettingsState;
```

### 2.2 Settings Tabs

**Streaming Quality Tab (Blue)**
- Quality Preset: [720p/1080p] (dropdown) → `config.resolution`
- FPS Target: [30/60] (dropdown) → `config.fps`
- Hardware Decode: [On/Off] (toggle) → *STUB - Always on*
- Custom Settings: [On/Off] (toggle) → *STUB*

**Video Settings Tab (Green)**
- Aspect Ratio: [16:9/4:3] (dropdown) → *STUB*
- Brightness: [slider] → *STUB*
- Video Smoothing: [On/Off] (toggle) → *STUB*

**Network Settings Tab (Orange)**
- Connection Type: [Local/Remote] (dropdown) → *STUB*
- Network Timeout: [5s/10s/30s] (dropdown) → *STUB*
- MTU Size: [Auto/1500/1400] (dropdown) → *STUB*

**Controller Settings Tab (Purple)**
- Motion Controls: [On/Off] (toggle) → *STUB*
- Touch Controls: [On/Off] (toggle) → *STUB*
- Deadzone: [slider] → *STUB*
- Controller Map ID: [0-7,25,99] (dropdown) → `config.controller_map_id`

### 2.3 Navigation Logic
- L1/R1: Switch tabs (cycle through 0-3)
- Up/Down: Navigate items within tab
- X: Activate toggle/dropdown
- Circle: Back to main menu
- For dropdowns: Up/Down to select option, X to confirm

### 2.4 Rendering Order
1. Particle background (already exists)
2. Wave navigation (already exists)
3. Tab bar at top
4. Current tab content (items)
5. Control hints at bottom
6. Dropdown overlay (if expanded, renders on top)

---

## Part 3: Profile/Registration Screen

### 3.1 Layout Sections

**Profile Card (Left Side)**
- Avatar placeholder (circular, 80px)
- Username display
- PSN Account ID display
- Uses existing `config.psn_account_id`

**Connection Info Card (Right Side)**
- Network Type: [display only]
- Console IP: [from active_host]
- Latency: [from stream stats] → *STUB for now*
- Connection Status: [status dot]

**Authentication Section (Bottom)**
- PSN Auth Status: [authenticated/not]
- "Add New" button to start registration
- Links to registration dialog

### 3.2 Navigation
- Up/Down: Navigate sections (Profile/Connection/Auth)
- X: Activate selected section (edit PSN ID, start registration)
- Circle: Back to main menu

---

## Part 4: Controller Configuration Screen

### 4.1 Layout

**Button Mapping Table (Left Half)**
```
PS Vita Control  →  PS5 Button
───────────────────────────────
Cross            →  Cross
Circle           →  Circle
Square           →  Square
Triangle         →  Triangle
...
```

**Vita Diagram (Right Half)**
- Simplified Vita outline
- Highlights selected button
- Shows button labels

### 4.2 Button Mappings
Use existing `config.controller_map_id`:
- 0-7: Official layouts
- 25: No touchpad
- 99: Custom vitaki layout
- +100: Swap L2/L3, R2/R3

Display mapping table based on selected map ID.

### 4.3 Navigation
- Up/Down: Navigate button list
- Left/Right: Switch between table and diagram
- X: Select map ID (opens dropdown)
- Circle: Back to main menu

---

## Part 5: Implementation Order & Commits

### Commit 1: UI Components Foundation
- Add reusable component functions
- Test each component individually
- File: `vita/src/ui.c` (add at top, after existing helpers)

### Commit 2: Settings Screen - Structure
- Add SettingsState structure
- Add tab bar rendering
- Add tab navigation (L1/R1)
- Wire Circle button to exit

### Commit 3: Settings Screen - Content
- Add all 4 tab contents
- Wire toggles/dropdowns to config
- Add stubs for unimplemented features
- Full navigation (Up/Down, X to activate)

### Commit 4: Profile/Registration Screen
- Add profile card rendering
- Add connection info card
- Wire to existing PSN account ID
- Link to registration dialog

### Commit 5: Controller Configuration Screen
- Add button mapping table
- Add Vita diagram
- Wire to controller_map_id
- Add map ID selection

### Commit 6: Polish & Testing
- Add control hints to all screens
- Test all navigation paths
- Fix any visual glitches
- Update documentation

---

## Stub Marking Convention

All stubbed features will be marked with:
```c
// TODO(PHASE2-STUB): [Feature name] - Not implemented yet
// This is a placeholder for future functionality
```

Examples:
- `// TODO(PHASE2-STUB): Hardware decode toggle - Always on for now`
- `// TODO(PHASE2-STUB): Video smoothing - Not wired to backend`
- `// TODO(PHASE2-STUB): Latency display - Need stream stats integration`

---

## Testing Checklist

For each screen:
- [ ] D-pad navigation works (Up/Down/Left/Right)
- [ ] X button activates controls
- [ ] Circle button returns to main menu
- [ ] L1/R1 work for tabs (Settings only)
- [ ] Visual highlights show current focus
- [ ] Settings save to config file
- [ ] No crashes or visual glitches
- [ ] Particle background and wave nav still render
- [ ] Touch input works (tap to select/activate)

---

## Config Integration Points

**Existing config fields to use:**
- `config.psn_account_id` - PSN account (Settings, Profile)
- `config.resolution` - Video resolution (Settings)
- `config.fps` - FPS target (Settings)
- `config.controller_map_id` - Controller mapping (Settings, Controller)
- `config.circle_btn_confirm` - Button layout preference (Settings)
- `config.auto_discovery` - Auto discovery toggle (Settings)

**Config functions:**
- `config_serialize(&context.config)` - Save changes
- `config_parse(&context.config)` - Load config

---

## File Structure

All changes in `vita/src/ui.c`:

```
// === PHASE 2: UI COMPONENTS ===
draw_rounded_rect()
draw_toggle_switch()
draw_dropdown()
draw_tab_bar()
draw_status_dot()

// === PHASE 2: SETTINGS SCREEN ===
SettingsState static vars
draw_settings_tab_bar()
draw_settings_streaming_tab()
draw_settings_video_tab()
draw_settings_network_tab()
draw_settings_controller_tab()
draw_settings() // Main function

// === PHASE 2: PROFILE SCREEN ===
draw_profile_screen()

// === PHASE 2: CONTROLLER CONFIG SCREEN ===
draw_controller_config_screen()
```

---

## Estimated Lines of Code

- UI Components: ~200 lines
- Settings Screen: ~400 lines
- Profile Screen: ~200 lines
- Controller Screen: ~150 lines
- **Total: ~950 lines**

## Estimated Time

- Components: 1 hour
- Settings: 2-3 hours
- Profile: 1-2 hours
- Controller: 1 hour
- Testing/Polish: 1 hour
- **Total: 6-8 hours**

---

## Success Criteria

- [ ] All 3 screens implemented
- [ ] Settings save/load from config
- [ ] Navigation works smoothly
- [ ] Visual design matches mockups
- [ ] Stubs clearly marked
- [ ] No regressions on main menu
- [ ] Clean commits with good messages
- [ ] Documentation updated
