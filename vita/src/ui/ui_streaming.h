#ifndef VITARPS5_UI_STREAMING_H
#define VITARPS5_UI_STREAMING_H

#include <psp2/ctrl.h>

#include "../core/session_manager.h"
#include "../core/vitarps5.h"
#include "../video/video_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Streaming UI state
typedef enum {
  STREAMING_UI_STATE_CONNECTING = 0,
  STREAMING_UI_STATE_ACTIVE,
  STREAMING_UI_STATE_PAUSED,
  STREAMING_UI_STATE_ERROR,
  STREAMING_UI_STATE_DISCONNECTED
} StreamingUIState;

// Streaming session information
typedef struct {
  char console_name[64];
  char console_ip[16];
  SessionState session_state;
  SessionStats session_stats;
  bool show_overlay;
  bool show_controls_hint;
  StreamingUIState ui_state;
  char error_message[256];  // Store error message for display
} StreamingInfo;

// Core Streaming UI API

/**
 * Initialize streaming UI subsystem
 */
VitaRPS5Result ui_streaming_init(void);

/**
 * Cleanup streaming UI subsystem
 */
void ui_streaming_cleanup(void);

/**
 * Start streaming session with PlayStation console
 */
VitaRPS5Result ui_streaming_start(const char* console_ip,
                                  uint8_t console_version);

/**
 * Stop current streaming session
 */
VitaRPS5Result ui_streaming_stop(void);

/**
 * Update streaming UI (call regularly)
 */
VitaRPS5Result ui_streaming_update(void);

/**
 * Render streaming UI
 */
void ui_streaming_render(void);

/**
 * Handle input during streaming
 */
void ui_streaming_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad);

/**
 * Get current streaming state
 */
StreamingUIState ui_streaming_get_state(void);

/**
 * Get streaming session information
 */
VitaRPS5Result ui_streaming_get_info(StreamingInfo* info);

/**
 * Toggle overlay visibility
 */
void ui_streaming_toggle_overlay(void);

/**
 * Pause/resume streaming
 */
VitaRPS5Result ui_streaming_toggle_pause(void);

// Session event callbacks (internal)
void ui_streaming_on_session_state_change(SessionState state, void* user_data);
void ui_streaming_on_video_frame(const VideoFrame* frame, void* user_data);
void ui_streaming_on_session_error(VitaRPS5Result error, const char* message,
                                   void* user_data);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_UI_STREAMING_H