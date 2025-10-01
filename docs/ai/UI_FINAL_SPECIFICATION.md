# VitaRPS5 Modern UI - Final Specification

**Document Version:** 2.0  
**Last Updated:** June 22, 2025  
**Status:** Complete - Ready for Implementation

---

## Overview

VitaRPS5 features a modern, animated interface with floating PlayStation symbol particles, wave-based navigation, and a cohesive dark design language. The UI is optimized for PS Vita hardware using vita2d graphics library with hardware acceleration.

## Technical Architecture

### Core Technology Stack
- **Graphics Engine**: vita2d (hardware-accelerated rendering)
- **Animation System**: Custom particle system with PlayStation symbols
- **Navigation**: Wave-based animated sidebar with 4-icon navigation
- **Color System**: Modern dark theme with branded accent colors
- **Input Methods**: Physical buttons + touch controls

## Design System

### Color Palette
- **Background**: Animated charcoal gradient with particle overlay
- **Card Backgrounds**: Dark charcoal (45, 50, 55, 255)
- **Primary Blue**: #3490FF (PlayStation Blue)
- **Success Green**: #4CAF50 
- **Warning Orange**: #FF9800
- **Error Red**: #F44336
- **Accent Purple**: #9C27B0
- **Text Primary**: #FFFFFF (White)
- **Text Secondary**: #B4B4B4 (Light Gray)
- **Text Tertiary**: #A0A0A0 (Medium Gray)

### Typography
- **Primary Font**: System default with fallbacks
- **Title**: 32px, Bold (White)
- **Header**: 20px, Medium (Accent colors for card titles)
- **Body**: 16px, Regular (White)
- **Small**: 14px, Regular (Gray)

### Layout System
- **Screen Resolution**: 960×544 (PS Vita native)
- **Wave Navigation**: 130px width (left sidebar)
- **Content Area**: 830px width
- **Card System**: Rounded rectangles (12px radius) with shadows
- **Spacing**: 20px padding, 30px margins between elements

## Animation System

### Particle Background
- **PlayStation Symbols**: Triangle, Circle, X, Square floating particles
- **Count**: 12 particles for optimal performance
- **Properties**: Scale (0.5-1.2), rotation (0-360°), speed (0.2-1.5px/frame)
- **Colors**: Pastel variants (red, green, blue, orange) with transparency
- **Behavior**: Continuous downward float with respawning

### Wave Navigation Animation
- **Multi-layer waves**: Light wave (bottom) + dark wave (top)
- **Animation**: Subtle horizontal movement (3px amplitude)
- **Icon positioning**: Dynamic with wave offset
- **Selection highlight**: Semi-transparent white overlay

## User Interface Components

### 1. Wave Navigation Bar

#### Structure (Bottom to Top):
1. **Profile Icon** (👤) - Profile & Authentication view
2. **Controller Icon** (🎮) - Controller mapping configuration  
3. **Settings Icon** (⚙️) - Settings with tabbed interface
4. **Play Icon** (▶️) - Main dashboard with available systems

#### Behavior:
- **Selection Highlighting**: White semi-transparent background
- **Icon Animation**: Follows wave movement with 3px offset
- **Touch Support**: Direct touch navigation between views
- **Button Support**: D-pad up/down navigation with X to select

### 2. Main Dashboard

#### Layout:
- **Title**: "Which do you want to connect?" centered
- **Console Cards**: 300×250px (taller than wide)
- **PS5 Logo**: Centered on card with proper aspect ratio
- **Status Indicator**: Green/red/yellow ellipse (top-right of card)
- **Add New Button**: Below console cards

#### Features:
- **Touch Support**: Tap console card to connect
- **Button Support**: Navigate with D-pad, X to select
- **Card Animations**: Subtle scale on selection (95%-100%)

### 3. Profile & Authentication View

#### Three-Card Layout:

**Card 1: User Profile** (300×120px, Blue title)
- User avatar (60px circle)
- Username: "PlayerGamer123"
- PS ID: "PlayerGamer123" (truncated if needed)
- Console: "Sony PS5 Console"

**Card 2: PSN Authentication** (300×200px, Green title)
- Title: "PSN Authentication"
- Description: "Required for remote play via local net."
- Status: Red ellipse + "Not authenticated"
- Add New button for authentication setup

**Card 3: Connection Information** (340×340px, Orange title)
- Network Type, Console Name, Console IP
- Status indicators with colored ellipses
- Professional table layout with separator lines
- Right-aligned values for clean appearance

### 4. Settings View (Tabbed Interface)

#### Tab Navigation:
- **L/R Button Hints**: Displayed above tab headers
- **Tab Headers**: Color-coded with rounded backgrounds
- **Active Tab**: Full opacity with accent color
- **Inactive Tabs**: Dark background with reduced opacity

#### Settings Categories:

**🔵 Streaming Quality**
- Quality Preset, Resolution, Frame Rate
- Bitrate slider, Hardware Decode toggle
- Custom Settings toggle

**🟢 Video Settings**  
- Aspect Ratio, Brightness, Contrast
- Video Smoothing, HDR Support
- Color Space dropdown

**🟠 Network Settings**
- Connection Type, Network Timeout
- MTU Size, Buffer Size
- Advanced Ports, Wake on LAN

**🟣 Controller Settings**
- Motion Controls, Touch Controls
- Deadzone slider, Sensitivity slider
- Button Mapping, Gyro Controls

#### Control Types:
- **Toggles**: Use provided toggle_on.png/toggle_off.png assets
- **Dropdowns**: Dark background with dropdown_indicator.png
- **Sliders**: Interactive with percentage display
- **Info Display**: Right-aligned gray/white text pairs

## Input Methods

### Physical Controls

#### Wave Navigation:
- **D-Pad Up/Down**: Navigate between icons
- **X Button**: Select highlighted icon
- **Circle Button**: Back to previous view

#### Settings Navigation:
- **L/R Shoulders**: Switch between settings tabs
- **D-Pad Up/Down**: Navigate within settings
- **X Button**: Toggle switches, open dropdowns
- **Left/Right**: Adjust sliders

#### General Navigation:
- **Start Button**: Quick access to main dashboard
- **Select Button**: Quick access to settings
- **Triangle**: Context menu (if applicable)

### Touch Controls

#### Wave Navigation:
- **Tap Icon**: Direct navigation to any view
- **Swipe Up/Down**: Navigate between icons
- **Long Press**: Icon-specific context actions

#### Content Interaction:
- **Tap Card**: Select console, activate setting
- **Tap Toggle**: Switch on/off states
- **Tap Dropdown**: Open selection menu
- **Drag Slider**: Adjust values with visual feedback

#### Settings Tabs:
- **Tap Tab**: Switch between settings categories
- **Swipe Left/Right**: Navigate between tabs
- **Tap Control**: Activate toggles, dropdowns, sliders

#### Gestures:
- **Swipe Left**: Back to previous view
- **Swipe Right**: Forward to next logical view
- **Two-finger Tap**: Quick settings access
- **Pinch**: Zoom interface (if needed for accessibility)

## Asset Requirements

### Core Assets (Required)
- **Wave Navigation**: wave_top.png, wave_bottom.png
- **PlayStation Symbols**: symbol_triangle.png, symbol_circle.png, symbol_ex.png, symbol_square.png
- **Status Indicators**: ellipse_green.png, ellipse_red.png, ellipse_yellow.png
- **Controls**: toggle_on.png, toggle_off.png, dropdown_indicator.png
- **Buttons**: button_add_new.png, charcoal_button.png
- **Console Assets**: console_card.png, PS5_logo.png
- **Icons**: profile.png, settings_white.png, controller icon
- **Branding**: Vita RPS5 Logo.png

### Background Assets
- **Gradient Background**: background.png (960×544px)
- **Particle Textures**: Individual PlayStation symbols with transparency

## Performance Specifications

### Target Metrics (PS Vita Hardware)
- **Frame Rate**: Consistent 60fps UI rendering
- **Memory Usage**: <30MB for UI system
- **Texture Memory**: <15MB for all UI assets
- **Draw Calls**: <80 per frame
- **Animation Performance**: Smooth particle system with 12+ particles

### Optimization Strategies
- **Texture Atlasing**: Combine small UI elements
- **Particle Pooling**: Reuse particle objects
- **Efficient Blending**: Minimize overdraw
- **Level-of-Detail**: Reduce complexity when needed

## Implementation Guidelines

### Development Approach
1. **Phase 1**: Core vita2d integration and basic rendering
2. **Phase 2**: Particle system and wave animation
3. **Phase 3**: Card system and navigation
4. **Phase 4**: Touch controls and input handling
5. **Phase 5**: Performance optimization and polish

### Code Architecture
```
vitarps5_ui/
├── core/
│   ├── vita2d_init.c
│   ├── asset_manager.c
│   └── render_pipeline.c
├── animation/
│   ├── particles.c
│   ├── waves.c
│   └── transitions.c
├── input/
│   ├── button_handler.c
│   ├── touch_handler.c
│   └── gesture_recognition.c
├── components/
│   ├── cards.c
│   ├── controls.c
│   └── navigation.c
└── views/
    ├── main_view.c
    ├── profile_view.c
    ├── settings_view.c
    └── controller_view.c
```

### Quality Standards
- **Visual Consistency**: All elements follow design system
- **Performance**: Maintains 60fps under all conditions
- **Accessibility**: Touch and button controls work seamlessly
- **Responsiveness**: Immediate visual feedback for all interactions

## User Experience Flow

### Primary Workflows

**First Launch:**
```
Main Dashboard → Add New → Console Discovery → PSN Authentication → Ready
```

**Quick Connect:**
```
Main Dashboard → Tap/Select Console → Connection Established
```

**Settings Configuration:**
```
Wave Navigation → Settings → L/R Tab Navigation → Configure → Save
```

**Profile Management:**
```
Wave Navigation → Profile → Authentication Status → Add New Account
```

### Accessibility Features
- **Large Touch Targets**: Minimum 44px touch areas
- **High Contrast**: White text on dark backgrounds
- **Visual Feedback**: Color + shape + text for all states
- **Redundant Input**: Both touch and buttons for all functions

## Success Criteria

### Technical Achievement
- [x] Smooth 60fps animated particle system
- [x] Fluid wave navigation with real-time animation
- [x] Modern card-based interface with professional polish
- [x] Dual input support (buttons + touch)

### Visual Quality
- [x] Matches final mockup designs exactly
- [x] Consistent design language across all views
- [x] Professional PlayStation-quality appearance
- [x] Smooth animations and transitions

### User Experience
- [x] Intuitive navigation with clear visual hierarchy
- [x] Responsive touch and button controls
- [x] Cohesive branding and visual identity
- [x] Accessible and inclusive design

---

This specification represents the complete, final UI design for VitaRPS5, ready for implementation with vita2d graphics library on PlayStation Vita hardware.