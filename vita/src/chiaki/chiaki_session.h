// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 adaptation of vitaki-fork session.h - COMPLETE WORKING INTERFACE

#ifndef CHIAKI_SESSION_H
#define CHIAKI_SESSION_H

#include <netdb.h>
#include <psp2/net/net.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chiaki_common.h"
#include "chiaki_controller.h"
#include "chiaki_ctrl_vitaki.h"
#include "chiaki_log.h"
#include "chiaki_rpcrypt_vitaki.h"
#include "chiaki_rudp.h"
#include "chiaki_streamconnection.h"
#include "vitaki_stoppipe.h"
#include "vitaki_thread.h"

// Forward declaration to avoid circular includes
struct chiaki_stream_connection_t;

#ifdef __cplusplus
extern "C" {
#endif

// Application reason constants (from vitaki-fork)
#define CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED 0x80108b09
#define CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID 0x80108b02
#define CHIAKI_RP_APPLICATION_REASON_IN_USE 0x80108b10
#define CHIAKI_RP_APPLICATION_REASON_CRASH 0x80108b15
#define CHIAKI_RP_APPLICATION_REASON_RP_VERSION 0x80108b11
#define CHIAKI_RP_APPLICATION_REASON_UNKNOWN 0x80108bff

const char *chiaki_rp_application_reason_string(uint32_t reason);
const char *chiaki_rp_version_string(ChiakiTarget target);
ChiakiTarget chiaki_rp_version_parse(const char *rp_version_str, bool is_ps5);

// Core Constants (from vitaki-fork)
#define CHIAKI_RP_DID_SIZE 32
#define CHIAKI_SESSION_ID_SIZE_MAX 80
#define CHIAKI_HANDSHAKE_KEY_SIZE 0x10
#define CHIAKI_SESSION_AUTH_SIZE 0x10

// Video Profile (from vitaki-fork)
typedef struct chiaki_connect_video_profile_t {
  unsigned int width;
  unsigned int height;
  unsigned int max_fps;
  unsigned int bitrate;
  ChiakiCodec codec;
} ChiakiConnectVideoProfile;

// Video Resolution Presets (from vitaki-fork)
typedef enum {
  CHIAKI_VIDEO_RESOLUTION_PRESET_360p = 1,
  CHIAKI_VIDEO_RESOLUTION_PRESET_540p = 2,
  CHIAKI_VIDEO_RESOLUTION_PRESET_720p = 3,
  CHIAKI_VIDEO_RESOLUTION_PRESET_1080p = 4
} ChiakiVideoResolutionPreset;

// Video FPS Presets (from vitaki-fork)
typedef enum {
  CHIAKI_VIDEO_FPS_PRESET_30 = 30,
  CHIAKI_VIDEO_FPS_PRESET_60 = 60
} ChiakiVideoFPSPreset;

void chiaki_connect_video_profile_preset(ChiakiConnectVideoProfile *profile,
                                         ChiakiVideoResolutionPreset resolution,
                                         ChiakiVideoFPSPreset fps);

// Connect Info (from vitaki-fork, adapted for PS Vita)
typedef struct chiaki_connect_info_t {
  bool ps5;
  const char *host;
  char regist_key[CHIAKI_SESSION_AUTH_SIZE];
  uint8_t morning[0x10];
  ChiakiConnectVideoProfile video_profile;
  bool video_profile_auto_downgrade;
  bool enable_keyboard;
  bool enable_dualsense;
  // Note: Removed holepunch_session and rudp_sock for PS Vita
  uint8_t psn_account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE];
  // VitaRPS5: optional control port overrides from discovery
  int control_port;               // if <=0, use default 9295
  int host_header_port_override;  // if >0, include this port in Host header
} ChiakiConnectInfo;

// Quit Reasons (from vitaki-fork)
typedef enum {
  CHIAKI_QUIT_REASON_NONE,
  CHIAKI_QUIT_REASON_STOPPED,
  CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN,
  CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED,
  CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE,
  CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH,
  CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH,
  CHIAKI_QUIT_REASON_CTRL_UNKNOWN,
  CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED,
  CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED,
  CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN,
  CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED,
  CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN,
  CHIAKI_QUIT_REASON_PSN_REGIST_FAILED,
} ChiakiQuitReason;

const char *chiaki_quit_reason_string(ChiakiQuitReason reason);

static inline bool chiaki_quit_reason_is_error(ChiakiQuitReason reason) {
  return reason != CHIAKI_QUIT_REASON_STOPPED &&
         reason != CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN;
}

// Event Types (from vitaki-fork, simplified for PS Vita)
typedef struct chiaki_quit_event_t {
  ChiakiQuitReason reason;
  const char *reason_str;
} ChiakiQuitEvent;

typedef struct chiaki_keyboard_event_t {
  const char *text_str;
} ChiakiKeyboardEvent;

typedef struct chiaki_rumble_event_t {
  uint8_t unknown;
  uint8_t left;
  uint8_t right;
} ChiakiRumbleEvent;

typedef enum {
  CHIAKI_EVENT_CONNECTED,
  CHIAKI_EVENT_LOGIN_PIN_REQUEST,
  CHIAKI_EVENT_KEYBOARD_OPEN,
  CHIAKI_EVENT_KEYBOARD_TEXT_CHANGE,
  CHIAKI_EVENT_KEYBOARD_REMOTE_CLOSE,
  CHIAKI_EVENT_RUMBLE,
  CHIAKI_EVENT_QUIT,
  CHIAKI_EVENT_NICKNAME_RECEIVED,
} ChiakiEventType;

typedef struct chiaki_event_t {
  ChiakiEventType type;
  union {
    ChiakiQuitEvent quit;
    ChiakiKeyboardEvent keyboard;
    ChiakiRumbleEvent rumble;
    struct {
      bool pin_incorrect;
    } login_pin_request;
    char server_nickname[0x20];
  };
} ChiakiEvent;

// Callback Types (from vitaki-fork)
typedef void (*ChiakiEventCallback)(ChiakiEvent *event, void *user);
typedef bool (*ChiakiVideoSampleCallback)(uint8_t *buf, size_t buf_size,
                                          int32_t frames_lost,
                                          bool frame_recovered, void *user);

// Complete Session Structure (from vitaki-fork, adapted for PS Vita)
typedef struct chiaki_session_t {
  struct {
    bool ps5;
    struct addrinfo *host_addrinfos;
    struct addrinfo *host_addrinfo_selected;
    char hostname[256];
    char regist_key[CHIAKI_RPCRYPT_KEY_SIZE];
    uint8_t morning[CHIAKI_RPCRYPT_KEY_SIZE];
    uint8_t did[CHIAKI_RP_DID_SIZE];
    ChiakiConnectVideoProfile video_profile;
    bool video_profile_auto_downgrade;
    bool enable_keyboard;
    bool enable_dualsense;
    uint8_t psn_account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE];
    int control_port;
    int host_header_port_override;
  } connect_info;

  ChiakiTarget target;

  uint8_t nonce[CHIAKI_RPCRYPT_KEY_SIZE];
  ChiakiRPCrypt rpcrypt;
  char session_id[CHIAKI_SESSION_ID_SIZE_MAX];
  uint8_t handshake_key[CHIAKI_HANDSHAKE_KEY_SIZE];
  uint32_t mtu_in;
  uint32_t mtu_out;
  uint64_t rtt_us;
  // Note: Removed ChiakiECDH for PS Vita

  ChiakiQuitReason quit_reason;
  char *quit_reason_str;

  ChiakiEventCallback event_cb;
  void *event_cb_user;
  ChiakiVideoSampleCallback video_sample_cb;
  void *video_sample_cb_user;
  // Note: Removed audio_sink, haptics_sink, display_sink for PS Vita

  ChiakiThread session_thread;

  ChiakiCond state_cond;
  ChiakiMutex state_mutex;
  ChiakiStopPipe stop_pipe;
  bool should_stop;
  bool ctrl_failed;
  bool ctrl_session_id_received;
  bool ctrl_login_pin_requested;
  bool login_pin_entered;
  bool psn_regist_succeeded;
  bool stream_connection_switch_received;
  uint8_t *login_pin;
  size_t login_pin_size;

  // CRITICAL FIX: Add ChiakiCtrl stub for compatibility
  ChiakiCtrl ctrl;

  // Note: Removed ChiakiHolepunchSession for PS Vita
  ChiakiRudp rudp;  // RUDP connection for remote play

  // Takion connection reference (passed from session manager)
  void *takion_connection;

  ChiakiLog *log;

  // VITAKI-FORK COMPATIBILITY: Use embedded struct like vitaki-fork
  ChiakiStreamConnection stream_connection;

  ChiakiControllerState controller_state;
} ChiakiSession;

// Core API Functions (from vitaki-fork)
ChiakiErrorCode chiaki_session_init(ChiakiSession *session,
                                    ChiakiConnectInfo *connect_info,
                                    ChiakiLog *log);
void chiaki_session_fini(ChiakiSession *session);
ChiakiErrorCode chiaki_session_start(ChiakiSession *session);
ChiakiErrorCode chiaki_session_stop(ChiakiSession *session);
ChiakiErrorCode chiaki_session_join(ChiakiSession *session);
ChiakiErrorCode chiaki_session_set_controller_state(
    ChiakiSession *session, ChiakiControllerState *state);
ChiakiErrorCode chiaki_session_set_login_pin(ChiakiSession *session,
                                             const uint8_t *pin,
                                             size_t pin_size);

// Additional session functions for PS Vita (simplified from vitaki-fork)
ChiakiErrorCode chiaki_session_goto_bed(ChiakiSession *session);
ChiakiErrorCode chiaki_session_keyboard_set_text(ChiakiSession *session,
                                                 const char *text);
ChiakiErrorCode chiaki_session_keyboard_reject(ChiakiSession *session);
ChiakiErrorCode chiaki_session_keyboard_accept(ChiakiSession *session);

// VITAKI-FORK COMPATIBILITY: Callback setters (critical for stability)
void chiaki_session_set_event_cb(ChiakiSession *session, ChiakiEventCallback cb,
                                 void *user);
void chiaki_session_set_video_sample_cb(ChiakiSession *session,
                                        ChiakiVideoSampleCallback cb,
                                        void *user);

// Utility functions
const char *chiaki_error_string(ChiakiErrorCode error);
bool chiaki_target_is_ps5(ChiakiTarget target);
bool chiaki_target_is_unknown(ChiakiTarget target);
const char *chiaki_target_string(ChiakiTarget target);
void chiaki_controller_state_set_idle(ChiakiControllerState *state);

#ifdef __cplusplus
}
#endif

#endif  // CHIAKI_SESSION_H
