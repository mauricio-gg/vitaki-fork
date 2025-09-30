#ifndef VITARPS5_TAKION_H
#define VITARPS5_TAKION_H

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdbool.h>
#include <stdint.h>

// PS Vita network constants (if not defined in headers)
#ifndef SCE_NET_MSG_DONTWAIT
#define SCE_NET_MSG_DONTWAIT 0x0040
#endif
#include "../chiaki/chiaki_common.h"
#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// Takion Protocol Constants (vitaki-fork compatible format)
// CRITICAL: Real Takion protocol format is
// [packet_type_byte][message_header_16_bytes][payload]

// Protocol constants from vitaki-fork
#define TAKION_A_RWND 0x19000  // Advertised receiver window
// Use conservative stream counts to match common implementations
#define TAKION_OUTBOUND_STREAMS 0x10     // Outbound stream count (16)
#define TAKION_INBOUND_STREAMS 0x10      // Inbound stream count (16)
#define TAKION_MESSAGE_HEADER_SIZE 0x10  // Message header size (16 bytes)
#define TAKION_COOKIE_SIZE 0x20          // Cookie size (32 bytes)
#define TAKION_EXPECT_TIMEOUT_MS \
  5000  // Handshake timeout per step (matches vitaki-fork)
#define TAKION_PACKET_BASE_TYPE_MASK 0xf  // Packet type mask

// GMAC constant is now defined in chiaki_common.h

// Buffer and queue management
#define TAKION_REORDER_QUEUE_SIZE_EXP 4  // Reorder queue size
#define TAKION_SEND_BUFFER_SIZE 16       // Send buffer entries
#define TAKION_POSTPONE_PACKETS_SIZE 32  // Postponed packets

// Version constants
#define TAKION_VERSION_PS4 7
#define TAKION_VERSION_PS4_PRO 9
#define TAKION_VERSION_PS5 12
#define TAKION_MTU_SIZE 1500
#define TAKION_MAX_PAYLOAD 1200

// Port constants from TakionConfig (vitaki-fork approach)
// PS5 Remote Play port configuration (protocol specification compliant)
#define TAKION_CONTROL_PORT 9295       // TCP: Session control/HTTP session init
#define TAKION_STREAM_PORT_VIDEO 9296  // UDP: Video streaming data
#define TAKION_STREAM_PORT_AUDIO 9297  // UDP: Audio/haptic streaming data

// Legacy compatibility
#define TAKION_STREAM_PORT TAKION_STREAM_PORT_VIDEO

// Port fallback range (as documented in protocol analysis)
#define TAKION_PORT_RANGE_START 9295
#define TAKION_PORT_RANGE_END 9304
#define TAKION_PORT_RANGE_SIZE \
  (TAKION_PORT_RANGE_END - TAKION_PORT_RANGE_START + 1)

// Base Packet Types (first byte of packet)
typedef enum {
  TAKION_PACKET_TYPE_CONTROL = 0,     // Control messages
  TAKION_PACKET_TYPE_VIDEO = 2,       // Video data
  TAKION_PACKET_TYPE_AUDIO = 3,       // Audio data
  TAKION_PACKET_TYPE_FEEDBACK = 4,    // Feedback/ACK
  TAKION_PACKET_TYPE_CONGESTION = 5,  // Congestion control
  TAKION_PACKET_TYPE_DISCONNECT = 6   // Disconnect
} TakionPacketType;

// Chunk Types (inside control message headers)
typedef enum {
  TAKION_CHUNK_TYPE_DATA = 0,
  TAKION_CHUNK_TYPE_INIT = 1,
  TAKION_CHUNK_TYPE_INIT_ACK = 2,
  TAKION_CHUNK_TYPE_DATA_ACK = 3,
  TAKION_CHUNK_TYPE_COOKIE = 0xa,
  TAKION_CHUNK_TYPE_COOKIE_ACK = 0xb
} TakionChunkType;

// Message Header Structure (16 bytes - comes after packet type byte)
typedef struct __attribute__((packed)) {
  uint32_t tag;           // Message tag (network byte order)
  uint32_t gmac;          // GMAC authentication (4 bytes)
  uint32_t key_pos;       // Key position (network byte order)
  uint8_t chunk_type;     // TakionChunkType
  uint8_t chunk_flags;    // Chunk flags
  uint16_t payload_size;  // Payload size + 4 (network byte order)
} TakionMessageHeader;

// INIT Message Payload (16 bytes)
typedef struct __attribute__((packed)) {
  uint32_t tag;               // Our local tag
  uint32_t a_rwnd;            // Advertised receiver window (0x19000)
  uint16_t outbound_streams;  // Outbound stream count (0x64)
  uint16_t inbound_streams;   // Inbound stream count (0x64)
  uint32_t initial_seq_num;   // Initial sequence number (our tag)
} TakionInitPayload;

// INIT_ACK Message Payload (16 bytes)
typedef struct __attribute__((packed)) {
  uint32_t tag;               // Remote tag
  uint32_t a_rwnd;            // Remote advertised receiver window
  uint16_t outbound_streams;  // Remote outbound stream count
  uint16_t inbound_streams;   // Remote inbound stream count
  uint32_t initial_seq_num;   // Remote initial sequence number
} TakionInitAckPayload;

// COOKIE Message Payload (32 bytes)
typedef struct __attribute__((packed)) {
  uint8_t cookie[TAKION_COOKIE_SIZE];  // Cookie data
} TakionCookiePayload;

// Input Packet Structure (PS Vita to PlayStation mapping)
typedef struct __attribute__((packed)) {
  uint8_t packet_type;  // TAKION_PACKET_TYPE_CONTROL
  TakionMessageHeader header;
  uint32_t buttons;         // Button state bitmask
  int16_t left_stick_x;     // Left analog X (-32768 to 32767)
  int16_t left_stick_y;     // Left analog Y
  int16_t right_stick_x;    // Right analog X
  int16_t right_stick_y;    // Right analog Y
  uint8_t left_trigger;     // L2 pressure (0-255)
  uint8_t right_trigger;    // R2 pressure (0-255)
  uint16_t touchpad_x;      // Touchpad X coordinate
  uint16_t touchpad_y;      // Touchpad Y coordinate
  uint8_t touchpad_active;  // Touchpad pressed
  uint8_t reserved[3];      // Padding
} TakionInputPacket;

// Video Packet Structure (PlayStation to PS Vita)
typedef struct __attribute__((packed)) {
  uint8_t packet_type;  // TAKION_PACKET_TYPE_VIDEO
  TakionMessageHeader header;
  uint32_t frame_sequence;  // Frame sequence number
  uint8_t codec_type;       // Video codec (H.264/H.265)
  uint8_t fragment_index;   // Fragment index for large frames
  uint8_t fragment_total;   // Total fragments for this frame
  uint8_t frame_flags;      // Frame type flags (I-frame, P-frame, etc.)
  uint8_t frame_data[];     // Variable length video data
} TakionVideoPacket;

// Audio Packet Structure (PlayStation to PS Vita)
typedef struct __attribute__((packed)) {
  uint8_t packet_type;  // TAKION_PACKET_TYPE_AUDIO
  TakionMessageHeader header;
  uint32_t audio_sequence;  // Audio sequence number
  uint8_t codec_type;       // Audio codec (AAC, etc.)
  uint8_t channels;         // Number of audio channels
  uint16_t sample_rate;     // Audio sample rate
  uint32_t timestamp;       // Audio timestamp
  uint8_t audio_data[];     // Variable length audio data
} TakionAudioPacket;

// Connection State
typedef enum {
  TAKION_STATE_IDLE = 0,
  TAKION_STATE_CONNECTING,
  TAKION_STATE_INIT_SENT,
  TAKION_STATE_INIT_ACK_RECEIVED,
  TAKION_STATE_COOKIE_SENT,
  TAKION_STATE_COOKIE_ACK_RECEIVED,
  TAKION_STATE_CONNECTED,
  TAKION_STATE_DISCONNECTING,
  TAKION_STATE_ERROR
} TakionState;

// Forward declarations
typedef struct TakionConnection TakionConnection;

// Callback functions
typedef void (*TakionStateCallback)(TakionState state, void* user_data);
// TODO: Implement proper video/audio packet structures for callbacks
// For now, use generic packet callbacks
typedef void (*TakionVideoCallback)(const uint8_t* data, size_t size,
                                    void* user_data);
typedef void (*TakionAudioCallback)(const uint8_t* data, size_t size,
                                    void* user_data);

// Connection configuration
typedef struct {
  char remote_ip[16];       // PlayStation console IP
  uint16_t control_port;    // Control port (9295 for TCP session control)
  uint16_t video_port;      // Video stream port (9296 for UDP video)
  uint16_t audio_port;      // Audio stream port (9297 for UDP audio/haptics)
  uint8_t console_version;  // Protocol version (7, 9, 12)
  uint32_t timeout_ms;      // Connection timeout

  // Protocol flow options
  bool enable_session_init;    // Use HTTP session init before Takion (PS5 only)
  bool enable_port_fallback;   // Try fallback ports if primary fails
  bool separate_audio_stream;  // Use separate audio stream (9297)

  // Legacy compatibility
  uint16_t stream_port;  // Legacy: maps to video_port for compatibility

  // Callbacks
  TakionStateCallback state_callback;
  TakionVideoCallback video_callback;
  TakionAudioCallback audio_callback;
  void* user_data;
} TakionConfig;

// Connection statistics
typedef struct {
  uint32_t packets_sent;
  uint32_t packets_received;
  uint32_t packets_lost;
  uint32_t bytes_sent;
  uint32_t bytes_received;
  float rtt_ms;
  float packet_loss_rate;
  TakionState current_state;
} TakionStats;

// Core Takion API

/**
 * Initialize Takion networking subsystem
 */
VitaRPS5Result takion_init(void);

/**
 * Cleanup Takion networking subsystem
 */
void takion_cleanup(void);

/**
 * Create a new Takion connection
 */
VitaRPS5Result takion_connection_create(const TakionConfig* config,
                                        TakionConnection** connection);

/**
 * Destroy a Takion connection
 */
void takion_connection_destroy(TakionConnection* connection);

/**
 * Connect to PlayStation console
 */
VitaRPS5Result takion_connect(TakionConnection* connection);

/**
 * Disconnect from PlayStation console
 */
VitaRPS5Result takion_disconnect(TakionConnection* connection);

/**
 * Update connection (process packets, handle timeouts)
 */
VitaRPS5Result takion_update(TakionConnection* connection);

/**
 * Send input packet to PlayStation console
 * TODO: Implement proper input packet structure
 */
VitaRPS5Result takion_send_input(TakionConnection* connection,
                                 const uint8_t* input_data, size_t input_size);

/**
 * Get connection state
 */
TakionState takion_get_state(const TakionConnection* connection);

/**
 * Get connection statistics
 */
VitaRPS5Result takion_get_stats(const TakionConnection* connection,
                                TakionStats* stats);

/**
 * Send raw packet (for testing)
 */
VitaRPS5Result takion_send_packet(TakionConnection* connection,
                                  const void* packet, size_t size);
// Send a Takion DATA chunk (control packet with header + payload) over the
// stream socket. Used for BIG/BANG/STREAMINFO protobuf messages.
VitaRPS5Result takion_send_data_chunk(TakionConnection* connection,
                                      const void* payload, size_t payload_size);

/**
 * Send packet on stream socket (for BIG/BANG/STREAMINFO protocol)
 */
VitaRPS5Result takion_send_stream_packet(TakionConnection* connection,
                                         const void* packet, size_t size);

/**
 * Set data callback for streamconnection (CRITICAL for BANG/STREAMINFO)
 */
VitaRPS5Result takion_set_data_callback(
    TakionConnection* connection,
    void (*callback)(void* user, int data_type, uint8_t* buf, size_t buf_size),
    void* user);

/**
 * Convert Takion state to string
 */
const char* takion_state_string(TakionState state);

/**
 * Helper functions for packet construction (vitaki-fork compatible)
 */

/**
 * Write message header in vitaki-fork format
 */
void takion_write_message_header(uint8_t* buf, uint32_t tag, uint32_t key_pos,
                                 uint8_t chunk_type, uint8_t chunk_flags,
                                 size_t payload_data_size);

/**
 * Send INIT message with correct format
 */
VitaRPS5Result takion_send_message_init(TakionConnection* connection);

/**
 * Send COOKIE message with correct format
 */
VitaRPS5Result takion_send_message_cookie(TakionConnection* connection,
                                          const uint8_t* cookie_data);

/**
 * Parse and validate message header
 */
VitaRPS5Result takion_parse_message_header(const uint8_t* buf, size_t buf_size,
                                           TakionMessageHeader* header,
                                           uint8_t** payload_out,
                                           size_t* payload_size_out);

/**
 * Send feedback state packet (motion/analog data)
 */
VitaRPS5Result takion_send_feedback_state(TakionConnection* connection,
                                          const uint8_t* state_data,
                                          size_t state_size);

/**
 * Send feedback history packet (button/touch events)
 */
VitaRPS5Result takion_send_feedback_history(TakionConnection* connection,
                                            const uint8_t* history_data,
                                            size_t history_size);

// Port Management and Fallback Utilities

/**
 * Create default Takion configuration for PS5
 */
void takion_config_init_ps5(TakionConfig* config, const char* console_ip);

/**
 * Create default Takion configuration for PS4
 */
void takion_config_init_ps4(TakionConfig* config, const char* console_ip);

/**
 * Get next fallback port in range
 */
uint16_t takion_get_fallback_port(uint16_t base_port, int attempt);

/**
 * Check if port is in valid Takion range
 */
bool takion_is_valid_port(uint16_t port);

/**
 * Get recommended ports for console type
 */
void takion_get_recommended_ports(uint8_t console_version,
                                  uint16_t* control_port, uint16_t* video_port,
                                  uint16_t* audio_port);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_TAKION_H
