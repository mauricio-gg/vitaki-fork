#ifndef VITARPS5_DIAGNOSTICS_H
#define VITARPS5_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/error_codes.h"
#include "../core/vitarps5.h"

// Network quality levels
typedef enum {
  NETWORK_EXCELLENT = 0,  // < 10ms
  NETWORK_GOOD = 1,       // 10-30ms
  NETWORK_FAIR = 2,       // 30-50ms
  NETWORK_POOR = 3,       // > 50ms
  NETWORK_UNREACHABLE = 4
} NetworkQuality;

// Port connectivity test result
typedef struct {
  uint16_t port;
  char ip_address[16];
  bool success;
  bool service_available;
  int error_code;
  char error_message[256];
  int response_time_ms;
} DiagnosticResult;

// PS5 connectivity report
typedef struct {
  char ip_address[16];
  DiagnosticResult control_port;    // 9295
  DiagnosticResult stream_port;     // 9296
  DiagnosticResult wake_port;       // 9302
  DiagnosticResult discovery_port;  // 987
  bool remote_play_ready;
  bool can_wake;
  bool can_discover;
  char summary[256];
} PS5DiagnosticReport;

// Ping result
typedef struct {
  bool success;
  bool reachable;
  int latency_ms;
  NetworkQuality quality;
} PingResult;

// Diagnostic functions
VitaRPS5Result diagnostics_test_port(const char* ip_address, uint16_t port,
                                     uint32_t timeout_ms,
                                     DiagnosticResult* result);

// Protocol-specific port tests
VitaRPS5Result diagnostics_test_port_tcp(const char* ip_address, uint16_t port,
                                         uint32_t timeout_ms,
                                         DiagnosticResult* result);

VitaRPS5Result diagnostics_test_port_udp(const char* ip_address, uint16_t port,
                                         uint32_t timeout_ms,
                                         DiagnosticResult* result);

VitaRPS5Result diagnostics_test_ps5_connectivity(const char* ip_address,
                                                 PS5DiagnosticReport* report);

VitaRPS5Result diagnostics_ping(const char* ip_address, PingResult* result);

// Helper function to get user-friendly port description
const char* diagnostics_get_port_description(uint16_t port);

// Generate detailed error report for connection failures
void diagnostics_generate_error_report(VitaRPS5ErrorCode error,
                                       const char* context, char* report_buffer,
                                       size_t buffer_size);

#endif  // VITARPS5_DIAGNOSTICS_H