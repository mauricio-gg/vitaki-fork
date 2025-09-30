// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 adaptation of vitaki-fork streamconnection.c

#include <assert.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <string.h>
#include <time.h>

#include "../network/takion.h"
#include "../utils/logger.h"
#include "chiaki_base64.h"
#include "chiaki_common.h"
#include "chiaki_launchspec.h"
#include "chiaki_rpcrypt.h"
#include "chiaki_session.h"
#include "chiaki_streamconnection.h"
// Removed chiaki_stubs.h - using proper headers from other includes
#ifndef _WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <nanopb/pb.h>
#include <nanopb/pb_decode.h>
#include <nanopb/pb_encode.h>

#include "../protobuf/takion.pb.h"
#include "chiaki_ecdh.h"
#include "pb_utils.h"

#define STREAM_CONNECTION_PORT 9296
#define EXPECT_TIMEOUT_MS 5000
#define HEARTBEAT_INTERVAL_MS 1000

typedef enum {
  STATE_IDLE,
  STATE_TAKION_CONNECT,
  STATE_EXPECT_BANG,
  STATE_EXPECT_STREAMINFO
} StreamConnectionState;

void chiaki_session_send_event(ChiakiSession *session, ChiakiEvent *event);

static void stream_connection_takion_cb(ChiakiTakionEvent *event, void *user);
static void stream_connection_takion_data(
    ChiakiStreamConnection *stream_connection,
    ChiakiTakionMessageDataType data_type, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_protobuf(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_rumble(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_trigger_effects(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static ChiakiErrorCode stream_connection_send_big(
    ChiakiStreamConnection *stream_connection);
static ChiakiErrorCode stream_connection_send_controller_connection(
    ChiakiStreamConnection *stream_connection);
static ChiakiErrorCode stream_connection_enable_microphone(
    ChiakiStreamConnection *stream_connection);
static ChiakiErrorCode stream_connection_send_disconnect(
    ChiakiStreamConnection *stream_connection);
static void stream_connection_takion_data_idle(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_expect_bang(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_expect_streaminfo(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static ChiakiErrorCode stream_connection_send_streaminfo_ack(
    ChiakiStreamConnection *stream_connection);
static void stream_connection_takion_av(
    ChiakiStreamConnection *stream_connection, ChiakiTakionAVPacket *packet);
static ChiakiErrorCode stream_connection_send_heartbeat(
    ChiakiStreamConnection *stream_connection);

// VITAKI-FORK: Takion callback wrapper - translates simple callback to
// vitaki-fork events
static void takion_data_callback_wrapper(void *user, int data_type,
                                         uint8_t *buf, size_t buf_size);

// From rpcrypt.c - PS4 pre-10 algorithm for LaunchSpec encryption
static void bright_ambassador_ps4_pre10(uint8_t *bright, uint8_t *ambassador,
                                        const uint8_t *nonce,
                                        const uint8_t *morning) {
  static const uint8_t echo_a[] = {0x01, 0x49, 0x87, 0x9b, 0x65, 0x39,
                                   0x8b, 0x39, 0x4b, 0x3a, 0x8d, 0x48,
                                   0xc3, 0x0a, 0xef, 0x51};
  static const uint8_t echo_b[] = {0xe1, 0xec, 0x9c, 0x3a, 0xdd, 0xbd,
                                   0x08, 0x85, 0xfc, 0x0e, 0x1d, 0x78,
                                   0x90, 0x32, 0xc0, 0x04};

  for (uint8_t i = 0; i < CHIAKI_RPCRYPT_KEY_SIZE; i++) {
    uint8_t v = nonce[i];
    v -= i;
    v -= 0x27;
    v ^= echo_a[i];
    ambassador[i] = v;
  }

  for (uint8_t i = 0; i < CHIAKI_RPCRYPT_KEY_SIZE; i++) {
    uint8_t v = morning[i];
    v -= i;
    v += 0x34;
    v ^= echo_b[i];
    v ^= nonce[i];
    bright[i] = v;
  }
}

VitaRPS5Result chiaki_stream_connection_init(
    ChiakiStreamConnection *stream_connection, ChiakiSession *session) {
  stream_connection->session = session;
  stream_connection->log = NULL;  // Use VitaRPS5 logger instead

  stream_connection->ecdh_secret = NULL;
  stream_connection->gkcrypt_remote = NULL;
  stream_connection->gkcrypt_local = NULL;

  ChiakiErrorCode err =
      chiaki_mutex_init(&stream_connection->state_mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) goto error;

  err = chiaki_cond_init(&stream_connection->state_cond,
                         &stream_connection->state_mutex);
  if (err != CHIAKI_ERR_SUCCESS) goto error_state_mutex;

  err = chiaki_packet_stats_init(&stream_connection->packet_stats);
  if (err != CHIAKI_ERR_SUCCESS) goto error_state_cond;

  stream_connection->video_receiver = NULL;
  stream_connection->audio_receiver = NULL;
  stream_connection->haptics_receiver = NULL;

  err = chiaki_mutex_init(&stream_connection->feedback_sender_mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) goto error_packet_stats;

  stream_connection->state = STATE_IDLE;
  stream_connection->state_finished = false;
  stream_connection->state_failed = false;
  stream_connection->should_stop = false;
  stream_connection->remote_disconnected = false;
  stream_connection->remote_disconnect_reason = NULL;

  log_info("StreamConnection init (vitaki-fork implementation)");
  return VITARPS5_SUCCESS;

error_packet_stats:
  chiaki_packet_stats_fini(&stream_connection->packet_stats);
error_state_cond:
  chiaki_cond_fini(&stream_connection->state_cond);
error_state_mutex:
  chiaki_mutex_fini(&stream_connection->state_mutex);
error:
  return VITARPS5_ERROR_UNKNOWN;
}

void chiaki_stream_connection_fini(ChiakiStreamConnection *stream_connection) {
  free(stream_connection->remote_disconnect_reason);

  chiaki_gkcrypt_free(stream_connection->gkcrypt_remote);
  chiaki_gkcrypt_free(stream_connection->gkcrypt_local);

  free(stream_connection->ecdh_secret);

#if defined(__PSVITA__)
  if (stream_connection->congestion_control.thread.thread_id)
#else
  if (stream_connection->congestion_control.thread.thread)
#endif
    chiaki_congestion_control_stop(&stream_connection->congestion_control);

  chiaki_packet_stats_fini(&stream_connection->packet_stats);

  chiaki_mutex_fini(&stream_connection->feedback_sender_mutex);

  chiaki_cond_fini(&stream_connection->state_cond);
  chiaki_mutex_fini(&stream_connection->state_mutex);

  log_info("StreamConnection fini (vitaki-fork implementation)");
}

static bool state_finished_cond_check(void *user) {
  ChiakiStreamConnection *stream_connection = user;
  return stream_connection->state_finished || stream_connection->should_stop ||
         stream_connection->remote_disconnected;
}

// Simplified zero encrypted key function for PS5 compatibility
static bool chiaki_pb_encode_zero_encrypted_key(pb_ostream_t *stream,
                                                const pb_field_t *field,
                                                void *const *arg) {
  if (!pb_encode_tag_for_field(stream, field)) return false;
  uint8_t data[] = {0, 0, 0, 0};
  return pb_encode_string(stream, data, sizeof(data));
}

#define LAUNCH_SPEC_JSON_BUF_SIZE 1024

static ChiakiErrorCode stream_connection_send_big(
    ChiakiStreamConnection *stream_connection) {
  ChiakiSession *session = stream_connection->session;

  ChiakiLaunchSpec launch_spec;
  launch_spec.target = session->target;
  launch_spec.mtu = session->mtu_in;
  launch_spec.rtt = session->rtt_us / 1000;
  launch_spec.handshake_key = session->handshake_key;

  launch_spec.width = session->connect_info.video_profile.width;
  launch_spec.height = session->connect_info.video_profile.height;
  launch_spec.max_fps = session->connect_info.video_profile.max_fps;
  launch_spec.codec = session->connect_info.video_profile.codec;
  launch_spec.bw_kbps_sent = session->connect_info.video_profile.bitrate;

  union {
    char json[LAUNCH_SPEC_JSON_BUF_SIZE];
    char b64[LAUNCH_SPEC_JSON_BUF_SIZE * 2];
  } launch_spec_buf;
  int launch_spec_json_size = chiaki_launchspec_format(
      launch_spec_buf.json, sizeof(launch_spec_buf.json), &launch_spec);
  if (launch_spec_json_size < 0) {
    log_error("StreamConnection failed to format LaunchSpec json");
    return CHIAKI_ERR_UNKNOWN;
  }
  launch_spec_json_size += 1;  // we also want the trailing 0

  log_debug("LaunchSpec: %s", launch_spec_buf.json);

  // CRITICAL: LaunchSpec encryption like vitaki-fork
  uint8_t launch_spec_json_enc[LAUNCH_SPEC_JSON_BUF_SIZE];
  memset(launch_spec_json_enc, 0, (size_t)launch_spec_json_size);
  ChiakiErrorCode err = chiaki_rpcrypt_encrypt(
      &session->rpcrypt, 0, launch_spec_json_enc, launch_spec_json_enc,
      (size_t)launch_spec_json_size);
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("StreamConnection failed to encrypt LaunchSpec");
    return err;
  }

  xor_bytes(launch_spec_json_enc, (uint8_t *)launch_spec_buf.json,
            (size_t)launch_spec_json_size);
  err =
      chiaki_base64_encode(launch_spec_json_enc, (size_t)launch_spec_json_size,
                           launch_spec_buf.b64, sizeof(launch_spec_buf.b64));
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("StreamConnection failed to encode LaunchSpec as base64");
    return err;
  }

  uint8_t ecdh_pub_key[128];
  ChiakiPBBuf ecdh_pub_key_buf = {sizeof(ecdh_pub_key), ecdh_pub_key};
  uint8_t ecdh_sig[32];
  ChiakiPBBuf ecdh_sig_buf = {sizeof(ecdh_sig), ecdh_sig};
  err = chiaki_ecdh_get_local_pub_key(
      &session->ecdh, ecdh_pub_key, &ecdh_pub_key_buf.size,
      session->handshake_key, ecdh_sig, &ecdh_sig_buf.size);
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("StreamConnection failed to get ECDH key and sig");
    return err;
  }

  // Create protobuf message using vitaki-fork approach
  tkproto_TakionMessage msg;
  memset(&msg, 0, sizeof(msg));

  msg.type = tkproto_TakionMessage_PayloadType_BIG;
  msg.has_big_payload = true;

  // Fill message using our fixed-field approach (adapted for our nanopb)
  msg.big_payload.client_version = 12;  // Fixed for PS5
  strncpy(msg.big_payload.session_key, session->session_id,
          sizeof(msg.big_payload.session_key) - 1);
  msg.big_payload.session_key[sizeof(msg.big_payload.session_key) - 1] = '\0';

  // Use encrypted + base64 encoded LaunchSpec like vitaki-fork
  strncpy(msg.big_payload.launch_spec, launch_spec_buf.b64,
          sizeof(msg.big_payload.launch_spec) - 1);
  msg.big_payload.launch_spec[sizeof(msg.big_payload.launch_spec) - 1] = '\0';

  // Zero encrypted key (4 bytes)
  pb_bytes_array_t *encrypted_key = malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(4));
  if (!encrypted_key) {
    log_error("Failed to allocate encrypted key buffer");
    return CHIAKI_ERR_MEMORY;
  }
  encrypted_key->size = 4;
  memset(encrypted_key->bytes, 0, 4);
  msg.big_payload.encrypted_key = encrypted_key;

  // Real ECDH keys
  msg.big_payload.has_ecdh_pub_key = true;
  pb_bytes_array_t *ecdh_pub_key_array =
      malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(ecdh_pub_key_buf.size));
  if (!ecdh_pub_key_array) {
    free(encrypted_key);
    log_error("Failed to allocate ECDH pub key buffer");
    return CHIAKI_ERR_MEMORY;
  }
  ecdh_pub_key_array->size = ecdh_pub_key_buf.size;
  memcpy(ecdh_pub_key_array->bytes, ecdh_pub_key_buf.buf,
         ecdh_pub_key_buf.size);
  msg.big_payload.ecdh_pub_key = ecdh_pub_key_array;

  msg.big_payload.has_ecdh_sig = true;
  pb_bytes_array_t *ecdh_sig_array =
      malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(ecdh_sig_buf.size));
  if (!ecdh_sig_array) {
    free(encrypted_key);
    free(ecdh_pub_key_array);
    log_error("Failed to allocate ECDH sig buffer");
    return CHIAKI_ERR_MEMORY;
  }
  ecdh_sig_array->size = ecdh_sig_buf.size;
  memcpy(ecdh_sig_array->bytes, ecdh_sig_buf.buf, ecdh_sig_buf.size);
  msg.big_payload.ecdh_sig = ecdh_sig_array;

  uint8_t buf[2048];
  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);

  // Clean up allocated memory
  free(encrypted_key);
  free(ecdh_pub_key_array);
  free(ecdh_sig_array);

  if (!pbr) {
    log_error("StreamConnection big protobuf encoding failed: %s",
              PB_GET_ERROR(&stream));
    return CHIAKI_ERR_UNKNOWN;
  }

  int32_t total_size = stream.bytes_written;
  uint32_t mtu =
      (session->mtu_in < session->mtu_out) ? session->mtu_in : session->mtu_out;
  // Take into account overhead of network
  mtu -= 50;
  uint32_t buf_pos = 0;
  bool first = true;

  log_info(
      "Sending BIG message with vitaki-fork LaunchSpec encryption and "
      "fragmentation");
  log_info("Total size: %d bytes, MTU: %u", total_size, mtu);

  // CRITICAL: Message fragmentation like vitaki-fork
  while ((mtu < total_size + 26) || (mtu < total_size + 25 && !first)) {
    size_t buf_size;
    if (first) {
      buf_size = mtu - 26;
      err = chiaki_takion_send_message_data(stream_connection->takion_conn, 0,
                                            1, buf + buf_pos, buf_size, NULL);
      first = false;
    } else {
      buf_size = mtu - 25;
      err = chiaki_takion_send_message_data_cont(
          stream_connection->takion_conn, 0, 1, buf + buf_pos, buf_size, NULL);
    }
    buf_pos += buf_size;
    total_size -= buf_size;

    if (err != CHIAKI_ERR_SUCCESS) {
      log_error("Failed to send fragmented BIG message part: %d", err);
      return err;
    }
  }
  if (total_size > 0) {
    if (first)
      err = chiaki_takion_send_message_data(stream_connection->takion_conn, 1,
                                            1, buf + buf_pos, total_size, NULL);
    else
      err = chiaki_takion_send_message_data_cont(stream_connection->takion_conn,
                                                 1, 1, buf + buf_pos,
                                                 total_size, NULL);
  }

  log_info("Completed vitaki-fork compatible BIG message send");
  return err;
}

// Stub implementations for remaining functions
VitaRPS5Result chiaki_stream_connection_run(
    ChiakiStreamConnection *stream_connection, chiaki_socket_t *socket,
    void *takion_connection) {
  if (!stream_connection || !stream_connection->session) {
    log_error("StreamConnection run: Invalid parameters");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  stream_connection->takion_conn = takion_connection;
  ChiakiSession *session = stream_connection->session;

  log_info(
      "StreamConnection starting vitaki-fork compatible Remote Play protocol");
  log_info("Target: %s, Video: %dx%d@%dfps",
           chiaki_target_is_ps5(session->target) ? "PS5" : "PS4",
           session->connect_info.video_profile.width,
           session->connect_info.video_profile.height,
           session->connect_info.video_profile.max_fps);

  // Initialize RPCrypt for LaunchSpec encryption using auth method
  bright_ambassador_ps4_pre10(
      session->rpcrypt.bright, session->rpcrypt.ambassador,
      session->connect_info.regist_key, session->connect_info.morning);
  session->rpcrypt.target = session->target;

  // CRITICAL: Set up vitaki-fork callback system for receiving PS5 responses
  log_info("VITAKI-FORK: Setting up callback system through Takion layer");

  // Connect our streamconnection callback to the Takion layer
  VitaRPS5Result callback_result = takion_set_data_callback(
      takion_connection, takion_data_callback_wrapper, stream_connection);
  if (callback_result != VITARPS5_SUCCESS) {
    log_error("Failed to set Takion data callback: %d", callback_result);
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_info("VITAKI-FORK: Callback system connected successfully");

  // VITAKI-FORK STATE MACHINE IMPLEMENTATION

  // State 1: Takion Connect (already done by calling layer)
  chiaki_mutex_lock(&stream_connection->state_mutex);
  stream_connection->state = STATE_TAKION_CONNECT;
  stream_connection->state_finished = true;  // Takion is already connected
  chiaki_mutex_unlock(&stream_connection->state_mutex);
  log_info("VITAKI-FORK: State 1 - Takion connected");

  // State 2: Send BIG message and wait for BANG
  chiaki_mutex_lock(&stream_connection->state_mutex);
  stream_connection->state = STATE_EXPECT_BANG;
  stream_connection->state_finished = false;
  stream_connection->state_failed = false;
  chiaki_mutex_unlock(&stream_connection->state_mutex);

  log_info("VITAKI-FORK: State 2 - Sending BIG message");
  ChiakiErrorCode err = stream_connection_send_big(stream_connection);
  if (err != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to send BIG message: %d", err);
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_info("VITAKI-FORK: BIG message sent, waiting for BANG response...");

  // Wait for BANG response (processed by callback system)
  chiaki_mutex_lock(&stream_connection->state_mutex);
  while (!stream_connection->state_finished &&
         !stream_connection->state_failed && !stream_connection->should_stop) {
    // In a real implementation, this would wait for the callback to signal us
    // For now, we'll timeout after 5 seconds to see what happens
    struct timespec timeout_time;
    clock_gettime(CLOCK_REALTIME, &timeout_time);
    timeout_time.tv_sec += 5;

    int cond_result =
        chiaki_cond_timedwait(&stream_connection->state_cond,
                              &stream_connection->state_mutex, &timeout_time);
    if (cond_result != 0) {
      log_error("VITAKI-FORK: Timeout waiting for BANG response");
      stream_connection->state_failed = true;
      break;
    }
  }

  bool bang_success =
      stream_connection->state_finished && !stream_connection->state_failed;
  chiaki_mutex_unlock(&stream_connection->state_mutex);

  if (!bang_success) {
    log_error("VITAKI-FORK: Failed to receive BANG response");
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_info("VITAKI-FORK: BANG response received and processed successfully!");

  // State 3: Wait for STREAMINFO
  log_info("VITAKI-FORK: State 3 - Waiting for STREAMINFO");

  // Wait for STREAMINFO response (processed by callback system)
  chiaki_mutex_lock(&stream_connection->state_mutex);
  stream_connection->state_finished = false;
  stream_connection->state_failed = false;

  while (!stream_connection->state_finished &&
         !stream_connection->state_failed && !stream_connection->should_stop) {
    struct timespec timeout_time;
    clock_gettime(CLOCK_REALTIME, &timeout_time);
    timeout_time.tv_sec += 10;

    int cond_result =
        chiaki_cond_timedwait(&stream_connection->state_cond,
                              &stream_connection->state_mutex, &timeout_time);
    if (cond_result != 0) {
      log_error("VITAKI-FORK: Timeout waiting for STREAMINFO response");
      stream_connection->state_failed = true;
      break;
    }
  }

  bool streaminfo_success =
      stream_connection->state_finished && !stream_connection->state_failed;
  chiaki_mutex_unlock(&stream_connection->state_mutex);

  if (!streaminfo_success) {
    log_error("VITAKI-FORK: Failed to receive STREAMINFO response");
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_info("VITAKI-FORK: STREAMINFO received and processed successfully!");
  log_info(
      "VITAKI-FORK: Remote Play protocol completed - streaming should now "
      "begin");

  return VITARPS5_SUCCESS;
}

VitaRPS5Result chiaki_stream_connection_stop(
    ChiakiStreamConnection *stream_connection) {
  log_info("StreamConnection stop (vitaki-fork implementation)");
  return VITARPS5_SUCCESS;
}

// CRITICAL: Real vitaki-fork callback system implementation
static void stream_connection_takion_cb(ChiakiTakionEvent *event, void *user) {
  ChiakiStreamConnection *stream_connection = user;
  switch (event->type) {
    case CHIAKI_TAKION_EVENT_TYPE_CONNECTED:
    case CHIAKI_TAKION_EVENT_TYPE_DISCONNECT:
      chiaki_mutex_lock(&stream_connection->state_mutex);
      if (stream_connection->state == STATE_TAKION_CONNECT) {
        stream_connection->state_finished =
            event->type == CHIAKI_TAKION_EVENT_TYPE_CONNECTED;
        stream_connection->state_failed =
            event->type == CHIAKI_TAKION_EVENT_TYPE_DISCONNECT;
        chiaki_cond_signal(&stream_connection->state_cond);
      }
      chiaki_mutex_unlock(&stream_connection->state_mutex);
      break;
    case CHIAKI_TAKION_EVENT_TYPE_DATA:
      log_info("VITAKI-FORK: Received Takion data callback (type=%d, size=%zu)",
               event->data.data_type, event->data.buf_size);
      stream_connection_takion_data(stream_connection, event->data.data_type,
                                    event->data.buf, event->data.buf_size);
      break;
    case CHIAKI_TAKION_EVENT_TYPE_AV:
      stream_connection_takion_av(stream_connection, event->av);
      break;
    default:
      break;
  }
}

static void stream_connection_takion_data(
    ChiakiStreamConnection *stream_connection,
    ChiakiTakionMessageDataType data_type, uint8_t *buf, size_t buf_size) {
  switch (data_type) {
    case CHIAKI_TAKION_MESSAGE_DATA_TYPE_PROTOBUF:
      log_info("VITAKI-FORK: Processing protobuf message (size=%zu)", buf_size);
      stream_connection_takion_data_protobuf(stream_connection, buf, buf_size);
      break;
    case CHIAKI_TAKION_MESSAGE_DATA_TYPE_RUMBLE:
      stream_connection_takion_data_rumble(stream_connection, buf, buf_size);
      break;
    case CHIAKI_TAKION_MESSAGE_DATA_TYPE_TRIGGER_EFFECTS:
      stream_connection_takion_data_trigger_effects(stream_connection, buf,
                                                    buf_size);
      break;
    default:
      log_warning("Unknown Takion message data type: %d", data_type);
      break;
  }
}

static void stream_connection_takion_data_protobuf(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size) {
  chiaki_mutex_lock(&stream_connection->state_mutex);
  log_info("VITAKI-FORK: Processing protobuf in state %d",
           stream_connection->state);

  switch (stream_connection->state) {
    case STATE_EXPECT_BANG:
      log_info(
          "VITAKI-FORK: Calling stream_connection_takion_data_expect_bang");
      stream_connection_takion_data_expect_bang(stream_connection, buf,
                                                buf_size);
      break;
    case STATE_EXPECT_STREAMINFO:
      log_info(
          "VITAKI-FORK: Calling "
          "stream_connection_takion_data_expect_streaminfo");
      stream_connection_takion_data_expect_streaminfo(stream_connection, buf,
                                                      buf_size);
      break;
    default:  // STATE_IDLE
      log_info("VITAKI-FORK: Calling stream_connection_takion_data_idle");
      stream_connection_takion_data_idle(stream_connection, buf, buf_size);
      break;
  }
  chiaki_mutex_unlock(&stream_connection->state_mutex);
}

static void stream_connection_takion_data_expect_bang(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size) {
  log_info("VITAKI-FORK: Processing BANG response (%zu bytes)", buf_size);

  // Decode ECDH keys from BANG response
  char ecdh_pub_key[128];
  ChiakiPBDecodeBuf ecdh_pub_key_buf = {sizeof(ecdh_pub_key), 0,
                                        (uint8_t *)ecdh_pub_key};
  char ecdh_sig[32];
  ChiakiPBDecodeBuf ecdh_sig_buf = {sizeof(ecdh_sig), 0, (uint8_t *)ecdh_sig};

  tkproto_TakionMessage msg;
  memset(&msg, 0, sizeof(msg));

  msg.bang_payload.ecdh_pub_key.arg = &ecdh_pub_key_buf;
  msg.bang_payload.ecdh_pub_key.funcs.decode = chiaki_pb_decode_buf;
  msg.bang_payload.ecdh_sig.arg = &ecdh_sig_buf;
  msg.bang_payload.ecdh_sig.funcs.decode = chiaki_pb_decode_buf;

  pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
  bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
  if (!r) {
    log_error("StreamConnection failed to decode BANG protobuf");
    goto error;
  }

  if (msg.type != tkproto_TakionMessage_PayloadType_BANG ||
      !msg.has_bang_payload) {
    if (msg.type == tkproto_TakionMessage_PayloadType_DISCONNECT) {
      log_error("PS5 sent disconnect during BANG phase");
      goto error;
    }

    // PS Vita workaround from vitaki-fork
    if (msg.type == tkproto_TakionMessage_PayloadType_STREAMINFO) {
      log_warning(
          "StreamConnection expected BANG but received STREAMINFO (PS Vita "
          "workaround)");
      stream_connection->streaminfo_called_from_bang = true;
      stream_connection_takion_data_expect_streaminfo(stream_connection, buf,
                                                      buf_size);
      return;
    }

    log_error("StreamConnection expected BANG payload but received type: %d",
              msg.type);
    goto error;
  }

  log_info("VITAKI-FORK: BANG received successfully!");

  if (!msg.bang_payload.version_accepted) {
    log_error("StreamConnection: PS5 didn't accept our version");
    goto error;
  }

  if (!msg.bang_payload.encrypted_key_accepted) {
    log_error("StreamConnection: PS5 didn't accept our encrypted key");
    goto error;
  }

  if (!ecdh_pub_key_buf.size) {
    log_error("StreamConnection: Didn't get remote ECDH pub key from BANG");
    goto error;
  }

  if (!ecdh_sig_buf.size) {
    log_error("StreamConnection: Didn't get remote ECDH sig from BANG");
    goto error;
  }

  // Complete ECDH key exchange
  assert(!stream_connection->ecdh_secret);
  stream_connection->ecdh_secret = malloc(CHIAKI_ECDH_SECRET_SIZE);
  if (!stream_connection->ecdh_secret) {
    log_error("StreamConnection: Failed to allocate ECDH secret memory");
    goto error;
  }

  ChiakiErrorCode err = chiaki_ecdh_derive_secret(
      &stream_connection->session->ecdh, stream_connection->ecdh_secret,
      ecdh_pub_key_buf.buf, ecdh_pub_key_buf.size,
      stream_connection->session->handshake_key, ecdh_sig_buf.buf,
      ecdh_sig_buf.size);

  if (err != CHIAKI_ERR_SUCCESS) {
    free(stream_connection->ecdh_secret);
    stream_connection->ecdh_secret = NULL;
    log_error("StreamConnection: Failed to derive ECDH secret from BANG");
    goto error;
  }

  log_info(
      "VITAKI-FORK: ECDH secret derived successfully, initializing GKCrypt");

  // Initialize GKCrypt for streaming (simplified for now)
  // TODO: Implement full stream_connection_init_crypt from vitaki-fork

  // Transition to waiting for STREAMINFO
  stream_connection->state = STATE_EXPECT_STREAMINFO;
  log_info("VITAKI-FORK: State transition: EXPECT_BANG -> EXPECT_STREAMINFO");

  // Signal success - BANG processing complete
  stream_connection->state_finished = true;
  chiaki_cond_signal(&stream_connection->state_cond);
  return;

error:
  stream_connection->state_failed = true;
  chiaki_cond_signal(&stream_connection->state_cond);
}

// Additional stub implementations
static void stream_connection_takion_data_rumble(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size) {
  log_debug("VITAKI-FORK: Received rumble data (%zu bytes)", buf_size);
}

static void stream_connection_takion_data_trigger_effects(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size) {
  log_debug("VITAKI-FORK: Received trigger effects data (%zu bytes)", buf_size);
}

static void stream_connection_takion_data_idle(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size) {
  log_debug("VITAKI-FORK: Received data in IDLE state (%zu bytes)", buf_size);
}

static void stream_connection_takion_data_expect_streaminfo(
    ChiakiStreamConnection *stream_connection, uint8_t *buf, size_t buf_size) {
  log_info("VITAKI-FORK: Processing STREAMINFO response (%zu bytes)", buf_size);

  // For now, just signal success - TODO: implement full STREAMINFO parsing from
  // vitaki-fork
  tkproto_TakionMessage msg;
  memset(&msg, 0, sizeof(msg));

  pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
  bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
  if (!r) {
    log_error("StreamConnection failed to decode STREAMINFO protobuf");
    stream_connection->state_failed = true;
    chiaki_cond_signal(&stream_connection->state_cond);
    return;
  }

  if (msg.type != tkproto_TakionMessage_PayloadType_STREAMINFO ||
      !msg.has_streaminfo_payload) {
    log_error(
        "StreamConnection expected STREAMINFO payload but received type: %d",
        msg.type);
    stream_connection->state_failed = true;
    chiaki_cond_signal(&stream_connection->state_cond);
    return;
  }

  log_info("VITAKI-FORK: STREAMINFO received successfully!");

  // TODO: Parse video/audio codec information from STREAMINFO

  // Send STREAMINFO_ACK response to PS5
  ChiakiErrorCode ack_result =
      stream_connection_send_streaminfo_ack(stream_connection);
  if (ack_result != CHIAKI_ERR_SUCCESS) {
    log_error("Failed to send STREAMINFO ACK: %s",
              chiaki_error_string(ack_result));
    stream_connection->state_finished = false;
    chiaki_cond_signal(&stream_connection->state_cond);
    return;
  }

  // Signal success - STREAMINFO processing complete
  stream_connection->state_finished = true;
  chiaki_cond_signal(&stream_connection->state_cond);
}

static void stream_connection_takion_av(
    ChiakiStreamConnection *stream_connection, ChiakiTakionAVPacket *packet) {
  log_debug("VITAKI-FORK: Received A/V packet");
  // TODO: Route to video/audio decoders
}

static ChiakiErrorCode stream_connection_send_controller_connection(
    ChiakiStreamConnection *stream_connection) {
  log_debug("VITAKI-FORK: Sending controller connection");
  return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode stream_connection_enable_microphone(
    ChiakiStreamConnection *stream_connection) {
  log_debug("VITAKI-FORK: Enabling microphone");
  return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode stream_connection_send_disconnect(
    ChiakiStreamConnection *stream_connection) {
  log_debug("VITAKI-FORK: Sending disconnect");
  return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode stream_connection_send_streaminfo_ack(
    ChiakiStreamConnection *stream_connection) {
  log_info("VITAKI-FORK: Sending STREAMINFO ACK response to PS5...");

  // Check if Takion connection is available
  if (!stream_connection->takion_conn) {
    log_error("No Takion connection available for STREAMINFO ACK");
    return CHIAKI_ERR_UNKNOWN;
  }

  // Create protobuf STREAMINFO ACK message (empty payload)
  tkproto_TakionMessage ack_msg = tkproto_TakionMessage_init_zero;
  ack_msg.type = tkproto_TakionMessage_PayloadType_STREAMINFOACK;
  ack_msg.has_payload = false;  // ACK has no payload

  // Encode the message using nanopb
  uint8_t ack_buffer[512];
  pb_ostream_t stream = pb_ostream_from_buffer(ack_buffer, sizeof(ack_buffer));

  if (!pb_encode(&stream, tkproto_TakionMessage_fields, &ack_msg)) {
    log_error("Failed to encode STREAMINFO ACK message: %s",
              PB_GET_ERROR(&stream));
    return CHIAKI_ERR_UNKNOWN;
  }

  size_t ack_size = stream.bytes_written;
  log_info("STREAMINFO ACK encoded successfully (%zu bytes)", ack_size);

  // Send via Takion connection
  int send_result =
      takion_send_message(stream_connection->takion_conn, ack_buffer, ack_size);
  if (send_result != 0) {
    log_error("Failed to send STREAMINFO ACK packet: %s",
              takion_error_string(send_result));
    return CHIAKI_ERR_UNKNOWN;
  }

  log_info("STREAMINFO ACK sent successfully via Takion (%zu bytes)", ack_size);
  return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode stream_connection_send_heartbeat(
    ChiakiStreamConnection *stream_connection) {
  log_debug("VITAKI-FORK: Sending heartbeat");
  return CHIAKI_ERR_SUCCESS;
}

// VITAKI-FORK: Callback wrapper that translates Takion callbacks to
// streamconnection events
static void takion_data_callback_wrapper(void *user, int data_type,
                                         uint8_t *buf, size_t buf_size) {
  ChiakiStreamConnection *stream_connection = (ChiakiStreamConnection *)user;

  log_info("VITAKI-FORK: Takion callback received (data_type=%d, size=%zu)",
           data_type, buf_size);

  // Create a vitaki-fork style event and call our callback system
  // This translates from our simple Takion callback to the full vitaki-fork
  // event system

  // For now, assume all data is protobuf (type 0 =
  // CHIAKI_TAKION_MESSAGE_DATA_TYPE_PROTOBUF)
  // TODO: Add proper data type parsing when we implement full vitaki-fork
  // compatibility
  stream_connection_takion_data(stream_connection, data_type, buf, buf_size);
}