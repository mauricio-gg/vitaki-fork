#ifndef VITARPS5_AUDIO_DECODER_H
#define VITARPS5_AUDIO_DECODER_H

#include <psp2/audioout.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/types.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/vitarps5.h"
#include "../network/takion.h"

#ifdef __cplusplus
extern "C" {
#endif

// Audio decoder configuration
#define AUDIO_BUFFER_COUNT 8          // Circular buffer pool
#define AUDIO_SAMPLES_PER_FRAME 1024  // Samples per audio frame
#define AUDIO_MAX_CHANNELS 2          // Stereo audio
#define AUDIO_SAMPLE_RATE_48K 48000
#define AUDIO_SAMPLE_RATE_44K 44100

// Audio decoder state
typedef enum {
  AUDIO_DECODER_STATE_IDLE = 0,
  AUDIO_DECODER_STATE_INITIALIZING,
  AUDIO_DECODER_STATE_READY,
  AUDIO_DECODER_STATE_DECODING,
  AUDIO_DECODER_STATE_ERROR
} AudioDecoderState;

// Audio format information
typedef enum {
  AUDIO_FORMAT_OPUS = 0,
  AUDIO_FORMAT_PCM_S16LE,
  AUDIO_FORMAT_UNKNOWN
} AudioFormat;

// Audio frame information
typedef struct {
  uint32_t sample_rate;   // Sample rate (48000 or 44100)
  uint32_t channels;      // Number of channels (1 or 2)
  uint32_t sample_count;  // Number of samples in this frame
  uint64_t timestamp;     // Frame timestamp
  AudioFormat format;     // Audio format
  void* sample_buffer;    // PCM sample data (16-bit signed)
  size_t buffer_size;     // Size of sample buffer in bytes
  bool is_silence;        // True if frame contains silence
} AudioFrame;

// Audio buffer pool for efficient memory management
typedef struct {
  AudioFrame frames[AUDIO_BUFFER_COUNT];
  uint32_t write_index;      // Next buffer to decode into
  uint32_t read_index;       // Next buffer to play from
  uint32_t available_count;  // Available buffers for decoding
  bool initialized;
} AudioBufferPool;

// Audio decoder context
typedef struct AudioDecoder AudioDecoder;

// Audio decoder callbacks
typedef void (*AudioFrameCallback)(const AudioFrame* frame, void* user_data);
typedef void (*AudioErrorCallback)(VitaRPS5Result error, const char* message,
                                   void* user_data);

// Audio decoder configuration
typedef struct {
  uint32_t sample_rate;          // Target sample rate
  uint32_t channels;             // Number of channels
  uint32_t buffer_size_ms;       // Audio buffer size in milliseconds
  bool enable_opus_decode;       // Enable Opus decoding
  uint32_t decode_buffer_count;  // Number of decode buffers

  // Callbacks
  AudioFrameCallback frame_callback;
  AudioErrorCallback error_callback;
  void* user_data;
} AudioDecoderConfig;

// Audio decoder statistics
typedef struct {
  uint32_t frames_decoded;
  uint32_t frames_dropped;
  uint32_t decode_errors;
  float average_decode_time_ms;
  float current_sample_rate;
  uint32_t memory_usage_bytes;
  bool opus_decoder_active;
  uint32_t audio_latency_ms;
} AudioDecoderStats;

// Core Audio Decoder API

/**
 * Initialize audio decoder subsystem
 */
VitaRPS5Result audio_decoder_init(void);

/**
 * Cleanup audio decoder subsystem
 */
void audio_decoder_cleanup(void);

/**
 * Create a new audio decoder instance
 */
VitaRPS5Result audio_decoder_create(const AudioDecoderConfig* config,
                                    AudioDecoder** decoder);

/**
 * Destroy audio decoder instance
 */
void audio_decoder_destroy(AudioDecoder* decoder);

/**
 * Start audio decoder
 */
VitaRPS5Result audio_decoder_start(AudioDecoder* decoder);

/**
 * Stop audio decoder
 */
VitaRPS5Result audio_decoder_stop(AudioDecoder* decoder);

/**
 * Process incoming audio packet from Takion
 * TODO: Update to use proper packet format when implemented
 */
VitaRPS5Result audio_decoder_process_packet(AudioDecoder* decoder,
                                            const uint8_t* packet_data,
                                            size_t packet_size);

/**
 * Get next decoded frame for playback
 */
VitaRPS5Result audio_decoder_get_frame(AudioDecoder* decoder,
                                       AudioFrame** frame);

/**
 * Return frame buffer to pool after playback
 */
VitaRPS5Result audio_decoder_return_frame(AudioDecoder* decoder,
                                          AudioFrame* frame);

/**
 * Get decoder state
 */
AudioDecoderState audio_decoder_get_state(const AudioDecoder* decoder);

/**
 * Get decoder statistics
 */
VitaRPS5Result audio_decoder_get_stats(const AudioDecoder* decoder,
                                       AudioDecoderStats* stats);

/**
 * Configure decoder parameters
 */
VitaRPS5Result audio_decoder_configure(AudioDecoder* decoder,
                                       uint32_t sample_rate, uint32_t channels);

/**
 * Flush decoder buffers
 */
VitaRPS5Result audio_decoder_flush(AudioDecoder* decoder);

// Buffer Management API

/**
 * Initialize audio buffer pool
 */
VitaRPS5Result audio_buffer_pool_init(AudioBufferPool* pool,
                                      uint32_t sample_rate, uint32_t channels);

/**
 * Cleanup audio buffer pool
 */
void audio_buffer_pool_cleanup(AudioBufferPool* pool);

/**
 * Get next available buffer for decoding
 */
AudioFrame* audio_buffer_pool_get_decode_buffer(AudioBufferPool* pool);

/**
 * Get next playback frame buffer
 */
AudioFrame* audio_buffer_pool_get_playback_buffer(AudioBufferPool* pool);

/**
 * Return buffer to pool
 */
void audio_buffer_pool_return_buffer(AudioBufferPool* pool, AudioFrame* frame);

// Utility Functions

/**
 * Convert audio decoder state to string
 */
const char* audio_decoder_state_string(AudioDecoderState state);

/**
 * Convert audio format to string
 */
const char* audio_format_string(AudioFormat format);

/**
 * Calculate required buffer size for audio frame
 */
size_t audio_calculate_frame_size(uint32_t sample_rate, uint32_t channels,
                                  uint32_t duration_ms);

/**
 * Check if Opus decoder is available
 */
bool audio_opus_decoder_available(void);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_AUDIO_DECODER_H