#include "takion.h"

#include <arpa/inet.h>  // For htonl/ntohl functions
#include <errno.h>      // For errno
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>  // For select() and fd_set
#include <sys/socket.h>  // For POSIX socket constants and getsockopt
#include <unistd.h>      // For standard socket operations

#include "../chiaki/chiaki_common.h"
#include "../chiaki/chiaki_gkcrypt_vitaki.h"
#include "../chiaki/chiaki_random.h"
#include "../utils/logger.h"

// Socket compatibility for PS Vita
#ifndef SOL_SOCKET
#define SOL_SOCKET SCE_NET_SOL_SOCKET
#endif
#ifndef SO_ERROR
#define SO_ERROR SCE_NET_SO_ERROR
#endif

// Diagnostic function for socket state
static void takion_diagnose_socket_state(int socket, const char* socket_name) {
  log_debug("🔍 SOCKET DIAGNOSTICS: %s (FD: %d)", socket_name, socket);

  // Check socket error state
  int sock_error = 0;
  unsigned int sock_error_len = sizeof(sock_error);
  if (sceNetGetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR,
                       &sock_error, &sock_error_len) == 0) {
    log_debug("  - Error state: %d (0=OK)", sock_error);
  } else {
    log_debug("  - Error state: Failed to get");
  }

  // Check receive buffer size
  int rcvbuf = 0;
  unsigned int rcvbuf_len = sizeof(rcvbuf);
  if (sceNetGetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVBUF, &rcvbuf,
                       &rcvbuf_len) == 0) {
    log_debug("  - Receive buffer: %d bytes", rcvbuf);
  } else {
    log_debug("  - Receive buffer: Failed to get");
  }

  // Check timeouts (PS Vita uses microseconds as uint32_t)
  uint32_t timeout_us;
  unsigned int timeout_len = sizeof(timeout_us);
  if (sceNetGetsockopt(socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO,
                       &timeout_us, &timeout_len) == 0) {
    log_debug("  - Receive timeout: %u microseconds (%u seconds)", timeout_us,
              timeout_us / 1000000);
  } else {
    log_debug("  - Receive timeout: Failed to get");
  }
}

// CRITICAL FIX: PS Vita socket macros (matches chiaki_http.c pattern)
#ifdef __vita__
#define recv sceNetRecv
#define send sceNetSend
#define MSG_NOSIGNAL 0
#endif

#include "../utils/logger.h"
#include "network_manager.h"

// Internal connection structure
struct TakionConnection {
  TakionConfig config;
  TakionState state;
  TakionStats stats;

  // Network sockets
  int control_socket;
  int stream_socket;
  SceNetSockaddrIn remote_addr;
  SceNetSockaddrIn stream_addr;  // Separate address for stream port

  // Protocol state (vitaki-fork compatible)
  uint32_t tag_local;   // Our local tag (random)
  uint32_t tag_remote;  // Remote tag (from INIT_ACK)
  uint16_t sequence_number;
  uint32_t session_id;
  uint64_t connection_start_time;
  uint64_t last_packet_time;
  bool timeout_logged;  // Flag to prevent repeated timeout logging

  // Threading
  SceUID network_thread;
  bool thread_running;

  // Packet buffers
  uint8_t send_buffer[TAKION_MTU_SIZE];
  uint8_t recv_buffer[TAKION_MTU_SIZE];

  // VITAKI-FORK: Callback system for streamconnection
  void (*data_callback)(void* user, int data_type, uint8_t* buf,
                        size_t buf_size);
  void* callback_user;
};

// Global state
static bool takion_initialized = false;

VitaRPS5Result takion_set_data_callback(
    TakionConnection* connection,
    void (*callback)(void* user, int data_type, uint8_t* buf, size_t buf_size),
    void* user);

// Internal functions
static int takion_network_thread(SceSize args, void* argp);
static VitaRPS5Result takion_process_packet(TakionConnection* conn,
                                            const uint8_t* data, size_t size);
static void takion_set_state(TakionConnection* conn, TakionState new_state);
static uint64_t takion_get_timestamp(void);

// CRITICAL: Add forward declarations for new functions
static VitaRPS5Result takion_select_single(TakionConnection* conn, int socket,
                                           bool write, uint64_t timeout_ms);
// New: wait for readability on either control or stream socket; returns the
// socket fd that is ready in out_ready_fd
static VitaRPS5Result takion_select_either(TakionConnection* conn,
                                           uint64_t timeout_ms,
                                           int* out_ready_fd) {
  if (!conn || !out_ready_fd) return VITARPS5_ERROR_INVALID_PARAM;
  int s1 = conn->control_socket;
  int s2 = conn->stream_socket;
  if (s1 < 0 && s2 < 0) return VITARPS5_ERROR_INVALID_STATE;

  fd_set read_fds;
  FD_ZERO(&read_fds);
  int nfds = 0;
  if (s1 >= 0) {
    FD_SET(s1, &read_fds);
    if (s1 + 1 > nfds) nfds = s1 + 1;
  }
  if (s2 >= 0) {
    FD_SET(s2, &read_fds);
    if (s2 + 1 > nfds) nfds = s2 + 1;
  }

  struct timeval timeout;
  struct timeval* timeout_ptr = NULL;
  if (timeout_ms > 0) {
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    timeout_ptr = &timeout;
  }

  int r = select(nfds, &read_fds, NULL, NULL, timeout_ptr);
  if (r < 0) return VITARPS5_ERROR_NETWORK;
  if (r == 0) return VITARPS5_ERROR_TIMEOUT;
  // Prefer control socket if both ready
  if (s1 >= 0 && FD_ISSET(s1, &read_fds)) {
    *out_ready_fd = s1;
    return VITARPS5_SUCCESS;
  }
  if (s2 >= 0 && FD_ISSET(s2, &read_fds)) {
    *out_ready_fd = s2;
    return VITARPS5_SUCCESS;
  }
  return VITARPS5_ERROR_NETWORK;
}
static VitaRPS5Result takion_recv(TakionConnection* conn, uint8_t* buf,
                                  size_t* buf_size, uint64_t timeout_ms);
static VitaRPS5Result takion_handshake(TakionConnection* conn,
                                       uint32_t* seq_num_remote_initial);
static VitaRPS5Result takion_read_extra_sock_messages(TakionConnection* conn);
static void takion_close_sockets(TakionConnection* conn);
static VitaRPS5Result takion_recreate_sockets(TakionConnection* conn);

// API Implementation

VitaRPS5Result takion_init(void) {
  if (takion_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing Takion protocol subsystem");

  // Use centralized network manager
  VitaRPS5Result result = network_manager_init();
  if (result != VITARPS5_SUCCESS) {
    if (result == VITARPS5_ERROR_OFFLINE) {
      log_warning("Network not available - continuing in offline mode");
    } else {
      log_error("Failed to initialize network manager: %d", result);
      return result;
    }
  }

  takion_initialized = true;
  log_info("Takion protocol initialized successfully");

  return VITARPS5_SUCCESS;
}

void takion_cleanup(void) {
  if (!takion_initialized) {
    return;
  }

  log_info("Cleaning up Takion protocol subsystem");

  // Use centralized network manager cleanup
  network_manager_cleanup();

  takion_initialized = false;
  log_info("Takion protocol cleanup complete");
}

VitaRPS5Result takion_connection_create(const TakionConfig* config,
                                        TakionConnection** connection) {
  if (!takion_initialized) {
    return VITARPS5_ERROR_NOT_INITIALIZED;
  }

  if (!config || !connection) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  TakionConnection* conn = malloc(sizeof(TakionConnection));
  if (!conn) {
    return VITARPS5_ERROR_MEMORY;
  }

  // Initialize connection
  memset(conn, 0, sizeof(TakionConnection));
  conn->config = *config;
  conn->state = TAKION_STATE_IDLE;
  conn->control_socket = -1;
  conn->stream_socket = -1;
  conn->sequence_number = 1;
  conn->thread_running = false;

  // CRITICAL FIX: Generate random local tag using chiaki_random_32() like
  // vitaki-fork This matches vitaki-fork's exact approach: takion->tag_local =
  // chiaki_random_32()
  conn->tag_local = chiaki_random_32();
  log_info(
      "PROTOCOL FIX: Using chiaki_random_32() for tag_local=0x%08X "
      "(vitaki-fork method)",
      conn->tag_local);
  conn->tag_remote = 0;  // Will be set from INIT_ACK

  // CRITICAL FIX: Set up remote addresses with proper validation and ordering
  // Control address MUST be set up FIRST (session init communication)
  memset(&conn->remote_addr, 0, sizeof(conn->remote_addr));
  conn->remote_addr.sin_family = SCE_NET_AF_INET;
  conn->remote_addr.sin_port = htons(config->control_port);

  int addr_result = sceNetInetPton(SCE_NET_AF_INET, config->remote_ip,
                                   &conn->remote_addr.sin_addr);
  if (addr_result <= 0) {
    log_error("CRITICAL: Invalid PS5 console IP address: %s",
              config->remote_ip);
    free(conn);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_debug("✅ Control address configured: %s:%d", config->remote_ip,
            config->control_port);

  // Stream address MUST be set up SECOND (video/audio data communication)
  memset(&conn->stream_addr, 0, sizeof(conn->stream_addr));
  conn->stream_addr.sin_family = SCE_NET_AF_INET;
  conn->stream_addr.sin_port = htons(config->stream_port);

  addr_result = sceNetInetPton(SCE_NET_AF_INET, config->remote_ip,
                               &conn->stream_addr.sin_addr);
  if (addr_result <= 0) {
    log_error("CRITICAL: Invalid PS5 console IP address for stream: %s",
              config->remote_ip);
    free(conn);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_debug("✅ Stream address configured: %s:%d", config->remote_ip,
            config->stream_port);

  // Verify address ordering is correct for PS5 protocol
  if (config->control_port == config->stream_port) {
    log_error("CRITICAL: Control and stream ports cannot be the same: %d",
              config->control_port);
    free(conn);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info(
      "✅ Takion connection addresses configured correctly: control=%s:%d, "
      "stream=%s:%d",
      config->remote_ip, config->control_port, config->remote_ip,
      config->stream_port);

  *connection = conn;
  return VITARPS5_SUCCESS;
}

void takion_connection_destroy(TakionConnection* connection) {
  if (!connection) {
    return;
  }

  log_info("Destroying Takion connection");

  // Disconnect if connected
  if (connection->state != TAKION_STATE_IDLE) {
    takion_disconnect(connection);
  }

  // CRITICAL FIX: Signal thread to stop FIRST (proper order)
  if (connection->thread_running) {
    log_debug("Signaling network thread to exit...");
    connection->thread_running = false;

    if (connection->network_thread >= 0) {
      log_debug("Waiting for network thread to exit...");

      // Add timeout protection to prevent infinite blocking
      SceUInt timeout = 5000000;  // 5 second timeout in microseconds
      int result =
          sceKernelWaitThreadEnd(connection->network_thread, NULL, &timeout);

      if (result < 0) {
        // Any negative result indicates an error (including timeout)
        log_error(
            "Network thread did not exit cleanly (error: 0x%08X) - forcing "
            "deletion",
            result);
        sceKernelDeleteThread(connection->network_thread);
      } else {
        log_debug("Network thread exited cleanly");
        sceKernelDeleteThread(connection->network_thread);
      }
      connection->network_thread = -1;
    }
  }

  // NOW close sockets after thread has stopped (correct order)
  if (connection->control_socket >= 0) {
    log_debug("Closing control socket...");
    sceNetSocketClose(connection->control_socket);
    connection->control_socket = -1;
  }
  if (connection->stream_socket >= 0) {
    log_debug("Closing stream socket...");
    sceNetSocketClose(connection->stream_socket);
    connection->stream_socket = -1;
  }

  log_debug("Takion connection destroyed successfully");
  free(connection);
}

VitaRPS5Result takion_connect(TakionConnection* connection) {
  if (!connection) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (connection->state != TAKION_STATE_IDLE) {
    return VITARPS5_ERROR_INVALID_STATE;
  }

  log_info("Connecting to PlayStation console at %s:%d",
           connection->config.remote_ip, connection->config.control_port);

  // Create control socket
  connection->control_socket =
      sceNetSocket("takion_control", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM,
                   SCE_NET_IPPROTO_UDP);
  if (connection->control_socket < 0) {
    log_error("Failed to create control socket: 0x%08X",
              connection->control_socket);
    return VITARPS5_ERROR_NETWORK;
  }

  // CRITICAL FIX: Set socket buffer size BEFORE connect (vitaki-fork pattern)
  int rcvbuf_size = TAKION_A_RWND;  // 102400 bytes, not 64KB
  int result =
      sceNetSetsockopt(connection->control_socket, SCE_NET_SOL_SOCKET,
                       SCE_NET_SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size));
  if (result < 0) {
    log_warn("Failed to set control socket receive buffer: 0x%08X", result);
  } else {
    log_debug(
        "Set control socket receive buffer to %d bytes (vitaki-fork pattern)",
        rcvbuf_size);
  }

  // CRITICAL FIX: Set socket timeouts for reliable communication
  // PS Vita expects timeout values in microseconds as uint32_t
  uint32_t recv_timeout_us = 30 * 1000000;  // 30 seconds in microseconds

  result = sceNetSetsockopt(connection->control_socket, SCE_NET_SOL_SOCKET,
                            SCE_NET_SO_RCVTIMEO, &recv_timeout_us,
                            sizeof(recv_timeout_us));
  if (result < 0) {
    log_warn("Failed to set control socket receive timeout: 0x%08X", result);
  } else {
    log_debug(
        "Set control socket receive timeout to %u microseconds (%u seconds)",
        recv_timeout_us, recv_timeout_us / 1000000);
  }

  uint32_t send_timeout_us = 10 * 1000000;  // 10 seconds in microseconds

  result = sceNetSetsockopt(connection->control_socket, SCE_NET_SOL_SOCKET,
                            SCE_NET_SO_SNDTIMEO, &send_timeout_us,
                            sizeof(send_timeout_us));
  if (result < 0) {
    log_warn("Failed to set control socket send timeout: 0x%08X", result);
  } else {
    log_debug("Set control socket send timeout to %u microseconds (%u seconds)",
              send_timeout_us, send_timeout_us / 1000000);
  }

  // Diagnostic check after configuration
  takion_diagnose_socket_state(connection->control_socket, "Control Socket");

  // CRITICAL FIX: Connect control socket like vitaki-fork (connected UDP)
  log_debug("CRITICAL DEBUG: Connecting control socket to %s:%d",
            connection->config.remote_ip, connection->config.control_port);
  int connect_result =
      sceNetConnect(connection->control_socket,
                    (const SceNetSockaddr*)&connection->remote_addr,
                    sizeof(connection->remote_addr));
  if (connect_result < 0) {
    log_error(
        "CRITICAL ERROR: Failed to connect control socket to %s:%d - error: "
        "0x%08X",
        connection->config.remote_ip, connection->config.control_port,
        connect_result);
    sceNetSocketClose(connection->control_socket);
    connection->control_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  }
  log_info(
      "CRITICAL FIX: Control socket connected successfully to %s:%d "
      "(vitaki-fork pattern)",
      connection->config.remote_ip, connection->config.control_port);

  // Create stream socket
  connection->stream_socket =
      sceNetSocket("takion_stream", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM,
                   SCE_NET_IPPROTO_UDP);
  if (connection->stream_socket < 0) {
    log_error("Failed to create stream socket: 0x%08X",
              connection->stream_socket);
    sceNetSocketClose(connection->control_socket);
    connection->control_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  }

  // CRITICAL FIX: Set stream socket buffer size BEFORE connect (vitaki-fork
  // pattern)
  result =
      sceNetSetsockopt(connection->stream_socket, SCE_NET_SOL_SOCKET,
                       SCE_NET_SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size));
  if (result < 0) {
    log_warn("Failed to set stream socket receive buffer: 0x%08X", result);
  } else {
    log_debug(
        "Set stream socket receive buffer to %d bytes (vitaki-fork pattern)",
        rcvbuf_size);
  }

  // CRITICAL FIX: Set stream socket timeouts for reliable communication
  result = sceNetSetsockopt(connection->stream_socket, SCE_NET_SOL_SOCKET,
                            SCE_NET_SO_RCVTIMEO, &recv_timeout_us,
                            sizeof(recv_timeout_us));
  if (result < 0) {
    log_warn("Failed to set stream socket receive timeout: 0x%08X", result);
  } else {
    log_debug(
        "Set stream socket receive timeout to %u microseconds (%u seconds)",
        recv_timeout_us, recv_timeout_us / 1000000);
  }

  result = sceNetSetsockopt(connection->stream_socket, SCE_NET_SOL_SOCKET,
                            SCE_NET_SO_SNDTIMEO, &send_timeout_us,
                            sizeof(send_timeout_us));
  if (result < 0) {
    log_warn("Failed to set stream socket send timeout: 0x%08X", result);
  } else {
    log_debug("Set stream socket send timeout to %u microseconds (%u seconds)",
              send_timeout_us, send_timeout_us / 1000000);
  }

  // CRITICAL FIX: Connect stream socket like vitaki-fork (connected UDP)
  log_debug("CRITICAL DEBUG: Connecting stream socket to %s:%d",
            connection->config.remote_ip, connection->config.stream_port);
  connect_result =
      sceNetConnect(connection->stream_socket,
                    (const SceNetSockaddr*)&connection->stream_addr,
                    sizeof(connection->stream_addr));
  if (connect_result < 0) {
    log_error(
        "CRITICAL ERROR: Failed to connect stream socket to %s:%d - error: "
        "0x%08X",
        connection->config.remote_ip, connection->config.stream_port,
        connect_result);
    sceNetSocketClose(connection->control_socket);
    sceNetSocketClose(connection->stream_socket);
    connection->control_socket = -1;
    connection->stream_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  }
  log_info(
      "CRITICAL FIX: Stream socket connected successfully to %s:%d "
      "(vitaki-fork pattern)",
      connection->config.remote_ip, connection->config.stream_port);

  // Socket buffer sizes already set before connect() calls (vitaki-fork
  // pattern) Additional send buffer configuration (optional)
  int sndbuf_size = 64 * 1024;  // Keep 64KB for send buffers
  sceNetSetsockopt(connection->control_socket, SCE_NET_SOL_SOCKET,
                   SCE_NET_SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size));
  sceNetSetsockopt(connection->stream_socket, SCE_NET_SOL_SOCKET,
                   SCE_NET_SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size));

  log_info(
      "Socket buffers configured: RCV=%d bytes, SND=%d bytes (vitaki-fork "
      "pattern)",
      rcvbuf_size, sndbuf_size);

  // CRITICAL TIMING FIX: Clear PS5 socket buffers IMMEDIATELY after connection
  // Don't wait for network thread to reduce timing delays that allow PS5 to
  // send preliminary data
  log_info(
      "CRITICAL TIMING-FIX: Clearing PS5 socket buffers immediately after "
      "connection");
  VitaRPS5Result cleanup_result = takion_read_extra_sock_messages(connection);
  if (cleanup_result != VITARPS5_SUCCESS) {
    log_warning("CRITICAL: Socket buffer cleanup had issues but continuing: %s",
                vitarps5_result_string(cleanup_result));
  }

  // Start network thread
  connection->thread_running = true;
  connection->network_thread = sceKernelCreateThread(
      "takion_net", takion_network_thread, 0x10000100, 0x10000, 0, 0, NULL);
  if (connection->network_thread < 0) {
    log_error("Failed to create network thread: 0x%08X",
              connection->network_thread);
    connection->thread_running = false;
    return VITARPS5_ERROR_INIT;
  }

  sceKernelStartThread(connection->network_thread, sizeof(TakionConnection*),
                       &connection);

  // CRITICAL CHANGE: Don't send INIT here - let network thread handle complete
  // handshake
  takion_set_state(connection, TAKION_STATE_CONNECTING);
  connection->connection_start_time = takion_get_timestamp();
  connection->timeout_logged = false;

  log_info(
      "CRITICAL: Takion connection started - handshake will be performed in "
      "network thread");

  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_disconnect(TakionConnection* connection) {
  if (!connection) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (connection->state == TAKION_STATE_IDLE) {
    return VITARPS5_SUCCESS;
  }

  log_info("Disconnecting Takion connection");

  takion_set_state(connection, TAKION_STATE_DISCONNECTING);

  // Send proper disconnect packet to PS5 using vitaki-fork format
  if (connection->control_socket >= 0) {
    uint8_t disconnect_packet[32];
    size_t packet_size = 0;

    // Set packet type for disconnect
    disconnect_packet[0] = TAKION_PACKET_TYPE_DISCONNECT;
    packet_size++;

    // Add message header for disconnect packet
    takion_write_message_header(&disconnect_packet[1],
                                connection->tag_local,  // Our tag
                                0,  // No key position for disconnect
                                TAKION_CHUNK_TYPE_DATA,  // Disconnect data
                                0x01,                    // Disconnect flag
                                0);                      // No payload
    packet_size += sizeof(TakionMessageHeader);

    log_info("Sending PS5 disconnect packet (size=%zu bytes)", packet_size);

    // Send disconnect packet via control socket
    ssize_t sent = sceNetSend(connection->control_socket, disconnect_packet,
                              packet_size, 0);
    if (sent < 0) {
      log_warn("Failed to send disconnect packet: error=0x%08X", sent);
    } else {
      log_debug("✅ Disconnect packet sent successfully: %zd bytes", sent);
      connection->stats.packets_sent++;
      connection->stats.bytes_sent += sent;
    }
  }

  log_info("Takion connection disconnected gracefully");

  takion_set_state(connection, TAKION_STATE_IDLE);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_update(TakionConnection* connection) {
  if (!connection) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Update statistics
  uint64_t current_time = takion_get_timestamp();

  // Check for timeouts
  if (connection->state != TAKION_STATE_IDLE &&
      connection->state != TAKION_STATE_CONNECTED &&
      connection->state != TAKION_STATE_ERROR) {
    uint64_t elapsed = current_time - connection->connection_start_time;
    if (elapsed >
        connection->config.timeout_ms * 1000) {  // Convert ms to microseconds
      if (!connection->timeout_logged) {
        log_error("Connection timeout after %llu ms", elapsed / 1000);
        connection->timeout_logged = true;
      }
      takion_set_state(connection, TAKION_STATE_ERROR);
      return VITARPS5_ERROR_TIMEOUT;
    }
  }

  // Process any pending packets (handled by network thread)

  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_send_input(TakionConnection* connection,
                                 const uint8_t* input_data, size_t input_size) {
  if (!connection || !input_data) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (connection->state != TAKION_STATE_CONNECTED) {
    return VITARPS5_ERROR_NOT_CONNECTED;
  }

  // Implement input packet transmission using PS5 Remote Play protocol
  log_debug("Sending PS5 input packet (size=%zu bytes)", input_size);

  // Validate input data size - PS5 expects specific input packet format
  if (input_size <
      sizeof(TakionInputPacket) - sizeof(((TakionInputPacket*)0)->header) - 1) {
    log_error("Input data size too small: %zu bytes (minimum: %zu)", input_size,
              sizeof(TakionInputPacket) -
                  sizeof(((TakionInputPacket*)0)->header) - 1);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create properly formatted input packet for PS5
  uint8_t packet_buffer[256];
  TakionInputPacket* input_packet = (TakionInputPacket*)packet_buffer;

  // Set packet type for PS5 controller input
  input_packet->packet_type = TAKION_PACKET_TYPE_CONTROL;

  // Write message header using PS5 protocol format
  takion_write_message_header((uint8_t*)&input_packet->header,
                              connection->tag_local,  // Our tag
                              0,  // No key position for input
                              TAKION_CHUNK_TYPE_DATA,  // Input data chunk
                              0,                       // No special flags
                              input_size);             // Payload size

  // Copy controller input data from caller
  memcpy(&input_packet->buttons, input_data, input_size);

  // Calculate total packet size (header + input data)
  size_t total_packet_size =
      sizeof(uint8_t) + sizeof(TakionMessageHeader) + input_size;

  log_debug("Constructed PS5 input packet: type=0x%02X, tag=0x%08X, size=%zu",
            input_packet->packet_type, connection->tag_local,
            total_packet_size);

  // Send packet to PS5 console via stream socket
  ssize_t sent = sceNetSend(connection->stream_socket, packet_buffer,
                            total_packet_size, 0);
  if (sent < 0) {
    int error_code = errno;
    log_error("Failed to send input packet to PS5: errno=%d", error_code);
    return VITARPS5_ERROR_NETWORK;
  }

  if ((size_t)sent != total_packet_size) {
    log_warn("Partial input packet sent: %zd/%zu bytes", sent,
             total_packet_size);
    return VITARPS5_ERROR_NETWORK;
  }

  log_debug("✅ PS5 input packet sent successfully: %zd bytes", sent);

  // Update statistics
  connection->stats.packets_sent++;
  connection->stats.bytes_sent += sent;

  return VITARPS5_SUCCESS;
}

TakionState takion_get_state(const TakionConnection* connection) {
  if (!connection) {
    return TAKION_STATE_ERROR;
  }

  return connection->state;
}

VitaRPS5Result takion_get_stats(const TakionConnection* connection,
                                TakionStats* stats) {
  if (!connection || !stats) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  *stats = connection->stats;
  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_send_packet(TakionConnection* connection,
                                  const void* packet, size_t size) {
  if (!connection || !packet || size == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (size > TAKION_MTU_SIZE) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Send protobuf packets (BIG/BANG/STREAMINFO) via stream socket by default
  int result = send(connection->stream_socket, packet, size, 0);

  if (result < 0) {
    log_error("Failed to send packet: 0x%08X", result);
    return VITARPS5_ERROR_NETWORK;
  }

  connection->stats.packets_sent++;
  connection->stats.bytes_sent += size;

  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_send_data_chunk(TakionConnection* connection,
                                      const void* payload,
                                      size_t payload_size) {
  if (!connection || !payload || payload_size == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (payload_size > TAKION_MTU_SIZE) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Build control packet: 1 (packet type) + 16 (header) + payload
  size_t total = 1 + TAKION_MESSAGE_HEADER_SIZE + payload_size;
  if (total > sizeof(((TakionInputPacket*)0)->packet_type) +
                  sizeof(TakionMessageHeader) + TAKION_MTU_SIZE) {
    // Fallback simple bound; total must be <= MTU-ish
  }

  uint8_t* buf = malloc(total);
  if (!buf) return VITARPS5_ERROR_MEMORY;

  buf[0] = TAKION_PACKET_TYPE_CONTROL;
  // Tag selection:
  // Use the remote tag when available; before association this is 0.
  // For PS5 (skipping INIT/ACK), many stacks expect 0 here for the first
  // DATA/BIG, then establish tags afterwards.
  uint32_t header_tag = connection->tag_remote;  // typically 0 pre-association
  takion_write_message_header(buf + 1, header_tag, 0, TAKION_CHUNK_TYPE_DATA, 0,
                              payload_size);
  memcpy(buf + 1 + TAKION_MESSAGE_HEADER_SIZE, payload, payload_size);

  int sent_stream = -1;
  int sent_control = -1;

  if (connection->stream_socket >= 0)
    sent_stream = send(connection->stream_socket, buf, total, 0);
  if (connection->control_socket >= 0)
    sent_control = send(connection->control_socket, buf, total, 0);
  free(buf);
  if ((sent_stream < 0 || (size_t)sent_stream != total) &&
      (sent_control < 0 || (size_t)sent_control != total)) {
    log_error(
        "Failed to send DATA chunk: stream=%d/%zu, control=%d/%zu (bytes)",
        sent_stream, total, sent_control, total);
    return VITARPS5_ERROR_NETWORK;
  }

  if (sent_stream >= 0) {
    // Log local/remote for stream socket
    SceNetSockaddrIn local_addr;
    unsigned int addr_len = sizeof(local_addr);
    char local_ip[16] = {0};
    if (sceNetGetsockname(connection->stream_socket,
                          (SceNetSockaddr*)&local_addr, &addr_len) == 0) {
      sceNetInetNtop(SCE_NET_AF_INET, &local_addr.sin_addr, local_ip,
                     sizeof(local_ip));
      log_info(
          "VITAKI-FORK: Sent DATA chunk on stream socket (%d bytes) from %s:%d "
          "-> %s:%d",
          sent_stream, local_ip, ntohs(local_addr.sin_port),
          connection->config.remote_ip,
          ntohs(connection->stream_addr.sin_port));
    } else {
      log_info("VITAKI-FORK: Sent DATA chunk on stream socket (%d bytes)",
               sent_stream);
    }
  }
  if (sent_control >= 0) {
    // Log local/remote for control socket
    SceNetSockaddrIn local_addr2;
    unsigned int addr_len2 = sizeof(local_addr2);
    char local_ip2[16] = {0};
    if (sceNetGetsockname(connection->control_socket,
                          (SceNetSockaddr*)&local_addr2, &addr_len2) == 0) {
      sceNetInetNtop(SCE_NET_AF_INET, &local_addr2.sin_addr, local_ip2,
                     sizeof(local_ip2));
      log_info(
          "VITAKI-FORK: Sent DATA chunk on control socket (%d bytes) from "
          "%s:%d -> %s:%d",
          sent_control, local_ip2, ntohs(local_addr2.sin_port),
          connection->config.remote_ip,
          ntohs(connection->remote_addr.sin_port));
    } else {
      log_info("VITAKI-FORK: Sent DATA chunk on control socket (%d bytes)",
               sent_control);
    }
  }
  connection->stats.packets_sent++;
  connection->stats.bytes_sent += (sent_stream > 0 ? sent_stream : 0) +
                                  (sent_control > 0 ? sent_control : 0);
  return VITARPS5_SUCCESS;
}

// VITAKI-FORK: Send packet on stream socket (for BIG/BANG/STREAMINFO protocol)
VitaRPS5Result takion_send_stream_packet(TakionConnection* connection,
                                         const void* packet, size_t size) {
  if (!connection || !packet || size == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  if (size > TAKION_MTU_SIZE) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // CRITICAL FIX: Send via connected stream socket (macros map send →
  // sceNetSend)
  int result = send(connection->stream_socket, packet, size, 0);

  if (result < 0) {
    log_error("Failed to send stream packet: 0x%08X", result);
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("VITAKI-FORK: Sent packet on stream socket (%zu bytes to port %d)",
           size, ntohs(connection->stream_addr.sin_port));

  connection->stats.packets_sent++;
  connection->stats.bytes_sent += size;

  return VITARPS5_SUCCESS;
}

const char* takion_state_string(TakionState state) {
  switch (state) {
    case TAKION_STATE_IDLE:
      return "Idle";
    case TAKION_STATE_CONNECTING:
      return "Connecting";
    case TAKION_STATE_INIT_SENT:
      return "Init Sent";
    case TAKION_STATE_INIT_ACK_RECEIVED:
      return "Init ACK Received";
    case TAKION_STATE_COOKIE_SENT:
      return "Cookie Sent";
    case TAKION_STATE_COOKIE_ACK_RECEIVED:
      return "Cookie ACK Received";
    case TAKION_STATE_CONNECTED:
      return "Connected";
    case TAKION_STATE_DISCONNECTING:
      return "Disconnecting";
    case TAKION_STATE_ERROR:
      return "Error";
    default:
      return "Unknown";
  }
}

// Internal function implementations

static int takion_network_thread(SceSize args, void* argp) {
  TakionConnection* connection = *(TakionConnection**)argp;

  log_info("CRITICAL: Takion network thread started");

  // For PS5 LAN (console_version >= TAKION_VERSION_PS5), skip SCTP INIT/ACK
  // handshake and proceed directly to protobuf BIG/BANG/STREAMINFO. Vitaki
  // sends BIG immediately on the Takion stream channel.
  if (connection->config.console_version >= TAKION_VERSION_PS5) {
    log_info("CRITICAL: PS5 detected – skipping INIT/ACK handshake");
    takion_set_state(connection, TAKION_STATE_CONNECTED);
  } else {
    // Legacy path: perform INIT/ACK handshake (PS4)
    uint32_t seq_num_remote_initial;
    VitaRPS5Result handshake_result;
    int handshake_attempts = 0;
    const int max_handshake_attempts = 3;
    while (handshake_attempts < max_handshake_attempts) {
      handshake_attempts++;
      log_info("CRITICAL: Takion handshake attempt %d/%d", handshake_attempts,
               max_handshake_attempts);
      handshake_result = takion_handshake(connection, &seq_num_remote_initial);
      if (handshake_result == VITARPS5_SUCCESS) {
        log_info("CRITICAL: ✅ Takion handshake succeeded on attempt %d",
                 handshake_attempts);
        break;
      }
      log_warning("CRITICAL: Takion handshake attempt %d failed: %s",
                  handshake_attempts, vitarps5_result_string(handshake_result));
      if (handshake_attempts < max_handshake_attempts) {
        int delay_ms = handshake_attempts * 1000;
        sceKernelDelayThread(delay_ms * 1000);
        takion_close_sockets(connection);
        if (takion_recreate_sockets(connection) != VITARPS5_SUCCESS) {
          takion_set_state(connection, TAKION_STATE_ERROR);
          return -1;
        }
        takion_set_state(connection, TAKION_STATE_CONNECTING);
      }
    }
    if (handshake_result != VITARPS5_SUCCESS) {
      takion_set_state(connection, TAKION_STATE_ERROR);
      return -1;
    }
    takion_set_state(connection, TAKION_STATE_CONNECTED);
  }

  // CRITICAL: Now enter packet processing loop like vitaki-fork (line 908)
  while (connection->thread_running) {
    // CRITICAL: Use proper blocking receive with timeout (like vitaki-fork line
    // 953)
    size_t received_size = TAKION_MTU_SIZE;
    uint8_t* buf = malloc(received_size);
    if (!buf) {
      log_error("CRITICAL: Failed to allocate receive buffer");
      break;
    }

    VitaRPS5Result err =
        takion_recv(connection, buf, &received_size, 1000);  // 1 second timeout
    if (err == VITARPS5_ERROR_TIMEOUT) {
      // Timeout is normal during idle periods
      free(buf);
      continue;
    }

    if (err != VITARPS5_SUCCESS) {
      log_error("CRITICAL: takion_recv failed: %s",
                vitarps5_result_string(err));
      free(buf);
      break;
    }

    if (!connection->thread_running) {
      free(buf);
      break;
    }

    connection->stats.packets_received++;
    connection->stats.bytes_received += received_size;
    connection->last_packet_time = takion_get_timestamp();

    log_debug("CRITICAL: Received packet after handshake (%zu bytes)",
              received_size);

    // CRITICAL: Process packet with proper infrastructure
    takion_process_packet(connection, buf, received_size);
    free(buf);
  }

  log_info("CRITICAL: Takion network thread ended after handshake completion");
  return 0;
}

// CRITICAL FIX: Proper socket readiness detection using select() (vitaki-fork
// approach)
static VitaRPS5Result takion_select_single(TakionConnection* conn, int socket,
                                           bool write, uint64_t timeout_ms) {
  if (socket < 0) {
    log_error("Invalid socket passed to takion_select_single");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Set up file descriptor sets for select()
  fd_set read_fds, write_fds;
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);

  if (write) {
    FD_SET(socket, &write_fds);
  } else {
    FD_SET(socket, &read_fds);
  }

  // Set up timeout for select()
  struct timeval timeout;
  struct timeval* timeout_ptr = NULL;

  if (timeout_ms > 0) {
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    timeout_ptr = &timeout;

#ifdef __PSVITA__
    // PS Vita-specific workaround for newlib timeout handling
    // Based on vitaki-fork implementation lines 227-234 in stoppipe.c
    if (timeout_ms >= 999999000) {
      timeout.tv_sec = 999999999;
      timeout.tv_usec = 0;
    }
#endif
  }

  log_debug(
      "VITAKI-FORK SELECT: Checking socket %d readiness (%s, timeout: %llu ms)",
      socket, write ? "write" : "read", timeout_ms);

  // Use select() to check socket readiness (vitaki-fork approach)
  int nfds = socket + 1;
  int select_result = select(nfds, write ? NULL : &read_fds,
                             write ? &write_fds : NULL, NULL, timeout_ptr);

  if (select_result < 0) {
    int error = errno;
    log_error("VITAKI-FORK SELECT: select() failed: %d (errno: %d)",
              select_result, error);
    return VITARPS5_ERROR_NETWORK;
  }

  if (select_result == 0) {
    // Timeout occurred
    log_debug("VITAKI-FORK SELECT: Socket timeout after %llu ms", timeout_ms);
    return VITARPS5_ERROR_TIMEOUT;
  }

  // Check if our socket is ready
  bool socket_ready = false;
  if (write) {
    socket_ready = FD_ISSET(socket, &write_fds);
  } else {
    socket_ready = FD_ISSET(socket, &read_fds);
  }

  if (!socket_ready) {
    log_error(
        "VITAKI-FORK SELECT: Socket %d not ready after select() returned %d",
        socket, select_result);
    return VITARPS5_ERROR_NETWORK;
  }

  log_debug("VITAKI-FORK SELECT: Socket %d ready for %s operation", socket,
            write ? "write" : "read");
  return VITARPS5_SUCCESS;
}

// PHASE B: PS5 reachability test before handshake attempts
static VitaRPS5Result takion_test_ps5_reachability(TakionConnection* conn) {
  log_info("🔍 Phase B: Testing PS5 reachability before handshake");

  // Create temporary socket for testing
  int test_socket = sceNetSocket("reachability_test", SCE_NET_AF_INET,
                                 SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
  if (test_socket < 0) {
    log_error("Failed to create reachability test socket: 0x%08X", test_socket);
    return VITARPS5_ERROR_NETWORK;
  }

  // Connect to PS5 control port
  int connect_result =
      sceNetConnect(test_socket, (const SceNetSockaddr*)&conn->remote_addr,
                    sizeof(conn->remote_addr));
  if (connect_result < 0) {
    log_error("❌ PS5 unreachable - connect failed: 0x%08X", connect_result);
    sceNetSocketClose(test_socket);
    return VITARPS5_ERROR_NETWORK;
  }

  // Send a small test packet (1 byte)
  uint8_t test_byte = 0xFF;
  int sent = send(test_socket, &test_byte, 1, 0);
  if (sent < 0) {
    log_error("❌ PS5 reachability test send failed: 0x%08X", sent);
    sceNetSocketClose(test_socket);
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("✅ PS5 reachability test passed - can send UDP to %s:%d",
           conn->config.remote_ip, conn->config.control_port);

  // CRITICAL DEBUG: Network packet capture instructions
  log_info("📊 NETWORK DEBUGGING INSTRUCTIONS:");
  log_info("  🔍 To verify PS5 is receiving our packets, set up Wireshark:");
  log_info("  1. Install Wireshark on a PC connected to the same network");
  log_info("  2. Capture on the network interface facing your router");
  log_info("  3. Use filter: udp and host %s and port %d",
           conn->config.remote_ip, TAKION_CONTROL_PORT);
  log_info("  4. Look for UDP packets from Vita IP to %s:9296",
           conn->config.remote_ip);
  log_info("  5. Expected: 33-byte INIT packet sent, no response from PS5");
  log_info(
      "  6. If packet appears: PS5 is ignoring our INIT (state/protocol "
      "issue)");
  log_info("  7. If no packet: Network routing problem between Vita and PS5");

  sceNetSocketClose(test_socket);
  return VITARPS5_SUCCESS;
}

// CRITICAL INFRASTRUCTURE: Proper blocking receive with timeout
static VitaRPS5Result takion_recv(TakionConnection* conn, uint8_t* buf,
                                  size_t* buf_size, uint64_t timeout_ms) {
  // Vita-compatible version of vitaki-fork's takion_recv

  log_info(
      "🔍 RECV DEBUG: Starting takion_recv (timeout: %llu ms, ctrl_fd=%d, "
      "strm_fd=%d)",
      (unsigned long long)timeout_ms, conn->control_socket,
      conn->stream_socket);

  int ready_fd = -1;
  VitaRPS5Result select_result =
      takion_select_either(conn, timeout_ms, &ready_fd);
  if (select_result == VITARPS5_ERROR_TIMEOUT) {
    log_warning(
        "⏰ INIT_ACK DEBUG: takion_recv TIMEOUT after %llu ms - no data "
        "available",
        (unsigned long long)timeout_ms);

    // CRITICAL DEBUG: Check if any data arrived after timeout (shouldn't happen
    // but let's verify)
    log_info(
        "🔍 POST-TIMEOUT DEBUG: Checking for any residual data on socket %d",
        conn->control_socket);

    // Non-blocking check for any data that might have arrived
    uint8_t residual_buffer[64];
    // Check both sockets for any late data
    int residual_received = -1;
    if (conn->control_socket >= 0)
      residual_received = recv(conn->control_socket, residual_buffer,
                               sizeof(residual_buffer), MSG_DONTWAIT);
    if (residual_received <= 0 && conn->stream_socket >= 0)
      residual_received = recv(conn->stream_socket, residual_buffer,
                               sizeof(residual_buffer), MSG_DONTWAIT);
    if (residual_received > 0) {
      log_warning(
          "⚠️ UNEXPECTED: %d bytes received AFTER timeout - PS5 responded too "
          "late!",
          residual_received);
      // Hex dump unexpected data
      for (int i = 0; i < residual_received && i < 32; i += 16) {
        char hex_line[64] = {0};
        for (int j = 0; j < 16 && (i + j) < residual_received; j++) {
          sprintf(hex_line + strlen(hex_line), "%02X ", residual_buffer[i + j]);
        }
        log_info("  LATE DATA %04X: %s", i, hex_line);
      }
    } else if (residual_received == 0) {
      log_info("  ✅ No residual data - socket cleanly closed by PS5");
    } else {
      log_info("  ✅ No residual data - PS5 sent nothing (errno: 0x%08X)",
               residual_received);
    }

    return VITARPS5_ERROR_TIMEOUT;
  }
  if (select_result != VITARPS5_SUCCESS) {
    log_error("CRITICAL: Socket select failed in takion_recv: %s",
              vitarps5_result_string(select_result));
    return select_result;
  }

  log_info("✅ RECV DEBUG: select() indicates data available on fd=%d",
           ready_fd);

  // CRITICAL FIX: Use recv (macros map recv → sceNetRecv)
  int received_sz = recv(ready_fd, buf, *buf_size, 0);
  if (received_sz <= 0) {
    if (received_sz < 0) {
      log_error("❌ INIT_ACK DEBUG: recv() failed with error: 0x%08X",
                received_sz);
    } else {
      log_error(
          "❌ INIT_ACK DEBUG: recv() returned 0 - connection closed by PS5");
    }
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("📦 INIT_ACK DEBUG: Successfully received %d bytes from PS5",
           received_sz);

  // Hex dump first 32 bytes of received data for analysis
  log_info("📦 INIT_ACK DEBUG: First 32 bytes of received data:");
  for (int i = 0; i < received_sz && i < 32; i += 16) {
    char hex_line[64] = {0};
    for (int j = 0; j < 16 && (i + j) < received_sz && (i + j) < 32; j++) {
      sprintf(hex_line + strlen(hex_line), "%02X ", buf[i + j]);
    }
    log_info("  %04X: %s", i, hex_line);
  }

  *buf_size = (size_t)received_sz;
  return VITARPS5_SUCCESS;
}

// OLD FUNCTION REMOVED - using new takion_send_message_init instead

// CRITICAL INFRASTRUCTURE: Complete handshake implementation
typedef struct {
  uint32_t tag;
  uint32_t a_rwnd;
  uint16_t outbound_streams;
  uint16_t inbound_streams;
  uint32_t initial_seq_num;
  uint8_t cookie[32];  // TAKION_COOKIE_SIZE
} TakionMessagePayloadInitAck;

static VitaRPS5Result takion_recv_message_init_ack(
    TakionConnection* conn, TakionMessagePayloadInitAck* payload) {
  uint8_t
      message[1 + 16 + 0x10 + 32];  // packet type + header + payload + cookie
  size_t received_size = sizeof(message);

  VitaRPS5Result err =
      takion_recv(conn, message, &received_size,
                  TAKION_EXPECT_TIMEOUT_MS);  // Use configurable timeout
  if (err != VITARPS5_SUCCESS) {
    log_error("CRITICAL: Failed to receive INIT_ACK: %s",
              vitarps5_result_string(err));
    return err;
  }

  // CRITICAL DEBUG: Hex dump received INIT_ACK packet for analysis
  log_info("CRITICAL DEBUG: Received INIT_ACK packet (%zu bytes):",
           received_size);
  for (size_t i = 0; i < received_size && i < 64; i += 16) {
    char hex_line[128];
    char ascii_line[32];
    int hex_pos = 0;
    int ascii_pos = 0;

    for (size_t j = 0; j < 16 && (i + j) < received_size && (i + j) < 64; j++) {
      hex_pos += snprintf(hex_line + hex_pos, sizeof(hex_line) - hex_pos,
                          "%02X ", message[i + j]);
      ascii_line[ascii_pos++] = (message[i + j] >= 32 && message[i + j] <= 126)
                                    ? message[i + j]
                                    : '.';
    }
    ascii_line[ascii_pos] = '\0';
    log_info("  %04X: %-48s %s", (unsigned int)i, hex_line, ascii_line);
  }

  if (received_size < sizeof(message)) {
    log_error("CRITICAL: INIT_ACK packet too small: %zu bytes, expected %zu",
              received_size, sizeof(message));
    return VITARPS5_ERROR_PROTOCOL;
  }

  if (message[0] != TAKION_PACKET_TYPE_CONTROL) {
    log_error("CRITICAL: Expected CONTROL packet, got type %02X", message[0]);
    return VITARPS5_ERROR_PROTOCOL;
  }

  // Parse message header
  const uint8_t* msg_header = message + 1;
  uint32_t tag = ntohl(*(uint32_t*)(msg_header + 0));
  uint8_t chunk_type = msg_header[12];
  uint16_t payload_size = ntohs(*(uint16_t*)(msg_header + 14));

  if (chunk_type != 2) {  // TAKION_CHUNK_TYPE_INIT_ACK
    log_error("CRITICAL: Expected INIT_ACK chunk (type 2), got type %d",
              chunk_type);
    return VITARPS5_ERROR_PROTOCOL;
  }

  log_info("CRITICAL: Received INIT_ACK from PS5 (tag=0x%08X, payload_size=%d)",
           tag, payload_size);

  // Parse INIT_ACK payload
  const uint8_t* pl = message + 1 + 16;  // Skip packet type + header
  payload->tag = ntohl(*(uint32_t*)(pl + 0));
  payload->a_rwnd = ntohl(*(uint32_t*)(pl + 4));
  payload->outbound_streams = ntohs(*(uint16_t*)(pl + 8));
  payload->inbound_streams = ntohs(*(uint16_t*)(pl + 10));
  payload->initial_seq_num = ntohl(*(uint32_t*)(pl + 12));
  memcpy(payload->cookie, pl + 16, 32);

  log_info("CRITICAL: INIT_ACK parsed - remote_tag=0x%08X, streams=%d/%d",
           payload->tag, payload->outbound_streams, payload->inbound_streams);

  return VITARPS5_SUCCESS;
}

// OLD FUNCTION REMOVED - Using new vitaki-fork compatible version below

static VitaRPS5Result takion_recv_message_cookie_ack(TakionConnection* conn) {
  uint8_t message[1 + 16 + 16];  // packet type + header + minimal payload
  size_t received_size = sizeof(message);

  VitaRPS5Result err =
      takion_recv(conn, message, &received_size,
                  TAKION_EXPECT_TIMEOUT_MS);  // Use configurable timeout
  if (err != VITARPS5_SUCCESS) {
    log_error("CRITICAL: Failed to receive COOKIE_ACK: %s",
              vitarps5_result_string(err));
    return err;
  }

  if (received_size < 17) {  // At least packet type + header
    log_error("CRITICAL: COOKIE_ACK packet too small: %zu bytes",
              received_size);
    return VITARPS5_ERROR_PROTOCOL;
  }

  if (message[0] != TAKION_PACKET_TYPE_CONTROL) {
    log_error("CRITICAL: Expected CONTROL packet, got type %02X", message[0]);
    return VITARPS5_ERROR_PROTOCOL;
  }

  // Parse message header
  const uint8_t* msg_header = message + 1;
  uint8_t chunk_type = msg_header[12];

  // CRITICAL PS5 COMPATIBILITY: Check for double INIT_ACK (vitaki-fork pattern)
  if (chunk_type == 2) {  // TAKION_CHUNK_TYPE_INIT_ACK
    log_info(
        "CRITICAL PS5-FIX: Received duplicate INIT_ACK, waiting for "
        "COOKIE_ACK");
    // PS5 sometimes sends duplicate INIT_ACK - read next message
    received_size = sizeof(message);
    err = takion_recv(conn, message, &received_size, TAKION_EXPECT_TIMEOUT_MS);
    if (err != VITARPS5_SUCCESS) {
      log_error(
          "CRITICAL: Failed to receive COOKIE_ACK after duplicate INIT_ACK: %s",
          vitarps5_result_string(err));
      return err;
    }

    if (received_size < 17) {
      log_error(
          "CRITICAL: COOKIE_ACK packet too small after duplicate INIT_ACK: %zu "
          "bytes",
          received_size);
      return VITARPS5_ERROR_PROTOCOL;
    }

    // Re-parse the actual COOKIE_ACK message
    msg_header = message + 1;
    chunk_type = msg_header[12];
  }

  if (chunk_type != 0xb) {  // TAKION_CHUNK_TYPE_COOKIE_ACK
    log_error("CRITICAL: Expected COOKIE_ACK chunk (type 0xb), got type %d",
              chunk_type);
    return VITARPS5_ERROR_PROTOCOL;
  }

  log_info("CRITICAL: Received COOKIE_ACK from PS5 - handshake complete!");
  return VITARPS5_SUCCESS;
}

// CRITICAL PS5 COMPATIBILITY: Clear preliminary data from socket buffers
// This is essential - PS5 sends stale data on socket connect that corrupts
// handshake
static VitaRPS5Result takion_read_extra_sock_messages(TakionConnection* conn) {
  log_info(
      "CRITICAL PS5-FIX: Clearing socket buffers before handshake (vitaki-fork "
      "pattern)");

  // Stop trying after 1 second (like vitaki-fork line 1667)
  uint64_t start_time = takion_get_timestamp();
  uint64_t timeout_us = 1000 * 1000;  // 1 second in microseconds

  while (true) {
    uint64_t now = takion_get_timestamp();
    if ((now - start_time) > timeout_us) {
      log_info(
          "CRITICAL PS5-FIX: Socket buffer cleanup completed (timeout "
          "reached)");
      return VITARPS5_SUCCESS;  // Timeout is expected and OK
    }

    // Check if data is available without blocking (like vitaki-fork line 1675)
    VitaRPS5Result select_result = takion_select_single(
        conn, conn->control_socket, false, 200);  // 200ms poll
    if (select_result == VITARPS5_ERROR_TIMEOUT) {
      // No more data to read - socket is clean
      log_info(
          "CRITICAL PS5-FIX: Socket buffer cleanup completed (no more data)");
      return VITARPS5_SUCCESS;
    }
    if (select_result != VITARPS5_SUCCESS) {
      log_warning("CRITICAL PS5-FIX: Socket buffer cleanup failed: %s",
                  vitarps5_result_string(select_result));
      return VITARPS5_SUCCESS;  // Continue anyway - not fatal
    }

    // CRITICAL FIX: Read and discard preliminary data using standard recv()
    // (vitaki-fork pattern)
    uint8_t discard_buffer[1500];  // MTU-sized buffer
    int received =
        recv(conn->control_socket, discard_buffer, sizeof(discard_buffer), 0);
    if (received < 0) {
      log_warning("CRITICAL PS5-FIX: Socket recv error during cleanup: 0x%08X",
                  received);
      return VITARPS5_SUCCESS;  // Continue anyway - socket may be clean now
    }

    if (received > 0) {
      log_info("CRITICAL PS5-FIX: Discarded %d bytes of preliminary PS5 data",
               received);
      // Continue loop to read more data if available
    }
  }
}

// CRITICAL: Complete Takion handshake implementation (like vitaki-fork line
// 792)
static VitaRPS5Result takion_handshake(TakionConnection* conn,
                                       uint32_t* seq_num_remote_initial) {
  VitaRPS5Result err;

  log_info("CRITICAL: Starting complete Takion handshake sequence");
  log_info(
      "CRITICAL: Socket buffers already cleared in main thread for optimal "
      "timing");

  // STEP 0: Test PS5 reachability first
  log_info("CRITICAL: Step 0 - Testing PS5 reachability");
  VitaRPS5Result reachability_result = takion_test_ps5_reachability(conn);
  if (reachability_result != VITARPS5_SUCCESS) {
    log_error(
        "CRITICAL: PS5 reachability test failed - network issue detected");
    return reachability_result;
  }

  // INIT ->
  log_info("CRITICAL: Step 1 - Sending INIT message (vitaki-fork format)");
  err = takion_send_message_init(conn);
  if (err != VITARPS5_SUCCESS) {
    log_error("CRITICAL: Failed to send INIT: %s", vitarps5_result_string(err));
    return err;
  }

  log_info("CRITICAL: INIT sent successfully");

  // INIT_ACK <-
  log_info("CRITICAL: Step 2 - Waiting for INIT_ACK");
  TakionMessagePayloadInitAck init_ack_payload;
  err = takion_recv_message_init_ack(conn, &init_ack_payload);
  if (err != VITARPS5_SUCCESS) {
    log_error("CRITICAL: Failed to receive INIT_ACK: %s",
              vitarps5_result_string(err));
    return err;
  }

  if (init_ack_payload.tag == 0) {
    log_error("CRITICAL: Remote tag in INIT_ACK is 0");
    return VITARPS5_ERROR_PROTOCOL;
  }

  log_info("CRITICAL: INIT_ACK received successfully - remote_tag=0x%08X",
           init_ack_payload.tag);
  conn->tag_remote = init_ack_payload.tag;  // Store remote tag
  *seq_num_remote_initial = conn->tag_remote;

  // COOKIE ->
  log_info("CRITICAL: Step 3 - Sending COOKIE message");
  err = takion_send_message_cookie(conn, init_ack_payload.cookie);
  if (err != VITARPS5_SUCCESS) {
    log_error("CRITICAL: Failed to send COOKIE: %s",
              vitarps5_result_string(err));
    return err;
  }

  log_info("CRITICAL: COOKIE sent successfully");

  // COOKIE_ACK <-
  log_info("CRITICAL: Step 4 - Waiting for COOKIE_ACK");
  err = takion_recv_message_cookie_ack(conn);
  if (err != VITARPS5_SUCCESS) {
    log_error("CRITICAL: Failed to receive COOKIE_ACK: %s",
              vitarps5_result_string(err));
    return err;
  }

  log_info("CRITICAL: ✅ COMPLETE TAKION HANDSHAKE SUCCESS! PS5 connected.");

  return VITARPS5_SUCCESS;
}

// CRITICAL FIX: Update packet processing for real Takion protocol format
static VitaRPS5Result takion_process_packet(TakionConnection* conn,
                                            const uint8_t* data, size_t size) {
  if (size < 1) {
    log_error("Received packet too small: %zu bytes", size);
    return VITARPS5_ERROR_PROTOCOL;
  }

  // Real Takion protocol: first byte is the packet type
  uint8_t base_packet_type = data[0] & 0xF;  // Lower nibble is the base type

  log_debug("VITAKI-FORK: Received packet with base type %d, size %zu",
            base_packet_type, size);

  // For control messages, we need to parse the message header
  if (base_packet_type != TAKION_PACKET_TYPE_CONTROL) {
    log_debug("Non-control packet type %d - skipping for now",
              base_packet_type);
    return VITARPS5_SUCCESS;
  }

  // Parse control message (minimum: 1 + 16 bytes for packet type + header)
  if (size < 17) {
    log_error("Control message too small: %zu bytes", size);
    return VITARPS5_ERROR_PROTOCOL;
  }

  // Parse message header starting at byte 1
  const uint8_t* msg_header = data + 1;
  uint32_t tag = ntohl(*(uint32_t*)(msg_header + 0));
  uint32_t key_pos = ntohl(*(uint32_t*)(msg_header + 8));
  uint8_t chunk_type = msg_header[12];
  uint8_t chunk_flags = msg_header[13];
  uint16_t payload_size = ntohs(*(uint16_t*)(msg_header + 14));

  // Suppress unused variable warnings for now
  (void)key_pos;
  (void)chunk_flags;

  log_debug(
      "VITAKI-FORK: Control message - tag=0x%08X, chunk_type=%d, "
      "payload_size=%d",
      tag, chunk_type, payload_size);

  // Process based on chunk type (the real protocol uses chunk types, not packet
  // types)
  switch (chunk_type) {
    case 2:  // TAKION_CHUNK_TYPE_INIT_ACK
      if (conn->state == TAKION_STATE_INIT_SENT) {
        log_info("VITAKI-FORK: Received INIT_ACK - PS5 responded to our INIT!");

        // Parse INIT_ACK payload to get remote tag and cookie
        if (payload_size >= 16 + 32) {  // 16 byte header + 32 byte cookie
          const uint8_t* payload = data + 1 + 16;  // Skip packet type + header
          uint32_t remote_tag = ntohl(*(uint32_t*)(payload + 0));

          log_info("VITAKI-FORK: Remote tag from PS5: 0x%08X", remote_tag);

          // Store remote tag for future messages
          conn->session_id = remote_tag;

          takion_set_state(conn, TAKION_STATE_INIT_ACK_RECEIVED);
          // TODO: Send COOKIE with the cookie data from INIT_ACK
          takion_set_state(conn, TAKION_STATE_CONNECTED);
        }
      }
      break;

    case 0xb:  // TAKION_CHUNK_TYPE_COOKIE_ACK
      if (conn->state == TAKION_STATE_COOKIE_SENT) {
        log_info("VITAKI-FORK: Received COOKIE_ACK - handshake complete!");
        takion_set_state(conn, TAKION_STATE_CONNECTED);
      }
      break;

    case 0:  // TAKION_CHUNK_TYPE_DATA - CRITICAL for BANG/STREAMINFO responses
      log_info("VITAKI-FORK: Received DATA chunk (payload_size=%d)",
               payload_size);
      if (payload_size > 0 && conn->data_callback) {
        const uint8_t* payload = data + 1 + 16;  // Skip packet type + header

        // CRITICAL: Parse actual data type from payload to distinguish protobuf
        // vs video
        int data_type = 0;  // Default to protobuf for compatibility

        // Check if this might be video data by examining payload header
        if (payload_size >= 4) {
          // Check for video packet markers (this is a heuristic)
          uint32_t possible_marker = *(uint32_t*)payload;
          if (possible_marker == 0x00000001 ||  // H.264 NAL unit marker
              (payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x00 &&
               payload[3] == 0x01)) {
            data_type = 1;  // VIDEO_DATA_TYPE
            log_info(
                "CRITICAL: Detected potential video data (marker=0x%08X, "
                "size=%d)",
                possible_marker, payload_size);
          }
        }

        log_info(
            "VITAKI-FORK: Calling streamconnection data callback (type=%d, "
            "size=%d)",
            data_type, payload_size);
        conn->data_callback(conn->callback_user, data_type, (uint8_t*)payload,
                            payload_size);
      }
      break;

    default:
      log_debug("VITAKI-FORK: Unhandled chunk type: %d", chunk_type);
      break;
  }

  return VITARPS5_SUCCESS;
}

static void takion_set_state(TakionConnection* conn, TakionState new_state) {
  if (!conn || conn->state == new_state) {
    return;
  }

  TakionState old_state = conn->state;
  conn->state = new_state;
  conn->stats.current_state = new_state;

  log_info("Takion state changed: %s -> %s", takion_state_string(old_state),
           takion_state_string(new_state));

  if (conn->config.state_callback) {
    conn->config.state_callback(new_state, conn->config.user_data);
  }
}

// FIXME: Implement these when we need the full handshake
// static VitaRPS5Result takion_send_bang(TakionConnection* conn) { return
// VITARPS5_SUCCESS; } static VitaRPS5Result
// takion_send_streaminfo_request(TakionConnection* conn) { return
// VITARPS5_SUCCESS; }

static uint64_t takion_get_timestamp(void) {
  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  return tick.tick;
}

// CRITICAL FIX: Close existing sockets for retry attempts
static void takion_close_sockets(TakionConnection* conn) {
  if (!conn) {
    return;
  }

  log_info("CRITICAL FIX: Closing existing sockets for retry attempt");

  if (conn->control_socket >= 0) {
    log_debug("Closing control socket (FD: %d)", conn->control_socket);
    sceNetSocketClose(conn->control_socket);
    conn->control_socket = -1;
  }

  if (conn->stream_socket >= 0) {
    log_debug("Closing stream socket (FD: %d)", conn->stream_socket);
    sceNetSocketClose(conn->stream_socket);
    conn->stream_socket = -1;
  }

  log_info("CRITICAL FIX: Sockets closed successfully");
}

// CRITICAL FIX: Recreate sockets for retry attempts
static VitaRPS5Result takion_recreate_sockets(TakionConnection* conn) {
  if (!conn) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  log_info("CRITICAL FIX: Recreating sockets for retry attempt");

  // Create control socket
  conn->control_socket = sceNetSocket("takion_control", SCE_NET_AF_INET,
                                      SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
  if (conn->control_socket < 0) {
    log_error("CRITICAL: Failed to recreate control socket: 0x%08X",
              conn->control_socket);
    return VITARPS5_ERROR_NETWORK;
  }
  log_debug("CRITICAL FIX: Control socket recreated (FD: %d)",
            conn->control_socket);

  // Set control socket buffer size
  int rcvbuf_size = TAKION_A_RWND;  // 102400 bytes
  int result =
      sceNetSetsockopt(conn->control_socket, SCE_NET_SOL_SOCKET,
                       SCE_NET_SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size));
  if (result < 0) {
    log_warn("Failed to set control socket receive buffer: 0x%08X", result);
  }

  // Connect control socket
  int connect_result = sceNetConnect(conn->control_socket,
                                     (const SceNetSockaddr*)&conn->remote_addr,
                                     sizeof(conn->remote_addr));
  if (connect_result < 0) {
    log_error("CRITICAL: Failed to connect control socket: 0x%08X",
              connect_result);
    sceNetSocketClose(conn->control_socket);
    conn->control_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  }
  log_debug("CRITICAL FIX: Control socket connected to %s:%d",
            conn->config.remote_ip, conn->config.control_port);

  // Create stream socket
  conn->stream_socket = sceNetSocket("takion_stream", SCE_NET_AF_INET,
                                     SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
  if (conn->stream_socket < 0) {
    log_error("CRITICAL: Failed to recreate stream socket: 0x%08X",
              conn->stream_socket);
    sceNetSocketClose(conn->control_socket);
    conn->control_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  }
  log_debug("CRITICAL FIX: Stream socket recreated (FD: %d)",
            conn->stream_socket);

  // Set stream socket buffer size
  result =
      sceNetSetsockopt(conn->stream_socket, SCE_NET_SOL_SOCKET,
                       SCE_NET_SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size));
  if (result < 0) {
    log_warn("Failed to set stream socket receive buffer: 0x%08X", result);
  }

  // Connect stream socket
  connect_result = sceNetConnect(conn->stream_socket,
                                 (const SceNetSockaddr*)&conn->stream_addr,
                                 sizeof(conn->stream_addr));
  if (connect_result < 0) {
    log_error("CRITICAL: Failed to connect stream socket: 0x%08X",
              connect_result);
    sceNetSocketClose(conn->control_socket);
    sceNetSocketClose(conn->stream_socket);
    conn->control_socket = -1;
    conn->stream_socket = -1;
    return VITARPS5_ERROR_NETWORK;
  }
  log_debug("CRITICAL FIX: Stream socket connected to %s:%d",
            conn->config.remote_ip, conn->config.stream_port);

  // Set send buffer sizes
  int sndbuf_size = 64 * 1024;  // 64KB for send buffers
  sceNetSetsockopt(conn->control_socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDBUF,
                   &sndbuf_size, sizeof(sndbuf_size));
  sceNetSetsockopt(conn->stream_socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDBUF,
                   &sndbuf_size, sizeof(sndbuf_size));

  log_info(
      "CRITICAL FIX: Sockets recreated successfully - RCV=%d, SND=%d bytes",
      rcvbuf_size, sndbuf_size);

  // Clear socket buffers after recreation
  VitaRPS5Result cleanup_result = takion_read_extra_sock_messages(conn);
  if (cleanup_result != VITARPS5_SUCCESS) {
    log_warning("CRITICAL: Socket buffer cleanup had issues but continuing: %s",
                vitarps5_result_string(cleanup_result));
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_set_data_callback(
    TakionConnection* connection,
    void (*callback)(void* user, int data_type, uint8_t* buf, size_t buf_size),
    void* user) {
  if (!connection) {
    log_error("takion_set_data_callback: Invalid connection");
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  connection->data_callback = callback;
  connection->callback_user = user;

  log_info("VITAKI-FORK: Data callback registered for streamconnection");
  return VITARPS5_SUCCESS;
}

// CRITICAL: New vitaki-fork compatible packet format functions

void takion_write_message_header(uint8_t* buf, uint32_t tag, uint32_t key_pos,
                                 uint8_t chunk_type, uint8_t chunk_flags,
                                 size_t payload_data_size) {
  // CRITICAL FIX: Use EXACT vitaki-fork message header construction
  // Match vitaki-fork/lib/src/takion.c line ~1450 byte-for-byte

  // Bytes 0-3: tag (network byte order) - EXACTLY like vitaki-fork
  *((uint32_t*)(buf + 0)) = htonl(tag);

  // Bytes 4-7: GMAC field (4 bytes, initialized to zero) - EXACTLY like
  // vitaki-fork
  memset(buf + 4, 0, CHIAKI_GKCRYPT_GMAC_SIZE);

  // Bytes 8-11: key_pos (network byte order) - EXACTLY like vitaki-fork
  *((uint32_t*)(buf + 8)) = htonl(key_pos);

  // Byte 12: chunk_type - EXACTLY like vitaki-fork (buf + 0xc)
  *(buf + 0xc) = chunk_type;

  // Byte 13: chunk_flags - EXACTLY like vitaki-fork (buf + 0xd)
  *(buf + 0xd) = chunk_flags;

  // Bytes 14-15: payload_size (exact payload bytes). PS5 expects 0x0010 for
  // INIT. Do not add 4.
  *((uint16_t*)(buf + 0xe)) = htons((uint16_t)(payload_data_size));

  log_debug(
      "VITAKI-FORK EXACT: Message header written - tag=0x%08X, key_pos=0x%08X, "
      "chunk_type=%d, flags=%d, payload_size=%zu",
      tag, key_pos, chunk_type, chunk_flags, payload_data_size);
}

VitaRPS5Result takion_send_message_init(TakionConnection* connection) {
  if (!connection) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // CRITICAL FIX: Validate socket state before sending INIT message
  if (connection->control_socket < 0) {
    log_error(
        "CRITICAL: Invalid control socket state (FD: %d) - cannot send INIT",
        connection->control_socket);
    return VITARPS5_ERROR_INVALID_STATE;
  }

  log_info("🚀 INIT DEBUG: Socket validation passed - Control socket FD: %d",
           connection->control_socket);
  log_info(
      "🚀 INIT DEBUG: Connection state - local_tag: 0x%08X, remote_tag: 0x%08X",
      connection->tag_local, connection->tag_remote);

  // CRITICAL FIX: Build INIT message EXACTLY like vitaki-fork
  // Total: 1 (packet type) + 16 (header) + 16 (payload) = 33 bytes
  uint8_t message[1 + TAKION_MESSAGE_HEADER_SIZE +
                  0x10];  // Use 0x10 like vitaki-fork
  message[0] = TAKION_PACKET_TYPE_CONTROL;

  // CRITICAL FIX: Use EXACT vitaki-fork calling pattern
  // vitaki-fork: takion_write_message_header(message + 1, takion->tag_remote,
  // 0, TAKION_CHUNK_TYPE_INIT, 0, 0x10); At INIT time, takion->tag_remote = 0
  // (we haven't received INIT_ACK yet)
  takion_write_message_header(message + 1, connection->tag_remote, 0,
                              TAKION_CHUNK_TYPE_INIT, 0, 0x10);

  // CRITICAL FIX: Build payload EXACTLY like vitaki-fork
  // vitaki-fork pattern from line ~1460:
  uint8_t* pl = message + 1 + TAKION_MESSAGE_HEADER_SIZE;
  *((uint32_t*)(pl + 0)) = htonl(connection->tag_local);     // tag_local
  *((uint32_t*)(pl + 4)) = htonl(TAKION_A_RWND);             // a_rwnd
  *((uint16_t*)(pl + 8)) = htons(TAKION_OUTBOUND_STREAMS);   // outbound_streams
  *((uint16_t*)(pl + 0xa)) = htons(TAKION_INBOUND_STREAMS);  // inbound_streams
  *((uint32_t*)(pl + 0xc)) = htonl(
      connection
          ->tag_local);  // initial_seq_num (use tag_local like vitaki-fork)

  // CRITICAL DEBUG: Verify EXACT vitaki-fork format
  log_info("🔍 VITAKI-FORK EXACT: Sending INIT message with exact format");
  log_info(
      "  - Header tag: 0x%08X (connection->tag_remote, should be 0 for INIT)",
      connection->tag_remote);
  log_info("  - Payload tag_local: 0x%08X (our local tag for PS5 to learn)",
           connection->tag_local);
  log_info(
      "  - Payload fields: tag=0x%08X rwnd=0x%08X streams=0x%04X/0x%04X "
      "seq=0x%08X",
      connection->tag_local, TAKION_A_RWND, TAKION_OUTBOUND_STREAMS,
      TAKION_INBOUND_STREAMS, connection->tag_local);
  log_info(
      "  - SCTP Protocol: INIT header_tag=tag_remote (0), PS5 responds with "
      "its tag in INIT_ACK");

  // CRITICAL: Verify message header byte order (the fix we implemented)
  log_info("🔍 HEADER VERIFICATION: chunk_type/chunk_flags byte order:");
  log_info("  - Byte 12 (chunk_type): 0x%02X (expected: 0x01)",
           message[1 + 12]);
  log_info("  - Byte 13 (chunk_flags): 0x%02X (expected: 0x00)",
           message[1 + 13]);
  if (message[1 + 12] == 0x01 && message[1 + 13] == 0x00) {
    log_info("  ✅ CHUNK TYPE/FLAGS BYTE ORDER: CORRECT (fix applied)");
  } else {
    log_error(
        "  ❌ CHUNK TYPE/FLAGS BYTE ORDER: INCORRECT - PS5 will ignore this "
        "message!");
  }

  // Hex dump the complete INIT message for analysis
  log_info("🔍 INIT DEBUG: Complete INIT message (%zu bytes):",
           sizeof(message));
  for (size_t i = 0; i < sizeof(message); i += 16) {
    char hex_line[64] = {0};
    for (size_t j = 0; j < 16 && (i + j) < sizeof(message); j++) {
      sprintf(hex_line + strlen(hex_line), "%02X ", message[i + j]);
    }
    log_info("  %04X: %s", (unsigned int)i, hex_line);
  }

  // CRITICAL FIX: Use send (macros map send → sceNetSend)
  // Socket macros automatically handle the conversion to sceNetSend for PS Vita
  // Send INIT on control socket
  int sent = -1;
  if (connection->control_socket >= 0)
    sent = send(connection->control_socket, message, sizeof(message), 0);
  // Also send on stream socket to be robust to implementations expecting INIT
  // on video port
  if (connection->stream_socket >= 0) {
    int sent2 = send(connection->stream_socket, message, sizeof(message), 0);
    (void)sent2;
  }
  if (sent < 0) {
    log_error("❌ INIT DEBUG: Failed to send INIT message: 0x%08X", sent);
    return VITARPS5_ERROR_NETWORK;
  }

  if (sent != sizeof(message)) {
    log_error(
        "⚠️ INIT DEBUG: Partial INIT send - expected %zu bytes, sent %d bytes",
        sizeof(message), sent);
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("✅ INIT DEBUG: Successfully sent INIT message to PS5 (%d bytes)",
           sent);
  log_info("📡 INIT DEBUG: Waiting for PS5 to respond with INIT_ACK...");

  // CRITICAL DEBUG: Network packet verification
  log_info("🌐 NETWORK DEBUG: Packet transmission verification:");
  log_info("  📍 Destination: %s:%d (PS5 control port)",
           connection->config.remote_ip, TAKION_CONTROL_PORT);
  log_info("  📦 UDP packet: %d bytes sent via socket FD %d", sent,
           connection->control_socket);
  log_info(
      "  🔍 Packet should be visible in network capture as UDP %s:%s -> %s:%d",
      "192.168.1.xxx", "ephemeral", connection->config.remote_ip,
      connection->config.control_port);

  // CRITICAL DEBUG: Verify socket is ready to receive INIT_ACK
  log_info("🔍 SOCKET DEBUG: Control socket FD=%d, state verification:",
           connection->control_socket);

  // Check socket error state using PS Vita API
  int sock_error = 0;
  unsigned int sock_error_len = sizeof(sock_error);
  if (sceNetGetsockopt(connection->control_socket, SCE_NET_SOL_SOCKET,
                       SCE_NET_SO_ERROR, &sock_error, &sock_error_len) == 0) {
    log_info("  ✅ Socket error state: %d (0=OK)", sock_error);
  } else {
    log_warning("  ⚠️ Failed to get socket error state");
  }

  // Get and log socket local address for network debugging using PS Vita API
  SceNetSockaddrIn local_addr;
  unsigned int addr_len = sizeof(local_addr);
  if (sceNetGetsockname(connection->control_socket,
                        (SceNetSockaddr*)&local_addr, &addr_len) == 0) {
    char local_ip[16];
    sceNetInetNtop(SCE_NET_AF_INET, &local_addr.sin_addr, local_ip,
                   sizeof(local_ip));
    log_info("  📍 Local socket: %s:%d", local_ip, ntohs(local_addr.sin_port));
    log_info("  🔍 Wireshark filter: udp and host %s and port %d",
             connection->config.remote_ip, connection->config.control_port);
  } else {
    log_warning("  ⚠️ Failed to get local socket address");
  }

  log_info("  🔄 Expecting INIT_ACK response within %d ms...",
           TAKION_EXPECT_TIMEOUT_MS);

  // CRITICAL DEBUG: Enhanced hex dump with byte-by-byte verification
  log_info(
      "CRITICAL FIX: INIT message hex dump (%d bytes) - structured format:",
      sent);
  for (int i = 0; i < sent; i += 16) {
    char hex_line[64] = {0};
    char ascii_line[20] = {0};
    int hex_pos = 0, ascii_pos = 0;

    for (int j = 0; j < 16 && (i + j) < sent; j++) {
      hex_pos += snprintf(hex_line + hex_pos, sizeof(hex_line) - hex_pos,
                          "%02X ", message[i + j]);
      ascii_line[ascii_pos++] = (message[i + j] >= 32 && message[i + j] <= 126)
                                    ? message[i + j]
                                    : '.';
    }
    ascii_line[ascii_pos] = '\0';
    log_info("  %04X: %-48s %s", i, hex_line, ascii_line);
  }

  // CRITICAL: Detailed format validation with PROTOCOL FIX
  log_info("🔍 PROTOCOL FIXED FORMAT VALIDATION:");
  log_info("  📦 Packet format (33 bytes total):");
  log_info("    - Byte 0: Packet type = 0x%02X (expect: 0x00=CONTROL)",
           message[0]);
  log_info("    - Bytes 1-16: Message header (16 bytes)");
  log_info(
      "      * Header tag (1-4): 0x%02X%02X%02X%02X (FIXED: 0x00000000 for "
      "INIT)",
      message[1], message[2], message[3], message[4]);
  log_info("      * Header key_pos (5-12): 0x%02X%02X%02X%02X%02X%02X%02X%02X",
           message[5], message[6], message[7], message[8], message[9],
           message[10], message[11], message[12]);
  log_info("      * Chunk type (13): 0x%02X (expect: 0x01=INIT)", message[13]);
  log_info("      * Chunk flags (14): 0x%02X (expect: 0x00)", message[14]);
  log_info("      * Payload size (15-16): 0x%02X%02X (expect: 0x0010)",
           message[15], message[16]);
  log_info("    - Bytes 17-32: INIT payload (16 bytes)");
  log_info("      * Payload tag (17-20): 0x%02X%02X%02X%02X (tag_local=0x%08X)",
           message[17], message[18], message[19], message[20],
           connection->tag_local);
  log_info("      * A_RWND (21-24): 0x%02X%02X%02X%02X (0x%08X)", message[21],
           message[22], message[23], message[24], TAKION_A_RWND);
  log_info("      * Streams (25-28): 0x%02X%02X%02X%02X (%d/%d)", message[25],
           message[26], message[27], message[28], TAKION_OUTBOUND_STREAMS,
           TAKION_INBOUND_STREAMS);
  log_info("      * Initial seq (29-32): 0x%02X%02X%02X%02X (0x%08X)",
           message[29], message[30], message[31], message[32],
           connection->tag_local);

  takion_set_state(connection, TAKION_STATE_INIT_SENT);
  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_send_message_cookie(TakionConnection* connection,
                                          const uint8_t* cookie_data) {
  if (!connection || !cookie_data) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Build COOKIE message in vitaki-fork format
  uint8_t message[1 + TAKION_MESSAGE_HEADER_SIZE +
                  TAKION_COOKIE_SIZE];      // packet_type + header + cookie
  message[0] = TAKION_PACKET_TYPE_CONTROL;  // First byte: CONTROL packet type

  // Write message header (use remote tag, key_pos=0, chunk_type=COOKIE)
  takion_write_message_header(message + 1, connection->tag_remote, 0,
                              TAKION_CHUNK_TYPE_COOKIE, 0, TAKION_COOKIE_SIZE);

  // Write COOKIE payload (32 bytes)
  uint8_t* payload = message + 1 + TAKION_MESSAGE_HEADER_SIZE;
  memcpy(payload, cookie_data, TAKION_COOKIE_SIZE);

  // CRITICAL FIX: Use send (macros map send → sceNetSend)
  int sent = send(connection->control_socket, message, sizeof(message), 0);
  if (sent < 0) {
    log_error("Failed to send COOKIE message: 0x%08X", sent);
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("VITAKI-FORK: Sent COOKIE message (tag=0x%08X, size=%d)",
           connection->tag_remote, sent);
  takion_set_state(connection, TAKION_STATE_COOKIE_SENT);

  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_parse_message_header(const uint8_t* buf, size_t buf_size,
                                           TakionMessageHeader* header,
                                           uint8_t** payload_out,
                                           size_t* payload_size_out) {
  if (!buf || buf_size < 1 + TAKION_MESSAGE_HEADER_SIZE || !header) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // First byte should be packet type
  if (buf[0] != TAKION_PACKET_TYPE_CONTROL) {
    log_debug("Not a control packet (type=%d)", buf[0]);
    return VITARPS5_ERROR_PROTOCOL;
  }

  // Parse message header (16 bytes after packet type)
  const uint8_t* msg_header = buf + 1;
  header->tag = ntohl(*(uint32_t*)(msg_header + 0));
  header->gmac = *(uint32_t*)(msg_header + 4);  // Keep in original byte order
  header->key_pos = ntohl(*(uint32_t*)(msg_header + 8));
  header->chunk_type = msg_header[12];
  header->chunk_flags = msg_header[13];
  header->payload_size = ntohs(*(uint16_t*)(msg_header + 14));

  // Validate payload size
  size_t expected_total_size =
      1 + TAKION_MESSAGE_HEADER_SIZE + header->payload_size - 4;
  if (buf_size < expected_total_size) {
    log_error("Invalid payload size: expected %zu, got %zu",
              expected_total_size, buf_size);
    return VITARPS5_ERROR_PROTOCOL;
  }

  // Set payload outputs
  if (payload_out) {
    *payload_out = (uint8_t*)(buf + 1 + TAKION_MESSAGE_HEADER_SIZE);
  }
  if (payload_size_out) {
    *payload_size_out = header->payload_size - 4;  // Subtract the "+4" overhead
  }

  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_send_feedback_state(TakionConnection* connection,
                                          const uint8_t* state_data,
                                          size_t state_size) {
  if (!connection || !state_data || state_size == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create feedback state packet with Takion protocol wrapper
  uint8_t packet[1 + TAKION_MESSAGE_HEADER_SIZE + state_size];
  packet[0] = TAKION_PACKET_TYPE_CONTROL;  // Control packet type

  // Write message header for feedback state
  takion_write_message_header(
      packet + 1,
      connection->tag_remote,         // Use remote tag
      connection->sequence_number++,  // Increment sequence number
      0x8E,  // Feedback state chunk type (from vitaki-fork)
      0,     // No flags
      state_size);

  // Copy feedback state payload
  memcpy(packet + 1 + TAKION_MESSAGE_HEADER_SIZE, state_data, state_size);

  // Send packet via UDP
  VitaRPS5Result result =
      takion_send_packet(connection, packet, sizeof(packet));
  if (result != VITARPS5_SUCCESS) {
    log_debug("Failed to send feedback state packet: %s",
              vitarps5_result_string(result));
    return result;
  }

  log_debug("Feedback state packet sent (%zu bytes)", sizeof(packet));
  return VITARPS5_SUCCESS;
}

VitaRPS5Result takion_send_feedback_history(TakionConnection* connection,
                                            const uint8_t* history_data,
                                            size_t history_size) {
  if (!connection || !history_data || history_size == 0) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Create feedback history packet with Takion protocol wrapper
  uint8_t packet[1 + TAKION_MESSAGE_HEADER_SIZE + history_size];
  packet[0] = TAKION_PACKET_TYPE_CONTROL;  // Control packet type

  // Write message header for feedback history
  takion_write_message_header(
      packet + 1,
      connection->tag_remote,         // Use remote tag
      connection->sequence_number++,  // Increment sequence number
      0x8F,  // Feedback history chunk type (from vitaki-fork)
      0,     // No flags
      history_size);

  // Copy feedback history payload
  memcpy(packet + 1 + TAKION_MESSAGE_HEADER_SIZE, history_data, history_size);

  // Send packet via UDP
  VitaRPS5Result result =
      takion_send_packet(connection, packet, sizeof(packet));
  if (result != VITARPS5_SUCCESS) {
    log_debug("Failed to send feedback history packet: %s",
              vitarps5_result_string(result));
    return result;
  }

  log_debug("Feedback history packet sent (%zu bytes)", sizeof(packet));
  return VITARPS5_SUCCESS;
}

// Port Management and Fallback Utility Functions

void takion_config_init_ps5(TakionConfig* config, const char* console_ip) {
  if (!config) return;

  memset(config, 0, sizeof(TakionConfig));
  strncpy(config->remote_ip, console_ip, sizeof(config->remote_ip) - 1);
  config->control_port = TAKION_CONTROL_PORT;
  config->video_port = TAKION_STREAM_PORT_VIDEO;
  config->audio_port = TAKION_STREAM_PORT_AUDIO;
  config->stream_port = config->video_port;  // Legacy compatibility
  config->console_version = TAKION_VERSION_PS5;
  config->timeout_ms = 10000;  // 10 seconds
  config->enable_session_init = true;
  config->enable_port_fallback = true;
  config->separate_audio_stream = true;

  log_debug("Initialized PS5 Takion config: control=%d, video=%d, audio=%d",
            config->control_port, config->video_port, config->audio_port);
}

void takion_config_init_ps4(TakionConfig* config, const char* console_ip) {
  if (!config) return;

  memset(config, 0, sizeof(TakionConfig));
  strncpy(config->remote_ip, console_ip, sizeof(config->remote_ip) - 1);
  config->control_port = TAKION_CONTROL_PORT;
  config->video_port = TAKION_STREAM_PORT_VIDEO;
  config->audio_port =
      TAKION_STREAM_PORT_VIDEO;  // PS4 uses same port for audio/video
  config->stream_port = config->video_port;  // Legacy compatibility
  config->console_version = TAKION_VERSION_PS4;
  config->timeout_ms = 10000;           // 10 seconds
  config->enable_session_init = false;  // PS4 doesn't support HTTP session init
  config->enable_port_fallback = true;
  config->separate_audio_stream = false;  // PS4 multiplexes audio/video

  log_debug("Initialized PS4 Takion config: control=%d, video=%d, audio=%d",
            config->control_port, config->video_port, config->audio_port);
}

uint16_t takion_get_fallback_port(uint16_t base_port, int attempt) {
  if (attempt <= 0) {
    return base_port;
  }

  uint16_t fallback_port = base_port + attempt;

  // Ensure we stay within the valid port range
  if (fallback_port > TAKION_PORT_RANGE_END) {
    // Wrap around within the range
    fallback_port =
        TAKION_PORT_RANGE_START +
        ((fallback_port - TAKION_PORT_RANGE_START) % TAKION_PORT_RANGE_SIZE);
  }

  log_debug("Fallback port for %d (attempt %d): %d", base_port, attempt,
            fallback_port);
  return fallback_port;
}

bool takion_is_valid_port(uint16_t port) {
  return (port >= TAKION_PORT_RANGE_START && port <= TAKION_PORT_RANGE_END);
}

void takion_get_recommended_ports(uint8_t console_version,
                                  uint16_t* control_port, uint16_t* video_port,
                                  uint16_t* audio_port) {
  if (!control_port || !video_port || !audio_port) {
    return;
  }

  // All console versions use the same port layout
  *control_port = TAKION_CONTROL_PORT;
  *video_port = TAKION_STREAM_PORT_VIDEO;

  if (console_version >= TAKION_VERSION_PS5) {
    // PS5 supports separate audio stream
    *audio_port = TAKION_STREAM_PORT_AUDIO;
  } else {
    // PS4 multiplexes audio with video
    *audio_port = TAKION_STREAM_PORT_VIDEO;
  }

  log_debug("Recommended ports for version %d: control=%d, video=%d, audio=%d",
            console_version, *control_port, *video_port, *audio_port);
}
