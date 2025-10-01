# UI Development Guide

## Design Principles

### Layout Approach
- **Parent-child hierarchy** - Build UI elements within their containers
- **Consistent spacing** - Use constants for margins, padding, button sizes
- **Design system** - 3-4 font sizes, consistent colors throughout

### Constants and Consistency
```c
// Screen dimensions
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

// UI Constants
#define MARGIN_STANDARD 16
#define MARGIN_LARGE 32
#define BUTTON_HEIGHT 48
#define CARD_PADDING 20

// Font sizes
#define FONT_SIZE_LARGE 24
#define FONT_SIZE_MEDIUM 18
#define FONT_SIZE_SMALL 14
#define FONT_SIZE_TINY 12

// Colors (consistent theme)
#define COLOR_PRIMARY IM_COL32(0, 123, 255, 255)      // Blue
#define COLOR_SUCCESS IM_COL32(40, 167, 69, 255)      // Green
#define COLOR_WARNING IM_COL32(255, 193, 7, 255)      // Yellow
#define COLOR_DANGER IM_COL32(220, 53, 69, 255)       // Red
```

### Component Structure
```c
// Parent-child approach example
void RenderConsoleCard(const ConsoleInfo* console, ImVec2 card_pos, ImVec2 card_size) {
    ImGui::SetCursorScreenPos(card_pos);

    if (ImGui::BeginChild("ConsoleCard", card_size, true)) {
        // Title within card
        ImVec2 title_pos = ImGui::GetCursorPos();
        title_pos.x += CARD_PADDING;
        title_pos.y += CARD_PADDING;
        ImGui::SetCursorPos(title_pos);

        ImGui::PushFont(font_large);
        ImGui::Text("%s", console->nickname);
        ImGui::PopFont();

        // Status within card
        ImVec2 status_pos = title_pos;
        status_pos.y += FONT_SIZE_LARGE + MARGIN_STANDARD;
        ImGui::SetCursorPos(status_pos);

        RenderConsoleStatus(console->status);

        // Button within card (positioned relative to card)
        ImVec2 button_pos = {
            card_size.x - 120 - CARD_PADDING,  // Right-aligned within card
            card_size.y - BUTTON_HEIGHT - CARD_PADDING  // Bottom-aligned
        };
        ImGui::SetCursorPos(button_pos);

        if (ImGui::Button("Connect", ImVec2(120, BUTTON_HEIGHT))) {
            connect_to_console(console->ip);
        }
    }
    ImGui::EndChild();
}
```

## Performance Optimization

### Image Assets
```bash
# Always run pngquant on PNG assets
pngquant --quality=65-80 --output assets/optimized/ assets/source/*.png
```

### Efficient Rendering
```c
// Cache expensive calculations
static ImVec2 cached_button_pos = {0, 0};
static bool layout_calculated = false;

if (!layout_calculated) {
    cached_button_pos.x = SCREEN_WIDTH - 120 - MARGIN_STANDARD;
    cached_button_pos.y = SCREEN_HEIGHT - BUTTON_HEIGHT - MARGIN_STANDARD;
    layout_calculated = true;
}

ImGui::SetCursorScreenPos(cached_button_pos);
```

## Component Patterns

### Reusable Components
```c
// Status indicator component
void RenderStatusIndicator(ConnectionStatus status, ImVec2 pos) {
    ImGui::SetCursorScreenPos(pos);

    const char* status_text;
    ImU32 status_color;

    switch (status) {
        case STATUS_CONNECTED:
            status_text = "Connected";
            status_color = COLOR_SUCCESS;
            break;
        case STATUS_CONNECTING:
            status_text = "Connecting...";
            status_color = COLOR_WARNING;
            break;
        default:
            status_text = "Disconnected";
            status_color = COLOR_DANGER;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, status_color);
    ImGui::Text("%s", status_text);
    ImGui::PopStyleColor();
}

// Loading spinner component
void RenderLoadingSpinner(ImVec2 center, float radius) {
    static float rotation = 0.0f;
    rotation += ImGui::GetIO().DeltaTime * 3.0f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddCircle(center, radius, COLOR_PRIMARY, 12, 3.0f);

    ImVec2 spinner_pos = {
        center.x + cos(rotation) * radius,
        center.y + sin(rotation) * radius
    };
    draw_list->AddCircleFilled(spinner_pos, 4.0f, COLOR_PRIMARY);
}
```

### Controller Diagram
```c
void RenderControllerDiagram(const ControllerMapping* mapping) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    // Draw controller outline
    ImVec2 controller_center = {
        canvas_pos.x + canvas_size.x * 0.5f,
        canvas_pos.y + canvas_size.y * 0.5f
    };

    draw_list->AddRect(
        ImVec2(controller_center.x - 150, controller_center.y - 80),
        ImVec2(controller_center.x + 150, controller_center.y + 80),
        IM_COL32(255, 255, 255, 255), 10.0f, 0, 2.0f
    );

    // Highlight mapped buttons
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (mapping->buttons[i].is_mapped) {
            ImVec2 button_pos = GetButtonPosition(controller_center, i);
            draw_list->AddCircleFilled(button_pos, 12.0f, COLOR_PRIMARY);
        }
    }
}
```

## Screen Management

### Screen System
```c
typedef enum {
    SCREEN_MAIN_MENU,
    SCREEN_CONSOLE_LIST,
    SCREEN_STREAMING,
    SCREEN_SETTINGS,
    SCREEN_CONTROLLER_CONFIG
} ScreenType;

typedef struct {
    ScreenType current_screen;
    ScreenType previous_screen;
    void* screen_data;
} UIState;

void RenderCurrentScreen(UIState* ui_state) {
    switch (ui_state->current_screen) {
        case SCREEN_MAIN_MENU:
            RenderMainMenu();
            break;
        case SCREEN_CONSOLE_LIST:
            RenderConsoleList((ConsoleListData*)ui_state->screen_data);
            break;
        case SCREEN_STREAMING:
            RenderStreamingView((StreamingData*)ui_state->screen_data);
            break;
        // ... other screens
    }
}
```

## Input Handling

### Vita Controls
```c
void HandleVitaInput(UIState* ui_state) {
    SceCtrlData ctrl_data;
    sceCtrlPeekBufferPositive(0, &ctrl_data, 1);

    // Map Vita buttons to ImGui navigation
    if (ctrl_data.buttons & SCE_CTRL_CROSS) {
        ImGui::GetIO().KeysDown[ImGuiKey_Enter] = true;
    }
    if (ctrl_data.buttons & SCE_CTRL_CIRCLE) {
        ImGui::GetIO().KeysDown[ImGuiKey_Escape] = true;
    }

    // Analog stick to mouse movement
    float stick_x = (ctrl_data.lx - 128) / 128.0f;
    float stick_y = (ctrl_data.ly - 128) / 128.0f;

    if (abs(stick_x) > 0.1f || abs(stick_y) > 0.1f) {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos.x += stick_x * 5.0f;
        io.MousePos.y += stick_y * 5.0f;
    }
}
```

Remember: UI should feel responsive and professional, matching the quality of official Sony applications.
