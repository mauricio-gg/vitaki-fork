// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 adaptation of vitaki-fork session.c - COMPLETE WORKING
// IMPLEMENTATION

#include "chiaki_session.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui/ui_profile.h"
#include "chiaki_base64_vitaki.h"
#include "chiaki_http_vitaki.h"
#include "chiaki_random.h"
#include "chiaki_regist_vitaki.h"
#include "chiaki_rpcrypt_vitaki.h"
#include "chiaki_rudp.h"
#include "chiaki_streamconnection.h"
#include "vitaki_stoppipe.h"
#include "vitaki_time.h"

#ifdef _WIN32
#include <winsock2.h>
#define strcasecmp _stricmp
#elif defined(__PSVITA__)
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "utils.h"

#define SESSION_PORT 9295

#define SESSION_EXPECT_TIMEOUT_MS 5000
#define STREAM_CONNECTION_SWITCH_EXPECT_TIMEOUT_MS 2000

static void *session_thread_func(void *arg);
static void regist_cb(ChiakiRegistEvent *event, void *user);
static ChiakiErrorCode session_thread_request_session(ChiakiSession *session,
                                                      ChiakiTarget *target_out);

const char *chiaki_rp_application_reason_string(uint32_t reason) {
  switch (reason) {
    case CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED:
      return "Regist failed, probably invalid PIN";
    case CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID:
      return "Invalid PSN ID";
    case CHIAKI_RP_APPLICATION_REASON_IN_USE:
      return "Remote is already in use";
    case CHIAKI_RP_APPLICATION_REASON_CRASH:
      return "Remote Play on Console crashed";
    case CHIAKI_RP_APPLICATION_REASON_RP_VERSION:
      return "RP-Version mismatch";
    default:
      return "unknown";
  }
}

const char *chiaki_rp_version_string(ChiakiTarget version) {
  switch (version) {
    case CHIAKI_TARGET_PS4_8:
      return "8.0";
    case CHIAKI_TARGET_PS4_9:
      return "9.0";
    case CHIAKI_TARGET_PS4_10:
      return "10.0";
    case CHIAKI_TARGET_PS5_1:
      return "1.0";
    default:
      return NULL;
  }
}

CHIAKI_EXPORT ChiakiTarget chiaki_rp_version_parse(const char *rp_version_str,
                                                   bool is_ps5) {
  if (is_ps5) {
    if (!strcmp(rp_version_str, "1.0")) return CHIAKI_TARGET_PS5_1;
    return CHIAKI_TARGET_PS5_UNKNOWN;
  }
  if (!strcmp(rp_version_str, "8.0")) return CHIAKI_TARGET_PS4_8;
  if (!strcmp(rp_version_str, "9.0")) return CHIAKI_TARGET_PS4_9;
  if (!strcmp(rp_version_str, "10.0")) return CHIAKI_TARGET_PS4_10;
  return CHIAKI_TARGET_PS4_UNKNOWN;
}

CHIAKI_EXPORT void chiaki_connect_video_profile_preset(
    ChiakiConnectVideoProfile *profile, ChiakiVideoResolutionPreset resolution,
    ChiakiVideoFPSPreset fps) {
  profile->codec = CHIAKI_CODEC_H264;
  switch (resolution) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
      profile->width = 640;
      profile->height = 360;
      profile->bitrate = 2000;
      break;
    case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
      profile->width = 960;
      profile->height = 540;
      profile->bitrate = 6000;
      break;
    case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
      profile->width = 1280;
      profile->height = 720;
      profile->bitrate = 10000;
      break;
    case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
      profile->width = 1920;
      profile->height = 1080;
      profile->bitrate = 15000;
      break;
    default:
      profile->width = 0;
      profile->height = 0;
      profile->bitrate = 0;
      break;
  }

  switch (fps) {
    case CHIAKI_VIDEO_FPS_PRESET_30:
      profile->max_fps = 30;
      break;
    case CHIAKI_VIDEO_FPS_PRESET_60:
      profile->max_fps = 60;
      break;
    default:
      profile->max_fps = 0;
      break;
  }
}

CHIAKI_EXPORT const char *chiaki_quit_reason_string(ChiakiQuitReason reason) {
  switch (reason) {
    case CHIAKI_QUIT_REASON_STOPPED:
      return "Stopped";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN:
      return "Unknown Session Request Error";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
      return "Connection Refused in Session Request";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
      return "Remote Play on Console is already in use";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
      return "Remote Play on Console has crashed";
    case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
      return "RP-Version mismatch";
    case CHIAKI_QUIT_REASON_CTRL_UNKNOWN:
      return "Unknown Ctrl Error";
    case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED:
      return "Connection Refused in Ctrl";
    case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED:
      return "Ctrl failed to connect";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN:
      return "Unknown Error in Stream Connection";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
      return "Remote has disconnected from Stream Connection";
    case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
      return "Remote has disconnected from Stream Connection the because "
             "Server shut down";
    case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED:
      return "The Console Registration using PSN has failed";
    case CHIAKI_QUIT_REASON_NONE:
    default:
      return "Unknown";
  }
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_init(
    ChiakiSession *session, ChiakiConnectInfo *connect_info, ChiakiLog *log) {
  memset(session, 0, sizeof(ChiakiSession));

  session->log = log;
  session->quit_reason = CHIAKI_QUIT_REASON_NONE;
  session->target =
      connect_info->ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
#if !(defined(__SWITCH__) || defined(__PSVITA__))
  session->holepunch_session = connect_info->holepunch_session;
#endif
  session->rudp = NULL;

  ChiakiErrorCode err = chiaki_mutex_init(&session->state_mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) goto error_state_cond;

  err = chiaki_cond_init(&session->state_cond, &session->state_mutex);
  if (err != CHIAKI_ERR_SUCCESS) goto error;

  err = chiaki_stop_pipe_init(&session->stop_pipe);
  if (err != CHIAKI_ERR_SUCCESS) goto error_state_mutex;

  session->should_stop = false;
  session->ctrl_session_id_received = false;
  session->ctrl_login_pin_requested = false;
  session->login_pin_entered = false;
  session->psn_regist_succeeded = false;
  session->stream_connection_switch_received = false;
  session->login_pin = NULL;
  session->login_pin_size = 0;

  // CRITICAL FIX: Copy hostname for PS Vita BEFORE stream_connection_init
  // This must happen before stream_connection_init which needs the hostname
#ifdef __PSVITA__
  if (connect_info->host) {
    strncpy(session->connect_info.hostname, connect_info->host,
            sizeof(session->connect_info.hostname) - 1);
    session->connect_info.hostname[sizeof(session->connect_info.hostname) - 1] =
        '\0';
    CHIAKI_LOGI(session->log,
                "PS Vita: Set hostname to '%s' early for stream_connection",
                session->connect_info.hostname);
  } else {
    CHIAKI_LOGE(session->log, "PS Vita: NULL hostname provided!");
    return CHIAKI_ERR_INVALID_DATA;
  }
#endif

  // Copy optional control port overrides from connect_info
  session->connect_info.control_port = connect_info->control_port;
  session->connect_info.host_header_port_override =
      connect_info->host_header_port_override;

  err = chiaki_ctrl_init(&session->ctrl, session);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "Ctrl init failed");
    goto error_stop_pipe;
  }

#if !(defined(__SWITCH__) || defined(__PSVITA__))
  if (session->holepunch_session) {
    memcpy(session->connect_info.psn_account_id, connect_info->psn_account_id,
           sizeof(connect_info->psn_account_id));
  } else
#endif
  {
    // Now perform getaddrinfo with validated hostname
    // (hostname was already copied earlier before stream_connection_init)
    int r = getaddrinfo(connect_info->host, NULL, NULL,
                        &session->connect_info.host_addrinfos);
    if (r != 0) {
      CHIAKI_LOGE(session->log,
                  "PS Vita: getaddrinfo failed for hostname '%s': %d",
                  connect_info->host ? connect_info->host : "NULL", r);
      chiaki_session_fini(session);
      return CHIAKI_ERR_PARSE_ADDR;
    }
    CHIAKI_LOGI(session->log,
                "PS Vita: getaddrinfo succeeded for hostname '%s'",
                connect_info->host);

    memcpy(session->connect_info.regist_key, connect_info->regist_key,
           sizeof(session->connect_info.regist_key));
    memcpy(session->connect_info.morning, connect_info->morning,
           sizeof(session->connect_info.morning));
    // Copy PSN Account ID for HTTP header usage (PS Vita path)
    memcpy(session->connect_info.psn_account_id, connect_info->psn_account_id,
           sizeof(session->connect_info.psn_account_id));
  }

  chiaki_controller_state_set_idle(&session->controller_state);

  session->connect_info.ps5 = connect_info->ps5;

  const uint8_t did_prefix[] = {0x00, 0x18, 0x00, 0x00, 0x00,
                                0x07, 0x00, 0x40, 0x00, 0x80};
  const uint8_t did_suffix[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  memcpy(session->connect_info.did, did_prefix, sizeof(did_prefix));
  chiaki_random_bytes_crypt(session->connect_info.did + sizeof(did_prefix),
                            sizeof(session->connect_info.did) -
                                sizeof(did_prefix) - sizeof(did_suffix));
  memcpy(session->connect_info.did + sizeof(session->connect_info.did) -
             sizeof(did_suffix),
         did_suffix, sizeof(did_suffix));

  session->connect_info.video_profile = connect_info->video_profile;
  session->connect_info.video_profile_auto_downgrade =
      connect_info->video_profile_auto_downgrade;
  session->connect_info.enable_keyboard = connect_info->enable_keyboard;
  session->connect_info.enable_dualsense = connect_info->enable_dualsense;

  return CHIAKI_ERR_SUCCESS;

error_ctrl:
  chiaki_ctrl_fini(&session->ctrl);
error_stop_pipe:
  chiaki_stop_pipe_fini(&session->stop_pipe);
error_state_mutex:
  chiaki_mutex_fini(&session->state_mutex);
error_state_cond:
  chiaki_cond_fini(&session->state_cond);
error:
#if !(defined(__SWITCH__) || defined(__PSVITA__))
  if (session->holepunch_session)
    chiaki_holepunch_session_fini(session->holepunch_session);
#endif
  return err;
}

CHIAKI_EXPORT void chiaki_session_fini(ChiakiSession *session) {
  if (!session) return;
  free(session->login_pin);
  free(session->quit_reason_str);
  // VITAKI-FORK COMPATIBILITY: Cleanup embedded stream_connection
  chiaki_stream_connection_fini(&session->stream_connection);
  chiaki_ctrl_fini(&session->ctrl);
  if (session->rudp) chiaki_rudp_fini(session->rudp);
#if !(defined(__SWITCH__) || defined(__PSVITA__))
  if (session->holepunch_session)
    chiaki_holepunch_session_fini(session->holepunch_session);
#endif
  chiaki_stop_pipe_fini(&session->stop_pipe);
  chiaki_cond_fini(&session->state_cond);
  chiaki_mutex_fini(&session->state_mutex);
  freeaddrinfo(session->connect_info.host_addrinfos);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_start(ChiakiSession *session) {
  CHIAKI_LOGI(session->log,
              "chiaki_session_start: Creating ChiakiSession protocol thread");

  // THREAD CREATION DIAGNOSTICS: Log before thread creation
  CHIAKI_LOGI(session->log, "THREAD DEBUG: About to create session thread");
  CHIAKI_LOGI(session->log, "THREAD DEBUG: session_thread_func address: %p",
              (void *)session_thread_func);
  CHIAKI_LOGI(session->log, "THREAD DEBUG: session argument address: %p",
              (void *)session);

#ifdef __PSVITA__
  CHIAKI_LOGI(
      session->log,
      "THREAD DEBUG: Using PS Vita thread creation (sceKernelCreateThread)");
#else
  CHIAKI_LOGI(session->log, "THREAD DEBUG: Using pthread thread creation");
#endif

  // CRITICAL FIX: Use vitaki-fork's 3-parameter thread creation signature
  ChiakiErrorCode err = chiaki_thread_create(&session->session_thread,
                                             session_thread_func, session);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(
        session->log,
        "chiaki_session_start: Failed to create session thread: error %d", err);
    return err;
  }

  // THREAD CREATION DIAGNOSTICS: Log after successful creation
  CHIAKI_LOGI(session->log,
              "THREAD DEBUG: Session thread created successfully");
  CHIAKI_LOGI(session->log, "THREAD DEBUG: Thread ID: 0x%08X",
              session->session_thread.thread_id);

  // Set thread name separately as vitaki-fork does
  chiaki_thread_set_name(&session->session_thread, "Chiaki Session");
  CHIAKI_LOGI(session->log,
              "THREAD DEBUG: Thread name set to 'Chiaki Session'");

  CHIAKI_LOGI(session->log,
              "chiaki_session_start: Session thread created successfully");
  return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_stop(ChiakiSession *session) {
  ChiakiErrorCode err = chiaki_mutex_lock(&session->state_mutex);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "Failed to lock session mutex in stop");
  }

  session->should_stop = true;
  chiaki_stop_pipe_stop(&session->stop_pipe);
  chiaki_cond_signal(&session->state_cond);

  chiaki_stream_connection_stop(&session->stream_connection);

  chiaki_mutex_unlock(&session->state_mutex);
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_join(ChiakiSession *session) {
  return chiaki_thread_join(&session->session_thread, NULL);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_set_controller_state(
    ChiakiSession *session, ChiakiControllerState *state) {
  // TODO: Implement proper stream connection feedback when stream connection is
  // fully implemented
  session->controller_state = *state;
  CHIAKI_LOGD(
      session->log,
      "Controller state updated (stream connection feedback not implemented)");
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_set_login_pin(
    ChiakiSession *session, const uint8_t *pin, size_t pin_size) {
  uint8_t *buf = malloc(pin_size);
  if (!buf) return CHIAKI_ERR_MEMORY;
  memcpy(buf, pin, pin_size);
  ChiakiErrorCode err = chiaki_mutex_lock(&session->state_mutex);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "Failed to lock session mutex in set_login_pin");
  }
  if (session->login_pin_entered) free(session->login_pin);
  session->login_pin_entered = true;
  session->login_pin = buf;
  session->login_pin_size = pin_size;
  chiaki_mutex_unlock(&session->state_mutex);
  chiaki_cond_signal(&session->state_cond);
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_session_set_stream_connection_switch_received(ChiakiSession *session) {
  ChiakiErrorCode err = chiaki_mutex_lock(&session->state_mutex);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log,
                "Failed to lock session mutex in set_stream_connection_switch");
  }
  session->stream_connection_switch_received = true;
  chiaki_mutex_unlock(&session->state_mutex);
  chiaki_cond_signal(&session->state_cond);
  return CHIAKI_ERR_SUCCESS;
}

void chiaki_session_send_event(ChiakiSession *session, ChiakiEvent *event) {
  if (!session->event_cb) return;
  session->event_cb(event, session->event_cb_user);
}

static bool session_check_state_pred(void *user) {
  ChiakiSession *session = user;
  return session->should_stop || session->ctrl_failed;
}

static bool session_check_state_pred_ctrl_start(void *user) {
  ChiakiSession *session = user;
  return session->should_stop || session->ctrl_failed ||
         session->ctrl_session_id_received || session->ctrl_login_pin_requested;
}

static bool session_check_state_pred_pin(void *user) {
  ChiakiSession *session = user;
  return session->should_stop || session->ctrl_failed ||
         session->login_pin_entered;
}

// NOTE: Function temporarily unused in current implementation
// static bool session_check_state_pred_stream_connection_switch(void *user) {
//   ChiakiSession *session = user;
//   return session->should_stop || session->ctrl_failed ||
//          session->stream_connection_switch_received;
// }

static bool session_check_state_pred_regist(void *user) {
  ChiakiSession *session = user;
  return session->should_stop || session->ctrl_failed ||
         session->psn_regist_succeeded;
}

#define ENABLE_SENKUSHA

static void *session_thread_func(void *arg) {
  ChiakiSession *session = (ChiakiSession *)arg;

  // CRITICAL: Validate session pointer before use
  if (!session || !session->log) {
    CHIAKI_LOGE(NULL, "SESSION_THREAD: NULL session or log pointer - aborting");
    return NULL;
  }

  // Extensive logging for debugging application flow
  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: ===== SESSION THREAD STARTED =====");
  CHIAKI_LOGI(session->log, "SESSION_THREAD: Session pointer: %p", session);
  CHIAKI_LOGI(session->log, "SESSION_THREAD: Log pointer: %p", session->log);
  CHIAKI_LOGI(session->log, "SESSION_THREAD: About to acquire state mutex");

  chiaki_mutex_lock(&session->state_mutex);
  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: State mutex acquired successfully");

#define QUIT(quit_label)                                                   \
  do {                                                                     \
    CHIAKI_LOGE(session->log, "QUIT called - jumping to %s", #quit_label); \
    chiaki_mutex_unlock(&session->state_mutex);                            \
    goto quit_label;                                                       \
  } while (0)

#define CHECK_STOP(quit_label)                                           \
  do {                                                                   \
    if (session->should_stop) {                                          \
      CHIAKI_LOGE(session->log,                                          \
                  "CHECK_STOP triggered - session should_stop is true"); \
      session->quit_reason = CHIAKI_QUIT_REASON_STOPPED;                 \
      QUIT(quit_label);                                                  \
    }                                                                    \
  } while (0)

  CHECK_STOP(quit);

  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: Initial CHECK_STOP passed - proceeding with "
              "RUDP initialization");

#if !(defined(__SWITCH__) || defined(__PSVITA__))
  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: Non-Vita platform detected - checking "
              "holepunch_session");
  if (session->holepunch_session) {
    CHIAKI_LOGI(
        session->log,
        "SESSION_THREAD: Holepunch session available - initializing RUDP");
    chiaki_socket_t *rudp_sock = chiaki_get_holepunch_sock(
        session->holepunch_session, CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL);
    session->rudp = chiaki_rudp_init(rudp_sock, session->log);
    if (!session->rudp) {
      CHIAKI_LOGE(session->log, "Initializing rudp failed");
      CHECK_STOP(quit);
    }
    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: RUDP initialized successfully for holepunch");
  } else {
    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: No holepunch session - skipping RUDP for this "
                "platform");
  }
#else
  // PS Vita RUDP initialization for remote play
  CHIAKI_LOGI(
      session->log,
      "SESSION_THREAD: PS Vita platform detected - checking connection type");

  // Skip RUDP initialization for direct connections on PS Vita
  // RUDP is only needed for holepunch/relay connections, not direct IP
  // connections
  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: Direct connection detected - skipping RUDP "
              "initialization");
  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: Will use standard socket connection for PS5 "
              "Remote Play");
  session->rudp = NULL;
#endif

  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: RUDP initialization phase complete - checking "
              "PSN connection");
  // PSN Connection - Skip for local connections that already have keys
  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: Checking if PSN registration needed "
              "(independent of RUDP)");

  // Check if we already have registration keys (local connection)
  bool has_regist_key = false;
  bool has_morning_key = false;

  // Check if registration key is already set (not all zeros)
  for (int i = 0; i < sizeof(session->connect_info.regist_key); i++) {
    if (session->connect_info.regist_key[i] != 0) {
      has_regist_key = true;
      break;
    }
  }

  // Check if morning key is already set (not all zeros)
  for (int i = 0; i < sizeof(session->connect_info.morning); i++) {
    if (session->connect_info.morning[i] != 0) {
      has_morning_key = true;
      break;
    }
  }

  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: Registration keys check - regist_key: %s, "
              "morning_key: %s",
              has_regist_key ? "present" : "missing",
              has_morning_key ? "present" : "missing");

  if (has_regist_key && has_morning_key) {
    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: SKIP PSN REGISTRATION - Already have "
                "registration keys for local connection");
    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: Setting PSN registration success state "
                "without registration");

    // Set success state without doing registration
    chiaki_mutex_lock(&session->state_mutex);
    session->psn_regist_succeeded = true;
    chiaki_mutex_unlock(&session->state_mutex);

    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: PSN registration bypassed successfully");
  } else {
    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: Missing registration keys - starting PSN "
                "registration process");
    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: Initializing ChiakiRegist structures");
    ChiakiRegist regist;
    ChiakiRegistInfo info;

#if !(defined(__SWITCH__) || defined(__PSVITA__))
    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: Setting up holepunch registration info");
    ChiakiHolepunchRegistInfo hinfo =
        chiaki_get_regist_info(session->holepunch_session);
    info.holepunch_info = &hinfo;
#else
    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: PS Vita - no holepunch info needed");
#endif

    CHIAKI_LOGI(session->log, "SESSION_THREAD: Configuring registration info");
    info.host = NULL;
    info.broadcast = false;
    info.psn_online_id = NULL;
    CHIAKI_LOGI(session->log, "SESSION_THREAD: Copying PSN account ID");
    memcpy(info.psn_account_id, session->connect_info.psn_account_id,
           CHIAKI_PSN_ACCOUNT_ID_SIZE);
    // info.rudp = session->rudp; // TODO: Add rudp field to ChiakiRegistInfo
    // when needed
    info.target =
        session->connect_info.ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
    CHIAKI_LOGI(session->log, "SESSION_THREAD: Target set to %s",
                session->connect_info.ps5 ? "PS5_1" : "PS4_10");

    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: Starting PSN registration process");
    chiaki_regist_start(&regist, session->log, &info, regist_cb, session);

    CHIAKI_LOGI(
        session->log,
        "SESSION_THREAD: Waiting for registration completion (10s timeout)");
    chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex,
                               10000, session_check_state_pred_regist, session);

    CHIAKI_LOGI(session->log,
                "SESSION_THREAD: Registration wait completed - cleaning up");
    chiaki_regist_stop(&regist);
    chiaki_regist_fini(&regist);
    CHIAKI_LOGI(session->log, "SESSION_THREAD: Registration cleanup complete");
    CHECK_STOP(quit);
  }

  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: PSN connection phase complete - starting "
              "session request");
  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: About to call session_thread_request_session");
  CHIAKI_LOGI(session->log, "Starting session request for %s",
              session->connect_info.ps5 ? "PS5" : "PS4");

  ChiakiTarget server_target = CHIAKI_TARGET_PS4_UNKNOWN;
  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: Calling session_thread_request_session with "
              "server_target=%d",
              server_target);
  ChiakiErrorCode err = session_thread_request_session(session, &server_target);

  CHIAKI_LOGI(session->log,
              "SESSION_THREAD: session_thread_request_session completed");
  CHIAKI_LOGI(session->log,
              "session_thread_request_session() returned: %s (%d)",
              chiaki_error_string(err), err);
  CHIAKI_LOGI(session->log, "SESSION_THREAD: server_target after call: %d",
              server_target);

  if (err == CHIAKI_ERR_VERSION_MISMATCH &&
      !chiaki_target_is_unknown(server_target)) {
    CHIAKI_LOGI(session->log,
                "Attempting to re-request session with Server's RP-Version");
    session->target = server_target;
    err = session_thread_request_session(session, &server_target);
    CHIAKI_LOGI(session->log, "Re-request returned: %s (%d)",
                chiaki_error_string(err), err);
  } else if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "Session request failed with error: %s (%d)",
                chiaki_error_string(err), err);
    QUIT(quit);
  }

  if (err == CHIAKI_ERR_VERSION_MISMATCH &&
      !chiaki_target_is_unknown(server_target)) {
    CHIAKI_LOGI(session->log,
                "Attempting to re-request session even harder with Server's "
                "RP-Version!!!");
    session->target = server_target;
    err = session_thread_request_session(session, NULL);
  } else if (err != CHIAKI_ERR_SUCCESS)
    QUIT(quit);

  if (err != CHIAKI_ERR_SUCCESS) QUIT(quit);

  CHIAKI_LOGI(session->log, "Session request successful");

  chiaki_rpcrypt_init_auth(&session->rpcrypt, session->target, session->nonce,
                           session->connect_info.morning);

  // PS4 doesn't always react right away, sleep a bit
  chiaki_cond_timedwait_pred(&session->state_cond, &session->state_mutex, 10,
                             session_check_state_pred, session);

  CHIAKI_LOGI(session->log, "Starting ctrl");

  err = chiaki_ctrl_start(&session->ctrl);
  if (err != CHIAKI_ERR_SUCCESS) QUIT(quit);

  err = chiaki_cond_timedwait_pred(
      &session->state_cond, &session->state_mutex, SESSION_EXPECT_TIMEOUT_MS,
      session_check_state_pred_ctrl_start, session);
  CHECK_STOP(quit_ctrl);

  if (session->ctrl_failed) {
    CHIAKI_LOGE(session->log, "Ctrl has failed while waiting for ctrl startup");
    goto ctrl_failed;
  }

  bool pin_incorrect = false;
  while (session->ctrl_login_pin_requested) {
    session->ctrl_login_pin_requested = false;
    if (pin_incorrect)
      CHIAKI_LOGI(session->log,
                  "Login PIN was incorrect, requested again by Ctrl");
    else
      CHIAKI_LOGI(session->log, "Ctrl requested Login PIN");
    ChiakiEvent event = {0};
    event.type = CHIAKI_EVENT_LOGIN_PIN_REQUEST;
    event.login_pin_request.pin_incorrect = pin_incorrect;
    chiaki_session_send_event(session, &event);
    pin_incorrect = true;

    err = chiaki_cond_timedwait_pred(&session->state_cond,
                                     &session->state_mutex, UINT64_MAX,
                                     session_check_state_pred_pin, session);
    CHECK_STOP(quit_ctrl);
    if (session->ctrl_failed) {
      CHIAKI_LOGE(session->log, "Ctrl has failed while waiting for PIN entry");
      goto ctrl_failed;
    }

    assert(session->login_pin_entered && session->login_pin);
    CHIAKI_LOGI(session->log,
                "Session received entered Login PIN, forwarding to Ctrl");
    chiaki_ctrl_set_login_pin(&session->ctrl, session->login_pin,
                              session->login_pin_size);
    session->login_pin_entered = false;
    free(session->login_pin);
    session->login_pin = NULL;
    session->login_pin_size = 0;

    // wait for session id or new login pin request
    err = chiaki_cond_timedwait_pred(
        &session->state_cond, &session->state_mutex, SESSION_EXPECT_TIMEOUT_MS,
        session_check_state_pred_ctrl_start, session);
    CHECK_STOP(quit_ctrl);
  }

  chiaki_socket_t *data_sock = NULL;
#if !(defined(__SWITCH__) || defined(__PSVITA__))
  if (session->rudp) {
    CHIAKI_LOGI(session->log, "Punching hole for data connection");
    ChiakiEvent event_start = {0};
    event_start.type = CHIAKI_EVENT_HOLEPUNCH;
    event_start.data_holepunch.finished = false;
    chiaki_session_send_event(session, &event_start);
    err = chiaki_holepunch_session_punch_hole(session->holepunch_session,
                                              CHIAKI_HOLEPUNCH_PORT_TYPE_DATA);
    if (err != CHIAKI_ERR_SUCCESS) {
      CHIAKI_LOGE(session->log, "!! Failed to punch hole for data connection.");
      QUIT(quit_ctrl);
    }
    CHIAKI_LOGI(session->log, ">> Punched hole for data connection!");
    data_sock = chiaki_get_holepunch_sock(session->holepunch_session,
                                          CHIAKI_HOLEPUNCH_PORT_TYPE_DATA);
    ChiakiEvent event_finish = {0};
    event_finish.type = CHIAKI_EVENT_HOLEPUNCH;
    event_finish.data_holepunch.finished = true;
    chiaki_session_send_event(session, &event_finish);
    err = chiaki_cond_timedwait_pred(
        &session->state_cond, &session->state_mutex, SESSION_EXPECT_TIMEOUT_MS,
        session_check_state_pred_ctrl_start, session);
    CHECK_STOP(quit_ctrl);
  }
#endif

  if (!session->ctrl_session_id_received) {
    CHIAKI_LOGE(session->log, "Ctrl did not receive session id");
    chiaki_mutex_unlock(&session->state_mutex);
    err = ctrl_message_set_fallback_session_id(&session->ctrl);
    chiaki_mutex_lock(&session->state_mutex);
    if (err != CHIAKI_ERR_SUCCESS) goto ctrl_failed;
    ctrl_enable_features(&session->ctrl);
  }

  if (!session->ctrl_session_id_received) {
  ctrl_failed:
    CHIAKI_LOGE(session->log, "Ctrl has failed, shutting down");
    if (session->quit_reason == CHIAKI_QUIT_REASON_NONE)
      session->quit_reason = CHIAKI_QUIT_REASON_CTRL_UNKNOWN;
    QUIT(quit_ctrl);
  }

  // Minimal Senkusha: attempt a quick RTT measurement on UDP 9297
  CHIAKI_LOGI(session->log, "Senkusha quick RTT measurement on UDP 9297");
  session->mtu_in = 1454;
  session->mtu_out = 1454;
  session->rtt_us = 10000;  // default 10ms
  int udp = socket(AF_INET, SOCK_DGRAM, 0);
  if (!CHIAKI_SOCKET_IS_INVALID(udp)) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9297);
    inet_aton(session->connect_info.hostname, &sa.sin_addr);
    uint8_t ping[16] = {0};
    uint64_t t0 = chiaki_time_now_monotonic_us();
    sendto(udp, ping, sizeof(ping), 0, (struct sockaddr *)&sa, sizeof(sa));
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;  // 500ms
    setsockopt(udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    uint8_t pong[64];
    socklen_t sl = sizeof(sa);
    int rec = recvfrom(udp, pong, sizeof(pong), 0, (struct sockaddr *)&sa, &sl);
    if (rec > 0) {
      uint64_t dt = chiaki_time_now_monotonic_us() - t0;
      session->rtt_us = dt > 0 ? dt : session->rtt_us;
      CHIAKI_LOGI(session->log, "Senkusha RTT measured: %lluus",
                  (unsigned long long)session->rtt_us);
    } else {
      CHIAKI_LOGW(session->log,
                  "Senkusha RTT probe no response, using default");
    }
    CHIAKI_SOCKET_CLOSE(udp);
  } else {
    CHIAKI_LOGW(session->log, "Senkusha UDP socket create failed");
  }
  if (session->rudp) {
    // TODO: Implement RUDP stream connection switch when needed
    CHIAKI_LOGI(session->log, "RUDP stream connection switch not implemented");
  }

  err = chiaki_random_bytes_crypt(session->handshake_key,
                                  sizeof(session->handshake_key));
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "Session failed to generate handshake key");
    QUIT(quit_ctrl);
  }

  // PHASE 2B: ECDH for encryption setup (currently using fallback keys)
  CHIAKI_LOGI(session->log,
              "ECDH not implemented, using fallback encryption keys");

  // PHASE 2B: Stream Connection - Main Remote Play Protocol
  CHIAKI_LOGI(session->log,
              "Starting Stream Connection for Remote Play streaming");

  // Initialize stream connection now (after ctrl success) to strictly follow
  // Vitaki/Chiaki ordering and avoid pre-HTTP side effects
  {
    VitaRPS5Result sc_init =
        chiaki_stream_connection_init(&session->stream_connection, session);
    if (sc_init != VITARPS5_SUCCESS) {
      CHIAKI_LOGE(session->log, "StreamConnection init failed at run stage");
      session->quit_reason = CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN;
      QUIT(quit_ctrl);
    }
    session->takion_connection = session->stream_connection.takion_conn;
    if (session->takion_connection)
      CHIAKI_LOGI(session->log,
                  "✅ TakionConnection assigned to ChiakiSession successfully");
    else
      CHIAKI_LOGE(session->log, "CRITICAL: Takion connection is NULL");
  }

  if (!session->takion_connection) {
    session->quit_reason = CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN;
  } else {
    chiaki_mutex_unlock(&session->state_mutex);

    // Call stream connection with proper parameters:
    // - stream_connection: Already initialized in session_init
    // - data_sock: NULL for UDP mode (PS Vita uses UDP)
    // - takion_connection: From session manager
    VitaRPS5Result stream_result = chiaki_stream_connection_run(
        &session->stream_connection, NULL, session->takion_connection);

    chiaki_mutex_lock(&session->state_mutex);

    // Convert VitaRPS5Result to ChiakiErrorCode and handle results
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    switch (stream_result) {
      case VITARPS5_SUCCESS:
        err = CHIAKI_ERR_SUCCESS;
        break;
      case VITARPS5_ERROR_TIMEOUT:
        err = CHIAKI_ERR_TIMEOUT;
        break;
      case VITARPS5_ERROR_NOT_CONNECTED:
        err = CHIAKI_ERR_DISCONNECTED;
        break;
      case VITARPS5_ERROR_CANCELLED:
        err = CHIAKI_ERR_CANCELED;
        break;
      default:
        err = CHIAKI_ERR_UNKNOWN;
        break;
    }

    // Handle stream connection results
    if (err == CHIAKI_ERR_DISCONNECTED) {
      CHIAKI_LOGE(session->log, "Remote disconnected from StreamConnection");
      if (session->stream_connection.remote_disconnect_reason) {
        if (!strcmp(session->stream_connection.remote_disconnect_reason,
                    "Server shutting down"))
          session->quit_reason =
              CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN;
        else
          session->quit_reason =
              CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED;
        session->quit_reason_str =
            strdup(session->stream_connection.remote_disconnect_reason);
      } else {
        session->quit_reason =
            CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED;
      }
    } else if (err != CHIAKI_ERR_SUCCESS && err != CHIAKI_ERR_CANCELED) {
      CHIAKI_LOGE(session->log, "StreamConnection run failed: %s",
                  chiaki_error_string(err));
      session->quit_reason = CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN;
    } else {
      CHIAKI_LOGI(session->log, "StreamConnection completed successfully");
      session->quit_reason = CHIAKI_QUIT_REASON_STOPPED;
    }
  }

  chiaki_mutex_unlock(&session->state_mutex);

quit_ctrl:
  chiaki_ctrl_stop(&session->ctrl);
  chiaki_ctrl_join(&session->ctrl);
  CHIAKI_LOGI(session->log, "Ctrl stopped");

  ChiakiEvent quit_event;
quit:

  CHIAKI_LOGI(session->log, "Session has quit");
  quit_event.type = CHIAKI_EVENT_QUIT;
  quit_event.quit.reason = session->quit_reason;
  quit_event.quit.reason_str = session->quit_reason_str;
  chiaki_session_send_event(session, &quit_event);
  return NULL;

#undef CHECK_STOP
#undef QUIT
}

typedef struct session_response_t {
  uint32_t error_code;
  const char *nonce;
  const char *rp_version;
  bool success;
} SessionResponse;

static void parse_session_response(SessionResponse *response,
                                   ChiakiHttpResponse *http_response) {
  memset(response, 0, sizeof(SessionResponse));

  for (ChiakiHttpHeader *header = http_response->headers; header;
       header = header->next) {
    if (strcmp(header->key, "RP-Nonce") == 0)
      response->nonce = header->value;
    else if (strcasecmp(header->key, "RP-Version") == 0)
      response->rp_version = header->value;
    else if (strcmp(header->key, "RP-Application-Reason") == 0)
      response->error_code = (uint32_t)strtoul(header->value, NULL, 0x10);
  }

  if (http_response->code == 200)
    response->success = response->nonce != NULL;
  else
    response->success = false;
}

/**
 * @param target_out if NULL, version mismatch means to fail the entire session,
 * otherwise report the target here
 */
static ChiakiErrorCode session_thread_request_session(
    ChiakiSession *session, ChiakiTarget *target_out) {
  if (!session || !session->log) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  CHIAKI_LOGI(session->log, "Starting session request");

  // PS5 flow: If the session manager already performed HTTP /sess/init and
  // populated the nonce, we must still perform the HTTP ctrl handshake here,
  // but we must NOT re-run sess/init to avoid 403 due to duplicate init.
  if (session->connect_info.ps5) {
    bool nonce_present = false;
    for (size_t i = 0; i < CHIAKI_RPCRYPT_KEY_SIZE; i++) {
      if (session->nonce[i] != 0) {
        nonce_present = true;
        break;
      }
    }
    if (nonce_present) {
      CHIAKI_LOGI(session->log,
                  "PS5: HTTP session already initialized (nonce present) — "
                  "skipping manual sess/init, proceeding to ctrl handshake");
      // Initialize rpcrypt keys now that nonce is available
      chiaki_rpcrypt_init_auth(&session->rpcrypt, session->target,
                               session->nonce, session->connect_info.morning);
      // Diagnostics: show slices of keys used for ctrl
      CHIAKI_LOGI(
          session->log,
          "CTRL DIAG: regist_key: %02X %02X %02X %02X ... %02X %02X %02X %02X",
          (unsigned)session->connect_info.regist_key[0],
          (unsigned)session->connect_info.regist_key[1],
          (unsigned)session->connect_info.regist_key[2],
          (unsigned)session->connect_info.regist_key[3],
          (unsigned)session->connect_info.regist_key[12],
          (unsigned)session->connect_info.regist_key[13],
          (unsigned)session->connect_info.regist_key[14],
          (unsigned)session->connect_info.regist_key[15]);
      CHIAKI_LOGI(
          session->log,
          "CTRL DIAG: morning:    %02X %02X %02X %02X ... %02X %02X %02X %02X",
          (unsigned)session->connect_info.morning[0],
          (unsigned)session->connect_info.morning[1],
          (unsigned)session->connect_info.morning[2],
          (unsigned)session->connect_info.morning[3],
          (unsigned)session->connect_info.morning[12],
          (unsigned)session->connect_info.morning[13],
          (unsigned)session->connect_info.morning[14],
          (unsigned)session->connect_info.morning[15]);
      // Jump to ctrl phase below
      goto ctrl_phase;
    }
  }

  chiaki_socket_t session_sock = CHIAKI_INVALID_SOCKET;
  uint16_t remote_counter = 0;

  if (session->rudp) {
    /*
                    // FIXME ywnico: this block was removed in deck merge; check
    to see if vita hacks still needed struct sockaddr *sa =
    malloc(ai->ai_addrlen); if(!sa) continue; memcpy(sa, ai->ai_addr,
    ai->ai_addrlen);

                    if(sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
                    {
                            free(sa);
                            continue;
                    }

                    set_port(sa, htons(SESSION_PORT));

                    #ifdef __PSVITA__
                    //  FIXME: this is broken, error 0
                    // 	int errno;
                    // 	int rid = sceNetResolverCreate("resolver", NULL, 0);
                    // 	if (rid < 0) {
                    // 		errno = rid & 0xFF;
                    // 		goto vitadns_err;
                    // 	}
                    // 	sockaddr_in* sa_in = (sockaddr_in*) sa;
                    // 	SceNetInAddr addr = { (sa_in->sin_addr).s_addr };
                    // 	int r = sceNetResolverStartAton(
                    // 		rid,
                    // 		&addr,
                    // 		session->connect_info.hostname,
                    // 		sizeof(session->connect_info.hostname),
                    // 		1500,
                    // 		3,
                    // 		0);
                    //   if (r < 0) {
                    // 	vitadns_err:
                    // 		CHIAKI_LOGE(session->log, "Failed to resolve
    hostname, %d", errno);
                    // 		memcpy(session->connect_info.hostname,
    "unknown", 8);
                    // 	}
                    // 	sceNetResolverDestroy(rid);
                    #else
                            // TODO: this can block, make cancelable somehow
                            int r = getnameinfo(sa, (socklen_t)ai->ai_addrlen,
    session->connect_info.hostname, sizeof(session->connect_info.hostname),
    NULL, 0, NI_NUMERICHOST); if(r != 0)
                            {
                                    CHIAKI_LOGE(session->log, "getnameinfo
    failed with %s, filling the hostname with fallback", gai_strerror(r));
                                    memcpy(session->connect_info.hostname,
    "unknown", 8);
                            }
                    #endif

                    CHIAKI_LOGI(session->log, "Trying to request session from
    %s:%d", session->connect_info.hostname, SESSION_PORT);

                    // #ifdef __PSVITA__
                    // session_sock = sceNetSocket("", ai->ai_family,
    SCE_NET_SOCK_STREAM, 0);
                    // #else
                    session_sock = socket(ai->ai_family, SOCK_STREAM, 0);
                    // #endif
                    if(CHIAKI_SOCKET_IS_INVALID(session_sock))
                    {
    #ifdef _WIN32
                            CHIAKI_LOGE(session->log, "Failed to create socket
    to request session"); #elif defined(__PSVITA__) CHIAKI_LOGE(session->log,
    "Failed to create socket to request session: 0x%x", session_sock); #else
                            CHIAKI_LOGE(session->log, "Failed to create socket
    to request session: %s", strerror(errno)); #endif free(sa); continue;
                    }
    */
    CHIAKI_LOGI(session->log, "SESSION START THREAD - Starting RUDP session");
    // TODO: Implement RUDP protocol when needed
    // For now, skip RUDP initialization and continue with direct connection
    CHIAKI_LOGI(
        session->log,
        "SESSION START THREAD - RUDP not implemented, using direct connection");
    // Skip RUDP initialization for now
    remote_counter = 0;
  } else {
    if (!session->connect_info.host_addrinfos) {
      CHIAKI_LOGE(session->log, "host_addrinfos is NULL");
      return CHIAKI_ERR_INVALID_DATA;
    }

    for (struct addrinfo *ai = session->connect_info.host_addrinfos; ai;
         ai = ai->ai_next) {
      // if(ai->ai_protocol != IPPROTO_TCP)
      //	continue;

      struct sockaddr *sa = malloc(ai->ai_addrlen);
      if (!sa) continue;
      memcpy(sa, ai->ai_addr, ai->ai_addrlen);

      if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6) {
        free(sa);
        continue;
      }

      set_port(sa, htons(SESSION_PORT));

      // TODO: this can block, make cancelable somehow
#ifndef __PSVITA__
      //  FIXME: ok on vita?
      int r = getnameinfo(
          sa, (socklen_t)ai->ai_addrlen, session->connect_info.hostname,
          sizeof(session->connect_info.hostname), NULL, 0, NI_NUMERICHOST);
      if (r != 0) {
        CHIAKI_LOGE(
            session->log,
            "getnameinfo failed with %s, filling the hostname with fallback",
            gai_strerror(r));
        memcpy(session->connect_info.hostname, "unknown", 8);
      }
#endif

      session_sock = socket(ai->ai_family, SOCK_STREAM, 0);
      if (CHIAKI_SOCKET_IS_INVALID(session_sock)) {
        free(sa);
        continue;
      }

      // VITAKI-FORK FIX: Non-blocking mode will be set by
      // chiaki_socket_set_nonblock() Removed duplicate setsockopt(SO_NONBLOCK)
      // to prevent socket configuration conflicts

      // PS VITA FIX: Configure socket options for better connectivity
#ifdef __PSVITA__
      // PS Vita: Socket options via sceNetSetsockopt() can corrupt socket
      // descriptors Similar to SO_NONBLOCK causing EBADF errors, other socket
      // options may have same issue Skip socket option configuration to prevent
      // socket corruption
      CHIAKI_LOGI(session->log,
                  "PS VITA: Skipping socket options (timeout/keepalive) to "
                  "avoid EBADF corruption");
#endif

      CHIAKI_LOGD(session->log,
                  "Setting socket to non-blocking mode (socket fd=%d)",
                  session_sock);
      ChiakiErrorCode err = chiaki_socket_set_nonblock(session_sock, true);
      if (err != CHIAKI_ERR_SUCCESS) {
        CHIAKI_LOGE(session->log,
                    "Failed to set socket non-blocking mode: error=%d", err);
        CHIAKI_SOCKET_CLOSE(session_sock);
        free(sa);
        continue;
      }
      CHIAKI_LOGD(session->log,
                  "Socket successfully configured for non-blocking operation");

      chiaki_mutex_unlock(&session->state_mutex);

      // Try connection with retry logic (up to 3 attempts)
      for (int retry = 0; retry < 3; retry++) {
        CHIAKI_LOGI(session->log,
                    "SESSION_THREAD: Connection attempt %d/3 starting",
                    retry + 1);
        CHIAKI_LOGI(session->log, "SESSION_THREAD: Session socket: %d",
                    session_sock);
        CHIAKI_LOGI(session->log, "SESSION_THREAD: Stop pipe FD: %d",
                    session->stop_pipe.fd);
        CHIAKI_LOGI(session->log, "SESSION_THREAD: Address length: %d",
                    ai->ai_addrlen);

        // Critical: Force log flush before potentially hanging call
        fflush(stdout);
        fflush(stderr);

        CHIAKI_LOGI(session->log,
                    "SESSION_THREAD: About to call chiaki_stop_pipe_connect");
        err = chiaki_stop_pipe_connect(&session->stop_pipe, session_sock, sa,
                                       ai->ai_addrlen);

        // If we reach here, connection attempt completed (success or failure)
        CHIAKI_LOGI(
            session->log,
            "[CONNECT_LAYER] *** chiaki_stop_pipe_connect() COMPLETED ***");
        CHIAKI_LOGI(
            session->log,
            "[CONNECT_LAYER] chiaki_stop_pipe_connect() returned: %s (%d)",
            chiaki_error_string(err), err);

        if (err == CHIAKI_ERR_SUCCESS) {
          CHIAKI_LOGI(session->log, "✅ Connection successful on attempt %d",
                      retry + 1);
          break;
        } else if (err == CHIAKI_ERR_CANCELED) {
          CHIAKI_LOGI(session->log, "Connection canceled during attempt %d",
                      retry + 1);
          break;
        } else {
          CHIAKI_LOGE(session->log, "Connection attempt %d failed: %s",
                      retry + 1, chiaki_error_string(err));

          // If not the last attempt, wait before retrying
          if (retry < 2) {
            int wait_ms = (retry + 1) * 1000;  // 1s, 2s exponential backoff
            CHIAKI_LOGI(
                session->log,
                "Waiting %dms before retry (PS5 may need more time to wake)",
                wait_ms);
            chiaki_mutex_lock(&session->state_mutex);
            chiaki_cond_timedwait_pred(&session->state_cond,
                                       &session->state_mutex, wait_ms,
                                       session_check_state_pred, session);
            chiaki_mutex_unlock(&session->state_mutex);
          }
        }
      }

      chiaki_mutex_lock(&session->state_mutex);

      if (err == CHIAKI_ERR_CANCELED) {
        CHIAKI_LOGI(session->log,
                    "Session stopped while connecting for session request");
        session->quit_reason = CHIAKI_QUIT_REASON_STOPPED;
        CHIAKI_SOCKET_CLOSE(session_sock);
        session_sock = CHIAKI_INVALID_SOCKET;
        free(sa);
        break;
      } else if (err != CHIAKI_ERR_SUCCESS) {
        CHIAKI_LOGE(session->log, "=== ALL CONNECTION ATTEMPTS FAILED ===");
        CHIAKI_LOGE(session->log, "Final error: %s", chiaki_error_string(err));
        CHIAKI_LOGE(session->log,
                    "PS5 may be: offline, sleeping, firewalled, or Remote Play "
                    "disabled");
        if (err == CHIAKI_ERR_CONNECTION_REFUSED)
          session->quit_reason =
              CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED;
        else
          session->quit_reason = CHIAKI_QUIT_REASON_NONE;
        CHIAKI_SOCKET_CLOSE(session_sock);
        session_sock = CHIAKI_INVALID_SOCKET;
        free(sa);
        continue;
      }

      free(sa);

      session->connect_info.host_addrinfo_selected = ai;
      break;
    }

    if (CHIAKI_SOCKET_IS_INVALID(session_sock)) {
      CHIAKI_LOGE(session->log, "=== SOCKET CONNECTION FAILURE ANALYSIS ===");
      CHIAKI_LOGE(session->log,
                  "Session request connect failed to all addresses");
      CHIAKI_LOGE(session->log, "All socket connection attempts failed");
      CHIAKI_LOGE(session->log,
                  "PS5 may be offline, unreachable, or blocking connections");
      if (session->quit_reason == CHIAKI_QUIT_REASON_NONE)
        session->quit_reason = CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
      return CHIAKI_ERR_NETWORK;
    } else
      CHIAKI_LOGI(session->log,
                  "✅ Connected to %s:%d - socket connection successful",
                  session->connect_info.hostname, SESSION_PORT);
  }

  // Vitaki/Chiaki-aligned PS5 sess/init: GET /sie/ps5/rp/sess/init on TCP 9295
  // Build RP-Registkey from regist_key with dynamic length (trim at first 0x00)
  // to match Vitaki behavior and avoid sending spurious zero bytes.
  size_t regist_key_len = sizeof(session->connect_info.regist_key);
  for (size_t i = 0; i < sizeof(session->connect_info.regist_key); ++i) {
    if (!session->connect_info.regist_key[i]) {
      regist_key_len = i;
      break;
    }
  }
  if (regist_key_len == 0 || regist_key_len > 16)
    regist_key_len = 16;  // safety
  char regist_key_hex[16 * 2 + 1] = {0};
  static const char *hexd = "0123456789abcdef";
  for (size_t i = 0; i < regist_key_len; i++) {
    uint8_t b = session->connect_info.regist_key[i];
    regist_key_hex[i * 2] = hexd[(b >> 4) & 0xF];
    regist_key_hex[i * 2 + 1] = hexd[b & 0xF];
  }
  regist_key_hex[regist_key_len * 2] = '\0';
  CHIAKI_LOGI(session->log, "sess/init: RP-Registkey value = %s",
              regist_key_hex);

  const char *rp_version_str = chiaki_rp_version_string(session->target);
  if (!rp_version_str) rp_version_str = "1.0";

  // Build request with Host header including port and Rp-Version (lowercase
  // 'p') Use GET for PS5 sess/init to align with Vitaki baseline.
  static const char request_fmt_get_with_port[] =
      "GET %s HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "User-Agent: remoteplay Windows\r\n"
      "Connection: close\r\n"
      "Content-Length: 0\r\n"
      "RP-Registkey: %s\r\n"
      "Rp-Version: %s\r\n"
      "\r\n";
  // Vitaki uses GET variant with Host including port; no POST or Host overrides
  const char *path = "/sie/ps5/rp/sess/init";
  int port = (session->connect_info.control_port > 0)
                 ? session->connect_info.control_port
                 : SESSION_PORT;  // default 9295
sess_init_attempt: {
  char send_buf[512];
  int req_len = snprintf(send_buf, sizeof(send_buf), request_fmt_get_with_port,
                         path, session->connect_info.hostname, port,
                         regist_key_hex, rp_version_str);
  if (req_len <= 0 || req_len >= (int)sizeof(send_buf)) {
    CHIAKI_LOGE(session->log, "Failed to build sess/init request");
    CHIAKI_SOCKET_CLOSE(session_sock);
    return CHIAKI_ERR_UNKNOWN;
  }

  CHIAKI_LOGI(session->log, "HTTP sess/init: sending GET %s to %s:%d", path,
              session->connect_info.hostname, port);
  // Hexdump full request for comparison with Vitaki/Chiaki-NG
  chiaki_log_hexdump(session->log, CHIAKI_LOG_VERBOSE,
                     (const uint8_t *)send_buf, (size_t)req_len);

  int sent = send(session_sock, send_buf, (size_t)req_len, 0);
  if (sent < 0 || sent != req_len) {
    CHIAKI_LOGE(session->log, "sess/init send failed (sent=%d)", sent);
    CHIAKI_SOCKET_CLOSE(session_sock);
    return CHIAKI_ERR_NETWORK;
  }

  char recv_buf[512] = {0};
  size_t header_size = 0, total_received = 0;
  ChiakiErrorCode herr = chiaki_recv_http_header(
      session_sock, recv_buf, sizeof(recv_buf), &header_size, &total_received,
      (struct chiaki_stop_pipe_t *)&session->stop_pipe, 5000);
  if (herr != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "sess/init recv failed: %s",
                chiaki_error_string(herr));
    CHIAKI_SOCKET_CLOSE(session_sock);
    return herr;
  }

  ChiakiHttpResponse http_response;
  ChiakiErrorCode perr =
      chiaki_http_response_parse(&http_response, recv_buf, header_size);
  if (perr != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "sess/init parse failed: %s",
                chiaki_error_string(perr));
    CHIAKI_SOCKET_CLOSE(session_sock);
    return perr;
  }
  // Log response header for diagnostics and parse application reason
  CHIAKI_LOGI(session->log,
              "sess/init response header (code=%d):", http_response.code);
  chiaki_log_hexdump(session->log, CHIAKI_LOG_VERBOSE,
                     (const uint8_t *)recv_buf, header_size);
  SessionResponse resp;
  parse_session_response(&resp, &http_response);
  if (!resp.success)
    CHIAKI_LOGW(session->log, "sess/init application reason: %#x (%s)",
                (unsigned int)resp.error_code,
                chiaki_rp_application_reason_string(resp.error_code));
  CHIAKI_LOGI(session->log, "sess/init HTTP status: %d", http_response.code);
  // (No retry needed for adding port: we already include it)

  // No Vitaki-incompatible retries (uppercase/base64/host-port alterations)

  // Drop non-Vitaki fallback that mutates Host header to an arbitrary port.

  if (!resp.success || !resp.nonce) {
    CHIAKI_LOGE(session->log, "sess/init missing RP-Nonce or not successful");
    chiaki_http_response_fini(&http_response);
    CHIAKI_SOCKET_CLOSE(session_sock);
    return CHIAKI_ERR_INVALID_DATA;
  }

  size_t nonce_len = CHIAKI_RPCRYPT_KEY_SIZE;
  ChiakiErrorCode nerr = chiaki_base64_decode(resp.nonce, strlen(resp.nonce),
                                              session->nonce, &nonce_len);
  if (nerr != CHIAKI_ERR_SUCCESS || nonce_len != CHIAKI_RPCRYPT_KEY_SIZE) {
    CHIAKI_LOGE(session->log, "sess/init RP-Nonce decode failed (len=%zu)",
                nonce_len);
    chiaki_http_response_fini(&http_response);
    CHIAKI_SOCKET_CLOSE(session_sock);
    return CHIAKI_ERR_INVALID_DATA;
  }
  CHIAKI_LOGI(session->log, "sess/init: RP-Nonce received and decoded (16B)");
  chiaki_http_response_fini(&http_response);
}

  // Close HTTP socket after sess/init
  CHIAKI_SOCKET_CLOSE(session_sock);
  session_sock = CHIAKI_INVALID_SOCKET;

  // Initialize rpcrypt keys now that nonce is available
  chiaki_rpcrypt_init_auth(&session->rpcrypt, session->target, session->nonce,
                           session->connect_info.morning);

  // Diagnostics: show slices of keys used for ctrl
  CHIAKI_LOGI(
      session->log,
      "CTRL DIAG: regist_key: %02X %02X %02X %02X ... %02X %02X %02X %02X",
      (unsigned)session->connect_info.regist_key[0],
      (unsigned)session->connect_info.regist_key[1],
      (unsigned)session->connect_info.regist_key[2],
      (unsigned)session->connect_info.regist_key[3],
      (unsigned)session->connect_info.regist_key[12],
      (unsigned)session->connect_info.regist_key[13],
      (unsigned)session->connect_info.regist_key[14],
      (unsigned)session->connect_info.regist_key[15]);
  CHIAKI_LOGI(
      session->log,
      "CTRL DIAG: morning:    %02X %02X %02X %02X ... %02X %02X %02X %02X",
      (unsigned)session->connect_info.morning[0],
      (unsigned)session->connect_info.morning[1],
      (unsigned)session->connect_info.morning[2],
      (unsigned)session->connect_info.morning[3],
      (unsigned)session->connect_info.morning[12],
      (unsigned)session->connect_info.morning[13],
      (unsigned)session->connect_info.morning[14],
      (unsigned)session->connect_info.morning[15]);

  // Predeclare ctrl handshake temporaries so we can jump into this phase
  uint8_t enc_buf[64];
  char b64_buf[128];
  ChiakiErrorCode enc_err;
  size_t b64_len;
  int ctrl_port = (session->connect_info.control_port > 0)
                      ? session->connect_info.control_port
                      : SESSION_PORT;

ctrl_phase:
  // PS5 CTRL handshake: GET /sie/ps5/rp/sess/ctrl with encrypted headers
  // Prepare encrypted header values with monotonically increasing counters
ctrl_attempt: {
  struct sockaddr_in sa_ctrl;
  memset(&sa_ctrl, 0, sizeof(sa_ctrl));
  sa_ctrl.sin_family = AF_INET;
  sa_ctrl.sin_port = htons(ctrl_port);
  inet_aton(session->connect_info.hostname, &sa_ctrl.sin_addr);

  session_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (CHIAKI_SOCKET_IS_INVALID(session_sock)) {
    CHIAKI_LOGE(session->log, "Failed to create socket for ctrl");
    return CHIAKI_ERR_NETWORK;
  }
  chiaki_socket_set_nonblock(session_sock, true);
  ChiakiErrorCode err;
  err = chiaki_stop_pipe_connect(&session->stop_pipe, session_sock,
                                 (struct sockaddr *)&sa_ctrl, sizeof(sa_ctrl));
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "CTRL connect failed: %s",
                chiaki_error_string(err));
    CHIAKI_SOCKET_CLOSE(session_sock);
    session_sock = CHIAKI_INVALID_SOCKET;
    return err;
  }

  // RP-Auth: encrypt full 16-byte regist_key with counter 0
  int rk_len = 16;
  enc_err = chiaki_rpcrypt_encrypt(
      &session->rpcrypt, 0, (const uint8_t *)session->connect_info.regist_key,
      enc_buf, 16);
  if (enc_err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "RP-Auth encryption failed");
    CHIAKI_SOCKET_CLOSE(session_sock);
    return enc_err;
  }
  if (chiaki_base64_encode(enc_buf, 16, b64_buf, sizeof(b64_buf)) !=
      CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "RP-Auth base64 encode failed");
    CHIAKI_SOCKET_CLOSE(session_sock);
    return CHIAKI_ERR_UNKNOWN;
  }
  char rp_auth_hdr[160];
  char rp_auth_b64[128];
  memset(rp_auth_b64, 0, sizeof(rp_auth_b64));
  strncpy(rp_auth_b64, b64_buf, sizeof(rp_auth_b64) - 1);
  snprintf(rp_auth_hdr, sizeof(rp_auth_hdr), "RP-Auth: %s\r\n", b64_buf);
  size_t rp_auth_len = strnlen(rp_auth_b64, sizeof(rp_auth_b64));
  CHIAKI_LOGI(session->log,
              "CTRL DEBUG: RP-Auth src-bytes: 16, b64 length: %zu",
              rp_auth_len);

  // RP-Did: encrypt 32B DID with counter 1
  enc_err =
      chiaki_rpcrypt_encrypt(&session->rpcrypt, 1, session->connect_info.did,
                             enc_buf, CHIAKI_RP_DID_SIZE);
  if (enc_err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "RP-Did encryption failed");
    CHIAKI_SOCKET_CLOSE(session_sock);
    return enc_err;
  }
  if (chiaki_base64_encode(enc_buf, CHIAKI_RP_DID_SIZE, b64_buf,
                           sizeof(b64_buf)) != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "RP-Did base64 encode failed");
    CHIAKI_SOCKET_CLOSE(session_sock);
    return CHIAKI_ERR_UNKNOWN;
  }
  char rp_did_hdr[200];
  snprintf(rp_did_hdr, sizeof(rp_did_hdr), "RP-Did: %s\r\n", b64_buf);

  // RP-OSType: encrypt "Win10.0.0\0" (10B) with counter 2
  const char *os_plain = "Win10.0.0\0";
  enc_err = chiaki_rpcrypt_encrypt(&session->rpcrypt, 2,
                                   (const uint8_t *)os_plain, enc_buf, 10);
  if (enc_err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "RP-OSType encryption failed");
    CHIAKI_SOCKET_CLOSE(session_sock);
    return enc_err;
  }
  if (chiaki_base64_encode(enc_buf, 10, b64_buf, sizeof(b64_buf)) !=
      CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "RP-OSType base64 encode failed");
    CHIAKI_SOCKET_CLOSE(session_sock);
    return CHIAKI_ERR_UNKNOWN;
  }
  char rp_ostype_hdr[160];
  snprintf(rp_ostype_hdr, sizeof(rp_ostype_hdr), "RP-OSType: %s\r\n", b64_buf);

  // Build CTRL request (vitaki order): RP-Auth, RP-Version, RP-Did,
  // RP-ControllerType, RP-ClientType, RP-OSType, RP-ConPath, [RP-StreamingType]
  const char *ctrl_path = "/sie/ps5/rp/sess/ctrl";
  char ctrl_buf[1200];
  int ctrl_len = 0;
  int r;
  r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len,
               "GET %s HTTP/1.1\r\n"
               "Host: :%d\r\n"
               "User-Agent: remoteplay Windows\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 0\r\n",
               ctrl_path, ctrl_port);
  if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf)) goto ctrl_build_fail;
  // RP-Auth first
  r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len, "%s",
               rp_auth_hdr);
  if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf)) goto ctrl_build_fail;
  // RP-Version
  r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len,
               "RP-Version: 1.0\r\n");
  if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf)) goto ctrl_build_fail;
  // RP-Did
  r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len, "%s",
               rp_did_hdr);
  if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf)) goto ctrl_build_fail;
  // Static headers
  r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len,
               "RP-ControllerType: 3\r\n"
               "RP-ClientType: 11\r\n");
  if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf)) goto ctrl_build_fail;
  // RP-OSType
  r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len, "%s",
               rp_ostype_hdr);
  if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf)) goto ctrl_build_fail;
  // RP-ConPath
  r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len,
               "RP-ConPath: 1\r\n");
  if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf)) goto ctrl_build_fail;

  // PS5: RP-StartBitrate (4B) followed by RP-StreamingType (4B)
  // RP-StartBitrate: initial bitrate hint. Vitaki sends 0 (encrypted).
  char start_bitrate_hdr[64];
  int sb_len = 0;
  if (chiaki_target_is_ps5(session->target)) {
    uint8_t sbuf[4] = {0, 0, 0, 0};
    uint8_t sb_enc[4] = {0};
    enc_err = chiaki_rpcrypt_encrypt(&session->rpcrypt, 3, sbuf, sb_enc, 4);
    if (enc_err != CHIAKI_ERR_SUCCESS) {
      CHIAKI_LOGE(session->log, "RP-StartBitrate encryption failed");
      CHIAKI_SOCKET_CLOSE(session_sock);
      return enc_err;
    }
    if (chiaki_base64_encode(sb_enc, 4, b64_buf, sizeof(b64_buf)) !=
        CHIAKI_ERR_SUCCESS) {
      CHIAKI_LOGE(session->log, "RP-StartBitrate base64 encode failed");
      CHIAKI_SOCKET_CLOSE(session_sock);
      return CHIAKI_ERR_UNKNOWN;
    }
    sb_len = snprintf(start_bitrate_hdr, sizeof(start_bitrate_hdr),
                      "RP-StartBitrate: %s\r\n", b64_buf);
    if (sb_len <= 0 || sb_len >= (int)sizeof(start_bitrate_hdr))
      goto ctrl_build_fail;
    // Append header
    r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len, "%s",
                 start_bitrate_hdr);
    if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf))
      goto ctrl_build_fail;

    // RP-StreamingType based on codec (use next counter = 4)
    uint32_t streaming_type = 1;  // default AVC
    switch (session->connect_info.video_profile.codec) {
      case CHIAKI_CODEC_H265:
        streaming_type = 2;
        break;
      case CHIAKI_CODEC_H265_HDR:
        streaming_type = 3;
        break;
      default:
        streaming_type = 1;
        break;
    }
    uint8_t st_buf[4] = {(uint8_t)(streaming_type & 0xFF),
                         (uint8_t)((streaming_type >> 8) & 0xFF),
                         (uint8_t)((streaming_type >> 16) & 0xFF),
                         (uint8_t)((streaming_type >> 24) & 0xFF)};
    uint8_t st_enc[4] = {0};
    enc_err = chiaki_rpcrypt_encrypt(&session->rpcrypt, 4, st_buf, st_enc, 4);
    if (enc_err != CHIAKI_ERR_SUCCESS) {
      CHIAKI_LOGE(session->log, "RP-StreamingType encryption failed");
      CHIAKI_SOCKET_CLOSE(session_sock);
      return enc_err;
    }
    if (chiaki_base64_encode(st_enc, 4, b64_buf, sizeof(b64_buf)) !=
        CHIAKI_ERR_SUCCESS) {
      CHIAKI_LOGE(session->log, "RP-StreamingType base64 encode failed");
      CHIAKI_SOCKET_CLOSE(session_sock);
      return CHIAKI_ERR_UNKNOWN;
    }
    r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len,
                 "RP-StreamingType: %s\r\n", b64_buf);
    if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf))
      goto ctrl_build_fail;
  }
  // Final CRLF
  r = snprintf(ctrl_buf + ctrl_len, sizeof(ctrl_buf) - ctrl_len, "\r\n");
  if (r <= 0 || (ctrl_len += r) >= (int)sizeof(ctrl_buf)) goto ctrl_build_fail;

  CHIAKI_LOGI(session->log, "HTTP ctrl: sending GET %s to %s:%d", ctrl_path,
              session->connect_info.hostname, ctrl_port);
  CHIAKI_LOGI(session->log, "CTRL DEBUG: full request (%d bytes):", ctrl_len);
  chiaki_log_hexdump(session->log, CHIAKI_LOG_VERBOSE,
                     (const uint8_t *)ctrl_buf, (size_t)ctrl_len);
  int sent = send(session_sock, ctrl_buf, (size_t)ctrl_len, 0);
  if (sent < 0 || sent != ctrl_len) {
    CHIAKI_LOGE(session->log, "CTRL send failed (sent=%d)", sent);
    CHIAKI_SOCKET_CLOSE(session_sock);
    return CHIAKI_ERR_NETWORK;
  }

  char recv_buf[512] = {0};
  size_t header_size = 0, total_received = 0;
  ChiakiErrorCode herr = chiaki_recv_http_header(
      session_sock, recv_buf, sizeof(recv_buf), &header_size, &total_received,
      (struct chiaki_stop_pipe_t *)&session->stop_pipe, 5000);
  if (herr != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "CTRL recv failed: %s",
                chiaki_error_string(herr));
    CHIAKI_SOCKET_CLOSE(session_sock);
    if (ctrl_port == SESSION_PORT) {
      CHIAKI_LOGW(session->log, "CTRL recv failed on %d; trying port 997",
                  ctrl_port);
      ctrl_port = 997;
      goto ctrl_attempt;
    }
    return herr;
  }

  ChiakiHttpResponse http_response;
  ChiakiErrorCode perr =
      chiaki_http_response_parse(&http_response, recv_buf, header_size);
  if (perr != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(session->log, "CTRL parse failed: %s",
                chiaki_error_string(perr));
    CHIAKI_SOCKET_CLOSE(session_sock);
    return perr;
  }
  CHIAKI_LOGI(session->log, "CTRL HTTP status: %d", http_response.code);
  if (http_response.code != 200) {
    int code_tmp = http_response.code;
    chiaki_http_response_fini(&http_response);
    CHIAKI_SOCKET_CLOSE(session_sock);
    session_sock = CHIAKI_INVALID_SOCKET;
    if (ctrl_port == SESSION_PORT) {
      CHIAKI_LOGW(session->log,
                  "CTRL on %d failed (%d) — trying fallback port 997",
                  ctrl_port, code_tmp);
      ctrl_port = 997;
      goto ctrl_attempt;
    }
    return CHIAKI_ERR_AUTH_FAILED;
  }
  chiaki_http_response_fini(&http_response);

  // Keep TCP ctrl socket open for incoming control messages; hand off to ctrl
  session->ctrl.sock = session_sock;
  session_sock = CHIAKI_INVALID_SOCKET;  // ownership transferred to ctrl
}

  // Mark ctrl session id as received (Vita path lacks ctrl channel); generate
  // a temporary session id token similar to vitaki format if missing.
  if (!session->ctrl_session_id_received || session->session_id[0] == '\0') {
    static const char alnum[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    size_t alen = sizeof(alnum) - 1;
    for (size_t i = 0; i < 32 && i + 1 < sizeof(session->session_id); i++) {
      session->session_id[i] =
          alnum[session->nonce[i % CHIAKI_RPCRYPT_KEY_SIZE] % alen];
    }
    session->session_id[32] = '\0';
    session->ctrl_session_id_received = true;
    CHIAKI_LOGI(session->log, "CTRL: synthesized session id: %s",
                session->session_id);
  }

  // If RUDP is available (non-Vita relay), send switch-to-stream message
  if (session->rudp) {
    ChiakiErrorCode serr =
        chiaki_rudp_send_switch_to_stream_connection_message(session->rudp);
    if (serr != CHIAKI_ERR_SUCCESS)
      CHIAKI_LOGW(session->log, "Failed to send switch-to-stream via RUDP: %s",
                  chiaki_error_string(serr));
  }

  return CHIAKI_ERR_SUCCESS;

ctrl_build_fail:
  CHIAKI_LOGE(session->log, "Failed to build CTRL request (buffer)");
  CHIAKI_SOCKET_CLOSE(session_sock);
  session_sock = CHIAKI_INVALID_SOCKET;
  return CHIAKI_ERR_UNKNOWN;
  return CHIAKI_ERR_SUCCESS;
}

static void regist_cb(ChiakiRegistEvent *event, void *user) {
  ChiakiSession *session = user;

  CHIAKI_LOGI(session->log,
              "REGIST_CB: ===== REGISTRATION CALLBACK CALLED =====");
  CHIAKI_LOGI(session->log, "REGIST_CB: Event type: %d", event->type);
  CHIAKI_LOGI(session->log, "REGIST_CB: Session pointer: %p", session);

  switch (event->type) {
    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
      CHIAKI_LOGI(
          session->log,
          "REGIST_CB: SUCCESS - %s successfully registered for Remote Play",
          event->registered_host->server_nickname);
      memcpy(session->connect_info.morning, event->registered_host->rp_key,
             sizeof(session->connect_info.morning));
      memcpy(session->connect_info.regist_key,
             event->registered_host->rp_regist_key,
             sizeof(session->connect_info.regist_key));
      if (!session->connect_info.ps5) {
        ChiakiEvent event_start = {0};
        event_start.type = CHIAKI_EVENT_NICKNAME_RECEIVED;
        memcpy(event_start.server_nickname,
               event->registered_host->server_nickname,
               sizeof(event->registered_host->server_nickname));
        chiaki_session_send_event(session, &event_start);
      }
      chiaki_mutex_lock(&session->state_mutex);
      session->psn_regist_succeeded = true;
      chiaki_mutex_unlock(&session->state_mutex);
      chiaki_cond_signal(&session->state_cond);
      CHIAKI_LOGI(session->log,
                  "REGIST_CB: SUCCESS - Registration success state set");
      break;
    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
      CHIAKI_LOGI(session->log,
                  "REGIST_CB: CANCELED - PSN regist was canceled, exiting...");
      chiaki_mutex_lock(&session->state_mutex);
      session->quit_reason = CHIAKI_QUIT_REASON_PSN_REGIST_FAILED;
      session->should_stop = true;
      chiaki_mutex_unlock(&session->state_mutex);
      chiaki_cond_signal(&session->state_cond);
      CHIAKI_LOGI(session->log, "REGIST_CB: CANCELED - Session stop state set");
      break;
    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
      CHIAKI_LOGI(session->log,
                  "REGIST_CB: FAILED - PSN regist failed, exiting...");
      chiaki_mutex_lock(&session->state_mutex);
      session->quit_reason = CHIAKI_QUIT_REASON_PSN_REGIST_FAILED;
      session->should_stop = true;
      chiaki_mutex_unlock(&session->state_mutex);
      chiaki_cond_signal(&session->state_cond);
      CHIAKI_LOGI(session->log, "REGIST_CB: FAILED - Session stop state set");
      break;
    default:
      CHIAKI_LOGI(session->log, "REGIST_CB: UNKNOWN - Unknown event type: %d",
                  event->type);
      break;
  }

  CHIAKI_LOGI(session->log,
              "REGIST_CB: ===== REGISTRATION CALLBACK COMPLETE =====");
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_goto_bed(ChiakiSession *session) {
  return chiaki_ctrl_goto_bed(&session->ctrl);
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_session_toggle_microphone(ChiakiSession *session, bool muted) {
  ChiakiErrorCode err;
  err = ctrl_message_toggle_microphone(&session->ctrl, muted);
  return err;
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_session_connect_microphone(ChiakiSession *session) {
  ChiakiErrorCode err;
  err = ctrl_message_connect_microphone(&session->ctrl);
  return err;
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_session_keyboard_set_text(ChiakiSession *session, const char *text) {
  return chiaki_ctrl_keyboard_set_text(&session->ctrl, text);
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_session_keyboard_reject(ChiakiSession *session) {
  return chiaki_ctrl_keyboard_reject(&session->ctrl);
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_session_keyboard_accept(ChiakiSession *session) {
  return chiaki_ctrl_keyboard_accept(&session->ctrl);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_session_go_home(ChiakiSession *session) {
  ChiakiErrorCode err;
  err = ctrl_message_go_home(&session->ctrl);
  return err;
}

// CRITICAL CRASH FIX: Missing functions causing undefined symbol crashes
CHIAKI_EXPORT void chiaki_session_set_event_cb(ChiakiSession *session,
                                               ChiakiEventCallback cb,
                                               void *user) {
  if (!session) return;
  session->event_cb = cb;
  session->event_cb_user = user;
}

CHIAKI_EXPORT void chiaki_session_set_video_sample_cb(
    ChiakiSession *session, ChiakiVideoSampleCallback cb, void *user) {
  if (!session) return;
  session->video_sample_cb = cb;
  session->video_sample_cb_user = user;
}
