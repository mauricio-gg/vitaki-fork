#include "vitarps5.h"

#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../audio/audio_decoder.h"
#include "../auth/ps5_auth.h"
#include "../discovery/ps5_discovery.h"
#include "../network/takion.h"
#include "../performance/performance_monitor.h"
#include "../utils/logger.h"
#include "../video/video_decoder.h"
#include "../video/video_renderer.h"

// Internal session structure
struct VitaRPS5Session {
  VitaRPS5Config config;
  VitaRPS5State state;
  VitaRPS5Stats stats;

  // Callbacks
  VitaRPS5StateCallback state_callback;
  VitaRPS5ErrorCallback error_callback;
  void* callback_user_data;

  // Takion connection
  TakionConnection* takion_connection;

  // Video decoder
  VideoDecoder* video_decoder;

  // Video renderer
  VideoRenderer* video_renderer;

  // Audio decoder
  AudioDecoder* audio_decoder;

  // Performance monitor
  PerformanceMonitor* performance_monitor;

  // PS5 discovery
  PS5Discovery* ps5_discovery;

  // PS5 authentication
  PS5Auth* ps5_auth;

  // Threading
  SceUID network_thread;
  SceUID decode_thread;
  bool thread_running;

  // Performance counters
  uint64_t start_time;
  uint64_t last_frame_time;
  uint32_t frame_count;
};

// Global state
static bool vitarps5_initialized = false;

// Helper functions
static void session_set_state(VitaRPS5Session* session,
                              VitaRPS5State new_state);
static void session_report_error(VitaRPS5Session* session, VitaRPS5Result error,
                                 const char* message);
static void on_takion_state_changed(TakionState takion_state, void* user_data);
static void on_takion_video_packet(const uint8_t* data, size_t size,
                                   void* user_data);
static void on_takion_audio_packet(const uint8_t* data, size_t size,
                                   void* user_data);
static VitaRPS5State takion_to_vitarps5_state(TakionState takion_state);

// Core API Implementation

VitaRPS5Result vitarps5_init(void) {
  if (vitarps5_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing VitaRPS5 v%s", VITARPS5_VERSION_STRING);

  // Initialize Takion networking
  VitaRPS5Result result = takion_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize Takion protocol: %s",
              vitarps5_result_string(result));
    return result;
  }

  // Initialize video decoder
  result = video_decoder_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize video decoder: %s",
              vitarps5_result_string(result));
    takion_cleanup();
    return result;
  }

  // Initialize video renderer
  result = video_renderer_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize video renderer: %s",
              vitarps5_result_string(result));
    video_decoder_cleanup();
    takion_cleanup();
    return result;
  }

  // Initialize audio decoder
  result = audio_decoder_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize audio decoder: %s",
              vitarps5_result_string(result));
    video_renderer_cleanup();
    video_decoder_cleanup();
    takion_cleanup();
    return result;
  }

  // Initialize performance monitor
  result = performance_monitor_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize performance monitor: %s",
              vitarps5_result_string(result));
    audio_decoder_cleanup();
    video_renderer_cleanup();
    video_decoder_cleanup();
    takion_cleanup();
    return result;
  }

  // Initialize PS5 discovery
  result = ps5_discovery_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize PS5 discovery: %s",
              vitarps5_result_string(result));
    performance_monitor_cleanup();
    audio_decoder_cleanup();
    video_renderer_cleanup();
    video_decoder_cleanup();
    takion_cleanup();
    return result;
  }

  // Initialize PS5 authentication
  result = ps5_auth_init();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize PS5 authentication: %s",
              vitarps5_result_string(result));
    ps5_discovery_cleanup();
    performance_monitor_cleanup();
    audio_decoder_cleanup();
    video_renderer_cleanup();
    video_decoder_cleanup();
    takion_cleanup();
    return result;
  }

  vitarps5_initialized = true;
  log_info("VitaRPS5 initialization complete");

  return VITARPS5_SUCCESS;
}

void vitarps5_cleanup(void) {
  if (!vitarps5_initialized) {
    return;
  }

  log_info("Cleaning up VitaRPS5");

  // Cleanup PS5 authentication
  ps5_auth_cleanup();

  // Cleanup PS5 discovery
  ps5_discovery_cleanup();

  // Cleanup performance monitor
  performance_monitor_cleanup();

  // Cleanup audio decoder
  audio_decoder_cleanup();

  // Cleanup video renderer
  video_renderer_cleanup();

  // Cleanup video decoder
  video_decoder_cleanup();

  // Cleanup Takion networking
  takion_cleanup();

  vitarps5_initialized = false;
  log_info("VitaRPS5 cleanup complete");
}

VitaRPS5Result vitarps5_session_create(const VitaRPS5Config* config,
                                       VitaRPS5Session** session) {
  if (!vitarps5_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!config || !session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  VitaRPS5Session* new_session = malloc(sizeof(VitaRPS5Session));
  if (!new_session) {
    return VITARPS5_ERROR_MEMORY;
  }

  // Initialize session
  memset(new_session, 0, sizeof(VitaRPS5Session));
  new_session->config = *config;
  new_session->state = VITARPS5_STATE_IDLE;
  new_session->thread_running = false;

  // Create Takion connection
  TakionConfig takion_config;
  memset(&takion_config, 0, sizeof(takion_config));
  strncpy(takion_config.remote_ip, config->ps_address,
          sizeof(takion_config.remote_ip) - 1);
  takion_config.remote_ip[sizeof(takion_config.remote_ip) - 1] =
      '\0';  // Ensure null termination
  takion_config.control_port = config->control_port;
  takion_config.stream_port = config->stream_port;
  takion_config.console_version = (config->console_type == VITARPS5_CONSOLE_PS5)
                                      ? TAKION_VERSION_PS5
                                      : TAKION_VERSION_PS4;
  takion_config.timeout_ms = 10000;  // 10 second timeout
  takion_config.state_callback = on_takion_state_changed;
  takion_config.video_callback = on_takion_video_packet;
  takion_config.audio_callback = on_takion_audio_packet;
  takion_config.user_data = new_session;

  VitaRPS5Result result =
      takion_connection_create(&takion_config, &new_session->takion_connection);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create Takion connection: %s",
              vitarps5_result_string(result));
    free(new_session);
    return result;
  }

  // Create video decoder
  VideoDecoderConfig video_config;
  memset(&video_config, 0, sizeof(video_config));
  video_config.max_width = config->width;
  video_config.max_height = config->height;
  video_config.target_fps = config->target_fps;
  video_config.enable_hardware_decode = config->hardware_decode;
  video_config.decode_buffer_count = config->video_buffer_count;
  video_config.frame_callback = NULL;  // Will be set up when streaming starts
  video_config.error_callback = NULL;
  video_config.user_data = new_session;

  result = video_decoder_create(&video_config, &new_session->video_decoder);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create video decoder: %s",
              vitarps5_result_string(result));
    takion_connection_destroy(new_session->takion_connection);
    free(new_session);
    return result;
  }

  // Create video renderer
  VideoRendererConfig renderer_config;
  memset(&renderer_config, 0, sizeof(renderer_config));
  renderer_config.display_width = 960;   // PS Vita screen width
  renderer_config.display_height = 544;  // PS Vita screen height
  renderer_config.stream_width = config->width;
  renderer_config.stream_height = config->height;
  renderer_config.maintain_aspect_ratio = true;
  renderer_config.show_debug_overlay = false;

  result =
      video_renderer_create(&renderer_config, &new_session->video_renderer);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create video renderer: %s",
              vitarps5_result_string(result));
    video_decoder_destroy(new_session->video_decoder);
    takion_connection_destroy(new_session->takion_connection);
    free(new_session);
    return result;
  }

  // Create audio decoder
  AudioDecoderConfig audio_config;
  memset(&audio_config, 0, sizeof(audio_config));
  audio_config.sample_rate = config->audio_sample_rate;
  audio_config.channels = 2;          // Stereo audio
  audio_config.buffer_size_ms = 100;  // 100ms buffer
  audio_config.enable_opus_decode = true;
  audio_config.decode_buffer_count = AUDIO_BUFFER_COUNT;
  audio_config.frame_callback = NULL;  // Will be set up when streaming starts
  audio_config.error_callback = NULL;
  audio_config.user_data = new_session;

  result = audio_decoder_create(&audio_config, &new_session->audio_decoder);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create audio decoder: %s",
              vitarps5_result_string(result));
    video_renderer_destroy(new_session->video_renderer);
    video_decoder_destroy(new_session->video_decoder);
    takion_connection_destroy(new_session->takion_connection);
    free(new_session);
    return result;
  }

  // Create performance monitor
  PerformanceMonitorConfig perf_config;
  memset(&perf_config, 0, sizeof(perf_config));
  perf_config.enable_latency_tracking = true;
  perf_config.enable_frame_rate_tracking = true;
  perf_config.enable_resource_tracking = true;
  perf_config.enable_network_tracking = true;
  perf_config.latency_alert_threshold_ms = 75.0f;  // Alert if >75ms
  perf_config.update_interval_ms = 100;            // Update every 100ms
  perf_config.alert_callback = NULL;               // No alert callback for now
  perf_config.user_data = new_session;

  result = performance_monitor_create(&perf_config,
                                      &new_session->performance_monitor);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create performance monitor: %s",
              vitarps5_result_string(result));
    audio_decoder_destroy(new_session->audio_decoder);
    video_renderer_destroy(new_session->video_renderer);
    video_decoder_destroy(new_session->video_decoder);
    takion_connection_destroy(new_session->takion_connection);
    free(new_session);
    return result;
  }

  // Create PS5 discovery
  PS5DiscoveryConfig discovery_config;
  memset(&discovery_config, 0, sizeof(discovery_config));
  discovery_config.scan_timeout_ms = 10000;  // 10 second scan
  discovery_config.scan_interval_ms = 1000;  // 1 second intervals
  discovery_config.enable_wake_on_lan = true;
  discovery_config.filter_local_network_only = true;
  discovery_config.console_found_callback = NULL;  // No callback for now
  discovery_config.discovery_complete_callback = NULL;
  discovery_config.user_data = new_session;

  result = ps5_discovery_create(&discovery_config, &new_session->ps5_discovery);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create PS5 discovery: %s",
              vitarps5_result_string(result));
    performance_monitor_destroy(new_session->performance_monitor);
    audio_decoder_destroy(new_session->audio_decoder);
    video_renderer_destroy(new_session->video_renderer);
    video_decoder_destroy(new_session->video_decoder);
    takion_connection_destroy(new_session->takion_connection);
    free(new_session);
    return result;
  }

  // Create PS5 authentication
  PS5AuthConfig auth_config;
  memset(&auth_config, 0, sizeof(auth_config));
  auth_config.enable_credential_storage = true;
  auth_config.enable_auto_login = false;  // Manual login for security
  auth_config.enable_console_auto_register = false;
  auth_config.session_timeout_minutes = 60;  // 1 hour session timeout
  auth_config.token_refresh_threshold_minutes =
      15;                             // Refresh 15 minutes before expiry
  auth_config.state_callback = NULL;  // No callback for now
  auth_config.console_registered_callback = NULL;
  auth_config.session_created_callback = NULL;
  auth_config.user_data = new_session;

  result = ps5_auth_create(&auth_config, &new_session->ps5_auth);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to create PS5 authentication: %s",
              vitarps5_result_string(result));
    ps5_discovery_destroy(new_session->ps5_discovery);
    performance_monitor_destroy(new_session->performance_monitor);
    audio_decoder_destroy(new_session->audio_decoder);
    video_renderer_destroy(new_session->video_renderer);
    video_decoder_destroy(new_session->video_decoder);
    takion_connection_destroy(new_session->takion_connection);
    free(new_session);
    return result;
  }

  log_info(
      "Created session for %s:%d (hw_decode: %s, rendering: %dx%d, audio: "
      "%dHz, perf_monitor: enabled, discovery: enabled, auth: enabled)",
      config->ps_address, config->control_port,
      config->hardware_decode ? "enabled" : "disabled", config->width,
      config->height, config->audio_sample_rate);

  *session = new_session;
  return VITARPS5_SUCCESS;
}

void vitarps5_session_destroy(VitaRPS5Session* session) {
  if (!session) {
    return;
  }

  log_info("Destroying session");

  // Disconnect if connected
  if (session->state != VITARPS5_STATE_IDLE) {
    vitarps5_disconnect(session);
  }

  // Destroy PS5 authentication
  if (session->ps5_auth) {
    ps5_auth_destroy(session->ps5_auth);
    session->ps5_auth = NULL;
  }

  // Destroy PS5 discovery
  if (session->ps5_discovery) {
    ps5_discovery_destroy(session->ps5_discovery);
    session->ps5_discovery = NULL;
  }

  // Destroy performance monitor
  if (session->performance_monitor) {
    performance_monitor_destroy(session->performance_monitor);
    session->performance_monitor = NULL;
  }

  // Destroy audio decoder
  if (session->audio_decoder) {
    audio_decoder_destroy(session->audio_decoder);
    session->audio_decoder = NULL;
  }

  // Destroy video renderer
  if (session->video_renderer) {
    video_renderer_destroy(session->video_renderer);
    session->video_renderer = NULL;
  }

  // Destroy video decoder
  if (session->video_decoder) {
    video_decoder_destroy(session->video_decoder);
    session->video_decoder = NULL;
  }

  // Destroy Takion connection
  if (session->takion_connection) {
    takion_connection_destroy(session->takion_connection);
    session->takion_connection = NULL;
  }

  // Stop threads if running
  if (session->thread_running) {
    session->thread_running = false;
    // TODO: Wait for threads to finish
  }

  free(session);
}

VitaRPS5Result vitarps5_connect(VitaRPS5Session* session) {
  if (!session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (session->state != VITARPS5_STATE_IDLE) {
    return VITARPS5_ERROR_INVALID_STATE;
  }

  if (!session->takion_connection) {
    return VITARPS5_ERROR_INIT;
  }

  log_info("Connecting to %s:%d", session->config.ps_address,
           session->config.control_port);

  // Start video renderer
  if (session->video_renderer) {
    VitaRPS5Result result = video_renderer_start(session->video_renderer);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to start video renderer: %s",
                vitarps5_result_string(result));
      session_report_error(session, result, "Video renderer start failed");
      return result;
    }
  }

  // Start video decoder
  if (session->video_decoder) {
    VitaRPS5Result result = video_decoder_start(session->video_decoder);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to start video decoder: %s",
                vitarps5_result_string(result));
      session_report_error(session, result, "Video decoder start failed");

      // Stop video renderer on failure
      if (session->video_renderer) {
        video_renderer_stop(session->video_renderer);
      }
      return result;
    }
  }

  // Start audio decoder
  if (session->audio_decoder) {
    VitaRPS5Result result = audio_decoder_start(session->audio_decoder);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to start audio decoder: %s",
                vitarps5_result_string(result));
      session_report_error(session, result, "Audio decoder start failed");

      // Stop video systems on failure
      if (session->video_decoder) {
        video_decoder_stop(session->video_decoder);
      }
      if (session->video_renderer) {
        video_renderer_stop(session->video_renderer);
      }
      return result;
    }
  }

  // Start performance monitoring
  if (session->performance_monitor) {
    VitaRPS5Result result =
        performance_monitor_start(session->performance_monitor);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to start performance monitor: %s",
                vitarps5_result_string(result));
      // Continue anyway, performance monitoring is optional
    }
  }

  // Start Takion connection
  VitaRPS5Result result = takion_connect(session->takion_connection);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to start Takion connection: %s",
              vitarps5_result_string(result));
    session_report_error(session, result, "Takion connection failed");

    // Stop audio and video systems on connection failure
    if (session->audio_decoder) {
      audio_decoder_stop(session->audio_decoder);
    }
    if (session->video_decoder) {
      video_decoder_stop(session->video_decoder);
    }
    if (session->video_renderer) {
      video_renderer_stop(session->video_renderer);
    }
    return result;
  }

  // State will be updated via Takion callbacks

  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_disconnect(VitaRPS5Session* session) {
  if (!session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (session->state == VITARPS5_STATE_IDLE) {
    return VITARPS5_SUCCESS;
  }

  log_info("Disconnecting session");

  // Stop performance monitoring
  if (session->performance_monitor) {
    VitaRPS5Result result =
        performance_monitor_stop(session->performance_monitor);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to stop performance monitor: %s",
                vitarps5_result_string(result));
      // Continue anyway
    }
  }

  // Stop audio decoder
  if (session->audio_decoder) {
    VitaRPS5Result result = audio_decoder_stop(session->audio_decoder);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to stop audio decoder: %s",
                vitarps5_result_string(result));
      // Continue anyway
    }
  }

  // Stop video decoder
  if (session->video_decoder) {
    VitaRPS5Result result = video_decoder_stop(session->video_decoder);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to stop video decoder: %s",
                vitarps5_result_string(result));
      // Continue anyway
    }
  }

  // Stop video renderer
  if (session->video_renderer) {
    VitaRPS5Result result = video_renderer_stop(session->video_renderer);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to stop video renderer: %s",
                vitarps5_result_string(result));
      // Continue anyway
    }
  }

  // Disconnect Takion connection
  if (session->takion_connection) {
    VitaRPS5Result result = takion_disconnect(session->takion_connection);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to disconnect Takion: %s",
                vitarps5_result_string(result));
      // Continue anyway
    }
  }

  session_set_state(session, VITARPS5_STATE_IDLE);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_update(VitaRPS5Session* session) {
  if (!session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Update Takion connection
  if (session->takion_connection) {
    VitaRPS5Result result = takion_update(session->takion_connection);
    if (result != VITARPS5_SUCCESS) {
      log_error("Takion update failed: %s", vitarps5_result_string(result));
      session_report_error(session, result, "Network update failed");
      return result;
    }
  }

  // Update performance counters
  session->frame_count++;

  // Update performance monitoring
  if (session->performance_monitor) {
    performance_monitor_update(session->performance_monitor);
  }

  // TODO: Process decoded video frames, audio, etc.

  return VITARPS5_SUCCESS;
}

VitaRPS5Result vitarps5_send_input(VitaRPS5Session* session,
                                   const SceCtrlData* pad) {
  if (!session || !pad) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (session->state != VITARPS5_STATE_STREAMING) {
    return VITARPS5_ERROR_NOT_CONNECTED;
  }

  if (!session->takion_connection) {
    return VITARPS5_ERROR_INIT;
  }

  // Convert PS Vita input to PlayStation input format
  TakionInputPacket input_packet;
  memset(&input_packet, 0, sizeof(input_packet));

  // Map Vita buttons to PlayStation buttons
  uint32_t ps_buttons = 0;
  if (pad->buttons & SCE_CTRL_SELECT) ps_buttons |= (1 << 0);     // Share
  if (pad->buttons & SCE_CTRL_L3) ps_buttons |= (1 << 1);         // L3
  if (pad->buttons & SCE_CTRL_R3) ps_buttons |= (1 << 2);         // R3
  if (pad->buttons & SCE_CTRL_START) ps_buttons |= (1 << 3);      // Options
  if (pad->buttons & SCE_CTRL_UP) ps_buttons |= (1 << 4);         // D-Pad Up
  if (pad->buttons & SCE_CTRL_RIGHT) ps_buttons |= (1 << 5);      // D-Pad Right
  if (pad->buttons & SCE_CTRL_DOWN) ps_buttons |= (1 << 6);       // D-Pad Down
  if (pad->buttons & SCE_CTRL_LEFT) ps_buttons |= (1 << 7);       // D-Pad Left
  if (pad->buttons & SCE_CTRL_LTRIGGER) ps_buttons |= (1 << 8);   // L2
  if (pad->buttons & SCE_CTRL_RTRIGGER) ps_buttons |= (1 << 9);   // R2
  if (pad->buttons & SCE_CTRL_L1) ps_buttons |= (1 << 10);        // L1
  if (pad->buttons & SCE_CTRL_R1) ps_buttons |= (1 << 11);        // R1
  if (pad->buttons & SCE_CTRL_TRIANGLE) ps_buttons |= (1 << 12);  // Triangle
  if (pad->buttons & SCE_CTRL_CIRCLE) ps_buttons |= (1 << 13);    // Circle
  if (pad->buttons & SCE_CTRL_CROSS) ps_buttons |= (1 << 14);     // Cross
  if (pad->buttons & SCE_CTRL_SQUARE) ps_buttons |= (1 << 15);    // Square

  input_packet.buttons = ps_buttons;

  // Convert analog sticks (Vita: 0-255, PlayStation: -32768 to 32767)
  input_packet.left_stick_x = (int16_t)((pad->lx - 128) * 256);
  input_packet.left_stick_y = (int16_t)((pad->ly - 128) * 256);
  input_packet.right_stick_x = (int16_t)((pad->rx - 128) * 256);
  input_packet.right_stick_y = (int16_t)((pad->ry - 128) * 256);

  // L2/R2 triggers (Vita uses buttons, PlayStation uses analog)
  input_packet.left_trigger = (pad->buttons & SCE_CTRL_LTRIGGER) ? 255 : 0;
  input_packet.right_trigger = (pad->buttons & SCE_CTRL_RTRIGGER) ? 255 : 0;

  // TODO: Map rear touchpad to PlayStation touchpad
  input_packet.touchpad_x = 0;
  input_packet.touchpad_y = 0;
  input_packet.touchpad_active = 0;

  // Record input performance event
  if (session->performance_monitor) {
    uint64_t input_timestamp = performance_get_timestamp_us();
    performance_record_input_latency(session->performance_monitor,
                                     pad->timeStamp, input_timestamp);
  }

  // Send input via Takion
  return takion_send_input(session->takion_connection,
                           (const uint8_t*)&input_packet, sizeof(input_packet));
}

VitaRPS5State vitarps5_get_state(const VitaRPS5Session* session) {
  if (!session) {
    return VITARPS5_STATE_ERROR;
  }

  return session->state;
}

VitaRPS5Result vitarps5_get_stats(const VitaRPS5Session* session,
                                  VitaRPS5Stats* stats) {
  if (!session || !stats) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *stats = session->stats;
  return VITARPS5_SUCCESS;
}

void vitarps5_config_default(VitaRPS5Config* config) {
  if (!config) {
    return;
  }

  memset(config, 0, sizeof(VitaRPS5Config));

  // Network defaults
  strcpy(config->ps_address, "192.168.1.100");
  config->control_port = VITARPS5_PORT_CONTROL;
  config->stream_port = VITARPS5_PORT_STREAM;
  config->console_type = VITARPS5_CONSOLE_PS5;

  // Video defaults (Performance preset)
  config->quality = VITARPS5_QUALITY_PERFORMANCE;
  config->hardware_decode = true;
  config->target_bitrate = 5000;
  config->target_fps = 30;
  config->width = 720;
  config->height = 480;

  // Audio defaults
  config->audio_enabled = true;
  config->audio_sample_rate = 48000;

  // Input defaults
  config->motion_enabled = false;
  config->rear_touch_enabled = false;

  // Performance defaults
  config->video_buffer_count = 3;
  config->network_buffer_size = 65536;
  config->max_decode_threads = 1;
}

VitaRPS5Result vitarps5_set_quality(VitaRPS5Session* session,
                                    VitaRPS5Quality quality) {
  if (!session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  session->config.quality = quality;

  // Update related settings based on quality
  switch (quality) {
    case VITARPS5_QUALITY_PERFORMANCE:
      session->config.target_bitrate = 5000;
      session->config.target_fps = 30;
      break;
    case VITARPS5_QUALITY_BALANCED:
      session->config.target_bitrate = 10000;
      session->config.target_fps = 60;
      break;
    case VITARPS5_QUALITY_QUALITY:
      session->config.target_bitrate = 15000;
      session->config.target_fps = 60;
      break;
    case VITARPS5_QUALITY_CUSTOM:
      // Keep current settings
      break;
  }

  log_info("Quality set to %d (bitrate: %d kbps, fps: %d)", quality,
           session->config.target_bitrate, session->config.target_fps);

  return VITARPS5_SUCCESS;
}

void vitarps5_set_state_callback(VitaRPS5Session* session,
                                 VitaRPS5StateCallback callback,
                                 void* user_data) {
  if (!session) {
    return;
  }

  session->state_callback = callback;
  session->callback_user_data = user_data;
}

void vitarps5_set_error_callback(VitaRPS5Session* session,
                                 VitaRPS5ErrorCallback callback,
                                 void* user_data) {
  if (!session) {
    return;
  }

  session->error_callback = callback;
  session->callback_user_data = user_data;
}

const char* vitarps5_result_string(VitaRPS5Result result) {
  switch (result) {
    case VITARPS5_SUCCESS:
      return "Success";
    case VITARPS5_ERROR_INIT:
      return "Initialization error";
    case VITARPS5_ERROR_NOT_INITIALIZED:
      return "Not initialized";
    case VITARPS5_ERROR_INVALID_STATE:
      return "Invalid state";
    case VITARPS5_ERROR_NETWORK:
      return "Network error";
    case VITARPS5_ERROR_VIDEO:
      return "Video error";
    case VITARPS5_ERROR_AUDIO:
      return "Audio error";
    case VITARPS5_ERROR_INPUT:
      return "Input error";
    case VITARPS5_ERROR_MEMORY:
      return "Memory error";
    case VITARPS5_ERROR_CRYPTO:
      return "Cryptography error";
    case VITARPS5_ERROR_PROTOCOL:
      return "Protocol error";
    case VITARPS5_ERROR_INVALID_PARAM:
      return "Invalid parameter";
    case VITARPS5_ERROR_NOT_CONNECTED:
      return "Not connected";
    case VITARPS5_ERROR_TIMEOUT:
      return "Timeout";
    case VITARPS5_ERROR_HARDWARE:
      return "Hardware error";
    default:
      return "Unknown error";
  }
}

const char* vitarps5_state_string(VitaRPS5State state) {
  switch (state) {
    case VITARPS5_STATE_IDLE:
      return "Idle";
    case VITARPS5_STATE_TAKION_CONNECT:
      return "Connecting";
    case VITARPS5_STATE_EXPECT_BANG:
      return "Negotiating";
    case VITARPS5_STATE_EXPECT_STREAMINFO:
      return "Getting stream info";
    case VITARPS5_STATE_STREAMING:
      return "Streaming";
    case VITARPS5_STATE_DISCONNECTING:
      return "Disconnecting";
    case VITARPS5_STATE_ERROR:
      return "Error";
    default:
      return "Unknown";
  }
}

const char* vitarps5_get_version(void) { return VITARPS5_VERSION_STRING; }

// Helper function implementations

static void session_set_state(VitaRPS5Session* session,
                              VitaRPS5State new_state) {
  if (!session || session->state == new_state) {
    return;
  }

  VitaRPS5State old_state = session->state;
  session->state = new_state;

  log_info("State changed: %s -> %s", vitarps5_state_string(old_state),
           vitarps5_state_string(new_state));

  if (session->state_callback) {
    session->state_callback(new_state, session->callback_user_data);
  }
}

static void session_report_error(VitaRPS5Session* session, VitaRPS5Result error,
                                 const char* message) {
  if (!session) {
    return;
  }

  log_error("Session error: %s (%s)", vitarps5_result_string(error),
            message ? message : "");

  session_set_state(session, VITARPS5_STATE_ERROR);

  if (session->error_callback) {
    session->error_callback(error, message, session->callback_user_data);
  }
}

// Takion callback implementations

static void on_takion_state_changed(TakionState takion_state, void* user_data) {
  VitaRPS5Session* session = (VitaRPS5Session*)user_data;
  if (!session) {
    return;
  }

  VitaRPS5State new_state = takion_to_vitarps5_state(takion_state);
  session_set_state(session, new_state);
}

static void on_takion_video_packet(const uint8_t* data, size_t size,
                                   void* user_data) {
  VitaRPS5Session* session = (VitaRPS5Session*)user_data;
  if (!session || !data || size == 0) {
    return;
  }

  // Cast data to video packet structure for analysis
  const TakionVideoPacket* packet = (const TakionVideoPacket*)data;

  // Update statistics
  session->stats.frames_received++;

  // Record performance event
  if (session->performance_monitor) {
    performance_monitor_record_event(
        session->performance_monitor, PERF_EVENT_PACKET_RECEIVED,
        packet->frame_sequence, packet->header.payload_size);
  }

  log_debug("Received video packet: codec=%d, frame=%d, fragment=%d/%d",
            packet->codec_type, packet->frame_sequence, packet->fragment_index,
            packet->fragment_total);

  // Send packet to video decoder
  if (session->video_decoder) {
    VitaRPS5Result result =
        video_decoder_process_packet(session->video_decoder, data, size);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to process video packet: %s",
                vitarps5_result_string(result));
    }
  }
}

static void on_takion_audio_packet(const uint8_t* data, size_t size,
                                   void* user_data) {
  VitaRPS5Session* session = (VitaRPS5Session*)user_data;
  if (!session || !data || size == 0) {
    return;
  }

  // TODO: Update when TakionAudioPacket structure is properly defined
  log_debug("Received audio packet (packet structure needs updating)");

  // Update statistics
  session->stats.audio_frames_received++;

  // TODO: Record performance event when packet structure is properly defined
  // if (session->performance_monitor) {
  //   performance_monitor_record_event(
  //       session->performance_monitor, PERF_EVENT_PACKET_RECEIVED,
  //       0x80000000 | packet->header.timestamp, packet->header.payload_size);
  // }

  // TODO: Send packet to audio decoder when signature is updated
  // if (session->audio_decoder) {
  //   VitaRPS5Result result =
  //       audio_decoder_process_packet(session->audio_decoder, packet);
  //   if (result != VITARPS5_SUCCESS) {
  //     log_error("Failed to process audio packet: %s",
  //               vitarps5_result_string(result));
  //   }
  // }
}

static VitaRPS5State takion_to_vitarps5_state(TakionState takion_state) {
  switch (takion_state) {
    case TAKION_STATE_IDLE:
      return VITARPS5_STATE_IDLE;
    case TAKION_STATE_CONNECTING:
    case TAKION_STATE_INIT_SENT:
      return VITARPS5_STATE_TAKION_CONNECT;
    case TAKION_STATE_INIT_ACK_RECEIVED:
    case TAKION_STATE_COOKIE_SENT:
      return VITARPS5_STATE_EXPECT_BANG;
    case TAKION_STATE_COOKIE_ACK_RECEIVED:
      return VITARPS5_STATE_EXPECT_STREAMINFO;
    case TAKION_STATE_CONNECTED:
      return VITARPS5_STATE_STREAMING;
    case TAKION_STATE_DISCONNECTING:
      return VITARPS5_STATE_DISCONNECTING;
    case TAKION_STATE_ERROR:
    default:
      return VITARPS5_STATE_ERROR;
  }
}