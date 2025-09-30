// connect_local.c (reference)
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../core/console_registration.h"
#include "../core/vitarps5.h"
#include "../discovery/discovery.h"
#include "../utils/logger.h"
#include "session_init.h"
#include "wake.h"

// --- Replace these with your actual socket I/O ---
extern int udp_send(const char *ip, uint16_t port, const void *buf, size_t len);
extern int udp_recv_timeout(char *out, size_t cap, int timeout_ms);
extern int http_send_and_recv_9295(const char *ip, const char *req,
                                   size_t req_len, SessionInitResponse *resp);
// --------------------------------------------------

static void sleep_ms(int ms) {
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

bool connect_local(const char *ps5_ip, ConsoleRegistration *reg) {
  // 1) Discovery
  char probe[128];
  int pn = build_discovery_probe(probe, sizeof(probe));
  if (pn < 0) return false;
  if (udp_send(ps5_ip, 9302, probe, (size_t)pn) != 0) return false;

  char respbuf[1024];
  int got = udp_recv_timeout(respbuf, sizeof(respbuf) - 1, 1500);
  if (got <= 0) return false;
  respbuf[got] = '\0';

  Ps5State st = PS5_STATE_UNKNOWN;
  if (!parse_discovery_response(respbuf, (size_t)got, &st)) return false;

  // 2) Optional wake using unified wake function
  if (st == PS5_STATE_STANDBY) {
    log_info("Console is in standby - attempting wake up");
    VitaRPS5Result wake_result = console_registration_wake_console(ps5_ip);

    if (wake_result == VITARPS5_SUCCESS) {
      log_info("Wake packet sent - waiting for console to start up");
      // Give the console time to warm up
      sleep_ms(12000);
      // Re-probe to check if console woke up
      if (udp_send(ps5_ip, 9302, probe, (size_t)pn) != 0) return false;
      got = udp_recv_timeout(respbuf, sizeof(respbuf) - 1, 1500);
      if (got <= 0) {
        log_warn("Console did not respond after wake attempt");
        return false;
      }
    } else {
      log_warn("Failed to wake console: %s",
               vitarps5_result_string(wake_result));
      return false;
    }

    // Parse response after wake attempt
    respbuf[got] = '\0';
    if (!parse_discovery_response(respbuf, (size_t)got, &st)) return false;
  }
  if (st != PS5_STATE_READY) {
    // Still not ready
    return false;
  }

  // 3) Session init (HTTP over TCP 9295)
  SessionInitResponse sresp = {0};
  bool ok = session_init_with_fallback(reg, http_send_and_recv_9295, &sresp);
  if (!ok) return false;

  // 4) From here, open/control UDP 9296/9297, start stream, etc.
  return true;
}