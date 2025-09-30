// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 RUDP Implementation - based on vitaki-fork RUDP protocol
//
// RUDP (Reliable UDP) Protocol Implementation for PS5 Remote Play
// This provides reliable UDP communication for internet remote play
// functionality.

#include "chiaki_rudp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chiaki_random.h"
#include "vitaki_stoppipe.h"
#include "vitaki_thread.h"

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#define htons sceNetHtons
#define ntohs sceNetNtohs
#define htonl sceNetHtonl
#define ntohl sceNetNtohl
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// RUDP Protocol Constants
#define RUDP_CONSTANT 0x244F244F
#define RUDP_SEND_BUFFER_SIZE 16
#define RUDP_EXPECT_TIMEOUT_MS 1000

// RUDP Internal Structure
typedef struct rudp_t {
  uint16_t counter;
  uint32_t header;
  ChiakiMutex counter_mutex;
  ChiakiStopPipe stop_pipe;
  chiaki_socket_t sock;
  ChiakiLog *log;
  // TODO: Add send buffer when needed
  // ChiakiRudpSendBuffer send_buffer;
} RudpInstance;

// Internal function declarations
static uint16_t get_then_increase_counter(RudpInstance *rudp);
static ChiakiErrorCode chiaki_rudp_message_parse(uint8_t *serialized_msg,
                                                 size_t msg_size,
                                                 RudpMessage *message);
static void rudp_message_serialize(RudpMessage *message,
                                   uint8_t *serialized_msg, size_t *msg_size);
static void print_rudp_message_type(RudpInstance *rudp, RudpPacketType type);
static bool assign_submessage_to_message(RudpMessage *message);

// Utility function to get and increment counter thread-safely
static uint16_t get_then_increase_counter(RudpInstance *rudp) {
  if (!rudp) return 0;

  chiaki_mutex_lock(&rudp->counter_mutex);
  uint16_t current = rudp->counter;
  rudp->counter++;
  chiaki_mutex_unlock(&rudp->counter_mutex);

  return current;
}

// Print RUDP message type for debugging
static void print_rudp_message_type(RudpInstance *rudp, RudpPacketType type) {
  if (!rudp || !rudp->log) return;

  const char *type_str;
  switch (type) {
    case RUDP_INIT_REQUEST:
      type_str = "INIT_REQUEST";
      break;
    case RUDP_INIT_RESPONSE:
      type_str = "INIT_RESPONSE";
      break;
    case RUDP_COOKIE_REQUEST:
      type_str = "COOKIE_REQUEST";
      break;
    case RUDP_COOKIE_RESPONSE:
      type_str = "COOKIE_RESPONSE";
      break;
    case RUDP_SESSION_MESSAGE:
      type_str = "SESSION_MESSAGE";
      break;
    case RUDP_STREAM_CONNECTION_SWITCH_ACK:
      type_str = "STREAM_CONNECTION_SWITCH_ACK";
      break;
    case RUDP_ACK:
      type_str = "ACK";
      break;
    case RUDP_CTRL_MESSAGE:
      type_str = "CTRL_MESSAGE";
      break;
    case RUDP_FINISH:
      type_str = "FINISH";
      break;
    default:
      type_str = "UNKNOWN";
      break;
  }

  CHIAKI_LOGD(rudp->log, "RUDP: Message type: %s (0x%04X)", type_str, type);
}

// Serialize RUDP message to byte array
static void rudp_message_serialize(RudpMessage *message,
                                   uint8_t *serialized_msg, size_t *msg_size) {
  if (!message || !serialized_msg || !msg_size) return;

  size_t offset = 0;

  // Write RUDP constant
  *(uint32_t *)(serialized_msg + offset) = htonl(RUDP_CONSTANT);
  offset += 4;

  // Write message type
  *(uint16_t *)(serialized_msg + offset) = htons(message->type);
  offset += 2;

  // Write message size
  *(uint16_t *)(serialized_msg + offset) = htons(message->size);
  offset += 2;

  // Write message data
  if (message->data && message->data_size > 0) {
    memcpy(serialized_msg + offset, message->data, message->data_size);
    offset += message->data_size;
  }

  *msg_size = offset;
}

// Parse RUDP message from byte array (based on vitaki-fork implementation)
static ChiakiErrorCode chiaki_rudp_message_parse(uint8_t *serialized_msg,
                                                 size_t msg_size,
                                                 RudpMessage *message) {
  if (!serialized_msg || !message || msg_size < 8) {
    return CHIAKI_ERR_INVALID_DATA;
  }

  ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
  message->data = NULL;
  message->subMessage = NULL;
  message->subMessage_size = 0;
  message->data_size = 0;

  // Parse message header
  message->size = ntohs(*(uint16_t *)(serialized_msg));
  message->type = ntohs(*(uint16_t *)(serialized_msg + 6));
  message->subtype = serialized_msg[6] & 0xFF;

  // Eliminate 0xC before length (size of header + data but not submessage)
  serialized_msg[0] = serialized_msg[0] & 0x0F;
  message->remote_counter = 0;
  uint16_t length = ntohs(*(uint16_t *)(serialized_msg));

  int remaining = msg_size - 8;
  int data_size = 0;

  if (length > 8) {
    data_size = length - 8;
    if (remaining < data_size) {
      data_size = remaining;
    }
    message->data_size = data_size;
    message->data = malloc(message->data_size);
    if (!message->data) {
      return CHIAKI_ERR_MEMORY;
    }
    memcpy(message->data, serialized_msg + 8, data_size);

    if (data_size >= 2) {
      message->remote_counter = ntohs(*(uint16_t *)(message->data)) + 1;
    }
  }

  remaining = remaining - data_size;
  if (remaining >= 8) {
    message->subMessage = malloc(sizeof(RudpMessage));
    if (!message->subMessage) {
      if (message->data) {
        free(message->data);
        message->data = NULL;
      }
      return CHIAKI_ERR_MEMORY;
    }
    message->subMessage_size = remaining;
    err = chiaki_rudp_message_parse(serialized_msg + 8 + data_size, remaining,
                                    message->subMessage);
    if (err != CHIAKI_ERR_SUCCESS) {
      if (message->data) {
        free(message->data);
        message->data = NULL;
      }
      free(message->subMessage);
      message->subMessage = NULL;
      return err;
    }
  }

  return err;
}

// RUDP Public API Implementation

ChiakiRudp chiaki_rudp_init(chiaki_socket_t *sock, ChiakiLog *log) {
  if (!sock || !log) return NULL;

  RudpInstance *rudp = calloc(1, sizeof(RudpInstance));
  if (!rudp) {
    CHIAKI_LOGE(log, "RUDP: Failed to allocate memory for RUDP instance");
    return NULL;
  }

  rudp->log = log;

  // Handle invalid socket for PS Vita initialization
  if (sock && *sock != CHIAKI_INVALID_SOCKET) {
    rudp->sock = *sock;
  } else {
    rudp->sock = CHIAKI_INVALID_SOCKET;
    CHIAKI_LOGW(log,
                "RUDP: Initializing with invalid socket - will be set later");
  }

  // Initialize counter mutex
  ChiakiErrorCode err = chiaki_mutex_init(&rudp->counter_mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(log, "RUDP: Failed to initialize counter mutex");
    free(rudp);
    return NULL;
  }

  // Initialize stop pipe
  err = chiaki_stop_pipe_init(&rudp->stop_pipe);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(log, "RUDP: Failed to initialize stop pipe");
    chiaki_mutex_fini(&rudp->counter_mutex);
    free(rudp);
    return NULL;
  }

  // Initialize counter and header
  chiaki_rudp_reset_counter_header(rudp);

  CHIAKI_LOGI(log, "RUDP: Initialized successfully");
  return rudp;
}

void chiaki_rudp_reset_counter_header(ChiakiRudp rudp) {
  if (!rudp) return;

  RudpInstance *instance = (RudpInstance *)rudp;

  chiaki_mutex_lock(&instance->counter_mutex);
  instance->counter = chiaki_random_32() % 0x5E00 + 0x1FF;
  chiaki_mutex_unlock(&instance->counter_mutex);

  instance->header = chiaki_random_32() + 0x8000;

  CHIAKI_LOGD(instance->log, "RUDP: Reset counter to %d, header to 0x%08X",
              instance->counter, instance->header);
}

uint16_t chiaki_rudp_get_local_counter(ChiakiRudp rudp) {
  if (!rudp) return 0;

  RudpInstance *instance = (RudpInstance *)rudp;
  chiaki_mutex_lock(&instance->counter_mutex);
  uint16_t counter = instance->counter;
  chiaki_mutex_unlock(&instance->counter_mutex);

  return counter;
}

ChiakiErrorCode chiaki_rudp_send_init_message(ChiakiRudp rudp) {
  if (!rudp) return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGD(instance->log, "RUDP: Sending INIT message");

  RudpMessage message;
  uint16_t local_counter = get_then_increase_counter(instance);

  message.type = RUDP_INIT_REQUEST;
  message.subMessage = NULL;
  message.data_size = 14;

  uint8_t data[14];
  size_t alloc_size = 8 + message.data_size;

  message.size = (0xC << 12) | alloc_size;

  // Build INIT message data
  const uint8_t after_header[2] = {0x05, 0x82};
  const uint8_t after_counter[6] = {0x0B, 0x01, 0x01, 0x00, 0x01, 0x00};

  *(uint16_t *)(data) = htons(local_counter);
  memcpy(data + 2, after_counter, sizeof(after_counter));
  *(uint32_t *)(data + 8) = htonl(instance->header);
  memcpy(data + 12, after_header, sizeof(after_header));

  message.data = data;

  uint8_t *serialized_msg = malloc(alloc_size);
  if (!serialized_msg) {
    CHIAKI_LOGE(instance->log,
                "RUDP: Failed to allocate memory for INIT message");
    return CHIAKI_ERR_MEMORY;
  }

  size_t msg_size = 0;
  rudp_message_serialize(&message, serialized_msg, &msg_size);

  ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
  free(serialized_msg);

  if (err == CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGD(instance->log, "RUDP: INIT message sent successfully");
  } else {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to send INIT message: %d", err);
  }

  return err;
}

ChiakiErrorCode chiaki_rudp_send_raw(ChiakiRudp rudp, uint8_t *buf,
                                     size_t buf_size) {
  if (!rudp || !buf || buf_size == 0) return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGD(instance->log, "RUDP: Sending raw packet (%zu bytes)", buf_size);

#ifdef __vita__
  int sent = sceNetSend(instance->sock, buf, buf_size, 0);
#else
  int sent = send(instance->sock, buf, buf_size, 0);
#endif

  if (sent < 0) {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to send raw packet");
    return CHIAKI_ERR_NETWORK;
  }

  if ((size_t)sent != buf_size) {
    CHIAKI_LOGW(instance->log, "RUDP: Partial send (%d/%zu bytes)", sent,
                buf_size);
    return CHIAKI_ERR_NETWORK;
  }

  return CHIAKI_ERR_SUCCESS;
}

// Stub implementations for now (to be completed in subsequent phases)
ChiakiErrorCode chiaki_rudp_send_cookie_message(ChiakiRudp rudp,
                                                uint8_t *response_buf,
                                                size_t response_size) {
  if (!rudp || !response_buf || response_size == 0)
    return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGD(instance->log,
              "RUDP: Sending COOKIE message (%zu bytes response)",
              response_size);

  RudpMessage message;
  uint16_t local_counter = get_then_increase_counter(instance);

  message.type = RUDP_COOKIE_REQUEST;
  message.subMessage = NULL;
  message.data_size = 14 + response_size;

  size_t alloc_size = 8 + message.data_size;
  uint8_t *serialized_msg = malloc(alloc_size);
  if (!serialized_msg) {
    CHIAKI_LOGE(instance->log,
                "RUDP: Failed to allocate memory for COOKIE message");
    return CHIAKI_ERR_MEMORY;
  }

  message.size = (0xC << 12) | alloc_size;

  uint8_t *data = malloc(message.data_size);
  if (!data) {
    free(serialized_msg);
    CHIAKI_LOGE(instance->log,
                "RUDP: Failed to allocate memory for COOKIE data");
    return CHIAKI_ERR_MEMORY;
  }

  // Build COOKIE message data (same structure as INIT + response data)
  const uint8_t after_header[2] = {0x05, 0x82};
  const uint8_t after_counter[6] = {0x0B, 0x01, 0x01, 0x00, 0x01, 0x00};

  *(uint16_t *)(data) = htons(local_counter);
  memcpy(data + 2, after_counter, sizeof(after_counter));
  *(uint32_t *)(data + 8) = htonl(instance->header);
  memcpy(data + 12, after_header, sizeof(after_header));
  memcpy(data + 14, response_buf, response_size);

  message.data = data;

  size_t msg_size = 0;
  rudp_message_serialize(&message, serialized_msg, &msg_size);

  ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);

  free(data);
  free(serialized_msg);

  if (err == CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGD(instance->log, "RUDP: COOKIE message sent successfully");
  } else {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to send COOKIE message: %d", err);
  }

  return err;
}

ChiakiErrorCode chiaki_rudp_send_session_message(ChiakiRudp rudp,
                                                 uint16_t remote_counter,
                                                 uint8_t *session_msg,
                                                 size_t session_msg_size) {
  if (!rudp || !session_msg || session_msg_size == 0)
    return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGD(instance->log,
              "RUDP: Sending SESSION message (remote_counter=%d, %zu bytes)",
              remote_counter, session_msg_size);

  // Create sub-message (CTRL_MESSAGE)
  RudpMessage subMessage;
  uint16_t local_counter = get_then_increase_counter(instance);

  subMessage.type = RUDP_CTRL_MESSAGE;
  subMessage.subMessage = NULL;
  subMessage.data_size = 2 + session_msg_size;
  subMessage.size = (0xC << 12) | (8 + subMessage.data_size);

  uint8_t *subdata = malloc(subMessage.data_size);
  if (!subdata) {
    CHIAKI_LOGE(instance->log,
                "RUDP: Failed to allocate memory for SESSION submessage");
    return CHIAKI_ERR_MEMORY;
  }

  *(uint16_t *)(subdata) = htons(local_counter);
  memcpy(subdata + 2, session_msg, session_msg_size);
  subMessage.data = subdata;

  // Create main message (SESSION_MESSAGE)
  RudpMessage message;
  message.type = RUDP_SESSION_MESSAGE;
  message.subMessage = &subMessage;
  message.data_size = 4;

  size_t alloc_size = 8 + message.data_size + 8 + subMessage.data_size;
  uint8_t *serialized_msg = malloc(alloc_size);
  if (!serialized_msg) {
    free(subdata);
    CHIAKI_LOGE(instance->log,
                "RUDP: Failed to allocate memory for SESSION message");
    return CHIAKI_ERR_MEMORY;
  }

  message.size = (0xC << 12) | alloc_size;

  uint8_t *data = malloc(message.data_size);
  if (!data) {
    free(subdata);
    free(serialized_msg);
    CHIAKI_LOGE(instance->log,
                "RUDP: Failed to allocate memory for SESSION data");
    return CHIAKI_ERR_MEMORY;
  }

  // Build SESSION message data (contains remote counter)
  *(uint16_t *)(data) = htons(remote_counter);
  *(uint16_t *)(data + 2) = htons(local_counter);
  message.data = data;

  size_t msg_size = 0;
  rudp_message_serialize(&message, serialized_msg, &msg_size);

  ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);

  free(subdata);
  free(data);
  free(serialized_msg);

  if (err == CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGD(instance->log, "RUDP: SESSION message sent successfully");
  } else {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to send SESSION message: %d", err);
  }

  return err;
}

ChiakiErrorCode chiaki_rudp_send_ack_message(ChiakiRudp rudp,
                                             uint16_t remote_counter) {
  if (!rudp) return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGD(instance->log, "RUDP: Sending ACK message (remote_counter=%d)",
              remote_counter);

  RudpMessage message;
  uint16_t counter =
      instance->counter;  // Use current counter without incrementing for ACK

  message.type = RUDP_ACK;
  message.subMessage = NULL;
  message.data_size = 6;

  size_t alloc_size = 8 + message.data_size;
  uint8_t *serialized_msg = malloc(alloc_size);
  if (!serialized_msg) {
    CHIAKI_LOGE(instance->log,
                "RUDP: Failed to allocate memory for ACK message");
    return CHIAKI_ERR_MEMORY;
  }

  message.size = (0xC << 12) | alloc_size;

  uint8_t data[6];
  const uint8_t after_counters[2] = {0x00, 0x92};

  *(uint16_t *)(data) = htons(counter);
  *(uint16_t *)(data + 2) = htons(remote_counter);
  memcpy(data + 4, after_counters, sizeof(after_counters));

  message.data = data;

  size_t msg_size = 0;
  rudp_message_serialize(&message, serialized_msg, &msg_size);

  ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
  free(serialized_msg);

  if (err == CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGD(instance->log, "RUDP: ACK message sent successfully");
  } else {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to send ACK message: %d", err);
  }

  return err;
}

ChiakiErrorCode chiaki_rudp_send_ctrl_message(ChiakiRudp rudp,
                                              uint8_t *ctrl_message,
                                              size_t ctrl_message_size) {
  if (!rudp || !ctrl_message || ctrl_message_size == 0)
    return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGD(instance->log, "RUDP: Sending CTRL message (%zu bytes)",
              ctrl_message_size);

  RudpMessage message;
  uint16_t counter = get_then_increase_counter(instance);

  message.type = RUDP_CTRL_MESSAGE;
  message.subMessage = NULL;
  message.data_size = 2 + ctrl_message_size;

  size_t alloc_size = 8 + message.data_size;
  uint8_t *serialized_msg = malloc(alloc_size);
  if (!serialized_msg) {
    CHIAKI_LOGE(instance->log,
                "RUDP: Failed to allocate memory for CTRL message");
    return CHIAKI_ERR_MEMORY;
  }

  message.size = (0xC << 12) | alloc_size;

  uint8_t *data = malloc(message.data_size);
  if (!data) {
    free(serialized_msg);
    CHIAKI_LOGE(instance->log, "RUDP: Failed to allocate memory for CTRL data");
    return CHIAKI_ERR_MEMORY;
  }

  *(uint16_t *)(data) = htons(counter);
  memcpy(data + 2, ctrl_message, ctrl_message_size);

  message.data = data;

  size_t msg_size = 0;
  rudp_message_serialize(&message, serialized_msg, &msg_size);

  ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);

  free(data);
  free(serialized_msg);

  if (err == CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGD(instance->log, "RUDP: CTRL message sent successfully");
  } else {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to send CTRL message: %d", err);
  }

  return err;
}

ChiakiErrorCode chiaki_rudp_send_switch_to_stream_connection_message(
    ChiakiRudp rudp) {
  if (!rudp) return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  RudpMessage message;
  uint16_t counter = get_then_increase_counter(instance);
  uint16_t counter_ack = instance->counter;

  message.type = RUDP_CTRL_MESSAGE;
  message.subMessage = NULL;
  message.data_size = 26;

  size_t alloc_size = 8 + message.data_size;
  uint8_t *serialized_msg = malloc(alloc_size);
  if (!serialized_msg) {
    CHIAKI_LOGE(instance->log, "RUDP: Error allocating memory for message");
    return CHIAKI_ERR_MEMORY;
  }

  message.size = (0xC << 12) | alloc_size;

  const size_t buf_size = 16;
  uint8_t buf[buf_size];
  const uint8_t before_buf[8] = {0x00, 0x00, 0x00, 0x10,
                                 0x00, 0x0D, 0x00, 0x00};

  chiaki_random_bytes_crypt(buf, buf_size);

  uint8_t data[26];
  *(uint16_t *)(data) = htons(counter);
  memcpy(data + 2, before_buf, sizeof(before_buf));
  memcpy(data + 10, buf, buf_size);
  message.data = data;

  size_t msg_size = 0;
  rudp_message_serialize(&message, serialized_msg, &msg_size);
  ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
  // Our Vita RUDP implementation does not maintain a send buffer; free and
  // return
  free(serialized_msg);
  return err;
}

ChiakiErrorCode chiaki_rudp_send_recv(ChiakiRudp rudp, RudpMessage *message,
                                      uint8_t *buf, size_t buf_size,
                                      uint16_t remote_counter,
                                      RudpPacketType send_type,
                                      RudpPacketType recv_type,
                                      size_t min_data_size, size_t tries) {
  if (!rudp || !message) return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGD(instance->log,
              "RUDP: Send/Recv - sending 0x%04X, expecting 0x%04X (tries=%zu)",
              send_type, recv_type, tries);

  bool success = false;
  ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

  for (size_t i = 0; i < tries && !success; i++) {
    // Send the message based on send_type
    switch (send_type) {
      case RUDP_INIT_REQUEST:
        err = chiaki_rudp_send_init_message(rudp);
        break;
      case RUDP_COOKIE_REQUEST:
        if (!buf || buf_size == 0) {
          CHIAKI_LOGE(instance->log, "RUDP: COOKIE request requires buffer");
          return CHIAKI_ERR_INVALID_DATA;
        }
        err = chiaki_rudp_send_cookie_message(rudp, buf, buf_size);
        break;
      case RUDP_ACK:
        err = chiaki_rudp_send_ack_message(rudp, remote_counter);
        break;
      case RUDP_SESSION_MESSAGE:
        if (!buf || buf_size == 0) {
          CHIAKI_LOGE(instance->log, "RUDP: SESSION message requires buffer");
          return CHIAKI_ERR_INVALID_DATA;
        }
        err = chiaki_rudp_send_session_message(rudp, remote_counter, buf,
                                               buf_size);
        break;
      default:
        CHIAKI_LOGE(instance->log, "RUDP: Unsupported send type 0x%04X",
                    send_type);
        return CHIAKI_ERR_INVALID_DATA;
    }

    if (err != CHIAKI_ERR_SUCCESS) {
      CHIAKI_LOGE(instance->log,
                  "RUDP: Failed to send message (attempt %zu/%zu): %d", i + 1,
                  tries, err);
      continue;
    }

    // Wait for the expected response
    err = chiaki_rudp_select_recv(rudp, 1500, message);
    if (err == CHIAKI_ERR_TIMEOUT) {
      CHIAKI_LOGW(instance->log,
                  "RUDP: Timeout waiting for response (attempt %zu/%zu)", i + 1,
                  tries);
      continue;
    }
    if (err != CHIAKI_ERR_SUCCESS) {
      CHIAKI_LOGE(instance->log, "RUDP: Failed to receive response: %d", err);
      return err;
    }

    // Validate the response
    bool found = true;
    switch (recv_type) {
      case RUDP_INIT_RESPONSE:
        if (message->subtype != 0xD0) {
          CHIAKI_LOGW(
              instance->log,
              "RUDP: Expected INIT RESPONSE with subtype 0xD0, got 0x%02X",
              message->subtype);
          found = false;
        }
        break;
      case RUDP_COOKIE_RESPONSE:
        if (message->subtype != 0xA0) {
          CHIAKI_LOGW(
              instance->log,
              "RUDP: Expected COOKIE RESPONSE with subtype 0xA0, got 0x%02X",
              message->subtype);
          found = false;
        }
        break;
      case RUDP_ACK:
        // ACK messages don't have specific subtype requirements
        break;
      default:
        CHIAKI_LOGW(instance->log,
                    "RUDP: Received message type 0x%04X (expected 0x%04X)",
                    message->type, recv_type);
        if (message->type != recv_type) {
          found = false;
        }
        break;
    }

    // Check minimum data size requirement
    if (found && message->data_size < min_data_size) {
      CHIAKI_LOGW(instance->log, "RUDP: Message data size %zu < minimum %zu",
                  message->data_size, min_data_size);
      found = false;
    }

    if (found) {
      CHIAKI_LOGD(instance->log, "RUDP: Send/Recv successful (attempt %zu/%zu)",
                  i + 1, tries);
      success = true;
    } else {
      CHIAKI_LOGW(
          instance->log,
          "RUDP: Unexpected message received, retrying (attempt %zu/%zu)",
          i + 1, tries);
      chiaki_rudp_message_pointers_free(message);
    }
  }

  if (!success) {
    CHIAKI_LOGE(instance->log, "RUDP: Send/Recv failed after %zu attempts",
                tries);
    return CHIAKI_ERR_TIMEOUT;
  }

  return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_rudp_select_recv(ChiakiRudp rudp, size_t buf_size,
                                        RudpMessage *message) {
  if (!rudp || !message || buf_size < 8) return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGV(instance->log,
              "RUDP: Selecting and receiving message (buf_size=%zu)", buf_size);

  uint8_t *buf = malloc(buf_size);
  if (!buf) {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to allocate receive buffer");
    return CHIAKI_ERR_MEMORY;
  }

  // Use select to wait for data
  ChiakiErrorCode err = chiaki_stop_pipe_select_single(
      &instance->stop_pipe, instance->sock, false, RUDP_EXPECT_TIMEOUT_MS);
  if (err == CHIAKI_ERR_TIMEOUT || err == CHIAKI_ERR_CANCELED) {
    free(buf);
    return err;
  }
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(instance->log, "RUDP: Select failed");
    free(buf);
    return err;
  }

  // Receive data from socket
#ifdef __vita__
  int received_sz = sceNetRecv(instance->sock, buf, buf_size, 0);
#else
  int received_sz = recv(instance->sock, buf, buf_size, 0);
#endif

  if (received_sz <= 8) {
    if (received_sz < 0) {
      CHIAKI_LOGE(instance->log, "RUDP: Receive failed");
    } else {
      CHIAKI_LOGE(instance->log, "RUDP: Received less than 8 byte RUDP header");
    }
    free(buf);
    return CHIAKI_ERR_NETWORK;
  }

  CHIAKI_LOGV(instance->log, "RUDP: Received %d bytes", received_sz);

  // Parse the received message
  err = chiaki_rudp_message_parse(buf, received_sz, message);
  free(buf);

  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to parse received message");
    return err;
  }

  CHIAKI_LOGV(instance->log, "RUDP: Message parsed successfully (type=0x%04X)",
              message->type);

  return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_rudp_recv_only(ChiakiRudp rudp, size_t buf_size,
                                      RudpMessage *message) {
  if (!rudp || !message || buf_size < 8) return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGV(instance->log, "RUDP: Receiving message (buf_size=%zu)",
              buf_size);

  uint8_t *buf = malloc(buf_size);
  if (!buf) {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to allocate receive buffer");
    return CHIAKI_ERR_MEMORY;
  }

  // Receive data from socket without select
#ifdef __vita__
  int received_sz = sceNetRecv(instance->sock, buf, buf_size, 0);
#else
  int received_sz = recv(instance->sock, buf, buf_size, 0);
#endif

  if (received_sz <= 8) {
    if (received_sz < 0) {
      CHIAKI_LOGE(instance->log, "RUDP: Receive failed");
    } else {
      CHIAKI_LOGE(instance->log, "RUDP: Received less than 8 byte RUDP header");
    }
    free(buf);
    return CHIAKI_ERR_NETWORK;
  }

  CHIAKI_LOGV(instance->log, "RUDP: Received %d bytes", received_sz);

  // Parse the received message
  ChiakiErrorCode err = chiaki_rudp_message_parse(buf, received_sz, message);
  free(buf);

  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(instance->log, "RUDP: Failed to parse received message");
    return err;
  }

  CHIAKI_LOGV(instance->log, "RUDP: Message parsed successfully (type=0x%04X)",
              message->type);

  return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_rudp_stop_pipe_select_single(ChiakiRudp rudp,
                                                    ChiakiStopPipe *stop_pipe,
                                                    uint64_t timeout_ms) {
  (void)rudp;
  (void)stop_pipe;
  (void)timeout_ms;
  return CHIAKI_ERR_SUCCESS;  // TODO: Implement in next phase
}

ChiakiErrorCode chiaki_rudp_ack_packet(ChiakiRudp rudp,
                                       uint16_t counter_to_ack) {
  (void)rudp;
  (void)counter_to_ack;
  return CHIAKI_ERR_SUCCESS;  // TODO: Implement in next phase
}

void chiaki_rudp_print_message(ChiakiRudp rudp, RudpMessage *message) {
  if (!rudp || !message) return;

  RudpInstance *instance = (RudpInstance *)rudp;
  print_rudp_message_type(instance, message->type);

  CHIAKI_LOGD(instance->log, "RUDP: Message size: %d, data_size: %zu",
              message->size, message->data_size);
}

void chiaki_rudp_message_pointers_free(RudpMessage *message) {
  if (!message) return;

  if (message->data) {
    free(message->data);
    message->data = NULL;
  }

  if (message->subMessage) {
    chiaki_rudp_message_pointers_free(message->subMessage);
    free(message->subMessage);
    message->subMessage = NULL;
  }
}

ChiakiErrorCode chiaki_rudp_fini(ChiakiRudp rudp) {
  if (!rudp) return CHIAKI_ERR_INVALID_DATA;

  RudpInstance *instance = (RudpInstance *)rudp;

  CHIAKI_LOGI(instance->log, "RUDP: Cleaning up RUDP instance");

  // Cleanup stop pipe
  chiaki_stop_pipe_fini(&instance->stop_pipe);

  // Cleanup mutex
  chiaki_mutex_fini(&instance->counter_mutex);

  // Free instance
  free(instance);

  return CHIAKI_ERR_SUCCESS;
}
