// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 - Complete ChiakiStreamConnection Implementation
// Based on vitaki-fork streamconnection.c but adapted for PS Vita architecture

// Suppress compiler warning for protobuf struct initialization
// The auto-generated protobuf macros trigger pedantic warnings about field
// initialization
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "chiaki_streamconnection.h"

#include <assert.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <string.h>

#include "../network/takion.h"
#include "../utils/logger.h"
#include "chiaki_base64_vitaki.h"
#include "chiaki_ecdh_vitaki.h"
#include "chiaki_launchspec.h"
#include "chiaki_session.h"
#include "pb_utils.h"

#ifndef _WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Real protobuf implementation for PS5 Remote Play protocol
#include <nanopb/pb.h>
#include <nanopb/pb_decode.h>
#include <nanopb/pb_encode.h>

#include "../protobuf/takion.pb.h"
#include "chiaki_rpcrypt_vitaki.h"

#define STREAM_CONNECTION_PORT 9296
#define EXPECT_TIMEOUT_MS 5000
#define HEARTBEAT_INTERVAL_MS 1000

typedef enum {
  STATE_IDLE,
  STATE_TAKION_CONNECT,
  STATE_EXPECT_BANG,
  STATE_EXPECT_STREAMINFO
} StreamConnectionState;

// Takion callback implementations for stream connection
static void takion_state_callback(TakionState state, void *user_data);
static void takion_video_callback(const uint8_t *data, size_t size,
                                  void *user_data);
static void takion_audio_callback(const uint8_t *data, size_t size,
                                  void *user_data);

// Forward declarations for helper functions
static VitaRPS5Result send_big_message_vita(
    ChiakiStreamConnection *stream_connection);
static VitaRPS5Result send_streaminfo_ack_vita(
    ChiakiStreamConnection *stream_connection);
static VitaRPS5Result handle_bang_response_vita(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static VitaRPS5Result handle_streaminfo_response_vita(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);

// Callback system for receiving PS5 responses
static void stream_connection_takion_data_callback(void *user, int data_type,
                                                   uint8_t *buf,
                                                   size_t buf_size);

// =============================================================================================
// MAIN API IMPLEMENTATIONS
// =============================================================================================

VitaRPS5Result chiaki_stream_connection_init(
    ChiakiStreamConnection *stream_connection, ChiakiSession *session) {
  if (!stream_connection) {
    log_error("CRITICAL: stream_connection is NULL");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!session) {
    log_error("CRITICAL: session is NULL");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("StreamConnection initializing for PS Vita Remote Play...");

  // Initialize all fields properly
  memset(stream_connection, 0, sizeof(ChiakiStreamConnection));

  stream_connection->session = session;
  stream_connection->log = session->log;
  stream_connection->ecdh_secret = NULL;
  stream_connection->gkcrypt_remote = NULL;
  stream_connection->gkcrypt_local = NULL;
  stream_connection->video_receiver = NULL;
  stream_connection->audio_receiver = NULL;
  stream_connection->haptics_receiver = NULL;
  stream_connection->state = STATE_IDLE;
  stream_connection->state_finished = false;
  stream_connection->state_failed = false;
  stream_connection->should_stop = false;
  stream_connection->remote_disconnected = false;
  stream_connection->remote_disconnect_reason = NULL;
  stream_connection->feedback_sender_active = false;
  stream_connection->measured_bitrate = 0.0;
  // Create and initialize TakionConnection
  TakionConfig takion_config = {0};

  // CRITICAL FIX: Use the hostname field from connect_info for Takion
  // connection
  const char *target_ip = session->connect_info.hostname;
  if (!target_ip || strlen(target_ip) == 0) {
    log_error("No hostname specified in ChiakiSession connect_info");
    chiaki_stream_connection_fini(stream_connection);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  strncpy(takion_config.remote_ip, target_ip,
          sizeof(takion_config.remote_ip) - 1);
  log_info("✅ HOSTNAME FIX: Takion connection using IP: %s",
           takion_config.remote_ip);

  // CRITICAL FIX: Use distinct ports to avoid "cannot be the same" error
  takion_config.control_port = TAKION_CONTROL_PORT;      // 9295 - TCP control
  takion_config.stream_port = TAKION_STREAM_PORT_VIDEO;  // 9296 - UDP video

  log_info("✅ TAKION PORT FIX: control=%d, stream=%d (distinct ports)",
           takion_config.control_port, takion_config.stream_port);
  takion_config.console_version =
      session->target >= CHIAKI_TARGET_PS5_1 ? 12 : 7;
  takion_config.timeout_ms = VITARPS5_TIMEOUT_SECONDS * 1000;  // Global timeout
  takion_config.state_callback =
      takion_state_callback;  // Handle connection state changes
  takion_config.video_callback =
      takion_video_callback;  // Handle video data packets
  takion_config.audio_callback =
      takion_audio_callback;  // Handle audio data packets
  takion_config.user_data = stream_connection;

  // VITAKI-FORK APPROACH: Always create fresh Takion connection (clean
  // separation)
  VitaRPS5Result takion_result =
      takion_connection_create(&takion_config, &stream_connection->takion_conn);
  if (takion_result != VITARPS5_SUCCESS) {
    log_error("Failed to create TakionConnection: %s",
              vitarps5_result_string(takion_result));
    stream_connection->takion_conn = NULL;  // Ensure it's NULL on failure
  } else {
    log_info("TakionConnection created successfully");
  }

  // Initialize synchronization primitives for PS Vita
  ChiakiErrorCode err =
      chiaki_mutex_init(&stream_connection->state_mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to initialize state mutex: %d", err);
    return VITARPS5_ERROR_UNKNOWN;
  }

  err = chiaki_cond_init(&stream_connection->state_cond,
                         &stream_connection->state_mutex);
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to initialize state condition: %d", err);
    chiaki_mutex_fini(&stream_connection->state_mutex);
    return VITARPS5_ERROR_UNKNOWN;
  }

  err = chiaki_mutex_init(&stream_connection->feedback_sender_mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to initialize feedback sender mutex: %d", err);
    chiaki_cond_fini(&stream_connection->state_cond);
    chiaki_mutex_fini(&stream_connection->state_mutex);
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_info("StreamConnection initialized successfully (PS Vita optimized)");
  return VITARPS5_SUCCESS;
}

void chiaki_stream_connection_fini(ChiakiStreamConnection *stream_connection) {
  if (!stream_connection) {
    return;
  }

  log_info("StreamConnection finalizing resources for PS Vita...");

  // Free memory resources
  if (stream_connection->remote_disconnect_reason) {
    free(stream_connection->remote_disconnect_reason);
    stream_connection->remote_disconnect_reason = NULL;
  }

  if (stream_connection->ecdh_secret) {
    free(stream_connection->ecdh_secret);
    stream_connection->ecdh_secret = NULL;
  }

  // Clean up gkcrypt resources (stubs for now)
  stream_connection->gkcrypt_local = NULL;
  stream_connection->gkcrypt_remote = NULL;

  // Clean up receiver resources (stubs for now)
  stream_connection->video_receiver = NULL;
  stream_connection->audio_receiver = NULL;
  stream_connection->haptics_receiver = NULL;

  // Clean up feedback sender if active
  stream_connection->feedback_sender_active = false;

  // Clean up TakionConnection
  if (stream_connection->takion_conn) {
    log_info("Destroying TakionConnection");
    takion_connection_destroy(stream_connection->takion_conn);
    stream_connection->takion_conn = NULL;
  }

  // Clean up synchronization primitives
  chiaki_mutex_fini(&stream_connection->feedback_sender_mutex);
  chiaki_cond_fini(&stream_connection->state_cond);
  chiaki_mutex_fini(&stream_connection->state_mutex);

  log_info("StreamConnection finalized successfully");
}

VitaRPS5Result chiaki_stream_connection_run(
    ChiakiStreamConnection *stream_connection, chiaki_socket_t *socket,
    void *takion_connection) {
  if (!stream_connection || !stream_connection->session) {
    log_error("StreamConnection run: Invalid parameters");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (!takion_connection) {
    log_error("CRITICAL: Takion connection is NULL");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Store Takion connection for protocol communication
  stream_connection->takion_conn = takion_connection;

  ChiakiSession *session = stream_connection->session;
  log_info("StreamConnection starting PS5 Remote Play protocol sequence");
  log_info("Target: %s, Video: %dx%d@%dfps",
           chiaki_target_is_ps5(session->target) ? "PS5" : "PS4",
           session->connect_info.video_profile.width,
           session->connect_info.video_profile.height,
           session->connect_info.video_profile.max_fps);

  // Step 1: Establish Takion connection first
  log_info("PROTOCOL STEP 1: Establishing Takion connection to PS5");
  VitaRPS5Result result = takion_connect(stream_connection->takion_conn);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to establish Takion connection: %s",
              vitarps5_result_string(result));
    return result;
  }

  // Step 2: Wait for Takion handshake to complete
  log_info("PROTOCOL STEP 2: Waiting for Takion handshake completion...");
  int timeout_count = 0;
  while (takion_get_state(stream_connection->takion_conn) !=
             TAKION_STATE_CONNECTED &&
         takion_get_state(stream_connection->takion_conn) !=
             TAKION_STATE_ERROR) {
    sceKernelDelayThread(100000);  // 100ms
    timeout_count++;
    if (timeout_count > 100) {  // 10 second timeout
      log_error("CRITICAL: Timeout waiting for Takion handshake");
      return VITARPS5_ERROR_TIMEOUT;
    }
  }

  if (takion_get_state(stream_connection->takion_conn) == TAKION_STATE_ERROR) {
    log_error("CRITICAL: Takion handshake failed");
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("CRITICAL: ✅ Takion handshake completed successfully!");

  // Step 3: Register callback to receive PS5 responses (BANG, STREAMINFO)
  result = takion_set_data_callback(stream_connection->takion_conn,
                                    stream_connection_takion_data_callback,
                                    stream_connection);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to register Takion data callback: %s",
              vitarps5_result_string(result));
    return result;
  }
  log_info("Callback system registered for PS5 responses");

  // Step 4: Send BIG message (LaunchSpec)
  log_info("PROTOCOL STEP 3: Sending BIG message with LaunchSpec");
  result = send_big_message_vita(stream_connection);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to send BIG message: %s", vitarps5_result_string(result));
    return result;
  }

  // Step 5: Set expectation state for BANG response (callback-driven)
  log_info("PROTOCOL STEP 4: Ready to receive BANG response via callback...");
  stream_connection->state = STATE_EXPECT_BANG;

  // Protocol is now callback-driven - PS5 will send BANG→STREAMINFO
  // The callback system handles the responses and advances the protocol state
  log_info("BIG message sent, callback system will handle PS5 responses");
  log_info("Callback system active: BANG → STREAMINFO → Remote Play start");
  // Wait for BANG then STREAMINFO with reasonable overall timeout (e.g., 20s)
  // Use system time since process-time may not be available on Vita SDK
  uint64_t start_us = sceKernelGetSystemTimeWide();
  const uint64_t overall_timeout_us = 20000000ULL;  // 20 seconds

  ChiakiErrorCode lock_err = chiaki_mutex_lock(&stream_connection->state_mutex);
  if (lock_err != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to lock state mutex while waiting for responses: %d",
              lock_err);
    return VITARPS5_ERROR_UNKNOWN;
  }

  while (!stream_connection->state_failed &&
         !stream_connection->state_finished &&
         !stream_connection->should_stop) {
    // Use the same time source as start_us to avoid clock skew
    uint64_t now_us = sceKernelGetSystemTimeWide();
    if (now_us - start_us > overall_timeout_us) {
      log_error("Protocol timed out waiting for BANG/STREAMINFO responses");
      chiaki_mutex_unlock(&stream_connection->state_mutex);
      return VITARPS5_ERROR_TIMEOUT;
    }
    chiaki_cond_timedwait(&stream_connection->state_cond,
                          &stream_connection->state_mutex, 500);
  }

  bool failed = stream_connection->state_failed;
  chiaki_mutex_unlock(&stream_connection->state_mutex);
  if (failed) {
    log_error("Protocol failed while waiting for responses");
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("Protocol BANG/STREAMINFO completed");
  return VITARPS5_SUCCESS;
}

VitaRPS5Result chiaki_stream_connection_stop(
    ChiakiStreamConnection *stream_connection) {
  if (!stream_connection) {
    log_error("StreamConnection stop: Invalid stream_connection pointer");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("StreamConnection stopping PS Vita Remote Play session...");

  // Set stop flag and signal condition
  ChiakiErrorCode err = chiaki_mutex_lock(&stream_connection->state_mutex);
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to lock state mutex during stop: %d", err);
    return VITARPS5_ERROR_UNKNOWN;
  }

  stream_connection->should_stop = true;

  ChiakiErrorCode unlock_err =
      chiaki_mutex_unlock(&stream_connection->state_mutex);
  ChiakiErrorCode signal_err =
      chiaki_cond_signal(&stream_connection->state_cond);

  if (unlock_err != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to unlock state mutex during stop: %d", unlock_err);
    return VITARPS5_ERROR_UNKNOWN;
  }

  if (signal_err != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to signal state condition during stop: %d", signal_err);
    return VITARPS5_ERROR_UNKNOWN;
  }

  // Stop feedback sender if active
  if (stream_connection->feedback_sender_active) {
    err = chiaki_mutex_lock(&stream_connection->feedback_sender_mutex);
    if (err == CHIAKI_ERR_SUCCESS) {
      stream_connection->feedback_sender_active = false;
      chiaki_mutex_unlock(&stream_connection->feedback_sender_mutex);
      log_info("Feedback sender stopped");
    }
  }

  // Stop Takion connection if available
  if (stream_connection->takion_conn) {
    VitaRPS5Result result = takion_disconnect(stream_connection->takion_conn);
    if (result != VITARPS5_SUCCESS) {
      log_warning("Failed to disconnect Takion connection: %s",
                  vitarps5_result_string(result));
    } else {
      log_info("Takion connection stopped");
    }
  }

  log_info("StreamConnection stopped successfully");
  return VITARPS5_SUCCESS;
}

// =============================================================================================
// HELPER FUNCTIONS - PS VITA OPTIMIZED
// =============================================================================================

static VitaRPS5Result send_big_message_vita(
    ChiakiStreamConnection *stream_connection) {
  if (!stream_connection || !stream_connection->session) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  ChiakiSession *session = stream_connection->session;

  // Create LaunchSpec for BIG message (PS Vita optimized)
  ChiakiLaunchSpec launch_spec = {0};
  launch_spec.target = session->target;
  launch_spec.width = session->connect_info.video_profile.width;
  launch_spec.height = session->connect_info.video_profile.height;
  launch_spec.max_fps = session->connect_info.video_profile.max_fps;
  launch_spec.codec = session->connect_info.video_profile.codec;
  launch_spec.bw_kbps_sent = session->connect_info.video_profile.bitrate;
  launch_spec.mtu = 1454;  // Standard MTU for PS Remote Play
  launch_spec.rtt = 10;    // Default RTT estimate

  // Initialize handshake key (16 bytes) - using session key
  launch_spec.handshake_key = session->handshake_key;

  // Format LaunchSpec as JSON
  char launch_spec_json[2048];
  int json_size = chiaki_launchspec_format(
      launch_spec_json, sizeof(launch_spec_json), &launch_spec);
  if (json_size < 0) {
    log_error("Failed to format LaunchSpec JSON");
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_debug("LaunchSpec JSON (%d bytes): %s", json_size, launch_spec_json);

  // Encrypt LaunchSpec using Chiaki RPCrypt (vitaki-fork compatible)
  uint8_t *launch_spec_json_enc = malloc(json_size);
  if (!launch_spec_json_enc) {
    log_error("Failed to allocate encryption buffer");
    return VITARPS5_ERROR_MEMORY;
  }
  ChiakiErrorCode enc_err = chiaki_rpcrypt_encrypt(
      &session->rpcrypt, 0, (const uint8_t *)launch_spec_json,
      launch_spec_json_enc, (size_t)json_size);
  if (enc_err != CHIAKI_ERR_SUCCESS) {
    log_error("LaunchSpec encryption failed: %d", enc_err);
    free(launch_spec_json_enc);
    return VITARPS5_ERROR_UNKNOWN;
  }

  // Base64 encode
  char launch_spec_b64[4096];
  ChiakiErrorCode err =
      chiaki_base64_encode(launch_spec_json_enc, (size_t)json_size,
                           launch_spec_b64, sizeof(launch_spec_b64));
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("LaunchSpec base64 encoding failed: %d", err);
    free(launch_spec_json_enc);
    return VITARPS5_ERROR_UNKNOWN;
  }

  free(launch_spec_json_enc);

  log_info("LaunchSpec encrypted successfully (%zu bytes base64)",
           strlen(launch_spec_b64));

  // Create protobuf BIG message for PS5
  tkproto_TakionMessage msg;
  memset(&msg, 0, sizeof(msg));

  msg.type = tkproto_TakionMessage_PayloadType_BIG;
  msg.has_big_payload = true;

  // Fill client version
  msg.big_payload.client_version =
      session->target == CHIAKI_TARGET_PS5_1 ? 12 : 9;

  // Use real session ID from session structure
  strncpy(msg.big_payload.session_key, session->session_id,
          sizeof(msg.big_payload.session_key) - 1);
  msg.big_payload.session_key[sizeof(msg.big_payload.session_key) - 1] = '\0';

  // Use encrypted LaunchSpec
  strncpy(msg.big_payload.launch_spec, launch_spec_b64,
          sizeof(msg.big_payload.launch_spec) - 1);
  msg.big_payload.launch_spec[sizeof(msg.big_payload.launch_spec) - 1] = '\0';

  // Create zero encrypted key for PS5 compatibility (some firmwares ignore it)
  pb_bytes_array_t *encrypted_key = malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(4));
  if (!encrypted_key) {
    log_error("Failed to allocate encrypted key buffer");
    return VITARPS5_ERROR_MEMORY;
  }
  encrypted_key->size = 4;
  memset(encrypted_key->bytes, 0, 4);
  msg.big_payload.encrypted_key = encrypted_key;

  // ECDH: obtain real public key and signature using handshake_key
  msg.big_payload.has_ecdh_pub_key = true;
  uint8_t ecdh_pub_key[128];
  size_t ecdh_pub_key_len = sizeof(ecdh_pub_key);
  uint8_t ecdh_sig[64];
  size_t ecdh_sig_len = sizeof(ecdh_sig);
  ChiakiECDH ecdh_ctx;
  if (chiaki_ecdh_init(&ecdh_ctx) != CHIAKI_ERR_SUCCESS) {
    free(encrypted_key);
    log_error("Failed to initialize ECDH context");
    return VITARPS5_ERROR_UNKNOWN;
  }
  ChiakiErrorCode ecdh_err = chiaki_ecdh_get_local_pub_key(
      &ecdh_ctx, ecdh_pub_key, &ecdh_pub_key_len, session->handshake_key,
      ecdh_sig, &ecdh_sig_len);
  chiaki_ecdh_fini(&ecdh_ctx);
  if (ecdh_err != CHIAKI_ERR_SUCCESS) {
    free(encrypted_key);
    log_error("Failed to get ECDH key and signature: %d", ecdh_err);
    return VITARPS5_ERROR_UNKNOWN;
  }
  pb_bytes_array_t *ecdh_pub_key_array =
      malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(ecdh_pub_key_len));
  if (!ecdh_pub_key_array) {
    free(encrypted_key);
    log_error("Failed to allocate ECDH pub key buffer");
    return VITARPS5_ERROR_MEMORY;
  }
  ecdh_pub_key_array->size = ecdh_pub_key_len;
  memcpy(ecdh_pub_key_array->bytes, ecdh_pub_key, ecdh_pub_key_len);
  msg.big_payload.ecdh_pub_key = ecdh_pub_key_array;

  msg.big_payload.has_ecdh_sig = true;
  pb_bytes_array_t *ecdh_sig_array =
      malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(ecdh_sig_len));
  if (!ecdh_sig_array) {
    free(encrypted_key);
    free(ecdh_pub_key_array);
    log_error("Failed to allocate ECDH sig buffer");
    return VITARPS5_ERROR_MEMORY;
  }
  ecdh_sig_array->size = ecdh_sig_len;
  memcpy(ecdh_sig_array->bytes, ecdh_sig, ecdh_sig_len);
  msg.big_payload.ecdh_sig = ecdh_sig_array;

  // Encode using nanopb
  uint8_t big_packet[2048];
  pb_ostream_t stream = pb_ostream_from_buffer(big_packet, sizeof(big_packet));
  bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);

  // Clean up allocated memory
  free(encrypted_key);
  free(ecdh_pub_key_array);
  free(ecdh_sig_array);

  if (!pbr) {
    log_error("Failed to encode protobuf BIG message: %s",
              PB_GET_ERROR(&stream));
    return VITARPS5_ERROR_UNKNOWN;
  }

  size_t packet_size = stream.bytes_written;
  log_info("Encoded BIG message: %zu bytes", packet_size);

  // Send BIG as Takion DATA chunk over stream socket
  if (stream_connection->takion_conn) {
    log_info("Sending protobuf BIG message to PS5 (%zu bytes)", packet_size);
    VitaRPS5Result result = takion_send_data_chunk(
        stream_connection->takion_conn, big_packet, packet_size);
    if (result != VITARPS5_SUCCESS) {
      log_error("Failed to send protobuf BIG message: %s",
                vitarps5_result_string(result));
      return result;
    }
    log_info("Protobuf BIG message sent successfully (%zu bytes)", packet_size);
  } else {
    log_error("No Takion connection available for sending BIG message");
    return VITARPS5_ERROR_NOT_CONNECTED;
  }

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result send_streaminfo_ack_vita(
    ChiakiStreamConnection *stream_connection) {
  log_info("Sending STREAMINFO ACK response to PS5...");

  if (!stream_connection->takion_conn) {
    log_error("No Takion connection available for STREAMINFO ACK");
    return VITARPS5_ERROR_NOT_CONNECTED;
  }

  // Create protobuf STREAMINFO ACK message (empty payload)
  tkproto_TakionMessage ack_msg;
  memset(&ack_msg, 0, sizeof(ack_msg));
  ack_msg.type = tkproto_TakionMessage_PayloadType_STREAMINFOACK;

  // Encode the message using nanopb
  uint8_t ack_buffer[128];
  size_t ack_size;

  VitaRPS5Result encode_result = encode_takion_message(
      &ack_msg, ack_buffer, sizeof(ack_buffer), &ack_size);
  if (encode_result != VITARPS5_SUCCESS) {
    log_error("Failed to encode STREAMINFO ACK message: %s",
              vitarps5_result_string(encode_result));
    return encode_result;
  }

  log_info("STREAMINFO ACK encoded successfully (%zu bytes)", ack_size);

  // Send via Takion connection
  VitaRPS5Result result = takion_send_data_chunk(stream_connection->takion_conn,
                                                 ack_buffer, ack_size);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to send STREAMINFO ACK packet: %s",
              vitarps5_result_string(result));
    return result;
  }

  log_info("STREAMINFO ACK sent successfully via Takion (%zu bytes)", ack_size);
  return VITARPS5_SUCCESS;
}

// =============================================================================================
// CALLBACK SYSTEM - PS5 RESPONSE HANDLING
// =============================================================================================

static void stream_connection_takion_data_callback(void *user, int data_type,
                                                   uint8_t *buf,
                                                   size_t buf_size) {
  ChiakiStreamConnection *stream_connection = (ChiakiStreamConnection *)user;

  if (!stream_connection) {
    log_error("Invalid stream connection in callback");
    return;
  }

  log_info("Received PS5 data (type=%d, size=%zu, state=%d)", data_type,
           buf_size, stream_connection->state);

  // Handle video data regardless of protocol state (after handshake)
  if (data_type == 1 && stream_connection->state_finished) {
    // This is video data and handshake is complete
    log_info("CRITICAL: Video data received from PS5 (%zu bytes)", buf_size);

    // Forward to ChiakiSession video callback
    if (stream_connection->session &&
        stream_connection->session->video_sample_cb) {
      log_info("CRITICAL: Forwarding video data to ChiakiSession callback");
      bool consumed = stream_connection->session->video_sample_cb(
          buf, buf_size,
          0,      // frames_lost (TODO: implement proper loss detection)
          false,  // frame_recovered
          stream_connection->session->video_sample_cb_user);

      if (consumed) {
        log_debug("Video sample consumed by ChiakiSession callback");
      } else {
        log_warning("Video sample rejected by ChiakiSession callback");
      }
    } else {
      log_error("CRITICAL: No video callback registered in ChiakiSession");
    }
    return;
  }

  // Handle protobuf protocol messages (type 0)
  if (data_type == 0) {
    // Route to appropriate handler based on current protocol state
    switch (stream_connection->state) {
      case STATE_EXPECT_BANG:
        log_info("Processing BANG response...");
        handle_bang_response_vita(stream_connection, buf, buf_size);
        break;

      case STATE_EXPECT_STREAMINFO:
        log_info("Processing STREAMINFO response...");
        handle_streaminfo_response_vita(stream_connection, buf, buf_size);
        break;

      default:
        log_debug("Ignoring protobuf data in state %d",
                  stream_connection->state);
        break;
    }
  } else {
    log_debug("Ignoring data type %d in state %d", data_type,
              stream_connection->state);
  }
}

static VitaRPS5Result handle_bang_response_vita(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size) {
  log_info("BANG response received! (%zu bytes)", buf_size);

  // Decode the protobuf BANG message using nanopb
  tkproto_TakionMessage msg;
  memset(&msg, 0, sizeof(msg));

  // Setup buffers for ECDH data
  uint8_t ecdh_pub_key_buffer[128];
  uint8_t ecdh_sig_buffer[64];
  pb_bytes_array_t ecdh_pub_key = {0};
  pb_bytes_array_t ecdh_sig = {0};

  ecdh_pub_key.size = 0;
  ecdh_sig.size = 0;
  msg.bang_payload.ecdh_pub_key = &ecdh_pub_key;
  msg.bang_payload.ecdh_sig = &ecdh_sig;

  pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);

  if (!pb_decode(&stream, tkproto_TakionMessage_fields, &msg)) {
    log_error("Failed to decode BANG protobuf message: %s",
              PB_GET_ERROR(&stream));
    stream_connection->state_failed = true;
    return VITARPS5_ERROR_UNKNOWN;
  }

  // Validate message type
  if (msg.type != tkproto_TakionMessage_PayloadType_BANG ||
      !msg.has_bang_payload) {
    log_error("Expected BANG payload but received type: %d", msg.type);
    stream_connection->state_failed = true;
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_info("BANG message decoded - server_version=%d, token=%d",
           msg.bang_payload.server_version, msg.bang_payload.token);

  // Validate BANG response fields
  if (!msg.bang_payload.version_accepted) {
    log_error("PS5 rejected client version");
    stream_connection->state_failed = true;
    return VITARPS5_ERROR_UNKNOWN;
  }

  if (!msg.bang_payload.encrypted_key_accepted) {
    log_error("PS5 rejected encrypted key");
    stream_connection->state_failed = true;
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_info("PS5 accepted version and encrypted key ✅");

  // Process ECDH key exchange if available
  if (msg.bang_payload.has_ecdh_pub_key && ecdh_pub_key.size > 0) {
    log_info("Processing ECDH public key (%d bytes)", ecdh_pub_key.size);

    if (ecdh_pub_key.size <= sizeof(ecdh_pub_key_buffer)) {
      memcpy(ecdh_pub_key_buffer, ecdh_pub_key.bytes, ecdh_pub_key.size);
      log_info("ECDH key exchange ready (derivation deferred to Phase 3)");
    } else {
      log_error("ECDH public key too large: %d bytes", ecdh_pub_key.size);
    }
  } else {
    log_info("No ECDH public key in BANG response");
  }

  // Store session key from BANG response
  if (strlen(msg.bang_payload.session_key) > 0) {
    log_info("Session key received: %.16s...", msg.bang_payload.session_key);
  }

  log_info("BANG processed successfully - advancing to STREAMINFO state");
  stream_connection->state = STATE_EXPECT_STREAMINFO;

  log_info("Ready for STREAMINFO message from PS5");
  chiaki_cond_signal(&stream_connection->state_cond);
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result handle_streaminfo_response_vita(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size) {
  log_info("STREAMINFO response received! (%zu bytes)", buf_size);

  // Decode the protobuf STREAMINFO message using nanopb
  tkproto_TakionMessage msg;
  memset(&msg, 0, sizeof(msg));

  // Setup buffer for audio header
  uint8_t audio_header_buffer[64];
  pb_bytes_array_t audio_header = {0};
  audio_header.size = 0;
  msg.stream_info_payload.audio_header = &audio_header;

  pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);

  if (!pb_decode(&stream, tkproto_TakionMessage_fields, &msg)) {
    log_error("Failed to decode STREAMINFO protobuf message: %s",
              PB_GET_ERROR(&stream));
    stream_connection->state_failed = true;
    return VITARPS5_ERROR_UNKNOWN;
  }

  // Validate message type
  if (msg.type != tkproto_TakionMessage_PayloadType_STREAMINFO ||
      !msg.has_stream_info_payload) {
    log_error("Expected STREAMINFO payload but received type: %d", msg.type);
    stream_connection->state_failed = true;
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_info("STREAMINFO message decoded successfully");

  // Process audio header
  if (audio_header.size > 0) {
    log_info("Audio header received (%d bytes)", audio_header.size);
    if (audio_header.size <= sizeof(audio_header_buffer)) {
      memcpy(audio_header_buffer, audio_header.bytes, audio_header.size);
      log_info("Audio header processed (audio receiver init deferred)");
    }
  } else {
    log_warning("No audio header in STREAMINFO");
  }

  // Process optional timeout settings
  if (msg.stream_info_payload.has_start_timeout) {
    log_info("Start timeout: %d ms", msg.stream_info_payload.start_timeout);
  }
  if (msg.stream_info_payload.has_afk_timeout) {
    log_info("AFK timeout: %d ms", msg.stream_info_payload.afk_timeout);
  }

  // Mark protocol stage complete
  chiaki_mutex_lock(&stream_connection->state_mutex);
  stream_connection->state_finished = true;
  chiaki_cond_signal(&stream_connection->state_cond);
  chiaki_mutex_unlock(&stream_connection->state_mutex);

  // Send STREAMINFO ACK response to PS5
  VitaRPS5Result ack_result = send_streaminfo_ack_vita(stream_connection);
  if (ack_result != VITARPS5_SUCCESS) {
    log_error("Failed to send STREAMINFO ACK: %s",
              vitarps5_result_string(ack_result));
    stream_connection->state_failed = true;
    return ack_result;
  }

  log_info("STREAMINFO ACK sent successfully");

  // Protocol complete - ready for streaming
  stream_connection->state = STATE_IDLE;
  stream_connection->state_finished = true;

  log_info(
      "SUCCESS: Remote Play protocol sequence complete via protobuf "
      "callbacks!");
  log_info(
      "PS5 codec negotiation finished - ready to receive video/audio streams");

  return VITARPS5_SUCCESS;
}

// =============================================================================================
// ADDITIONAL UTILITY FUNCTIONS
// =============================================================================================

VitaRPS5Result stream_connection_send_heartbeat(
    ChiakiStreamConnection *stream_connection) {
  if (!stream_connection || !stream_connection->takion_conn) {
    log_error("No stream connection or Takion connection for heartbeat");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create protobuf HEARTBEAT message (empty payload)
  tkproto_TakionMessage heartbeat_msg;
  memset(&heartbeat_msg, 0, sizeof(heartbeat_msg));
  heartbeat_msg.type = tkproto_TakionMessage_PayloadType_HEARTBEAT;

  // Encode the message using nanopb
  uint8_t heartbeat_buffer[64];
  size_t heartbeat_size;

  VitaRPS5Result encode_result =
      encode_takion_message(&heartbeat_msg, heartbeat_buffer,
                            sizeof(heartbeat_buffer), &heartbeat_size);
  if (encode_result != VITARPS5_SUCCESS) {
    log_error("Failed to encode HEARTBEAT message: %s",
              vitarps5_result_string(encode_result));
    return encode_result;
  }

  // Send via Takion connection
  VitaRPS5Result result = takion_send_packet(stream_connection->takion_conn,
                                             heartbeat_buffer, heartbeat_size);
  if (result != VITARPS5_SUCCESS) {
    log_debug("Failed to send HEARTBEAT packet: %s",
              vitarps5_result_string(result));
    return result;
  }

  log_debug("HEARTBEAT sent successfully (%zu bytes)", heartbeat_size);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result stream_connection_send_corrupt_frame(
    ChiakiStreamConnection *stream_connection, ChiakiSeqNum16 start,
    ChiakiSeqNum16 end) {
  if (!stream_connection || !stream_connection->takion_conn) {
    log_error("No stream connection or Takion connection for corrupt frame");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // TEMPORARY FIX: Create minimal CORRUPTFRAME message without payload
  // NOTE: Current protobuf definition is missing tkproto_CorruptFramePayload
  // struct and corresponding fields in tkproto_TakionMessage. This is a known
  // limitation.
  tkproto_TakionMessage msg;
  memset(&msg, 0, sizeof(msg));
  msg.type = tkproto_TakionMessage_PayloadType_CORRUPTFRAME;

  // TODO: When protobuf definition is updated, add:
  // msg.has_corrupt_frame_payload = true;
  // msg.corrupt_frame_payload.start = start;
  // msg.corrupt_frame_payload.end = end;

  // Encode the message using nanopb
  uint8_t corrupt_buffer[64];
  size_t corrupt_size;

  VitaRPS5Result encode_result = encode_takion_message(
      &msg, corrupt_buffer, sizeof(corrupt_buffer), &corrupt_size);
  if (encode_result != VITARPS5_SUCCESS) {
    log_error("Failed to encode CORRUPTFRAME message: %s",
              vitarps5_result_string(encode_result));
    return encode_result;
  }

  // Send via Takion connection
  VitaRPS5Result result = takion_send_packet(stream_connection->takion_conn,
                                             corrupt_buffer, corrupt_size);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to send CORRUPTFRAME packet: %s",
              vitarps5_result_string(result));
    return result;
  }

  log_info(
      "CORRUPTFRAME sent successfully (frames %d to %d, %zu bytes) - payload "
      "stub",
      start, end, corrupt_size);
  log_warning(
      "CORRUPTFRAME payload not implemented - protobuf definition incomplete");
  return VITARPS5_SUCCESS;
}

VitaRPS5Result stream_connection_send_toggle_mute_direct_message(
    ChiakiStreamConnection *stream_connection, bool muted) {
  log_info("Toggle mute message (muted=%s) - stub implementation for PS Vita",
           muted ? "true" : "false");
  return VITARPS5_SUCCESS;
}

// =============================================================================
// Takion Callback Implementations
// =============================================================================

/**
 * Handle Takion connection state changes
 */
static void takion_state_callback(TakionState state, void *user_data) {
  ChiakiStreamConnection *stream_connection =
      (ChiakiStreamConnection *)user_data;
  if (!stream_connection) {
    log_error("takion_state_callback: NULL stream_connection");
    return;
  }

  log_info("Takion state changed: %s", takion_state_string(state));

  // Update stream connection state based on Takion state
  chiaki_mutex_lock(&stream_connection->state_mutex);

  switch (state) {
    case TAKION_STATE_CONNECTED:
      log_info("✅ Takion connection established - ready for streaming");
      stream_connection->state = STATE_TAKION_CONNECT;
      stream_connection->state_finished = false;
      stream_connection->state_failed = false;
      break;

    case TAKION_STATE_DISCONNECTING:
    case TAKION_STATE_IDLE:
      log_info("Takion connection disconnected");
      stream_connection->remote_disconnected = true;
      stream_connection->state_finished = true;
      break;

    case TAKION_STATE_ERROR:
      log_error("Takion connection error");
      stream_connection->state_failed = true;
      stream_connection->state_finished = true;
      break;

    default:
      log_debug("Takion state transition: %s", takion_state_string(state));
      break;
  }

  // Signal state change to waiting threads
  chiaki_cond_signal(&stream_connection->state_cond);
  chiaki_mutex_unlock(&stream_connection->state_mutex);
}

/**
 * Handle incoming video data from PS5
 */
static void takion_video_callback(const uint8_t *data, size_t size,
                                  void *user_data) {
  ChiakiStreamConnection *stream_connection =
      (ChiakiStreamConnection *)user_data;
  if (!stream_connection || !data || size == 0) {
    return;
  }

  log_debug("Received video packet from PS5: %zu bytes", size);

  // Forward video data to ChiakiSession for processing via video sample
  // callback
  if (stream_connection->session &&
      stream_connection->session->video_sample_cb) {
    // Call ChiakiSession video sample callback to process video
    bool processed = stream_connection->session->video_sample_cb(
        (uint8_t *)data, size, 0, false,
        stream_connection->session->video_sample_cb_user);

    if (!processed) {
      log_warn("ChiakiSession video callback rejected packet");
    }
  } else {
    log_warn("No ChiakiSession video callback - video packet dropped");
  }
}

/**
 * Handle incoming audio data from PS5
 */
static void takion_audio_callback(const uint8_t *data, size_t size,
                                  void *user_data) {
  ChiakiStreamConnection *stream_connection =
      (ChiakiStreamConnection *)user_data;
  if (!stream_connection || !data || size == 0) {
    return;
  }

  log_debug("Received audio packet from PS5: %zu bytes", size);

  // For now, just log audio packets since ChiakiSession doesn't have an audio
  // callback In a full implementation, this would be forwarded to an audio
  // decoder/sink
  log_debug("Audio packet processed (audio decoding not yet implemented)");
}

// Restore original compiler warning behavior
#pragma GCC diagnostic pop
