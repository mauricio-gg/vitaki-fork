#ifndef VITARPS5_SESSION_MANAGER_H
#define VITARPS5_SESSION_MANAGER_H

#include <stdbool.h>

#include "../audio/audio_decoder.h"
#include "../discovery/ps5_discovery.h"
#include "../network/takion.h"
#include "../video/video_decoder.h"
#include "vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// Remote Play session state
typedef enum {
  SESSION_STATE_IDLE = 0,
  SESSION_STATE_CONNECTING,
  SESSION_STATE_AUTHENTICATING,
  SESSION_STATE_STREAMING,
  SESSION_STATE_PAUSED,
  SESSION_STATE_DISCONNECTING,
  SESSION_STATE_ERROR
} SessionState;

// Remote Play session configuration
typedef struct {
  char console_ip[16];      // PlayStation console IP address
  uint16_t control_port;    // Control port (default: 9295)
  uint16_t stream_port;     // Stream port (default: 9296)
  uint8_t console_version;  // Protocol version (7=PS4, 12=PS5)

  // Authentication credentials
  char registration_key[9];  // Registration key (8 hex chars + null
                             // terminator)
  uint8_t rp_key[16];        // RP key from registration
  uint32_t rp_key_type;      // RP key type
  bool has_credentials;      // Whether credentials are valid

  // Video settings
  uint32_t video_width;   // Target video width (720p recommended)
  uint32_t video_height;  // Target video height
  uint32_t target_fps;    // Target frame rate (30 or 60)
  bool enable_hw_decode;  // Hardware H.264 decoder

  // Audio settings
  bool enable_audio;        // Enable audio streaming
  uint32_t audio_channels;  // Audio channels (2 = stereo)
  uint32_t audio_rate;      // Audio sample rate (48000)

  // Performance settings
  uint32_t timeout_ms;   // Connection timeout
  uint32_t max_bitrate;  // Maximum bitrate (Mbps)
} SessionConfig;

// Remote Play session context
typedef struct RemotePlaySession RemotePlaySession;

// Session event callbacks
typedef void (*SessionStateCallback)(SessionState state, void* user_data);
typedef void (*SessionVideoCallback)(const VideoFrame* frame, void* user_data);
typedef void (*SessionAudioCallback)(const void* audio_data, size_t size,
                                     void* user_data);
typedef void (*SessionErrorCallback)(VitaRPS5Result error, const char* message,
                                     void* user_data);

// Session callbacks structure
typedef struct {
  SessionStateCallback state_callback;
  SessionVideoCallback video_callback;
  SessionAudioCallback audio_callback;
  SessionErrorCallback error_callback;
  void* user_data;
} SessionCallbacks;

// Session statistics
typedef struct {
  SessionState current_state;
  float connection_quality;  // 0.0 to 1.0
  uint32_t frames_received;
  uint32_t frames_decoded;
  uint32_t frames_dropped;
  float current_fps;
  float current_bitrate_mbps;
  float latency_ms;
  bool hardware_decode_active;
  TakionStats network_stats;
  VideoDecoderStats video_stats;
} SessionStats;

// RESEARCHER PHASE 3: SessionContext with frozen PSN values
typedef struct {
  // Frozen PSN values during session attempt
  uint8_t frozen_psn_id[16];      // Binary PSN account ID (16 bytes)
  char frozen_psn_id_hex[33];     // Hex string representation
  char frozen_psn_id_base64[25];  // Base64 representation
  uint64_t frozen_timestamp;      // When PSN values were frozen

  // Session attempt context
  char console_ip[16];       // Target console IP
  char registration_key[9];  // Registration key being used
  uint16_t discovered_port;  // Port from discovery
  bool psn_frozen;           // Whether PSN is currently frozen
  bool session_active;       // Whether session attempt is active
} SessionContext;

// Core Session Management API

/**
 * Initialize session manager subsystem
 */
VitaRPS5Result session_manager_init(void);

/**
 * Cleanup session manager subsystem
 */
void session_manager_cleanup(void);

/**
 * Create a new Remote Play session
 */
VitaRPS5Result session_create(const SessionConfig* config,
                              const SessionCallbacks* callbacks,
                              RemotePlaySession** session);

/**
 * Destroy Remote Play session
 */
void session_destroy(RemotePlaySession* session);

/**
 * Start Remote Play streaming
 */
VitaRPS5Result session_start(RemotePlaySession* session);

/**
 * Stop Remote Play streaming
 */
VitaRPS5Result session_stop(RemotePlaySession* session);

/**
 * Pause Remote Play streaming
 */
VitaRPS5Result session_pause(RemotePlaySession* session);

/**
 * Resume Remote Play streaming
 */
VitaRPS5Result session_resume(RemotePlaySession* session);

/**
 * Send controller input to PlayStation
 */
VitaRPS5Result session_send_input(RemotePlaySession* session, uint32_t buttons,
                                  int16_t left_x, int16_t left_y,
                                  int16_t right_x, int16_t right_y,
                                  uint8_t left_trigger, uint8_t right_trigger);

/**
 * Update session (call regularly from main loop)
 */
VitaRPS5Result session_update(RemotePlaySession* session);

/**
 * Get session state
 */
SessionState session_get_state(const RemotePlaySession* session);

/**
 * Get session statistics
 */
VitaRPS5Result session_get_stats(const RemotePlaySession* session,
                                 SessionStats* stats);

/**
 * Get next video frame for rendering
 */
VitaRPS5Result session_get_video_frame(RemotePlaySession* session,
                                       VideoFrame** frame);

/**
 * Return video frame after rendering
 */
VitaRPS5Result session_return_video_frame(RemotePlaySession* session,
                                          VideoFrame* frame);

// Utility Functions

/**
 * Convert session state to string
 */
const char* session_state_string(SessionState state);

/**
 * Create default session configuration
 */
void session_config_defaults(SessionConfig* config);

/**
 * Initialize session configuration with safe defaults
 */
void session_config_init(SessionConfig* config);

/**
 * Configure session with registered console credentials
 */
VitaRPS5Result session_config_set_credentials(SessionConfig* config,
                                              const char* console_ip);

/**
 * Validate session configuration
 */
VitaRPS5Result session_config_validate(const SessionConfig* config);

// Wake function removed - use ps5_discovery_wake_console() instead

/**
 * Wake console and wait for ready state (discovery-aware)
 */
VitaRPS5Result session_wake_and_wait(const char* console_ip);

/**
 * Check if PS5 Remote Play service is ready for connections
 * @param console_ip PS5 IP address to test
 * @return true if service is ready, false otherwise
 */
bool check_ps5_service_readiness(const char* console_ip);

/**
 * RESEARCHER D) PATCH: Centralized session start with preconditions
 * Validates all prerequisites before attempting session start
 * @param console Console info with discovery and registration data
 * @return true if preconditions pass and session can start
 */
bool session_start_for_console(PS5ConsoleInfo* console);

// RESEARCHER PHASE 3: SessionContext management functions

/**
 * Create and initialize a new SessionContext with frozen PSN values
 * @param console_ip Target console IP address
 * @param registration_key Registration key to use
 * @param discovered_port Port from PS5 discovery
 * @return Pointer to SessionContext or NULL on failure
 */
SessionContext* session_context_create(const char* console_ip,
                                       const char* registration_key,
                                       uint16_t discovered_port);

/**
 * Destroy SessionContext and unfreeze PSN values
 * @param context SessionContext to destroy
 */
void session_context_destroy(SessionContext* context);

/**
 * Validate PSN endianness and log LE hex representation
 * @param context SessionContext with frozen PSN values
 * @return true if PSN endianness is valid
 */
bool session_context_validate_psn_endianness(const SessionContext* context);

// NOTE: session_establish_http_session() removed - ChiakiSession handles HTTP
// internally

// Original session management functions restored

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_SESSION_MANAGER_H