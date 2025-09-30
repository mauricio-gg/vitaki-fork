// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Common chiaki types adapted for PS Vita - extended for Remote Play

#ifndef CHIAKI_COMMON_H
#define CHIAKI_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// Export macro for API functions
#define CHIAKI_EXPORT

// Socket type for PS Vita - compatibility with chiaki_sock.h
typedef int chiaki_socket_t;
#define CHIAKI_INVALID_SOCKET (-1)
#define CHIAKI_SOCKET_IS_INVALID(s) ((s) < 0)
#define CHIAKI_SSIZET_TYPE int

// Forward declarations for types that are defined in other headers
// These prevent duplicate definitions while allowing cross-references
#ifndef CHIAKI_SESSION_H
typedef struct chiaki_session_t ChiakiSession;
typedef struct chiaki_opusdecoder_t ChiakiOpusDecoder;
typedef struct chiaki_opusencoder_t ChiakiOpusEncoder;
typedef struct chiaki_log_t ChiakiLog;
#endif

// Threading types - forward declarations only (defined in vitaki_thread.h)
typedef struct chiaki_thread_t ChiakiThread;
typedef struct chiaki_mutex_t ChiakiMutex;
typedef struct chiaki_cond_t ChiakiCond;
// ChiakiStopPipe is defined in vitaki_stoppipe.h

// Error Codes
#ifndef CHIAKI_ERRORCODE_DEFINED
#define CHIAKI_ERRORCODE_DEFINED
typedef enum {
  CHIAKI_ERR_SUCCESS = 0,
  CHIAKI_ERR_UNKNOWN = 1,
  CHIAKI_ERR_PARSE_ADDR = 2,
  CHIAKI_ERR_THREAD = 3,
  CHIAKI_ERR_MEMORY = 4,
  CHIAKI_ERR_NETWORK = 5,
  CHIAKI_ERR_CONNECTION_REFUSED = 6,
  CHIAKI_ERR_HOST_DOWN = 7,
  CHIAKI_ERR_HOST_UNREACH = 8,
  CHIAKI_ERR_DISCONNECTED = 9,
  CHIAKI_ERR_INVALID_DATA = 10,
  CHIAKI_ERR_INVALID_RESPONSE = 11,
  CHIAKI_ERR_INVALID_MAC = 12,
  CHIAKI_ERR_CANCELED = 13,
  CHIAKI_ERR_LOGIN_PIN_REQUEST = 14,
  CHIAKI_ERR_INVALID_PIN = 15,
  CHIAKI_ERR_PIN_INCORRECT = 16,
  CHIAKI_ERR_PIN_EXPIRED = 17,
  CHIAKI_ERR_REGIST_FAILED = 18,
  CHIAKI_ERR_VERSION_MISMATCH = 19,
  CHIAKI_ERR_BUF_TOO_SMALL = 20,
  CHIAKI_ERR_TIMEOUT = 21,
  CHIAKI_ERR_AUTH_FAILED = 22,
  CHIAKI_ERR_IN_PROGRESS = 23,
  CHIAKI_ERR_QUIT = 24
} ChiakiErrorCode;
#endif

// Threading function declarations are in vitaki_thread.h
// (Functions removed to prevent circular dependencies)

// ChiakiStopPipe functions are declared in vitaki_stoppipe.h

// Target Console Types - values MUST match vitaki-fork for compatibility
#ifndef CHIAKI_TARGET_DEFINED
#define CHIAKI_TARGET_DEFINED
typedef enum {
  CHIAKI_TARGET_PS4_UNKNOWN = 0,
  CHIAKI_TARGET_PS4_7 = 700,
  CHIAKI_TARGET_PS4_8 = 800,
  CHIAKI_TARGET_PS4_9 = 900,
  CHIAKI_TARGET_PS4_10 = 1000,
  CHIAKI_TARGET_PS5_UNKNOWN = 1000000,  // Must match vitaki-fork value
  CHIAKI_TARGET_PS5_1 = 1000100         // Must match vitaki-fork value
} ChiakiTarget;
#endif

// Video codec types
#ifndef CHIAKI_CODEC_DEFINED
#define CHIAKI_CODEC_DEFINED
typedef enum {
  CHIAKI_CODEC_H264 = 0,
  CHIAKI_CODEC_H265 = 1,
  CHIAKI_CODEC_H265_HDR = 2
} ChiakiCodec;
#endif

// Constants (don't redefine CHIAKI_HANDSHAKE_KEY_SIZE since it's in
// chiaki_session.h)
#define CHIAKI_ECDH_SECRET_SIZE 32
#define CHIAKI_AUDIO_HEADER_SIZE 16
#define CHIAKI_VIDEO_BUFFER_PADDING_SIZE 64
#define CHIAKI_VIDEO_PROFILES_MAX 8
#define CHIAKI_PSN_ACCOUNT_ID_SIZE 8

// Audio header structure
typedef struct {
  uint16_t sample_rate;
  uint8_t channels;
  uint8_t bits_per_sample;
  uint16_t frame_size;
} ChiakiAudioHeader;

// Video profile structure
typedef struct {
  uint32_t width;
  uint32_t height;
  size_t header_sz;
  uint8_t *header;
} ChiakiVideoProfile;

// Codec detection functions
#ifndef CHIAKI_CODEC_FUNCTIONS_DEFINED
#define CHIAKI_CODEC_FUNCTIONS_DEFINED
static inline bool chiaki_codec_is_h265(ChiakiCodec codec) {
  return codec == CHIAKI_CODEC_H265 || codec == CHIAKI_CODEC_H265_HDR;
}

static inline bool chiaki_codec_is_hdr(ChiakiCodec codec) {
  return codec == CHIAKI_CODEC_H265_HDR;
}
#endif

// Audio header utilities (to be implemented)
void chiaki_audio_header_load(ChiakiAudioHeader *header, const uint8_t *buf);
void chiaki_audio_header_save(const ChiakiAudioHeader *header, uint8_t *buf);
void chiaki_audio_header_set(ChiakiAudioHeader *header, uint8_t bits_per_sample,
                             uint8_t channels, uint32_t sample_rate,
                             uint16_t frame_size);

// Sequence number utilities (to be implemented)
bool chiaki_seq_num_32_gt(uint32_t a, uint32_t b);
bool chiaki_seq_num_32_lt(uint32_t a, uint32_t b);

// Memory alignment utilities (to be implemented)
void *chiaki_aligned_alloc(size_t alignment, size_t size);
void chiaki_aligned_free(void *ptr);

// Function declarations for new network implementation
// Note: These use types from chiaki_session.h - include that header first
#ifdef CHIAKI_SESSION_H
const char *chiaki_error_string(ChiakiErrorCode error);
ChiakiTarget chiaki_target_parse(const char *target);
#ifndef CHIAKI_TARGET_FUNCTIONS_DEFINED
bool chiaki_target_is_ps5(ChiakiTarget target);
#endif
uint16_t chiaki_target_get_firmware_version(ChiakiTarget target);
uint32_t chiaki_target_get_protocol_version(ChiakiTarget target);
#endif

// Utility macros
#define CHIAKI_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CHIAKI_MAX(a, b) ((a) > (b) ? (a) : (b))
#define CHIAKI_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#ifdef __cplusplus
}
#endif

#endif  // CHIAKI_COMMON_H