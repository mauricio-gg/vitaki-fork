#include "audio_decoder.h"

// #include <psp2/audioout.h>  // Not available in current VitaSDK
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"

// Opus decoder structures (placeholder for when Opus library is available)
// These are placeholder structures for Opus decoding integration
typedef struct {
  int sample_rate;
  int channels;
  int application;
  void* internal_state;
} OpusDecoder;

// Placeholder defines for Opus
#define OPUS_APPLICATION_AUDIO 2049
#define OPUS_OK 0
#define OPUS_INVALID_PACKET -4

// Placeholder Opus functions (to be replaced with actual Opus library)
static OpusDecoder* opus_decoder_create(int sample_rate, int channels,
                                        int* error) {
  log_debug("Opus decoder create (placeholder) - %dHz, %d channels",
            sample_rate, channels);

  OpusDecoder* decoder = malloc(sizeof(OpusDecoder));
  if (!decoder) {
    *error = -1;
    return NULL;
  }

  decoder->sample_rate = sample_rate;
  decoder->channels = channels;
  decoder->application = OPUS_APPLICATION_AUDIO;
  decoder->internal_state = NULL;

  *error = OPUS_OK;
  return decoder;
}

static void opus_decoder_destroy(OpusDecoder* decoder) {
  log_debug("Opus decoder destroy (placeholder)");
  if (decoder) {
    free(decoder);
  }
}

static int opus_decode(OpusDecoder* decoder, const unsigned char* data, int len,
                       short* pcm, int frame_size, int decode_fec) {
  // Placeholder: In actual implementation, this would decode Opus to PCM
  log_debug("Opus decode (placeholder) - input: %d bytes, output: %d samples",
            len, frame_size);

  // For now, generate silence or simple test tone
  for (int i = 0; i < frame_size * decoder->channels; i++) {
    pcm[i] = 0;  // Silence
  }

  return frame_size;  // Return number of samples decoded
}

// static int opus_decoder_get_size(int channels) {
//   return sizeof(OpusDecoder);
// }

// Internal decoder structure
struct AudioDecoder {
  AudioDecoderConfig config;
  AudioDecoderState state;
  AudioDecoderStats stats;

  // Opus decoder context
  OpusDecoder* opus_decoder;
  bool opus_decoder_available;

  // SceAudio playback
  int audio_port;
  bool audio_port_opened;

  // Buffer management
  AudioBufferPool buffer_pool;

  // Threading
  SceUID decode_thread;
  SceUID playback_thread;
  bool thread_running;

  // Frame queue for incoming packets
  TakionAudioPacket* packet_queue[32];
  uint32_t queue_write_index;
  uint32_t queue_read_index;
  uint32_t queue_size;
  SceUID queue_mutex;

  // Performance tracking
  uint64_t last_decode_time;
  uint32_t decode_time_samples[10];
  uint32_t decode_time_index;
};

// Global state
static bool audio_decoder_initialized = false;

// Internal functions
static int audio_decode_thread(SceSize args, void* argp);
static int audio_playback_thread(SceSize args, void* argp);
static VitaRPS5Result initialize_opus_decoder(AudioDecoder* decoder);
static VitaRPS5Result initialize_audio_port(AudioDecoder* decoder);
static VitaRPS5Result decode_frame_opus(AudioDecoder* decoder,
                                        const TakionAudioPacket* packet);
static VitaRPS5Result allocate_audio_buffers(AudioBufferPool* pool,
                                             uint32_t sample_rate,
                                             uint32_t channels);
static void free_audio_buffers(AudioBufferPool* pool);
static uint64_t get_timestamp_us(void);

// API Implementation

VitaRPS5Result audio_decoder_init(void) {
  if (audio_decoder_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing audio decoder subsystem");

  // Note: SceAudioOut is automatically available on PS Vita
  // No explicit initialization required

  audio_decoder_initialized = true;
  log_info("Audio decoder subsystem initialized");

  return VITARPS5_SUCCESS;
}

void audio_decoder_cleanup(void) {
  if (!audio_decoder_initialized) {
    return;
  }

  log_info("Cleaning up audio decoder subsystem");

  // Note: We don't call sceAudioOutTerm() as other applications may be using
  // audio

  audio_decoder_initialized = false;
  log_info("Audio decoder cleanup complete");
}

VitaRPS5Result audio_decoder_create(const AudioDecoderConfig* config,
                                    AudioDecoder** decoder) {
  if (!audio_decoder_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!config || !decoder) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  AudioDecoder* new_decoder = malloc(sizeof(AudioDecoder));
  if (!new_decoder) {
    return VITARPS5_ERROR_MEMORY;
  }

  // Initialize decoder
  memset(new_decoder, 0, sizeof(AudioDecoder));
  new_decoder->config = *config;
  new_decoder->state = AUDIO_DECODER_STATE_IDLE;
  new_decoder->thread_running = false;
  new_decoder->audio_port = -1;
  new_decoder->audio_port_opened = false;
  new_decoder->opus_decoder_available = audio_opus_decoder_available();

  // Create mutex for packet queue
  new_decoder->queue_mutex = sceKernelCreateMutex("audio_queue", 0, 0, NULL);
  if (new_decoder->queue_mutex < 0) {
    log_error("Failed to create audio queue mutex: 0x%08X",
              new_decoder->queue_mutex);
    free(new_decoder);
    return VITARPS5_ERROR_INIT;
  }

  // Initialize buffer pool
  VitaRPS5Result result = audio_buffer_pool_init(
      &new_decoder->buffer_pool, config->sample_rate, config->channels);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize audio buffer pool: %s",
              vitarps5_result_string(result));
    sceKernelDeleteMutex(new_decoder->queue_mutex);
    free(new_decoder);
    return result;
  }

  // Initialize Opus decoder if available and enabled
  if (new_decoder->opus_decoder_available && config->enable_opus_decode) {
    result = initialize_opus_decoder(new_decoder);
    if (result != VITARPS5_SUCCESS) {
      log_error("Opus decoder initialization failed, disabling Opus support");
      new_decoder->opus_decoder_available = false;
    }
  }

  // Initialize audio port
  result = initialize_audio_port(new_decoder);
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize audio port: %s",
              vitarps5_result_string(result));
    if (new_decoder->opus_decoder) {
      opus_decoder_destroy(new_decoder->opus_decoder);
    }
    audio_buffer_pool_cleanup(&new_decoder->buffer_pool);
    sceKernelDeleteMutex(new_decoder->queue_mutex);
    free(new_decoder);
    return result;
  }

  new_decoder->state = AUDIO_DECODER_STATE_READY;
  log_info("Created audio decoder (opus: %s, %dHz, %d channels, port: %d)",
           new_decoder->opus_decoder_available ? "enabled" : "disabled",
           config->sample_rate, config->channels, new_decoder->audio_port);

  *decoder = new_decoder;
  return VITARPS5_SUCCESS;
}

void audio_decoder_destroy(AudioDecoder* decoder) {
  if (!decoder) {
    return;
  }

  log_info("Destroying audio decoder");

  // Stop decoder if running
  if (decoder->state != AUDIO_DECODER_STATE_IDLE) {
    audio_decoder_stop(decoder);
  }

  // Close audio port
  if (decoder->audio_port_opened && decoder->audio_port >= 0) {
    // Placeholder: In actual implementation, this would release the audio port
    log_debug("Audio port %d released (placeholder)", decoder->audio_port);
    decoder->audio_port_opened = false;
  }

  // Cleanup Opus decoder
  if (decoder->opus_decoder) {
    opus_decoder_destroy(decoder->opus_decoder);
    decoder->opus_decoder = NULL;
  }

  // Cleanup buffer pool
  audio_buffer_pool_cleanup(&decoder->buffer_pool);

  // Delete mutex
  if (decoder->queue_mutex >= 0) {
    sceKernelDeleteMutex(decoder->queue_mutex);
  }

  free(decoder);
}

VitaRPS5Result audio_decoder_start(AudioDecoder* decoder) {
  if (!decoder) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (decoder->state != AUDIO_DECODER_STATE_READY) {
    return VITARPS5_ERROR_INVALID_STATE;
  }

  log_info("Starting audio decoder");

  // Start decode thread
  decoder->thread_running = true;
  decoder->decode_thread = sceKernelCreateThread(
      "audio_decode", audio_decode_thread, 0x10000100, 0x10000, 0, 0, NULL);
  if (decoder->decode_thread < 0) {
    log_error("Failed to create decode thread: 0x%08X", decoder->decode_thread);
    decoder->thread_running = false;
    return VITARPS5_ERROR_INIT;
  }

  sceKernelStartThread(decoder->decode_thread, sizeof(AudioDecoder*), &decoder);

  // Start playback thread
  decoder->playback_thread = sceKernelCreateThread(
      "audio_playback", audio_playback_thread, 0x10000100, 0x10000, 0, 0, NULL);
  if (decoder->playback_thread < 0) {
    log_error("Failed to create playback thread: 0x%08X",
              decoder->playback_thread);
    decoder->thread_running = false;
    sceKernelWaitThreadEnd(decoder->decode_thread, NULL, NULL);
    sceKernelDeleteThread(decoder->decode_thread);
    return VITARPS5_ERROR_INIT;
  }

  sceKernelStartThread(decoder->playback_thread, sizeof(AudioDecoder*),
                       &decoder);

  decoder->state = AUDIO_DECODER_STATE_DECODING;
  decoder->stats.opus_decoder_active = decoder->opus_decoder_available;

  return VITARPS5_SUCCESS;
}

VitaRPS5Result audio_decoder_stop(AudioDecoder* decoder) {
  if (!decoder) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (decoder->state == AUDIO_DECODER_STATE_IDLE) {
    return VITARPS5_SUCCESS;
  }

  log_info("Stopping audio decoder");

  // Stop threads
  if (decoder->thread_running) {
    decoder->thread_running = false;

    if (decoder->decode_thread >= 0) {
      sceKernelWaitThreadEnd(decoder->decode_thread, NULL, NULL);
      sceKernelDeleteThread(decoder->decode_thread);
    }

    if (decoder->playback_thread >= 0) {
      sceKernelWaitThreadEnd(decoder->playback_thread, NULL, NULL);
      sceKernelDeleteThread(decoder->playback_thread);
    }
  }

  // Flush decoder
  audio_decoder_flush(decoder);

  decoder->state = AUDIO_DECODER_STATE_READY;

  return VITARPS5_SUCCESS;
}

VitaRPS5Result audio_decoder_process_packet(AudioDecoder* decoder,
                                            const uint8_t* packet_data,
                                            size_t packet_size) {
  if (!decoder || !packet_data || packet_size == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Cast packet data to audio packet structure
  const TakionAudioPacket* packet = (const TakionAudioPacket*)packet_data;

  if (decoder->state != AUDIO_DECODER_STATE_DECODING) {
    return VITARPS5_ERROR_INVALID_STATE;
  }

  // Add packet to queue
  sceKernelLockMutex(decoder->queue_mutex, 1, NULL);

  if (decoder->queue_size >= 32) {
    // Queue full, drop oldest packet
    decoder->stats.frames_dropped++;
    decoder->queue_read_index = (decoder->queue_read_index + 1) % 32;
    decoder->queue_size--;
  }

  // Allocate memory for packet copy (use the provided packet_size)
  TakionAudioPacket* packet_copy = malloc(packet_size);
  if (packet_copy) {
    memcpy(packet_copy, packet_data, packet_size);
    decoder->packet_queue[decoder->queue_write_index] = packet_copy;
    decoder->queue_write_index = (decoder->queue_write_index + 1) % 32;
    decoder->queue_size++;
  }

  sceKernelUnlockMutex(decoder->queue_mutex, 1);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result audio_decoder_get_frame(AudioDecoder* decoder,
                                       AudioFrame** frame) {
  if (!decoder || !frame) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  AudioFrame* playback_frame =
      audio_buffer_pool_get_playback_buffer(&decoder->buffer_pool);
  if (!playback_frame) {
    *frame = NULL;
    return VITARPS5_SUCCESS;  // No frame available
  }

  *frame = playback_frame;
  return VITARPS5_SUCCESS;
}

VitaRPS5Result audio_decoder_return_frame(AudioDecoder* decoder,
                                          AudioFrame* frame) {
  if (!decoder || !frame) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  audio_buffer_pool_return_buffer(&decoder->buffer_pool, frame);
  return VITARPS5_SUCCESS;
}

AudioDecoderState audio_decoder_get_state(const AudioDecoder* decoder) {
  if (!decoder) {
    return AUDIO_DECODER_STATE_ERROR;
  }

  return decoder->state;
}

VitaRPS5Result audio_decoder_get_stats(const AudioDecoder* decoder,
                                       AudioDecoderStats* stats) {
  if (!decoder || !stats) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *stats = decoder->stats;
  return VITARPS5_SUCCESS;
}

VitaRPS5Result audio_decoder_flush(AudioDecoder* decoder) {
  if (!decoder) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Clear packet queue
  sceKernelLockMutex(decoder->queue_mutex, 1, NULL);

  for (uint32_t i = 0; i < decoder->queue_size; i++) {
    uint32_t index = (decoder->queue_read_index + i) % 32;
    if (decoder->packet_queue[index]) {
      free(decoder->packet_queue[index]);
      decoder->packet_queue[index] = NULL;
    }
  }

  decoder->queue_read_index = 0;
  decoder->queue_write_index = 0;
  decoder->queue_size = 0;

  sceKernelUnlockMutex(decoder->queue_mutex, 1);

  return VITARPS5_SUCCESS;
}

// Buffer Pool Implementation

VitaRPS5Result audio_buffer_pool_init(AudioBufferPool* pool,
                                      uint32_t sample_rate, uint32_t channels) {
  if (!pool) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(pool, 0, sizeof(AudioBufferPool));

  VitaRPS5Result result = allocate_audio_buffers(pool, sample_rate, channels);
  if (result != VITARPS5_SUCCESS) {
    return result;
  }

  pool->write_index = 0;
  pool->read_index = 0;
  pool->available_count = AUDIO_BUFFER_COUNT;
  pool->initialized = true;

  log_info("Audio buffer pool initialized: %dHz, %d channels, %d buffers",
           sample_rate, channels, AUDIO_BUFFER_COUNT);

  return VITARPS5_SUCCESS;
}

void audio_buffer_pool_cleanup(AudioBufferPool* pool) {
  if (!pool || !pool->initialized) {
    return;
  }

  free_audio_buffers(pool);
  memset(pool, 0, sizeof(AudioBufferPool));
}

AudioFrame* audio_buffer_pool_get_decode_buffer(AudioBufferPool* pool) {
  if (!pool || !pool->initialized || pool->available_count == 0) {
    return NULL;
  }

  AudioFrame* frame = &pool->frames[pool->write_index];
  pool->write_index = (pool->write_index + 1) % AUDIO_BUFFER_COUNT;
  pool->available_count--;

  return frame;
}

AudioFrame* audio_buffer_pool_get_playback_buffer(AudioBufferPool* pool) {
  if (!pool || !pool->initialized) {
    return NULL;
  }

  if (pool->available_count == AUDIO_BUFFER_COUNT) {
    // No frames ready for playback
    return NULL;
  }

  AudioFrame* frame = &pool->frames[pool->read_index];
  pool->read_index = (pool->read_index + 1) % AUDIO_BUFFER_COUNT;

  return frame;
}

void audio_buffer_pool_return_buffer(AudioBufferPool* pool, AudioFrame* frame) {
  if (!pool || !frame) {
    return;
  }

  pool->available_count++;
}

// Internal Functions

static int audio_decode_thread(SceSize args, void* argp) {
  AudioDecoder* decoder = *(AudioDecoder**)argp;

  log_info("Audio decode thread started");

  while (decoder->thread_running) {
    // Process packets from queue
    TakionAudioPacket* packet = NULL;

    sceKernelLockMutex(decoder->queue_mutex, 1, NULL);
    if (decoder->queue_size > 0) {
      packet = decoder->packet_queue[decoder->queue_read_index];
      decoder->queue_read_index = (decoder->queue_read_index + 1) % 32;
      decoder->queue_size--;
    }
    sceKernelUnlockMutex(decoder->queue_mutex, 1);

    if (packet) {
      uint64_t decode_start = get_timestamp_us();

      VitaRPS5Result result = VITARPS5_SUCCESS;
      if (decoder->opus_decoder_available) {
        result = decode_frame_opus(decoder, packet);
      } else {
        // PCM passthrough or other formats
        log_debug("Non-Opus audio decode not implemented yet");
        result = VITARPS5_ERROR_AUDIO;
      }

      uint64_t decode_time = get_timestamp_us() - decode_start;

      // Update statistics
      if (result == VITARPS5_SUCCESS) {
        decoder->stats.frames_decoded++;

        // Track decode time
        decoder->decode_time_samples[decoder->decode_time_index] =
            decode_time / 1000;  // Convert to ms
        decoder->decode_time_index = (decoder->decode_time_index + 1) % 10;

        // Calculate average decode time
        uint32_t total_time = 0;
        for (int i = 0; i < 10; i++) {
          total_time += decoder->decode_time_samples[i];
        }
        decoder->stats.average_decode_time_ms = total_time / 10.0f;
      } else {
        decoder->stats.decode_errors++;
        log_error("Audio frame decode failed: %s",
                  vitarps5_result_string(result));
      }

      free(packet);
    } else {
      // No packets to process, sleep briefly
      sceKernelDelayThread(5000);  // 5ms
    }
  }

  log_info("Audio decode thread ended");
  return 0;
}

static int audio_playback_thread(SceSize args, void* argp) {
  AudioDecoder* decoder = *(AudioDecoder**)argp;

  log_info("Audio playback thread started");

  while (decoder->thread_running) {
    // Get next frame for playback
    AudioFrame* frame = NULL;
    VitaRPS5Result result = audio_decoder_get_frame(decoder, &frame);

    if (result == VITARPS5_SUCCESS && frame) {
      // Placeholder: Output audio frame
      // In actual implementation, this would output to SceAudio/SceAudioOut
      int output_result = 0;  // Simulate successful output
      if (output_result < 0) {
        log_error("Audio output failed: 0x%08X", output_result);
      } else {
        decoder->stats.frames_decoded++;
      }

      // Return frame to pool
      audio_decoder_return_frame(decoder, frame);
    } else {
      // No frame available, sleep briefly
      sceKernelDelayThread(10000);  // 10ms
    }
  }

  log_info("Audio playback thread ended");
  return 0;
}

static VitaRPS5Result initialize_opus_decoder(AudioDecoder* decoder) {
  log_info("Initializing Opus decoder");

  int error;
  decoder->opus_decoder = opus_decoder_create(decoder->config.sample_rate,
                                              decoder->config.channels, &error);

  if (!decoder->opus_decoder || error != OPUS_OK) {
    log_error("Failed to create Opus decoder: %d", error);
    return VITARPS5_ERROR_AUDIO;
  }

  log_info("Opus decoder initialized successfully");
  return VITARPS5_SUCCESS;
}

static VitaRPS5Result initialize_audio_port(AudioDecoder* decoder) {
  log_info("Initializing SceAudio port");

  // Placeholder: Audio port initialization
  // In actual implementation, this would use SceAudioOut or SceAudio
  decoder->audio_port = 0;  // Placeholder port ID

  if (decoder->audio_port < 0) {
    log_error("Failed to open audio port: 0x%08X", decoder->audio_port);
    return VITARPS5_ERROR_AUDIO;
  }

  decoder->audio_port_opened = true;
  log_info(
      "Audio port %d initialized successfully (placeholder implementation)",
      decoder->audio_port);

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result decode_frame_opus(AudioDecoder* decoder,
                                        const TakionAudioPacket* packet) {
  if (!decoder->opus_decoder) {
    return VITARPS5_ERROR_INVALID_STATE;
  }

  // Get decode buffer
  AudioFrame* frame =
      audio_buffer_pool_get_decode_buffer(&decoder->buffer_pool);
  if (!frame) {
    decoder->stats.frames_dropped++;
    return VITARPS5_ERROR_MEMORY;
  }

  // Decode Opus frame to PCM
  int samples_decoded =
      opus_decode(decoder->opus_decoder, packet->audio_data,
                  packet->header.payload_size -
                      1024,  // TODO: Fix audio packet size calculation
                  (short*)frame->sample_buffer, AUDIO_SAMPLES_PER_FRAME,
                  0);  // No FEC

  if (samples_decoded < 0) {
    log_error("Opus decode failed: %d", samples_decoded);
    audio_buffer_pool_return_buffer(&decoder->buffer_pool, frame);
    return VITARPS5_ERROR_AUDIO;
  }

  // Update frame information
  frame->sample_count = samples_decoded;
  frame->sample_rate = decoder->config.sample_rate;
  frame->channels = decoder->config.channels;
  // TODO: Fix timestamp with new packet format
  frame->timestamp = 0;  // Temporary placeholder
  frame->format = AUDIO_FORMAT_PCM_S16LE;
  frame->is_silence = (samples_decoded == 0);

  // Frame is now ready for playback (automatically available via buffer pool)

  return VITARPS5_SUCCESS;
}

static VitaRPS5Result allocate_audio_buffers(AudioBufferPool* pool,
                                             uint32_t sample_rate,
                                             uint32_t channels) {
  size_t frame_size =
      audio_calculate_frame_size(sample_rate, channels, 20);  // 20ms frames

  for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
    AudioFrame* frame = &pool->frames[i];

    // Allocate sample buffer
    frame->sample_buffer = malloc(frame_size);
    if (!frame->sample_buffer) {
      log_error("Failed to allocate audio buffer %d", i);

      // Free previously allocated buffers
      for (int j = 0; j < i; j++) {
        free(pool->frames[j].sample_buffer);
      }
      return VITARPS5_ERROR_MEMORY;
    }

    frame->sample_rate = sample_rate;
    frame->channels = channels;
    frame->buffer_size = frame_size;
    frame->format = AUDIO_FORMAT_PCM_S16LE;

    log_debug("Allocated audio buffer %d: %p (%zu bytes)", i,
              frame->sample_buffer, frame_size);
  }

  log_info("Allocated %d audio buffers (%zu bytes each)", AUDIO_BUFFER_COUNT,
           frame_size);
  return VITARPS5_SUCCESS;
}

static void free_audio_buffers(AudioBufferPool* pool) {
  for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
    AudioFrame* frame = &pool->frames[i];
    if (frame->sample_buffer) {
      free(frame->sample_buffer);
      frame->sample_buffer = NULL;
    }
  }

  log_info("Freed all audio buffers");
}

static uint64_t get_timestamp_us(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick;
}

// Utility Functions

const char* audio_decoder_state_string(AudioDecoderState state) {
  switch (state) {
    case AUDIO_DECODER_STATE_IDLE:
      return "Idle";
    case AUDIO_DECODER_STATE_INITIALIZING:
      return "Initializing";
    case AUDIO_DECODER_STATE_READY:
      return "Ready";
    case AUDIO_DECODER_STATE_DECODING:
      return "Decoding";
    case AUDIO_DECODER_STATE_ERROR:
      return "Error";
    default:
      return "Unknown";
  }
}

const char* audio_format_string(AudioFormat format) {
  switch (format) {
    case AUDIO_FORMAT_OPUS:
      return "Opus";
    case AUDIO_FORMAT_PCM_S16LE:
      return "PCM 16-bit LE";
    case AUDIO_FORMAT_UNKNOWN:
    default:
      return "Unknown";
  }
}

size_t audio_calculate_frame_size(uint32_t sample_rate, uint32_t channels,
                                  uint32_t duration_ms) {
  // Calculate number of samples for given duration
  uint32_t samples = (sample_rate * duration_ms) / 1000;

  // 16-bit samples (2 bytes per sample)
  return samples * channels * sizeof(int16_t);
}

bool audio_opus_decoder_available(void) {
  // Note: Opus decoder is placeholder implementation for now
  // This function will return true when Opus library is properly integrated
  log_debug("Opus decoder check: using placeholder implementation");
  return true;  // Return true for placeholder testing
}