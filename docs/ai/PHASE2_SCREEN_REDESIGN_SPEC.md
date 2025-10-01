# Phase 2: Screen Redesign Implementation Spec

**Goal**: Redesign Settings, Profile/Registration, and Controller screens to match VitaRPS5 modern UI design.

**Date**: September 30, 2025
**Version**: v0.1.67+
**Status**: Planning

---

## Design System

### Visual Elements (From Mockups)

**Colors:**
- Background: Dark gray (#1A1D23)
- Card backgrounds: Slightly lighter (#2A2D33)
- Primary accent: PlayStation Blue (#0070CC)
- Section colors:
  - Streaming: Blue (#0070CC)
  - Video: Green (#2D8A3E)
  - Network: Orange (#D97706)
  - Controller: Purple (#7C3AED)

**Typography:**
- Headers: 24pt, white
- Section titles: 18pt, white
- Labels: 16pt, light gray
- Values: 16pt, white

**Components:**
- Rounded rectangles (12px radius) for cards
- Toggle switches (modern iOS-style)
- Dropdowns with chevron indicators
- Colored tab bars
- Status dots (green/yellow/red)

---

## Screen 1: Settings Screen

### Layout Structure

```
┌─────────────────────────────────────────────────────┐
│ [Wave Nav]  Settings                                │
│                                                       │
│             ┌─────────────────────────────────────┐ │
│             │ [Streaming] [Video] [Network] [Ctrl]│ │
│             │  Quality    Settings Settings Settings│
│             └─────────────────────────────────────┘ │
│                                                       │
│             ┌─────────────────────────────────────┐ │
│             │ Quality Preset      [Balanced ▼]    │ │
│             │ Hardware Decode     [Toggle ON]     │ │
│             │ Custom Settings     [Toggle OFF]    │ │
│             └─────────────────────────────────────┘ │
│                                                       │
│             Version: v0.1.67                          │
└─────────────────────────────────────────────────────┘
```

### Features

**Tab Bar:**
- 4 tabs with color-coded backgrounds
- L1/R1 to cycle tabs
- Selected tab has brighter color + indicator

**Settings Items:**
- Each item: Label (left) + Control (right)
- Toggle switches: Circular knob that slides
- Dropdowns: Value + down arrow, expands on X
- Sections visually separated

**Controls:**
- Up/Down: Navigate items within tab
- Left/Right: Cycle tabs
- X: Activate dropdown/toggle
- Circle: Back to main menu

### Implementation Tasks

1. Create tab bar rendering function
2. Create toggle switch rendering function
3. Create dropdown rendering function
4. Implement tab navigation (L1/R1)
5. Implement item navigation (Up/Down)
6. Wire to existing vitaki config system
7. Add visual feedback for selections

---

## Screen 2: Profile & Registration Screen

### Layout Structure

```
┌─────────────────────────────────────────────────────┐
│ [Wave Nav]  Profile & Authentication                │
│                                                       │
│         ┌─────────────────┐  ┌──────────────────┐  │
│         │  👤              │  │ Connection Info  │  │
│         │  PlayerGamer123 │  │ Network: Local   │  │
│         │  Account ID: 123│  │ Console IP: ...  │  │
│         └─────────────────┘  │ Latency: 45ms    │  │
│                              └──────────────────┘  │
│         ┌─────────────────────────────────────────┐│
│         │ PSN Authentication                      ││
│         │ ⚠ Not authenticated                     ││
│         │ [Add New]                               ││
│         └─────────────────────────────────────────┘│
└─────────────────────────────────────────────────────┘
```

### Features

**Profile Card:**
- Avatar placeholder (circular)
- Username
- Account info

**Connection Info Card:**
- Real-time connection stats
- Status indicators with colored dots
- Read-only information display

**PSN Authentication:**
- Status indicator (authenticated/not)
- Add New button
- Link code input when adding

### Implementation Tasks

1. Create profile card rendering
2. Create connection info card
3. Add status indicators (colored dots)
4. Integrate with existing registration flow
5. Display real-time connection info
6. Navigation between sections

---

## Screen 3: Controller Configuration

### Layout Structure

```
┌─────────────────────────────────────────────────────┐
│ [Wave Nav]  Controller Configuration                │
│                                                       │
│  ┌────────────────────┐  ┌────────────────────────┐ │
│  │ PS Vita Control    │  │ PS5 Button             │ │
│  ├────────────────────┤  ├────────────────────────┤ │
│  │ Cross    →  Cross  │  │                        │ │
│  │ Circle   →  Circle │  │      [Vita Diagram]    │ │
│  │ Square   →  Square │  │                        │ │
│  │ Triangle → Triangle│  │                        │ │
│  │ ...                │  └────────────────────────┘ │
│  └────────────────────┘                             │
└─────────────────────────────────────────────────────┘
```

### Features

**Mapping Table:**
- Two columns: Vita button | PS5 button
- Scrollable list of mappings
- Selected row highlighted
- Clear visual separation

**Vita Controller Diagram:**
- Visual representation of Vita
- Highlights selected button
- Shows button labels

### Implementation Tasks

1. Create two-column mapping table
2. Implement row selection and scrolling
3. Create simplified Vita controller diagram
4. Add button remapping functionality
5. Save/load mappings from config
6. Visual feedback for button presses

---

## Common UI Components to Build

These will be reusable across all screens:

1. **draw_rounded_rect(x, y, w, h, radius, color)** - Rounded rectangles for cards
2. **draw_toggle_switch(x, y, state)** - iOS-style toggle
3. **draw_dropdown(x, y, w, label, value, expanded)** - Dropdown control
4. **draw_tab_bar(tabs[], selected, colors[])** - Colored tab bar
5. **draw_status_dot(x, y, status)** - Colored status indicator
6. **draw_section_header(x, y, title, color)** - Section headers

---

## Implementation Order

### Phase 2A: Settings Screen (Highest Priority)
- Most complex
- Sets pattern for other screens
- Most frequently used

### Phase 2B: Profile/Registration Screen
- Moderate complexity
- Uses components from Settings
- Important for first-time setup

### Phase 2C: Controller Configuration Screen
- Simplest (mostly table)
- Uses established patterns
- Less frequently accessed

---

## Testing Checklist

For each screen:
- [ ] D-pad navigation works smoothly
- [ ] X button activates controls
- [ ] Circle button returns to main menu
- [ ] L1/R1 work for tabs (where applicable)
- [ ] Visual highlights are clear
- [ ] Touch input works (tap to select/activate)
- [ ] No visual glitches or overlaps
- [ ] Consistent with main menu aesthetics

---

## Notes

- Maintain wave nav sidebar and particle background across all screens
- Keep focus system consistent with main menu (FOCUS areas)
- Use existing vitaki config backend (don't recreate)
- All rendering with vita2d (no ImGui)
- Target 60 FPS on hardware
