// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 RUDP Implementation - based on vitaki-fork RUDP protocol
//
// RUDP (Reliable UDP) Protocol Implementation for PS5 Remote Play
// This is used for "Remote Play over Internet" functionality and PSN
// authentication.

#ifndef CHIAKI_RUDP_H
#define CHIAKI_RUDP_H

#include <stdbool.h>
#include <stdint.h>

#include "chiaki_common.h"
#include "chiaki_log.h"
#include "chiaki_sock.h"
#include "vitaki_stoppipe.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Handle to RUDP session state */
typedef struct rudp_t *ChiakiRudp;
typedef struct rudp_message_t RudpMessage;

/** RUDP Packet Types for PS5 Remote Play Protocol */
typedef enum rudp_packet_type_t {
  RUDP_INIT_REQUEST = 0x8030,
  RUDP_INIT_RESPONSE = 0xD000,
  RUDP_COOKIE_REQUEST = 0x9030,
  RUDP_COOKIE_RESPONSE = 0xA030,
  RUDP_SESSION_MESSAGE = 0x2030,
  RUDP_STREAM_CONNECTION_SWITCH_ACK = 0x242E,
  RUDP_ACK = 0x2430,
  RUDP_CTRL_MESSAGE = 0x0230,
  RUDP_UNKNOWN = 0x022F,
  RUDP_OFFSET8 = 0x1230,
  RUDP_OFFSET10 = 0x2630,
  RUDP_FINISH = 0xC000,
} RudpPacketType;

/** RUDP Message Structure */
struct rudp_message_t {
  uint8_t subtype;
  RudpPacketType type;
  uint16_t size;
  uint8_t *data;
  size_t data_size;
  uint16_t remote_counter;
  RudpMessage *subMessage;
  uint16_t subMessage_size;
};

/**
 * Initialize RUDP instance
 *
 * @param sock Pointer to socket for RUDP communication
 * @param log Logger instance for debugging
 * @return Initialized RUDP instance or NULL on failure
 */
ChiakiRudp chiaki_rudp_init(chiaki_socket_t *sock, ChiakiLog *log);

/**
 * Reset RUDP counter and header (used for session restart)
 *
 * @param rudp RUDP instance to reset
 */
void chiaki_rudp_reset_counter_header(ChiakiRudp rudp);

/**
 * Get current local counter value
 *
 * @param rudp RUDP instance
 * @return Current local counter value
 */
uint16_t chiaki_rudp_get_local_counter(ChiakiRudp rudp);

/**
 * Send INIT message to start RUDP handshake
 *
 * @param rudp RUDP instance
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_send_init_message(ChiakiRudp rudp);

/**
 * Send COOKIE message after receiving INIT response
 *
 * @param rudp RUDP instance
 * @param response_buf Response data from INIT message
 * @param response_size Size of response data
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_send_cookie_message(ChiakiRudp rudp,
                                                uint8_t *response_buf,
                                                size_t response_size);

/**
 * Send SESSION message containing registration data
 *
 * @param rudp RUDP instance
 * @param remote_counter Remote counter value
 * @param session_msg Session message data
 * @param session_msg_size Size of session message
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_send_session_message(ChiakiRudp rudp,
                                                 uint16_t remote_counter,
                                                 uint8_t *session_msg,
                                                 size_t session_msg_size);

/**
 * Send ACK message to acknowledge received packet
 *
 * @param rudp RUDP instance
 * @param remote_counter Counter value to acknowledge
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_send_ack_message(ChiakiRudp rudp,
                                             uint16_t remote_counter);

/**
 * Send control message during streaming
 *
 * @param rudp RUDP instance
 * @param ctrl_message Control message data
 * @param ctrl_message_size Size of control message
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_send_ctrl_message(ChiakiRudp rudp,
                                              uint8_t *ctrl_message,
                                              size_t ctrl_message_size);

/**
 * Send switch to stream connection message
 *
 * @param rudp RUDP instance
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_send_switch_to_stream_connection_message(
    ChiakiRudp rudp);

/**
 * Send raw UDP packet
 *
 * @param rudp RUDP instance
 * @param buf Raw packet data
 * @param buf_size Size of packet data
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_send_raw(ChiakiRudp rudp, uint8_t *buf,
                                     size_t buf_size);

/**
 * Send message and wait for specific response type
 *
 * @param rudp RUDP instance
 * @param message Message structure to populate with response
 * @param buf Buffer to send (optional)
 * @param buf_size Size of buffer
 * @param remote_counter Remote counter value
 * @param send_type Type of message to send
 * @param recv_type Expected response type
 * @param min_data_size Minimum response data size
 * @param tries Number of retry attempts
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_send_recv(ChiakiRudp rudp, RudpMessage *message,
                                      uint8_t *buf, size_t buf_size,
                                      uint16_t remote_counter,
                                      RudpPacketType send_type,
                                      RudpPacketType recv_type,
                                      size_t min_data_size, size_t tries);

/**
 * Select and receive RUDP message
 *
 * @param rudp RUDP instance
 * @param buf_size Expected message size
 * @param message Output message structure
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_select_recv(ChiakiRudp rudp, size_t buf_size,
                                        RudpMessage *message);

/**
 * Receive RUDP message (must call select separately)
 *
 * @param rudp RUDP instance
 * @param buf_size Expected message size
 * @param message Output message structure
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_recv_only(ChiakiRudp rudp, size_t buf_size,
                                      RudpMessage *message);

/**
 * Select with timeout and stop pipe support
 *
 * @param rudp RUDP instance
 * @param stop_pipe Stop pipe for cancellation
 * @param timeout_ms Timeout in milliseconds
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_stop_pipe_select_single(ChiakiRudp rudp,
                                                    ChiakiStopPipe *stop_pipe,
                                                    uint64_t timeout_ms);

/**
 * Acknowledge received packet
 *
 * @param rudp RUDP instance
 * @param counter_to_ack Counter value to acknowledge
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_ack_packet(ChiakiRudp rudp,
                                       uint16_t counter_to_ack);

/**
 * Print RUDP message for debugging
 *
 * @param rudp RUDP instance
 * @param message Message to print
 */
void chiaki_rudp_print_message(ChiakiRudp rudp, RudpMessage *message);

/**
 * Free RUDP message memory
 *
 * @param message Message to free
 */
void chiaki_rudp_message_pointers_free(RudpMessage *message);

/**
 * Cleanup RUDP instance
 *
 * @param rudp RUDP instance to cleanup
 * @return CHIAKI_ERR_SUCCESS on success, error code otherwise
 */
ChiakiErrorCode chiaki_rudp_fini(ChiakiRudp rudp);

#ifdef __cplusplus
}
#endif

#endif  // CHIAKI_RUDP_H