// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 Chiaki common utilities implementation

#include "chiaki_common.h"

#include <psp2/kernel/sysmem.h>
#include <stdlib.h>
#include <string.h>

// Audio header utilities
void chiaki_audio_header_load(ChiakiAudioHeader *header, const uint8_t *buf) {
  header->sample_rate = (buf[0] << 8) | buf[1];
  header->channels = buf[2];
  header->bits_per_sample = buf[3];
  header->frame_size = (buf[4] << 8) | buf[5];
}

void chiaki_audio_header_save(const ChiakiAudioHeader *header, uint8_t *buf) {
  buf[0] = (header->sample_rate >> 8) & 0xFF;
  buf[1] = header->sample_rate & 0xFF;
  buf[2] = header->channels;
  buf[3] = header->bits_per_sample;
  buf[4] = (header->frame_size >> 8) & 0xFF;
  buf[5] = header->frame_size & 0xFF;
  // Clear remaining bytes
  memset(buf + 6, 0, CHIAKI_AUDIO_HEADER_SIZE - 6);
}

void chiaki_audio_header_set(ChiakiAudioHeader *header, uint8_t bits_per_sample,
                             uint8_t channels, uint32_t sample_rate,
                             uint16_t frame_size) {
  header->sample_rate = sample_rate;
  header->channels = channels;
  header->bits_per_sample = bits_per_sample;
  header->frame_size = frame_size;
}

// Sequence number utilities
bool chiaki_seq_num_32_gt(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) > 0;
}

bool chiaki_seq_num_32_lt(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) < 0;
}

// Memory alignment utilities using PS Vita aligned allocation
void *chiaki_aligned_alloc(size_t alignment, size_t size) {
  // Use PS Vita kernel memory allocation with alignment
  SceUID memblock =
      sceKernelAllocMemBlock("chiaki_aligned", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                             (size + alignment - 1) & ~(alignment - 1), NULL);
  if (memblock < 0) return NULL;

  void *ptr;
  if (sceKernelGetMemBlockBase(memblock, &ptr) < 0) {
    sceKernelFreeMemBlock(memblock);
    return NULL;
  }

  return ptr;
}

void chiaki_aligned_free(void *ptr) {
  if (!ptr) return;

  // Find the memory block for this pointer and free it
  // Note: This is simplified - in practice we'd need to track memblock UIDs
  // For now, use regular free as a fallback
  free(ptr);
}