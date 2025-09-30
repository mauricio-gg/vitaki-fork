#include "diagnostics.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2common/net.h>
#include <stdio.h>
#include <string.h>

#include "../core/error_codes.h"
#include "../utils/logger.h"

// Protocol-aware port connectivity test
VitaRPS5Result diagnostics_test_port_tcp(const char* ip_address, uint16_t port,
                                         uint32_t timeout_ms,
                                         DiagnosticResult* result) {
  if (!ip_address || !result) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(result, 0, sizeof(DiagnosticResult));
  result->port = port;
  strncpy(result->ip_address, ip_address, sizeof(result->ip_address) - 1);

  log_info("DIAGNOSTICS: Testing TCP connectivity to %s:%d", ip_address, port);

  // Create TCP socket
  int sock =
      sceNetSocket("diag_tcp_socket", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
  if (sock < 0) {
    result->success = false;
    result->error_code = sock;
    snprintf(result->error_message, sizeof(result->error_message),
             "Failed to create TCP socket: 0x%08X", sock);
    log_error("DIAGNOSTICS: %s", result->error_message);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set timeout
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout_ms,
                   sizeof(timeout_ms));
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout_ms,
                   sizeof(timeout_ms));

  // Setup address
  SceNetSockaddrIn addr = {0};
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(port);

  int ret = sceNetInetPton(SCE_NET_AF_INET, ip_address, &addr.sin_addr);
  if (ret <= 0) {
    sceNetSocketClose(sock);
    result->success = false;
    result->error_code = ret;
    snprintf(result->error_message, sizeof(result->error_message),
             "Invalid IP address: %s", ip_address);
    log_error("DIAGNOSTICS: %s", result->error_message);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Record start time
  uint64_t start_time = sceKernelGetProcessTimeWide();

  // Attempt connection
  ret = sceNetConnect(sock, (SceNetSockaddr*)&addr, sizeof(addr));

  // Record end time
  uint64_t end_time = sceKernelGetProcessTimeWide();
  result->response_time_ms = (end_time - start_time) / 1000;

  sceNetSocketClose(sock);

  // Interpret results
  if (ret == 0) {
    result->success = true;
    result->service_available = true;
    snprintf(result->error_message, sizeof(result->error_message),
             "TCP port %d is open and accepting connections", port);
    log_info("DIAGNOSTICS: ✅ TCP port %d is OPEN (response time: %dms)", port,
             result->response_time_ms);
  } else {
    result->success = false;
    result->error_code = ret;

    // Detailed error interpretation
    switch (ret) {
      case 0x80410111:  // SCE_NET_ERROR_ECONNREFUSED (errno 111)
        result->service_available = false;
        snprintf(result->error_message, sizeof(result->error_message),
                 "Connection refused - TCP service not running on port %d",
                 port);
        log_warning("DIAGNOSTICS: ❌ TCP port %d REFUSED - service not running",
                    port);
        break;

      case 0x80410116:  // SCE_NET_ERROR_ETIMEDOUT
        result->service_available = false;
        snprintf(result->error_message, sizeof(result->error_message),
                 "Connection timed out - host may be down or port blocked");
        log_warning("DIAGNOSTICS: ⏱️ TCP port %d TIMEOUT - host down or blocked",
                    port);
        break;

      case 0x80410171:  // SCE_NET_ERROR_EHOSTUNREACH
        result->service_available = false;
        snprintf(result->error_message, sizeof(result->error_message),
                 "Host unreachable - check network connection");
        log_warning("DIAGNOSTICS: 🚫 TCP port %d UNREACHABLE - network issue",
                    port);
        break;

      default:
        result->service_available = false;
        snprintf(result->error_message, sizeof(result->error_message),
                 "TCP connection failed with error: 0x%08X", ret);
        log_warning("DIAGNOSTICS: ❓ TCP port %d ERROR: 0x%08X", port, ret);
        break;
    }
  }

  return result->success ? VITARPS5_SUCCESS : VITARPS5_ERROR_NETWORK;
}

// UDP port reachability test
VitaRPS5Result diagnostics_test_port_udp(const char* ip_address, uint16_t port,
                                         uint32_t timeout_ms,
                                         DiagnosticResult* result) {
  if (!ip_address || !result) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(result, 0, sizeof(DiagnosticResult));
  result->port = port;
  strncpy(result->ip_address, ip_address, sizeof(result->ip_address) - 1);

  log_info("DIAGNOSTICS: Testing UDP connectivity to %s:%d", ip_address, port);

  // Create UDP socket
  int sock =
      sceNetSocket("diag_udp_socket", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
  if (sock < 0) {
    result->success = false;
    result->error_code = sock;
    snprintf(result->error_message, sizeof(result->error_message),
             "Failed to create UDP socket: 0x%08X", sock);
    log_error("DIAGNOSTICS: %s", result->error_message);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set timeout
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout_ms,
                   sizeof(timeout_ms));
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout_ms,
                   sizeof(timeout_ms));

  // Setup address
  SceNetSockaddrIn addr = {0};
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(port);

  int ret = sceNetInetPton(SCE_NET_AF_INET, ip_address, &addr.sin_addr);
  if (ret <= 0) {
    sceNetSocketClose(sock);
    result->success = false;
    result->error_code = ret;
    snprintf(result->error_message, sizeof(result->error_message),
             "Invalid IP address: %s", ip_address);
    log_error("DIAGNOSTICS: %s", result->error_message);
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Record start time
  uint64_t start_time = sceKernelGetProcessTimeWide();

  // Send a small UDP packet to test reachability
  const char* test_packet = "ping";
  ret = sceNetSendto(sock, test_packet, strlen(test_packet), 0,
                     (SceNetSockaddr*)&addr, sizeof(addr));

  // Record end time
  uint64_t end_time = sceKernelGetProcessTimeWide();
  result->response_time_ms = (end_time - start_time) / 1000;

  sceNetSocketClose(sock);

  // For UDP, we can only check if the send succeeded
  if (ret >= 0) {
    result->success = true;
    result->service_available =
        true;  // UDP is connectionless, so we assume reachable if send succeeds
    snprintf(result->error_message, sizeof(result->error_message),
             "UDP port %d is reachable (sent %d bytes)", port, ret);
    log_info("DIAGNOSTICS: ✅ UDP port %d is REACHABLE (response time: %dms)",
             port, result->response_time_ms);
  } else {
    result->success = false;
    result->error_code = ret;
    result->service_available = false;
    snprintf(result->error_message, sizeof(result->error_message),
             "UDP send failed with error: 0x%08X", ret);
    log_warning("DIAGNOSTICS: ❌ UDP port %d UNREACHABLE - send failed: 0x%08X",
                port, ret);
  }

  return result->success ? VITARPS5_SUCCESS : VITARPS5_ERROR_NETWORK;
}

// Legacy function that routes to appropriate protocol test
VitaRPS5Result diagnostics_test_port(const char* ip_address, uint16_t port,
                                     uint32_t timeout_ms,
                                     DiagnosticResult* result) {
  // Route based on known PS5 port protocols
  switch (port) {
    case 9295:  // Session init - TCP
      return diagnostics_test_port_tcp(ip_address, port, timeout_ms, result);
    case 9296:  // Stream 1 - UDP
    case 9297:  // Stream 2 - UDP
    case 9302:  // Wake/Discovery - UDP
    case 987:   // Discovery - UDP
    case 997:   // PS5 advertised session port - TCP (from host-request-port)
      return diagnostics_test_port_tcp(ip_address, port, timeout_ms, result);
    default:
      // CRITICAL FIX: Default to UDP for unknown ports in PS5 range
      // Most PS5 communication uses UDP except for initial session setup
      if (port >= 9295 && port <= 9304) {
        log_info("✅ DIAGNOSTICS FIX: Unknown PS5 port %d defaulting to UDP",
                 port);
        return diagnostics_test_port_udp(ip_address, port, timeout_ms, result);
      }
      // For ports outside PS5 range, default to TCP
      return diagnostics_test_port_tcp(ip_address, port, timeout_ms, result);
  }
}

// Full PS5 connectivity diagnostic
VitaRPS5Result diagnostics_test_ps5_connectivity(const char* ip_address,
                                                 PS5DiagnosticReport* report) {
  if (!ip_address || !report) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(report, 0, sizeof(PS5DiagnosticReport));
  strncpy(report->ip_address, ip_address, sizeof(report->ip_address) - 1);

  log_info("=== PS5 CONNECTIVITY DIAGNOSTIC ===");
  log_info("Target: %s", ip_address);

  // Test control port (9295)
  diagnostics_test_port(ip_address, 9295, 2000, &report->control_port);

  // Test stream port (9296)
  diagnostics_test_port(ip_address, 9296, 2000, &report->stream_port);

  // Test wake port (9302)
  diagnostics_test_port(ip_address, 9302, 2000, &report->wake_port);

  // Test discovery port (987)
  diagnostics_test_port(ip_address, 987, 2000, &report->discovery_port);

  // Determine overall connectivity status
  report->remote_play_ready = report->control_port.service_available &&
                              report->stream_port.service_available;

  report->can_wake = report->wake_port.service_available;
  report->can_discover = report->discovery_port.service_available;

  // Generate summary
  if (report->remote_play_ready) {
    strncpy(report->summary, "PS5 Remote Play services are ready",
            sizeof(report->summary) - 1);
    log_info("✅ PS5 Remote Play is READY for connection");
  } else if (report->control_port.error_code == 0x80410111) {
    strncpy(report->summary,
            "PS5 is reachable but Remote Play service not running. Try waking "
            "the console.",
            sizeof(report->summary) - 1);
    log_warning("⚠️ PS5 needs to be woken up or Remote Play enabled");
  } else if (report->control_port.error_code == 0x80410171) {
    strncpy(report->summary,
            "Cannot reach PS5. Check network connection and IP address.",
            sizeof(report->summary) - 1);
    log_error("❌ PS5 is unreachable on the network");
  } else {
    strncpy(report->summary, "Unknown connectivity issue. Check PS5 settings.",
            sizeof(report->summary) - 1);
    log_error("❓ Unable to determine PS5 status");
  }

  log_info("=== DIAGNOSTIC COMPLETE ===");

  return VITARPS5_SUCCESS;
}

// Network latency test
VitaRPS5Result diagnostics_ping(const char* ip_address, PingResult* result) {
  if (!ip_address || !result) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  memset(result, 0, sizeof(PingResult));

  // Simple TCP connect test for latency measurement
  int sock =
      sceNetSocket("ping_socket", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
  if (sock < 0) {
    return VITARPS5_ERROR_NETWORK;
  }

  // Very short timeout for ping
  uint32_t timeout_ms = 1000;
  sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout_ms,
                   sizeof(timeout_ms));

  SceNetSockaddrIn addr = {0};
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(9295);  // Use control port for ping
  sceNetInetPton(SCE_NET_AF_INET, ip_address, &addr.sin_addr);

  uint64_t start = sceKernelGetProcessTimeWide();
  int ret = sceNetConnect(sock, (SceNetSockaddr*)&addr, sizeof(addr));
  uint64_t end = sceKernelGetProcessTimeWide();

  sceNetSocketClose(sock);

  if (ret == 0 || ret == 0x80410111) {  // Success or connection refused both
                                        // indicate host is up
    result->success = true;
    result->latency_ms = (end - start) / 1000;
    result->reachable = true;

    // Classify latency
    if (result->latency_ms < 10) {
      result->quality = NETWORK_EXCELLENT;
    } else if (result->latency_ms < 30) {
      result->quality = NETWORK_GOOD;
    } else if (result->latency_ms < 50) {
      result->quality = NETWORK_FAIR;
    } else {
      result->quality = NETWORK_POOR;
    }

    log_info("PING: %s - %dms (%s)", ip_address, result->latency_ms,
             result->quality == NETWORK_EXCELLENT ? "Excellent"
             : result->quality == NETWORK_GOOD    ? "Good"
             : result->quality == NETWORK_FAIR    ? "Fair"
                                                  : "Poor");
  } else {
    result->success = false;
    result->reachable = false;
    result->quality = NETWORK_UNREACHABLE;
    log_warning("PING: %s - unreachable", ip_address);
  }

  return result->success ? VITARPS5_SUCCESS : VITARPS5_ERROR_NETWORK;
}