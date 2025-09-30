// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 Chiaki stub types for compilation

#ifndef VITARPS5_CHIAKI_STUBS_H
#define VITARPS5_CHIAKI_STUBS_H

#include <psp2/kernel/threadmgr.h>

#include "chiaki_common.h"
#include "chiaki_session.h"

#ifdef __cplusplus
extern "C" {
#endif

// Threading types (map to PS Vita kernel)
typedef struct {
  SceUID thread_id;
} ChiakiThread;

typedef struct {
  SceUID mutex_id;
} ChiakiMutex;

typedef struct {
  SceUID cond_id;
  ChiakiMutex *mutex;
} ChiakiCond;

// Takion types (minimal implementation)
typedef struct {
  int control_socket;
  int stream_socket;
  uint32_t version;
  void *user_data;
  void (*state_callback)(int state, void *user_data);
} ChiakiTakion;

typedef struct {
  void *sa;
  size_t sa_len;
  bool close_socket;
  void *log;
  bool ip_dontfrag;
  bool enable_crypt;
  bool enable_dualsense;
  uint32_t protocol_version;
  void (*cb)(void *event, void *user);
  void *cb_user;
} ChiakiTakionConnectInfo;

typedef enum {
  CHIAKI_TAKION_EVENT_TYPE_CONNECTED,
  CHIAKI_TAKION_EVENT_TYPE_DISCONNECT,
  CHIAKI_TAKION_EVENT_TYPE_DATA,
  CHIAKI_TAKION_EVENT_TYPE_AV
} ChiakiTakionEventType;

typedef enum {
  CHIAKI_TAKION_MESSAGE_DATA_TYPE_PROTOBUF,
  CHIAKI_TAKION_MESSAGE_DATA_TYPE_RUMBLE,
  CHIAKI_TAKION_MESSAGE_DATA_TYPE_TRIGGER_EFFECTS
} ChiakiTakionMessageDataType;

typedef struct {
  ChiakiTakionEventType type;
  union {
    struct {
      ChiakiTakionMessageDataType data_type;
      uint8_t *buf;
      size_t buf_size;
    } data;
    void *av;
  };
} ChiakiTakionEvent;

typedef struct {
  bool is_video;
  bool is_haptics;
  uint64_t key_pos;
  uint8_t *data;
  size_t data_size;
} ChiakiTakionAVPacket;

// GKCrypt types (stub implementation)
typedef struct {
  void *log;
  uint8_t index;
  uint8_t key_base[CHIAKI_GKCRYPT_BLOCK_SIZE];
  uint8_t iv[CHIAKI_GKCRYPT_BLOCK_SIZE];
  uint8_t key_gmac_base[CHIAKI_GKCRYPT_BLOCK_SIZE];
  uint8_t key_gmac_current[CHIAKI_GKCRYPT_BLOCK_SIZE];
  uint64_t key_gmac_index_current;

  // Buffer management (simplified)
  void *key_buf;
  size_t key_buf_size;
  size_t key_buf_populated;
  uint64_t key_buf_key_pos_min;
  size_t key_buf_start_offset;
  uint64_t last_key_pos;
  bool key_buf_thread_stop;
  ChiakiMutex key_buf_mutex;
  ChiakiCond key_buf_cond;
  ChiakiThread key_buf_thread;
} ChiakiGKCrypt;

// Network statistics (stub)
typedef struct {
  uint64_t packets_received;
  uint64_t bytes_received;
  float packet_loss_rate;
} ChiakiPacketStats;

// Audio receiver (stub)
typedef struct {
  void *session;
  ChiakiPacketStats *packet_stats;
} ChiakiAudioReceiver;

// Video receiver (stub)
typedef struct {
  void *session;
  ChiakiPacketStats *packet_stats;
  struct {
    struct {
      uint64_t bytes_received;
      uint64_t frames_received;
    } stream_stats;
  } frame_processor;
} ChiakiVideoReceiver;

// Feedback sender (stub)
typedef struct {
  ChiakiTakion *takion;
  bool active;
} ChiakiFeedbackSender;

// Congestion control (stub)
typedef struct {
  ChiakiThread thread;
  ChiakiTakion *takion;
  ChiakiPacketStats *packet_stats;
} ChiakiCongestionControl;

// ECDH (stub)
typedef struct {
  uint8_t private_key[32];
  uint8_t public_key[64];
} ChiakiECDH;

// Stub function declarations
ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool recursive);
void chiaki_mutex_fini(ChiakiMutex *mutex);
ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex);
ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex);

ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond, ChiakiMutex *mutex);
void chiaki_cond_fini(ChiakiCond *cond);
ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond);
ChiakiErrorCode chiaki_cond_timedwait_pred(ChiakiCond *cond, ChiakiMutex *mutex,
                                           uint32_t timeout_ms,
                                           bool (*pred)(void *),
                                           void *pred_user);

ChiakiErrorCode chiaki_thread_create(ChiakiThread *thread,
                                     void *(*func)(void *), void *arg);
void chiaki_thread_set_name(ChiakiThread *thread, const char *name);
ChiakiErrorCode chiaki_thread_join(ChiakiThread *thread, void **retval);

// Takion stubs
ChiakiErrorCode chiaki_takion_connect(ChiakiTakion *takion,
                                      ChiakiTakionConnectInfo *info,
                                      void *socket);
void chiaki_takion_close(ChiakiTakion *takion);
void chiaki_takion_set_crypt(ChiakiTakion *takion, ChiakiGKCrypt *local,
                             ChiakiGKCrypt *remote);
ChiakiErrorCode chiaki_takion_send_message_data(ChiakiTakion *takion,
                                                uint8_t is_final, uint8_t type,
                                                const uint8_t *data,
                                                size_t data_size,
                                                void *user_data);
ChiakiErrorCode chiaki_takion_send_message_data_cont(
    ChiakiTakion *takion, uint8_t is_final, uint8_t type, const uint8_t *data,
    size_t data_size, void *user_data);

// GKCrypt stubs
ChiakiGKCrypt *chiaki_gkcrypt_new(void *log, size_t key_buf_blocks,
                                  uint8_t index, const uint8_t *handshake_key,
                                  const uint8_t *ecdh_secret);
void chiaki_gkcrypt_free(ChiakiGKCrypt *gkcrypt);
ChiakiErrorCode chiaki_gkcrypt_decrypt(ChiakiGKCrypt *gkcrypt, uint64_t key_pos,
                                       uint8_t *buf, size_t buf_size);

// Other stubs
ChiakiErrorCode chiaki_packet_stats_init(ChiakiPacketStats *stats);
void chiaki_packet_stats_fini(ChiakiPacketStats *stats);

ChiakiAudioReceiver *chiaki_audio_receiver_new(void *session,
                                               ChiakiPacketStats *packet_stats);
void chiaki_audio_receiver_free(ChiakiAudioReceiver *receiver);
void chiaki_audio_receiver_stream_info(ChiakiAudioReceiver *receiver,
                                       const ChiakiAudioHeader *header);
void chiaki_audio_receiver_av_packet(ChiakiAudioReceiver *receiver,
                                     ChiakiTakionAVPacket *packet);

ChiakiVideoReceiver *chiaki_video_receiver_new(void *session,
                                               ChiakiPacketStats *packet_stats);
void chiaki_video_receiver_free(ChiakiVideoReceiver *receiver);
void chiaki_video_receiver_stream_info(ChiakiVideoReceiver *receiver,
                                       const ChiakiVideoProfile *profiles,
                                       size_t profile_count);
void chiaki_video_receiver_av_packet(ChiakiVideoReceiver *receiver,
                                     ChiakiTakionAVPacket *packet);

ChiakiErrorCode chiaki_feedback_sender_init(ChiakiFeedbackSender *sender,
                                            ChiakiTakion *takion);
void chiaki_feedback_sender_fini(ChiakiFeedbackSender *sender);
void chiaki_feedback_sender_set_controller_state(ChiakiFeedbackSender *sender,
                                                 void *controller_state);

ChiakiErrorCode chiaki_congestion_control_start(
    ChiakiCongestionControl *control, ChiakiTakion *takion,
    ChiakiPacketStats *packet_stats);
void chiaki_congestion_control_stop(ChiakiCongestionControl *control);

ChiakiErrorCode chiaki_ecdh_derive_secret(ChiakiECDH *ecdh, uint8_t *secret,
                                          const uint8_t *remote_key,
                                          size_t remote_key_size,
                                          const uint8_t *handshake_key,
                                          const uint8_t *sig, size_t sig_size);
ChiakiErrorCode chiaki_ecdh_get_local_pub_key(ChiakiECDH *ecdh, uint8_t *key,
                                              size_t *key_size,
                                              const uint8_t *handshake_key,
                                              uint8_t *sig, size_t *sig_size);

// Stream stats stubs
double chiaki_stream_stats_bitrate(void *stats, uint32_t fps);
void chiaki_stream_stats_reset(void *stats);

// Utility stubs
ChiakiErrorCode set_port(void *sa, uint16_t port);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_CHIAKI_STUBS_H