// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 audio/video receiver implementation for PS5 Remote Play

#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"
#include "chiaki_streamconnection.h"  // Using proper header for types instead of stubs

// Packet statistics implementation
ChiakiErrorCode chiaki_packet_stats_init(ChiakiPacketStats *stats) {
  if (!stats) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  memset(stats, 0, sizeof(ChiakiPacketStats));
  log_debug("Packet stats initialized");
  return CHIAKI_ERR_SUCCESS;
}

void chiaki_packet_stats_fini(ChiakiPacketStats *stats) {
  // No cleanup needed for simple stats
  if (stats) {
    memset(stats, 0, sizeof(ChiakiPacketStats));
  }
  log_debug("Packet stats finalized");
}

// Audio receiver implementation
ChiakiAudioReceiver *chiaki_audio_receiver_new(
    void *session, ChiakiPacketStats *packet_stats) {
  ChiakiAudioReceiver *receiver = malloc(sizeof(ChiakiAudioReceiver));
  if (!receiver) {
    log_error("Failed to allocate audio receiver");
    return NULL;
  }

  receiver->session = session;
  receiver->packet_stats = packet_stats;

  log_info("Audio receiver created successfully");
  return receiver;
}

void chiaki_audio_receiver_free(ChiakiAudioReceiver *receiver) {
  if (!receiver) {
    return;
  }

  free(receiver);
  log_debug("Audio receiver freed");
}

void chiaki_audio_receiver_stream_info(ChiakiAudioReceiver *receiver,
                                       const ChiakiAudioHeader *header) {
  if (!receiver || !header) {
    log_error("Audio receiver stream info: Invalid parameters");
    return;
  }

  log_info("Audio stream info received:");
  log_info("  Sample rate: %u Hz", header->sample_rate);
  log_info("  Channels: %u", header->channels);
  log_info("  Bits per sample: %u", header->bits_per_sample);
  log_info("  Frame size: %u", header->frame_size);

  // TODO: Configure audio decoder based on stream info
  // TODO: Initialize PS Vita audio output
}

void chiaki_audio_receiver_av_packet(ChiakiAudioReceiver *receiver,
                                     ChiakiTakionAVPacket *packet) {
  if (!receiver || !packet || !packet->data) {
    log_error("Audio receiver AV packet: Invalid parameters");
    return;
  }

  if (!packet->is_video && !packet->is_haptics) {
    // This is an audio packet
    log_debug("Received audio packet: %zu bytes, key_pos=%llu",
              packet->data_size, packet->key_pos);

    // Update packet statistics
    if (receiver->packet_stats) {
      receiver->packet_stats->packets_received++;
      receiver->packet_stats->bytes_received += packet->data_size;
    }

    // TODO: Decrypt packet data using GKCrypt
    // TODO: Decode audio data (Opus)
    // TODO: Send to PS Vita audio output
  }
}

// Video receiver implementation
ChiakiVideoReceiver *chiaki_video_receiver_new(
    void *session, ChiakiPacketStats *packet_stats) {
  ChiakiVideoReceiver *receiver = malloc(sizeof(ChiakiVideoReceiver));
  if (!receiver) {
    log_error("Failed to allocate video receiver");
    return NULL;
  }

  receiver->session = session;
  receiver->packet_stats = packet_stats;

  // Initialize frame processor stats
  receiver->frame_processor.stream_stats.bytes_received = 0;
  receiver->frame_processor.stream_stats.frames_received = 0;

  log_info("Video receiver created successfully");
  return receiver;
}

void chiaki_video_receiver_free(ChiakiVideoReceiver *receiver) {
  if (!receiver) {
    return;
  }

  free(receiver);
  log_debug("Video receiver freed");
}

void chiaki_video_receiver_stream_info(ChiakiVideoReceiver *receiver,
                                       const ChiakiVideoProfile *profiles,
                                       size_t profile_count) {
  if (!receiver || !profiles || profile_count == 0) {
    log_error("Video receiver stream info: Invalid parameters");
    return;
  }

  log_info("Video stream info received (%zu profiles):", profile_count);
  for (size_t i = 0; i < profile_count; i++) {
    log_info("  Profile %zu: %ux%u, header_sz=%zu", i, profiles[i].width,
             profiles[i].height, profiles[i].header_sz);
  }

  // TODO: Select best profile for PS Vita capabilities
  // TODO: Initialize hardware video decoder
}

void chiaki_video_receiver_av_packet(ChiakiVideoReceiver *receiver,
                                     ChiakiTakionAVPacket *packet) {
  if (!receiver || !packet || !packet->data) {
    log_error("Video receiver AV packet: Invalid parameters");
    return;
  }

  if (packet->is_video && !packet->is_haptics) {
    // This is a video packet
    log_debug("Received video packet: %zu bytes, key_pos=%llu",
              packet->data_size, packet->key_pos);

    // Update packet statistics
    if (receiver->packet_stats) {
      receiver->packet_stats->packets_received++;
      receiver->packet_stats->bytes_received += packet->data_size;
    }

    // Update frame processor stats
    receiver->frame_processor.stream_stats.bytes_received += packet->data_size;
    receiver->frame_processor.stream_stats.frames_received++;

    // TODO: Decrypt packet data using GKCrypt
    // TODO: Decode video data (H.264/H.265)
    // TODO: Send to video renderer for display
  }
}

// Feedback sender implementation
ChiakiErrorCode chiaki_feedback_sender_init(ChiakiFeedbackSender *sender,
                                            ChiakiTakion *takion) {
  if (!sender || !takion) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  sender->takion = takion;
  sender->active = false;

  log_info("Feedback sender initialized");
  return CHIAKI_ERR_SUCCESS;
}

void chiaki_feedback_sender_fini(ChiakiFeedbackSender *sender) {
  if (!sender) {
    return;
  }

  sender->active = false;
  sender->takion = NULL;
  log_debug("Feedback sender finalized");
}

void chiaki_feedback_sender_set_controller_state(ChiakiFeedbackSender *sender,
                                                 void *controller_state) {
  if (!sender || !controller_state) {
    log_error("Feedback sender: Invalid parameters");
    return;
  }

  if (!sender->active) {
    log_debug("Feedback sender not active, ignoring controller state");
    return;
  }

  log_debug("Controller state updated");
  // TODO: Convert PS Vita input to PS5 controller format
  // TODO: Send input packet via Takion connection
}

// Congestion control implementation
ChiakiErrorCode chiaki_congestion_control_start(
    ChiakiCongestionControl *control, ChiakiTakion *takion,
    ChiakiPacketStats *packet_stats) {
  if (!control || !takion || !packet_stats) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  control->takion = takion;
  control->packet_stats = packet_stats;

  log_info("Congestion control started");
  return CHIAKI_ERR_SUCCESS;
}

void chiaki_congestion_control_stop(ChiakiCongestionControl *control) {
  if (!control) {
    return;
  }

  control->takion = NULL;
  control->packet_stats = NULL;
  log_debug("Congestion control stopped");
}

// Stream statistics stub
double chiaki_stream_stats_bitrate(void *stats, uint32_t fps) {
  // Return estimated bitrate based on current conditions
  (void)stats;
  return fps * 100.0;  // Simple estimate: 100 Kbps per FPS
}

void chiaki_stream_stats_reset(void *stats) {
  // Reset stream statistics
  (void)stats;
  log_debug("Stream stats reset");
}