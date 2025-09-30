#include "ui_streaming.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/console_registration.h"
#include "../input/input_mapping.h"
#include "../utils/logger.h"
#include "ui_core.h"
#include "ui_profile.h"
#include "vita2d_ui.h"

// Global streaming state
static struct {
  bool initialized;
  StreamingInfo info;

  // Session management
  RemotePlaySession* session;
  SessionCallbacks session_callbacks;

  // Video rendering
  VideoRenderer* video_renderer;
  VideoFrame* current_frame;

  // UI state
  bool overlay_visible;
  float overlay_fade_timer;
  uint64_t last_input_time;

  // Connection timeout handling
  uint64_t connection_start_time;
  uint32_t connection_timeout_ms;
  bool connection_cancelled;

  // Performance monitoring
  uint64_t stream_start_time;
  uint32_t total_frames_displayed;

} streaming_state = {0};

// Internal constants
#define OVERLAY_FADE_DURATION 3.0f   // 3 seconds
#define CONTROLS_HINT_DURATION 5.0f  // 5 seconds
#define CONNECTION_TIMEOUT_MS \
  (VITARPS5_TIMEOUT_SECONDS * 1000)  // Global timeout for connection

// Internal functions
static VitaRPS5Result initialize_video_renderer(uint32_t width,
                                                uint32_t height);
static void render_connection_screen(void);
static void render_video_stream(void);
static void render_overlay(void);
static void render_controls_hint(void);
static void render_error_screen(const char* error_message);
static void update_overlay_fade(void);
static bool check_connection_timeout(void);
static void handle_connection_cancellation(void);
static uint32_t vita_buttons_to_playstation(uint32_t vita_buttons);
static uint64_t get_timestamp_us(void);

// API Implementation

VitaRPS5Result ui_streaming_init(void) {
  if (streaming_state.initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing streaming UI subsystem");

  // Initialize session manager
  VitaRPS5Result result = session_manager_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize session manager: %s",
              vitarps5_result_string(result));
    return result;
  }

  // Initialize video renderer
  result = video_renderer_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize video renderer: %s",
              vitarps5_result_string(result));
    session_manager_cleanup();
    return result;
  }

  // Set up session callbacks
  streaming_state.session_callbacks.state_callback =
      ui_streaming_on_session_state_change;
  streaming_state.session_callbacks.video_callback =
      ui_streaming_on_video_frame;
  streaming_state.session_callbacks.audio_callback =
      NULL;  // TODO: Implement audio
  streaming_state.session_callbacks.error_callback =
      ui_streaming_on_session_error;
  streaming_state.session_callbacks.user_data = &streaming_state;

  // Initialize state
  streaming_state.info.ui_state = STREAMING_UI_STATE_DISCONNECTED;
  streaming_state.overlay_visible = true;
  streaming_state.info.show_overlay = true;
  streaming_state.info.show_controls_hint = true;

  log_info("Streaming UI state initialized: ui_state=%d, overlay_visible=%d",
           streaming_state.info.ui_state, streaming_state.overlay_visible);

  streaming_state.initialized = true;
  log_info("Streaming UI subsystem initialized successfully");

  return VITARPS5_SUCCESS;
}

void ui_streaming_cleanup(void) {
  if (!streaming_state.initialized) {
    return;
  }

  log_info("Cleaning up streaming UI subsystem");

  // Stop any active session
  ui_streaming_stop();

  // Cleanup video renderer
  if (streaming_state.video_renderer) {
    video_renderer_destroy(streaming_state.video_renderer);
    streaming_state.video_renderer = NULL;
  }

  video_renderer_cleanup();
  session_manager_cleanup();

  memset(&streaming_state, 0, sizeof(streaming_state));
  log_info("Streaming UI cleanup complete");
}

VitaRPS5Result ui_streaming_start(const char* console_ip,
                                  uint8_t console_version) {
  log_info("=== UI_STREAMING_START CALLED ===");
  log_info("Console IP: %s", console_ip ? console_ip : "NULL");
  log_info("Console version: %d", console_version);
  log_info("Streaming state initialized: %s",
           streaming_state.initialized ? "YES" : "NO");

  if (!streaming_state.initialized) {
    log_error("Streaming not initialized - this is the problem!");
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!console_ip) {
    log_error("Console IP is NULL - invalid parameter");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (streaming_state.session) {
    log_warning("Streaming session already active, stopping previous session");
    ui_streaming_stop();
  }

  log_info("Starting Remote Play session to %s (version %d)", console_ip,
           console_version);

  // Configure session
  log_info("Configuring streaming session...");
  SessionConfig config = {0};  // Initialize to zero first
  session_config_init(&config);
  strncpy(config.console_ip, console_ip, sizeof(config.console_ip) - 1);
  config.console_version = console_version;

  // Set authentication credentials using helper function
  VitaRPS5Result cred_result =
      session_config_set_credentials(&config, console_ip);
  if (cred_result != VITARPS5_SUCCESS) {
    log_warning("Session configured without credentials - connection may fail");
  }

  // Optimize for PS Vita hardware with reduced memory requirements
  config.video_width = 720;   // Reduced resolution to save VRAM
  config.video_height = 408;  // Reduced resolution (720p scaled for Vita)
  config.target_fps = 60;
  config.enable_hw_decode =
      false;  // Disable HW decode temporarily until VRAM issue resolved
  config.max_bitrate = 10;  // Reduced bitrate for Vita

  log_info("Session config: %dx%d@%dfps, bitrate=%dMbps, hw_decode=%s",
           config.video_width, config.video_height, config.target_fps,
           config.max_bitrate, config.enable_hw_decode ? "ON" : "OFF");

  // WAKE-FIX: Removed duplicate wake attempt - wake packet already sent by
  // dashboard This eliminates redundant wake operations and additional blocking
  // delays
  log_info(
      "Starting streaming session to %s (console already woken by dashboard)",
      console_ip);
  bool is_ps5 = (console_version == 12);

  // OPTIMIZATION: ChiakiSession handles HTTP session internally - no manual
  // setup needed
  log_info("PHASE-3 OWNER: ChiakiSession handles HTTP+RPCrypt+Takion protocol");
  log_info(
      "NO MANUAL HTTP: ChiakiSession performs /sie/ps5/rp/sess/init "
      "internally");

  // Create session - extended timing for PS5 wake process
  if (is_ps5) {
    log_info(
        "Creating PS5 streaming session - HTTP session complete, attempting "
        "Takion connection...");
  } else {
    log_info(
        "Creating streaming session - HTTP session complete, this may take a "
        "moment...");
  }
  VitaRPS5Result result = session_create(
      &config, &streaming_state.session_callbacks, &streaming_state.session);

  log_info("Session creation result: %s", vitarps5_result_string(result));
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create streaming session: %s",
              vitarps5_result_string(result));
    return result;
  }

  // Initialize video renderer for stream
  result = initialize_video_renderer(config.video_width, config.video_height);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize video renderer: %s",
              vitarps5_result_string(result));
    session_destroy(streaming_state.session);
    streaming_state.session = NULL;
    return result;
  }

  // Start session
  result = session_start(streaming_state.session);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to start streaming session: %s",
              vitarps5_result_string(result));
    video_renderer_destroy(streaming_state.video_renderer);
    streaming_state.video_renderer = NULL;
    session_destroy(streaming_state.session);
    streaming_state.session = NULL;
    return result;
  }

  // Update UI state
  streaming_state.info.ui_state = STREAMING_UI_STATE_CONNECTING;
  strncpy(streaming_state.info.console_ip, console_ip,
          sizeof(streaming_state.info.console_ip) - 1);
  snprintf(streaming_state.info.console_name,
           sizeof(streaming_state.info.console_name), "%s Console",
           console_version == 12 ? "PS5" : "PS4");

  // Initialize connection timeout tracking
  streaming_state.connection_start_time = get_timestamp_us();
  streaming_state.connection_timeout_ms = CONNECTION_TIMEOUT_MS;
  streaming_state.connection_cancelled = false;

  streaming_state.stream_start_time = get_timestamp_us();
  streaming_state.total_frames_displayed = 0;
  streaming_state.overlay_visible = true;
  streaming_state.last_input_time = get_timestamp_us();

  return VITARPS5_SUCCESS;
}

VitaRPS5Result ui_streaming_stop(void) {
  if (!streaming_state.session) {
    log_info("ui_streaming_stop: No active session to stop");
    return VITARPS5_SUCCESS;
  }

  log_info("ui_streaming_stop: Stopping Remote Play session");

  // Stop session
  VitaRPS5Result stop_result = session_stop(streaming_state.session);
  if (stop_result != VITARPS5_SUCCESS) {
    log_error("ui_streaming_stop: Failed to stop session: %s",
              vitarps5_result_string(stop_result));
  } else {
    log_info("ui_streaming_stop: Session stopped successfully");
  }

  session_destroy(streaming_state.session);
  log_info("ui_streaming_stop: Session destroyed successfully");

  streaming_state.session = NULL;

  // Cleanup video renderer
  if (streaming_state.video_renderer) {
    log_info("ui_streaming_stop: Stopping video renderer");
    video_renderer_stop(streaming_state.video_renderer);
    video_renderer_destroy(streaming_state.video_renderer);
    streaming_state.video_renderer = NULL;
    log_info("ui_streaming_stop: Video renderer cleanup completed");
  }

  // Reset current frame
  streaming_state.current_frame = NULL;

  // Update UI state
  streaming_state.info.ui_state = STREAMING_UI_STATE_DISCONNECTED;
  log_info("ui_streaming_stop: UI state set to DISCONNECTED");

  // Ensure all cleanup operations complete before returning
  // Add a small delay to ensure all threads have stopped
  sceKernelDelayThread(100000);  // 100ms delay

  log_info("ui_streaming_stop: Remote Play session cleanup completed");
  return VITARPS5_SUCCESS;
}

VitaRPS5Result ui_streaming_update(void) {
  if (!streaming_state.initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  // Check for connection timeout or cancellation during connecting state
  if (streaming_state.info.ui_state == STREAMING_UI_STATE_CONNECTING) {
    if (streaming_state.connection_cancelled) {
      log_info("Connection cancelled by user");
      handle_connection_cancellation();
      return VITARPS5_SUCCESS;
    }

    if (check_connection_timeout()) {
      log_error("Connection timed out after %d seconds",
                CONNECTION_TIMEOUT_MS / 1000);

      // Set error state first
      streaming_state.info.ui_state = STREAMING_UI_STATE_ERROR;

      // Store timeout error message
      snprintf(streaming_state.info.error_message,
               sizeof(streaming_state.info.error_message),
               "Connection timed out after %d seconds. Check your network "
               "connection and try again.",
               CONNECTION_TIMEOUT_MS / 1000);

      // Stop and destroy session but keep video renderer alive for error screen
      if (streaming_state.session) {
        log_info("Stopping Remote Play session due to timeout");
        session_stop(streaming_state.session);
        session_destroy(streaming_state.session);
        streaming_state.session = NULL;
      }

      // Reset current frame but keep video renderer for error screen rendering
      streaming_state.current_frame = NULL;

      // Video renderer will be cleaned up when user presses Circle to exit
      log_info(
          "Session stopped due to timeout - press Circle to return to "
          "dashboard");
      return VITARPS5_SUCCESS;
    }
  }

  // Update session if active
  if (streaming_state.session) {
    VitaRPS5Result result = session_update(streaming_state.session);
    if (result != VITARPS5_SUCCESS && result != VITARPS5_ERROR_TIMEOUT) {
      log_error("Session update failed: %s", vitarps5_result_string(result));
      streaming_state.info.ui_state = STREAMING_UI_STATE_ERROR;
    }

    // Update session stats
    session_get_stats(streaming_state.session,
                      &streaming_state.info.session_stats);
    streaming_state.info.session_state =
        streaming_state.info.session_stats.current_state;
  }

  // Update video renderer
  if (streaming_state.video_renderer) {
    video_renderer_update(streaming_state.video_renderer);
  }

  // Update overlay fade
  update_overlay_fade();

  return VITARPS5_SUCCESS;
}

void ui_streaming_render(void) {
  if (!streaming_state.initialized) {
    return;
  }

  switch (streaming_state.info.ui_state) {
    case STREAMING_UI_STATE_CONNECTING:
      render_connection_screen();
      break;

    case STREAMING_UI_STATE_ACTIVE:
      render_video_stream();
      if (streaming_state.overlay_visible ||
          streaming_state.info.show_overlay) {
        render_overlay();
      }
      if (streaming_state.info.show_controls_hint) {
        render_controls_hint();
      }
      break;

    case STREAMING_UI_STATE_PAUSED:
      render_video_stream();
      render_overlay();  // Always show overlay when paused
      break;

    case STREAMING_UI_STATE_ERROR:
      render_error_screen(streaming_state.info.error_message[0]
                              ? streaming_state.info.error_message
                              : "Connection Error");
      break;

    case STREAMING_UI_STATE_DISCONNECTED:
      // Render background or return to main menu
      ui_core_render_background();
      break;
  }
}

void ui_streaming_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad) {
  if (!streaming_state.initialized || !pad || !prev_pad) {
    return;
  }

  streaming_state.last_input_time = get_timestamp_us();

  // Handle UI controls first
  bool start_pressed =
      (pad->buttons & SCE_CTRL_START) && !(prev_pad->buttons & SCE_CTRL_START);
  bool select_pressed = (pad->buttons & SCE_CTRL_SELECT) &&
                        !(prev_pad->buttons & SCE_CTRL_SELECT);

  if (start_pressed) {
    ui_streaming_toggle_overlay();
  }

  if (select_pressed) {
    ui_streaming_toggle_pause();
  }

  // Handle X button - wake console if sleeping
  bool x_pressed =
      (pad->buttons & SCE_CTRL_CROSS) && !(prev_pad->buttons & SCE_CTRL_CROSS);
  if (x_pressed && streaming_state.info.ui_state == STREAMING_UI_STATE_ERROR) {
    // Check if this is a console sleeping error
    if (strstr(streaming_state.info.error_message, "sleeping") != NULL) {
      log_info("User attempting to wake sleeping console");

      // Attempt to wake console
      VitaRPS5Result wake_result =
          session_wake_and_wait(streaming_state.info.console_ip);
      if (wake_result == VITARPS5_SUCCESS) {
        // Update error message to show wake was sent
        snprintf(streaming_state.info.error_message,
                 sizeof(streaming_state.info.error_message),
                 "Wake signal sent to console.\nPlease wait 8 seconds, then "
                 "try connecting again.\nPress Circle to return to dashboard.");
        log_info("Wake signal sent to console %s",
                 streaming_state.info.console_ip);
      } else {
        // Update error message to show wake failed
        snprintf(
            streaming_state.info.error_message,
            sizeof(streaming_state.info.error_message),
            "Failed to wake console: %s\nPress Circle to return to dashboard.",
            vitarps5_result_string(wake_result));
        log_error("Failed to wake console: %s",
                  vitarps5_result_string(wake_result));
      }
    }
    return;
  }

  // Handle Circle button - cancel connection or exit streaming
  bool circle_pressed = (pad->buttons & SCE_CTRL_CIRCLE) &&
                        !(prev_pad->buttons & SCE_CTRL_CIRCLE);

  // DEBUG: Log circle button state for debugging
  if (circle_pressed) {
    log_info("=== CIRCLE BUTTON PRESSED DEBUG ===");
    log_info("Current streaming UI state: %d", streaming_state.info.ui_state);
    log_info("STREAMING_UI_STATE_ERROR constant: %d", STREAMING_UI_STATE_ERROR);
    log_info("Button state: pad=0x%08X, prev_pad=0x%08X", pad->buttons,
             prev_pad->buttons);
  }

  if (circle_pressed) {
    if (streaming_state.info.ui_state == STREAMING_UI_STATE_CONNECTING) {
      // Cancel connection in progress
      log_info("Circle pressed: Cancelling connection in progress");
      streaming_state.connection_cancelled = true;
      return;
    } else if (streaming_state.info.ui_state == STREAMING_UI_STATE_ERROR) {
      // Exit error screen and return to dashboard
      log_info("=== CIRCLE BUTTON ERROR SCREEN EXIT ===");
      log_info("Circle pressed: User exiting error screen");
      log_info("Current UI core state: %d", ui_core_get_state());

      // Clear any modal states that might interfere
      log_info("Clearing any blocking states before navigation...");

      VitaRPS5Result stop_result = ui_streaming_stop();
      log_info("ui_streaming_stop() result: %s",
               vitarps5_result_string(stop_result));

      if (stop_result == VITARPS5_SUCCESS) {
        log_info("Session cleanup completed, transitioning to dashboard");

        // Add small delay to ensure cleanup completes
        sceKernelDelayThread(50000);  // 50ms delay

        UIState pre_transition_state = ui_core_get_state();
        log_info("UI state before transition: %d", pre_transition_state);

        ui_core_set_state(UI_STATE_MAIN_DASHBOARD);
        log_info("ui_core_set_state(UI_STATE_MAIN_DASHBOARD) called");

        // Verify the state transition was successful with retry
        UIState current_ui_state = ui_core_get_state();
        log_info("UI state after transition: %d", current_ui_state);

        if (current_ui_state == UI_STATE_MAIN_DASHBOARD) {
          log_info("✅ UI state transition to dashboard confirmed");
          log_info("Circle button navigation SUCCESS");
        } else {
          log_error("❌ UI state transition failed: expected %d, got %d",
                    UI_STATE_MAIN_DASHBOARD, current_ui_state);
          log_error("Attempting fallback state transition...");

          // Fallback: Force state transition with delay
          sceKernelDelayThread(100000);  // 100ms delay
          ui_core_set_state(UI_STATE_MAIN_DASHBOARD);

          UIState fallback_state = ui_core_get_state();
          log_info("Fallback state check: %d", fallback_state);
        }
      } else {
        log_error("Failed to stop streaming session: %s",
                  vitarps5_result_string(stop_result));
        log_error(
            "FALLBACK: Session stop failed - forcing UI navigation anyway");

        // Force UI navigation even if input cleanup failed
        log_info(
            "FALLBACK: Forcing UI state transition despite input cleanup "
            "issues");
        ui_core_set_state(UI_STATE_MAIN_DASHBOARD);

        // Give UI time to process state change
        sceKernelDelayThread(50000);  // 50ms

        UIState fallback_state = ui_core_get_state();
        if (fallback_state == UI_STATE_MAIN_DASHBOARD) {
          log_info(
              "✅ FALLBACK: UI navigation succeeded despite session cleanup "
              "failure");
        } else {
          log_warning(
              "FALLBACK: UI navigation may have failed, current state: %d",
              fallback_state);
        }
      }
      return;
    } else if (streaming_state.overlay_visible &&
               streaming_state.info.ui_state == STREAMING_UI_STATE_ACTIVE) {
      // Exit streaming when overlay visible during active streaming
      log_info("User exiting active streaming session");
      VitaRPS5Result stop_result = ui_streaming_stop();
      if (stop_result == VITARPS5_SUCCESS) {
        log_info("Session cleanup completed, transitioning to dashboard");
        ui_core_set_state(UI_STATE_MAIN_DASHBOARD);
        log_info("UI state set to MAIN_DASHBOARD");

        // Verify the state transition was successful
        UIState current_ui_state = ui_core_get_state();
        if (current_ui_state == UI_STATE_MAIN_DASHBOARD) {
          log_info("✅ UI state transition to dashboard confirmed");
        } else {
          log_error("❌ UI state transition failed: expected %d, got %d",
                    UI_STATE_MAIN_DASHBOARD, current_ui_state);
        }
      } else {
        log_error("Failed to stop streaming session: %s",
                  vitarps5_result_string(stop_result));
      }
      return;
    }
  }

  // Send input to PlayStation if streaming is active
  if (streaming_state.info.ui_state == STREAMING_UI_STATE_ACTIVE &&
      streaming_state.session) {
    // Convert PS Vita controls to PlayStation format
    uint32_t ps_buttons = vita_buttons_to_playstation(pad->buttons);

    // Send analog sticks (PS Vita uses 0-255, PlayStation uses -32768 to 32767)
    int16_t left_x = (int16_t)((pad->lx - 128) * 256);
    int16_t left_y = (int16_t)((pad->ly - 128) * 256);
    int16_t right_x = (int16_t)((pad->rx - 128) * 256);
    int16_t right_y = (int16_t)((pad->ry - 128) * 256);

    // Map L2/R2 triggers (PS Vita doesn't have analog triggers, simulate with
    // buttons)
    uint8_t left_trigger = (pad->buttons & SCE_CTRL_LTRIGGER) ? 255 : 0;
    uint8_t right_trigger = (pad->buttons & SCE_CTRL_RTRIGGER) ? 255 : 0;

    session_send_input(streaming_state.session, ps_buttons, left_x, left_y,
                       right_x, right_y, left_trigger, right_trigger);
  }
}

// Session event callbacks

void ui_streaming_on_session_state_change(SessionState state, void* user_data) {
  log_debug("Session state changed: %s", session_state_string(state));

  // Don't override error state once set (it's a final state during active
  // sessions) But allow transitions from DISCONNECTED to CONNECTING when
  // starting new sessions
  if (streaming_state.info.ui_state == STREAMING_UI_STATE_ERROR) {
    log_debug("Ignoring session state change - UI in error state: %d",
              streaming_state.info.ui_state);
    return;
  }

  switch (state) {
    case SESSION_STATE_STREAMING:
      streaming_state.info.ui_state = STREAMING_UI_STATE_ACTIVE;
      streaming_state.info.show_controls_hint = true;
      log_info("Remote Play streaming started successfully");
      break;

    case SESSION_STATE_CONNECTING:
      streaming_state.info.ui_state = STREAMING_UI_STATE_CONNECTING;
      break;

    case SESSION_STATE_ERROR:
      streaming_state.info.ui_state = STREAMING_UI_STATE_ERROR;
      log_error("Remote Play session error");
      // Set generic error message if not already set
      if (strlen(streaming_state.info.error_message) == 0) {
        snprintf(streaming_state.info.error_message,
                 sizeof(streaming_state.info.error_message),
                 "Connection failed: Network error");
      }
      break;

    case SESSION_STATE_IDLE:
      // Only transition to disconnected if not already in error state
      if (streaming_state.info.ui_state != STREAMING_UI_STATE_ERROR) {
        streaming_state.info.ui_state = STREAMING_UI_STATE_DISCONNECTED;
      }
      break;

    default:
      break;
  }
}

void ui_streaming_on_video_frame(const VideoFrame* frame, void* user_data) {
  if (!frame || !streaming_state.video_renderer) {
    return;
  }

  // Render frame to screen
  VitaRPS5Result result =
      video_renderer_render_frame(streaming_state.video_renderer, frame);
  if (result == VITARPS5_SUCCESS) {
    streaming_state.total_frames_displayed++;

    // Hide controls hint after first few frames
    if (streaming_state.total_frames_displayed > 180) {  // ~3 seconds at 60fps
      streaming_state.info.show_controls_hint = false;
    }
  }
}

void ui_streaming_on_session_error(VitaRPS5Result error, const char* message,
                                   void* user_data) {
  log_error("Session error: %s - %s", vitarps5_result_string(error),
            message ? message : "Unknown error");
  streaming_state.info.ui_state = STREAMING_UI_STATE_ERROR;

  // Store error message for display with console-specific guidance
  if (error == VITARPS5_ERROR_CONSOLE_SLEEPING) {
    snprintf(streaming_state.info.error_message,
             sizeof(streaming_state.info.error_message),
             "Console is sleeping.\nPress X to wake up console, then try "
             "again.\nPress Circle to return to dashboard.");
  } else if (error == VITARPS5_ERROR_NOT_REGISTERED) {
    snprintf(streaming_state.info.error_message,
             sizeof(streaming_state.info.error_message),
             "Console not registered.\nPlease register console first.\nPress "
             "Circle to return to dashboard.");
  } else if (message) {
    snprintf(streaming_state.info.error_message,
             sizeof(streaming_state.info.error_message),
             "Connection failed: %s\nPress Circle to return to dashboard.",
             message);
  } else {
    snprintf(streaming_state.info.error_message,
             sizeof(streaming_state.info.error_message),
             "Connection failed: %s\nPress Circle to return to dashboard.",
             vitarps5_result_string(error));
  }
}

// Internal helper functions

static VitaRPS5Result initialize_video_renderer(uint32_t width,
                                                uint32_t height) {
  VideoRendererConfig config = {0};
  config.display_width = 960;   // PS Vita screen width
  config.display_height = 544;  // PS Vita screen height
  config.stream_width = width;
  config.stream_height = height;
  config.maintain_aspect_ratio = true;
  config.show_debug_overlay = false;

  VitaRPS5Result result =
      video_renderer_create(&config, &streaming_state.video_renderer);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  return video_renderer_start(streaming_state.video_renderer);
}

static void render_connection_screen(void) {
  vita2d_pgf* font = ui_core_get_font();

  // Background
  ui_core_render_background();

  // Connection status
  const char* status_text = "Connecting to PlayStation...";
  int text_width = vita2d_pgf_text_width(font, 1.2f, status_text);
  int x = (960 - text_width) / 2;
  vita2d_pgf_draw_text(font, x, 250, UI_COLOR_TEXT_PRIMARY, 1.2f, status_text);

  // Console information
  char info_text[128];
  snprintf(info_text, sizeof(info_text), "%s (%s)",
           streaming_state.info.console_name, streaming_state.info.console_ip);
  text_width = vita2d_pgf_text_width(font, 1.0f, info_text);
  x = (960 - text_width) / 2;
  vita2d_pgf_draw_text(font, x, 300, UI_COLOR_TEXT_SECONDARY, 1.0f, info_text);

  // Show timeout countdown
  if (streaming_state.connection_start_time > 0) {
    uint64_t now = get_timestamp_us();
    uint64_t elapsed_ms = (now - streaming_state.connection_start_time) / 1000;

    // Prevent integer underflow when elapsed time exceeds timeout
    if (elapsed_ms < streaming_state.connection_timeout_ms) {
      uint32_t remaining_ms =
          streaming_state.connection_timeout_ms - elapsed_ms;
      uint32_t remaining_s = remaining_ms / 1000;

      if (remaining_s > 0) {
        char timeout_text[64];
        snprintf(timeout_text, sizeof(timeout_text), "Timeout in %d seconds",
                 remaining_s);
        text_width = vita2d_pgf_text_width(font, 0.9f, timeout_text);
        x = (960 - text_width) / 2;
        vita2d_pgf_draw_text(font, x, 350, UI_COLOR_TEXT_TERTIARY, 0.9f,
                             timeout_text);
      }
    }
  }

  // Spinning animation (simple dot animation)
  static float anim_time = 0;
  anim_time += 0.1f;
  if (anim_time > 4.0f) anim_time = 0;

  char dots[5] = "....";
  for (int i = 0; i < (int)anim_time; i++) {
    dots[i] = '.';
  }
  dots[(int)anim_time] = '\0';

  vita2d_pgf_draw_text(font, x + text_width + 10, 300, UI_COLOR_TEXT_SECONDARY,
                       1.0f, dots);

  // Instructions
  vita2d_pgf_draw_text(font, 50, 500, UI_COLOR_TEXT_TERTIARY, 0.9f,
                       "Press CIRCLE to cancel connection");
}

static void render_video_stream(void) {
  // Clear screen first
  vita2d_clear_screen();

  if (streaming_state.video_renderer) {
    // Update video renderer
    video_renderer_update(streaming_state.video_renderer);

    // Draw video frame to screen with proper scaling
    VitaRPS5Result result =
        video_renderer_draw_to_screen(streaming_state.video_renderer);
    if (result != VITARPS5_SUCCESS) {
      log_warning("Failed to draw video frame: %s",
                  vitarps5_result_string(result));
    }
  } else if (!streaming_state.current_frame) {
    // Fallback: black background if no renderer or frame
    vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 255));
  }
}

static void render_overlay(void) {
  vita2d_pgf* font = ui_core_get_font();

  // Semi-transparent overlay background
  uint32_t overlay_bg = RGBA8(0, 0, 0, 180);
  vita2d_draw_rectangle(0, 0, 960, 80, overlay_bg);

  // Session information
  char status_text[128];
  const char* state_str =
      session_state_string(streaming_state.info.session_state);
  snprintf(status_text, sizeof(status_text), "%s - %s",
           streaming_state.info.console_name, state_str);
  vita2d_pgf_draw_text(font, 20, 30, UI_COLOR_TEXT_PRIMARY, 1.0f, status_text);

  // Performance stats
  char stats_text[128];
  snprintf(stats_text, sizeof(stats_text),
           "FPS: %.1f | Latency: %.1fms | Quality: %.0f%%",
           streaming_state.info.session_stats.current_fps,
           streaming_state.info.session_stats.latency_ms,
           streaming_state.info.session_stats.connection_quality * 100.0f);
  vita2d_pgf_draw_text(font, 20, 55, UI_COLOR_TEXT_SECONDARY, 0.8f, stats_text);

  // Controls
  vita2d_pgf_draw_text(font, 700, 30, UI_COLOR_TEXT_TERTIARY, 0.8f,
                       "START: Toggle Overlay");
  vita2d_pgf_draw_text(font, 700, 50, UI_COLOR_TEXT_TERTIARY, 0.8f,
                       "CIRCLE: Exit Stream");
}

static void render_controls_hint(void) {
  vita2d_pgf* font = ui_core_get_font();

  // Bottom overlay with controls
  uint32_t hint_bg = RGBA8(0, 0, 0, 200);
  vita2d_draw_rectangle(0, 480, 960, 64, hint_bg);

  vita2d_pgf_draw_text(
      font, 20, 510, UI_COLOR_TEXT_PRIMARY, 0.9f,
      "Remote Play Active - All controls forwarded to PlayStation");
  vita2d_pgf_draw_text(font, 20, 530, UI_COLOR_TEXT_SECONDARY, 0.8f,
                       "START: Show/Hide Info | SELECT: Pause | CIRCLE: Exit");
}

static void render_error_screen(const char* error_message) {
  vita2d_pgf* font = ui_core_get_font();

  // Background
  ui_core_render_background();

  // Error message
  const char* title = "Connection Failed";
  int text_width = vita2d_pgf_text_width(font, 1.2f, title);
  int x = (960 - text_width) / 2;
  vita2d_pgf_draw_text(font, x, 200, UI_COLOR_STATUS_UNAVAILABLE, 1.2f, title);

  if (error_message) {
    text_width = vita2d_pgf_text_width(font, 1.0f, error_message);
    x = (960 - text_width) / 2;
    vita2d_pgf_draw_text(font, x, 250, UI_COLOR_TEXT_SECONDARY, 1.0f,
                         error_message);
  }

  // Instructions
  vita2d_pgf_draw_text(font, 50, 500, UI_COLOR_TEXT_TERTIARY, 0.9f,
                       "Press CIRCLE to return to main menu");
}

static void update_overlay_fade(void) {
  // Auto-hide overlay after period of inactivity
  uint64_t now = get_timestamp_us();
  uint64_t time_since_input =
      (now - streaming_state.last_input_time) / 1000000;  // Convert to seconds

  if (time_since_input > OVERLAY_FADE_DURATION &&
      streaming_state.info.ui_state == STREAMING_UI_STATE_ACTIVE) {
    streaming_state.overlay_visible = false;
  }
}

static bool check_connection_timeout(void) {
  if (streaming_state.connection_start_time == 0) {
    return false;
  }

  uint64_t now = get_timestamp_us();
  uint64_t elapsed_ms = (now - streaming_state.connection_start_time) / 1000;

  return elapsed_ms > streaming_state.connection_timeout_ms;
}

static void handle_connection_cancellation(void) {
  log_info("Handling connection cancellation");

  // Stop the session cleanly
  ui_streaming_stop();

  // Return to main dashboard
  ui_core_set_state(UI_STATE_MAIN_DASHBOARD);
}

static uint32_t vita_buttons_to_playstation(uint32_t vita_buttons) {
  uint32_t ps_buttons = 0;

  // Face buttons (swapped for PlayStation layout)
  if (vita_buttons & SCE_CTRL_CROSS) ps_buttons |= (1 << 0);     // Cross
  if (vita_buttons & SCE_CTRL_CIRCLE) ps_buttons |= (1 << 1);    // Circle
  if (vita_buttons & SCE_CTRL_SQUARE) ps_buttons |= (1 << 2);    // Square
  if (vita_buttons & SCE_CTRL_TRIANGLE) ps_buttons |= (1 << 3);  // Triangle

  // Shoulder buttons
  if (vita_buttons & SCE_CTRL_LTRIGGER) ps_buttons |= (1 << 4);  // L1
  if (vita_buttons & SCE_CTRL_RTRIGGER) ps_buttons |= (1 << 5);  // R1

  // D-pad
  if (vita_buttons & SCE_CTRL_UP) ps_buttons |= (1 << 8);      // D-Up
  if (vita_buttons & SCE_CTRL_DOWN) ps_buttons |= (1 << 9);    // D-Down
  if (vita_buttons & SCE_CTRL_LEFT) ps_buttons |= (1 << 10);   // D-Left
  if (vita_buttons & SCE_CTRL_RIGHT) ps_buttons |= (1 << 11);  // D-Right

  // System buttons
  if (vita_buttons & SCE_CTRL_START)
    ps_buttons |= (1 << 12);  // Options (PS4/PS5)
  if (vita_buttons & SCE_CTRL_SELECT)
    ps_buttons |= (1 << 13);  // Share (PS4) / Create (PS5)

  return ps_buttons;
}

static uint64_t get_timestamp_us(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick;
}

// Public utility functions

StreamingUIState ui_streaming_get_state(void) {
  return streaming_state.info.ui_state;
}

VitaRPS5Result ui_streaming_get_info(StreamingInfo* info) {
  if (!info) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *info = streaming_state.info;
  return VITARPS5_SUCCESS;
}

void ui_streaming_toggle_overlay(void) {
  streaming_state.overlay_visible = !streaming_state.overlay_visible;
  streaming_state.info.show_overlay = streaming_state.overlay_visible;
  streaming_state.last_input_time = get_timestamp_us();  // Reset fade timer
}

VitaRPS5Result ui_streaming_toggle_pause(void) {
  if (!streaming_state.session) {
    return VITARPS5_ERROR_NOT_CONNECTED;
  }

  if (streaming_state.info.ui_state == STREAMING_UI_STATE_ACTIVE) {
    // TODO: Implement session pause
    streaming_state.info.ui_state = STREAMING_UI_STATE_PAUSED;
    streaming_state.overlay_visible = true;
    log_info("Streaming paused");
  } else if (streaming_state.info.ui_state == STREAMING_UI_STATE_PAUSED) {
    // TODO: Implement session resume
    streaming_state.info.ui_state = STREAMING_UI_STATE_ACTIVE;
    log_info("Streaming resumed");
  }

  return VITARPS5_SUCCESS;
}