# VitaRPS5 Project Structure Analysis

## Overview

This document analyzes the structure of both vitaki-fork (current working implementation) and vitarps5 (modern UI to migrate) for the UI migration project.

## vitaki-fork (Current Working Implementation)

### Directory Structure
```
vitaki-fork/
├── vita/                              # Original Vitaki PS Vita implementation
│   ├── src/
│   │   ├── main.c                    # Application entry point
│   │   ├── ui.c                      # Simple immediate-mode GUI (~1200 lines)
│   │   ├── context.c                 # Application context management
│   │   ├── host.c                    # Console host management
│   │   ├── discovery.c               # Console discovery
│   │   ├── config.c                  # Configuration management
│   │   ├── controller.c              # Input handling
│   │   ├── video.c                   # Video decoding
│   │   ├── audio.c                   # Audio processing
│   │   └── message_log.c             # Debug logging
│   ├── include/
│   │   ├── ui.h                      # UI interface (~34 lines)
│   │   ├── context.h                 # Application context
│   │   ├── host.h                    # Console management
│   │   └── [other headers]
│   ├── res/assets/                   # Simple UI assets
│   │   ├── btn_*.png                 # Button textures
│   │   ├── ps4.png, ps5.png          # Console images
│   │   └── fonts/                    # Font files
│   └── CMakeLists.txt                # Build configuration
├── lib/                              # Core Chiaki library
├── gui/                              # Desktop Qt implementation
├── android/                          # Android implementation
└── switch/                           # Nintendo Switch port
```

### Current UI Architecture
- **Single File Implementation**: All UI logic in `vita/src/ui.c`
- **Immediate Mode**: Simple immediate-mode GUI pattern
- **Basic Screens**: Main menu, settings, registration, messages
- **Simple Assets**: Basic PNG buttons and console images
- **Functional Design**: Plain interface focused on functionality

### UI Components Analysis
1. **Header Bar**: Button navigation (Add, Register, Discovery, Messages, Settings)
2. **Host Tiles**: 2x2 grid of console cards
3. **Settings Form**: Simple text inputs for PSN ID and controller mapping
4. **Registration Dialog**: PIN code input for console pairing
5. **Add Host Dialog**: Manual host addition form
6. **Messages View**: Scrollable debug log display

### Key Technical Details
- **Framework**: vita2d graphics library
- **Input**: Controller buttons + basic touch support
- **State Management**: Simple enum-based screen switching
- **Asset Loading**: Direct PNG file loading
- **Text Input**: IME dialog integration

## vitarps5 (Modern UI Implementation)

### Directory Structure
```
vitarps5/
├── src/
│   ├── ui/                           # Modular UI system
│   │   ├── vita2d_ui.h              # Modern UI definitions & constants (~342 lines)
│   │   ├── ui_core.c                # Core UI engine & state management
│   │   ├── ui_dashboard.c           # Main console selection view
│   │   ├── ui_settings.c            # Tabbed settings interface
│   │   ├── ui_components.c          # Reusable UI components
│   │   ├── ui_navigation.c          # Wave navigation system
│   │   ├── ui_controller.c          # Controller mapping interface
│   │   ├── ui_profile.c             # User profile management
│   │   ├── ui_streaming.c           # Active streaming interface
│   │   ├── ui_registration.c        # Console pairing workflow
│   │   └── ui_psn_login.c           # PSN authentication
│   ├── core/                        # Core functionality modules
│   ├── network/                     # Network & protocol handling
│   ├── video/                       # Video decoding pipeline
│   ├── audio/                       # Audio processing
│   └── [other modules]
├── assets/user_provided/             # Professional UI assets
│   ├── modern_assets/               # Modern UI elements
│   │   ├── toggle_on/off.png        # Custom toggle switches
│   │   ├── symbol_*.png             # PlayStation symbols
│   │   ├── wave_top.png             # Navigation effects
│   │   └── ellipse_*.png            # Status indicators
│   ├── icons/                       # Navigation & system icons
│   ├── images/                      # Branding & layout assets
│   └── ui_mockups_modern/           # Design references
└── CMakeLists.txt                   # Advanced build configuration
```

### Modern UI Architecture
- **Modular Design**: Each UI screen in separate file
- **Component System**: Reusable UI elements (toggles, dropdowns, sliders)
- **State Machine**: Sophisticated UI state management
- **Asset Management**: Comprehensive texture and icon system
- **Animation Framework**: Custom particle effects and transitions

### Key Modern Features
1. **Wave Navigation**: PlayStation symbol particles with smooth navigation
2. **Console Cards**: Modern card-based layout with status indicators
3. **Tabbed Settings**: 4-panel settings interface (Streaming/Video/Network/Controller)
4. **Touch Optimization**: Multi-touch and gesture support
5. **Professional Design**: PlayStation-inspired aesthetic with animations

### Modern UI Components
- **WaveNavigation**: Animated sidebar with PlayStation symbols
- **ConsoleCard**: Status-aware console selection cards
- **SettingsPanel**: Tabbed interface with modern controls
- **UIComponents**: Toggle switches, dropdowns, sliders with animations
- **Particle System**: PlayStation symbol effects

## Migration Analysis

### Assets to Migrate
1. **Modern Textures**: All files from `assets/user_provided/modern_assets/`
2. **Navigation Icons**: PlayStation-style navigation elements
3. **Status Indicators**: Green/red/yellow ellipses for console status
4. **Branding Assets**: VitaRPS5 logo and PlayStation design elements

### Code to Migrate
1. **UI Framework**: `vita2d_ui.h` - Complete modern UI definitions
2. **Core Engine**: `ui_core.c` - State management and rendering loop
3. **Component System**: `ui_components.c` - Reusable UI elements
4. **Navigation**: `ui_navigation.c` - Wave-based navigation system
5. **Dashboard**: `ui_dashboard.c` - Modern console grid
6. **Settings**: `ui_settings.c` - Tabbed settings interface

### Integration Points
1. **Host Management**: Preserve vitaki-fork's working host discovery/management
2. **Streaming Core**: Maintain existing Chiaki streaming integration
3. **Configuration**: Adapt modern UI to existing config system
4. **Input Handling**: Combine modern touch with existing controller support

## Success Metrics
- Modern PlayStation aesthetic replaces basic interface
- All existing functionality preserved and enhanced
- Smooth 60fps UI performance maintained
- Sub-50ms streaming latency preserved
- Professional code quality with modular architecture