// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 adaptation of vitaki-fork streamconnection.h

#ifndef VITARPS5_CHIAKI_STREAMCONNECTION_H
#define VITARPS5_CHIAKI_STREAMCONNECTION_H

#include <psp2/kernel/threadmgr.h>
#include <stdbool.h>

#include "../core/vitarps5.h"
#include "../utils/logger.h"
#include "chiaki_common.h"
#include "vitaki_thread.h"

// Forward declarations to avoid circular includes
struct chiaki_session_t;
typedef struct chiaki_session_t ChiakiSession;
typedef struct TakionConnection TakionConnection;

#ifdef __cplusplus
extern "C" {
#endif

// Threading types are defined in vitaki_thread.h
typedef uint16_t ChiakiSeqNum16;

// Forward declarations for types not yet implemented
typedef struct chiaki_takion_t ChiakiTakion;
typedef struct chiaki_gkcrypt_t ChiakiGKCrypt;
typedef struct chiaki_packet_stats_t ChiakiPacketStats;
typedef struct chiaki_audio_receiver_t ChiakiAudioReceiver;
typedef struct chiaki_video_receiver_t ChiakiVideoReceiver;
typedef struct chiaki_feedback_sender_t ChiakiFeedbackSender;
typedef struct chiaki_congestion_control_t ChiakiCongestionControl;

// Use existing event types from chiaki_session.h
// (ChiakiEventType, ChiakiEvent, etc. are already defined there)

// VitaRPS5 Stream Connection structure (simplified from vitaki-fork)
typedef struct chiaki_stream_connection_t {
  ChiakiSession *session;
  void *log;  // We use global logger instead
  ChiakiTakion *takion;
  TakionConnection *takion_conn;  // TakionConnection from session manager
  uint8_t *ecdh_secret;
  ChiakiGKCrypt *gkcrypt_local;
  ChiakiGKCrypt *gkcrypt_remote;

  ChiakiPacketStats *packet_stats;
  ChiakiAudioReceiver *audio_receiver;
  ChiakiVideoReceiver *video_receiver;
  ChiakiAudioReceiver *haptics_receiver;

  ChiakiFeedbackSender *feedback_sender;
  ChiakiCongestionControl *congestion_control;
  bool feedback_sender_active;
  ChiakiMutex feedback_sender_mutex;

  ChiakiCond state_cond;
  ChiakiMutex state_mutex;

  int state;
  bool state_finished;
  bool state_failed;
  bool should_stop;
  bool remote_disconnected;
  char *remote_disconnect_reason;

  // PS Vita specific workaround from vitaki-fork
  bool streaminfo_called_from_bang;

  double measured_bitrate;
} ChiakiStreamConnection;

// Core API functions (adapted from vitaki-fork)
VitaRPS5Result chiaki_stream_connection_init(
    ChiakiStreamConnection *stream_connection, ChiakiSession *session);
void chiaki_stream_connection_fini(ChiakiStreamConnection *stream_connection);
VitaRPS5Result chiaki_stream_connection_run(
    ChiakiStreamConnection *stream_connection, chiaki_socket_t *socket,
    void *takion_connection);
VitaRPS5Result chiaki_stream_connection_stop(
    ChiakiStreamConnection *stream_connection);

// Streaming control functions
VitaRPS5Result stream_connection_send_toggle_mute_direct_message(
    ChiakiStreamConnection *stream_connection, bool muted);
VitaRPS5Result stream_connection_send_corrupt_frame(
    ChiakiStreamConnection *stream_connection, ChiakiSeqNum16 start,
    ChiakiSeqNum16 end);

// Heartbeat system for PS5 connection keepalive
VitaRPS5Result stream_connection_send_heartbeat(
    ChiakiStreamConnection *stream_connection);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_CHIAKI_STREAMCONNECTION_H