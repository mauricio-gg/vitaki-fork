// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "chiaki_regist_vitaki.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logger.h"
#include "chiaki_base64_vitaki.h"
#include "chiaki_http_vitaki.h"
#include "chiaki_random.h"
#include "chiaki_rpcrypt_vitaki.h"
#include "chiaki_session.h"
#include "vitaki_time.h"
#include "vitaki_utils.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef uint32_t in_addr_t;
#else
#include <netdb.h>
#endif

#define REGIST_PORT 9295

#define SEARCH_REQUEST_SLEEP_MS 100
#define REGIST_SEARCH_TIMEOUT_MS 3000
#define REGIST_REPONSE_TIMEOUT_MS 3000

static void *regist_thread_func(void *user);
static ChiakiErrorCode regist_search(ChiakiRegist *regist,
                                     struct addrinfo *addrinfos,
                                     struct sockaddr *recv_addr,
                                     socklen_t *recv_addr_size);
static chiaki_socket_t regist_search_connect(ChiakiRegist *regist,
                                             struct addrinfo *addrinfos,
                                             struct sockaddr *send_addr,
                                             socklen_t *send_addr_len);
static chiaki_socket_t regist_request_connect(ChiakiRegist *regist,
                                              const struct sockaddr *addr,
                                              size_t addr_len);
static ChiakiErrorCode regist_recv_response(
    ChiakiRegist *regist, ChiakiRegisteredHost *host, chiaki_socket_t sock,
    ChiakiRPCrypt *rpcrypt, uint16_t remote_counter, char *send_buf,
    size_t send_buf_size);
static ChiakiErrorCode regist_parse_response_payload(ChiakiRegist *regist,
                                                     ChiakiRegisteredHost *host,
                                                     char *buf,
                                                     size_t buf_size);

CHIAKI_EXPORT ChiakiErrorCode chiaki_regist_start(ChiakiRegist *regist,
                                                  ChiakiLog *log,
                                                  const ChiakiRegistInfo *info,
                                                  ChiakiRegistCb cb,
                                                  void *cb_user) {
  // ENTRY POINT LOGGING - This confirms registration starts
  log_info("=== PS5 REGISTRATION START (Vitaki Implementation) ===");
  log_info("Target: %d, Host: %s, PIN: %08u", info ? info->target : -1,
           info && info->host ? info->host : "NULL", info ? info->pin : 0u);
  // We use psn_account_id, PSN Online ID not used in Vita flow

  regist->log = log;
  regist->info = *info;
#if !(defined(__SWITCH__) || defined(__PSVITA__))
  if (!regist->info.holepunch_info)
#endif
  {
    regist->info.host = strdup(regist->info.host);
    if (!regist->info.host) return CHIAKI_ERR_MEMORY;
  }

  ChiakiErrorCode err = CHIAKI_ERR_UNKNOWN;
  if (regist->info.psn_online_id) {
    regist->info.psn_online_id = strdup(regist->info.psn_online_id);
    if (!regist->info.psn_online_id) {
      err = CHIAKI_ERR_MEMORY;
      goto error_host;
    }
  }

  regist->cb = cb;
  regist->cb_user = cb_user;

  err = chiaki_stop_pipe_init(&regist->stop_pipe);
  if (err != CHIAKI_ERR_SUCCESS) goto error_psn_id;

  err = chiaki_thread_create(&regist->thread, regist_thread_func, regist);
  if (err != CHIAKI_ERR_SUCCESS) goto error_stop_pipe;

  return CHIAKI_ERR_SUCCESS;

error_stop_pipe:
  chiaki_stop_pipe_fini(&regist->stop_pipe);
error_psn_id:
  free((char *)regist->info.psn_online_id);
error_host:
  free((char *)regist->info.host);
  return err;
}

CHIAKI_EXPORT void chiaki_regist_fini(ChiakiRegist *regist) {
  chiaki_thread_join(&regist->thread, NULL);
  chiaki_stop_pipe_fini(&regist->stop_pipe);
  free((char *)regist->info.psn_online_id);
  free((char *)regist->info.host);
}

CHIAKI_EXPORT void chiaki_regist_stop(ChiakiRegist *regist) {
  chiaki_stop_pipe_stop(&regist->stop_pipe);
}

static void regist_event_simple(ChiakiRegist *regist,
                                ChiakiRegistEventType type) {
  ChiakiRegistEvent event = {0};
  event.type = type;
  regist->cb(&event, regist->cb_user);
}

static const char *const request_head_fmt =
    "POST %s HTTP/1.1\r\n HTTP/1.1\r\n"
    "HOST: %s\r\n"
    "User-Agent: remoteplay Windows\r\n"
    "Connection: close\r\n"
    "Content-Length: %llu\r\n";

static const char *request_path_ps5 = "/sie/ps5/rp/sess/rgst";
static const char *request_path_ps4 = "/sie/ps4/rp/sess/rgst";
static const char *request_path_ps4_pre10 = "/sce/rp/regist";

static const char *request_path(ChiakiTarget target) {
  switch (target) {
    case CHIAKI_TARGET_PS5_UNKNOWN:
    case CHIAKI_TARGET_PS5_1:
      return request_path_ps5;
    case CHIAKI_TARGET_PS4_8:
    case CHIAKI_TARGET_PS4_9:
      return request_path_ps4_pre10;
    default:
      return request_path_ps4;
  }
}

static const char *const request_rp_version_fmt = "RP-Version: %s\r\n";

static const char *const request_tail = "\r\n";

const char *client_type =
    "dabfa2ec873de5839bee8d3f4c0239c4282c07c25c6077a2931afcf0adc0d34f";
const char *client_type_ps4_pre10 = "Windows";

static const char *const request_inner_account_id_fmt =
    "Client-Type: %s\r\n"
    "Np-AccountId: %s\r\n";

static const char *const request_inner_online_id_fmt =
    "Client-Type: Windows\r\n"
    "Np-Online-Id: %s\r\n";

static int request_header_format(char *buf, size_t buf_size,
                                 size_t payload_size, ChiakiTarget target,
                                 char *regist_local_addr) {
  // Debug logging for header construction
  log_debug("Header format - target=%d, buf_size=%u, payload_size=%u", target,
            (unsigned)buf_size, (unsigned)payload_size);
  log_debug("Request path: %s", request_path(target));
  log_debug("Local addr: %s", regist_local_addr);

  int cur = snprintf(buf, buf_size, request_head_fmt, request_path(target),
                     regist_local_addr, (unsigned long long)payload_size);
  log_debug("Base header written: %d bytes", cur);

  if (cur < 0 || (unsigned)cur >= (unsigned)payload_size) {
    log_error("Base header size check failed: cur=%d, payload_size=%u", cur,
              (unsigned)payload_size);
    return -1;
  }

  // RP-Version header logic
  log_debug("RP-Version check: target=%d >= CHIAKI_TARGET_PS4_9=%d ? %s",
            target, CHIAKI_TARGET_PS4_9,
            (target >= CHIAKI_TARGET_PS4_9) ? "YES" : "NO");

  if (target >= CHIAKI_TARGET_PS4_9) {
    const char *rp_version_str = chiaki_rp_version_string(target);
    log_debug("RP-Version string for target %d: '%s'", target,
              rp_version_str ? rp_version_str : "NULL");

    size_t s = buf_size - cur;
    int r = snprintf(buf + cur, s, request_rp_version_fmt, rp_version_str);
    printf("[DEBUG] RP-Version header written: %d bytes\n", r);

    if (r < 0 || r >= s) {
      printf("[ERROR] RP-Version header size check failed: r=%d, s=%u\n", r,
             (unsigned)s);
      return -1;
    }
    cur += r;
  }

  size_t tail_size = strlen(request_tail) + 1;
  printf("[DEBUG] Adding tail: %u bytes\n", (unsigned)tail_size);

  if ((unsigned)cur + (unsigned)tail_size > (unsigned)payload_size) {
    printf(
        "[ERROR] Tail size check failed: cur=%d + tail_size=%u > "
        "payload_size=%u\n",
        cur, (unsigned)tail_size, (unsigned)payload_size);
    return -1;
  }

  memcpy(buf + cur, request_tail, tail_size);
  cur += (int)tail_size - 1;

  printf("[DEBUG] Final header size: %d bytes\n", cur);
  return cur;
}

#if !(defined(__SWITCH__) || defined(__PSVITA__))
CHIAKI_EXPORT ChiakiErrorCode chiaki_regist_request_payload_format(
    ChiakiTarget target, const uint8_t *ambassador, uint8_t *buf,
    size_t *buf_size, ChiakiRPCrypt *crypt, const char *psn_online_id,
    const uint8_t *psn_account_id, uint32_t pin,
    ChiakiHolepunchRegistInfo *holepunch_info)
#else
CHIAKI_EXPORT ChiakiErrorCode chiaki_regist_request_payload_format(
    ChiakiTarget target, const uint8_t *ambassador, uint8_t *buf,
    size_t *buf_size, ChiakiRPCrypt *crypt, const char *psn_online_id,
    const uint8_t *psn_account_id, uint32_t pin)
#endif
{
  size_t buf_size_val = *buf_size;
  static const size_t inner_header_off = 0x1e0;
  if (buf_size_val < inner_header_off) return CHIAKI_ERR_BUF_TOO_SMALL;
  memset(buf, 'A', inner_header_off);  // can be random

  if (target < CHIAKI_TARGET_PS4_10) {
    chiaki_rpcrypt_init_regist_ps4_pre10(crypt, ambassador, pin);
    chiaki_rpcrypt_aeropause_ps4_pre10(buf + 0x11c, crypt->ambassador);
  } else {
    size_t key_0_off = buf[0x18D] & 0x1F;
    size_t key_1_off = buf[0] >> 3;
    uint8_t aeropause[0x10];
    ChiakiErrorCode err;
#if !(defined(__SWITCH__) || defined(__PSVITA__))
    if (holepunch_info) {
      err = chiaki_rpcrypt_init_regist_psn(
          crypt, target, ambassador, key_0_off, holepunch_info->custom_data1,
          holepunch_info->data1, holepunch_info->data2);
      if (err != CHIAKI_ERR_SUCCESS) return err;
      err = chiaki_rpcrypt_aeropause_psn(target, key_1_off, aeropause,
                                         crypt->ambassador);
    } else
#endif
    {
      err =
          chiaki_rpcrypt_init_regist(crypt, target, ambassador, key_0_off, pin);
      if (err != CHIAKI_ERR_SUCCESS) return err;
      err = chiaki_rpcrypt_aeropause(target, key_1_off, aeropause,
                                     crypt->ambassador);
    }
    if (err != CHIAKI_ERR_SUCCESS) return err;
    memcpy(buf + 0xc7, aeropause + 8, 8);
    memcpy(buf + 0x191, aeropause, 8);
    psn_online_id = NULL;  // don't need this
  }

  int inner_header_size;
  if (psn_online_id)
    inner_header_size = snprintf((char *)buf + inner_header_off,
                                 buf_size_val - inner_header_off,
                                 request_inner_online_id_fmt, psn_online_id);
  else if (psn_account_id) {
    char account_id_b64[CHIAKI_PSN_ACCOUNT_ID_SIZE * 2];
    ChiakiErrorCode err =
        chiaki_base64_encode(psn_account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE,
                             account_id_b64, sizeof(account_id_b64));
    if (err != CHIAKI_ERR_SUCCESS) return err;
    inner_header_size = snprintf(
        (char *)buf + inner_header_off, buf_size_val - inner_header_off,
        request_inner_account_id_fmt,
        target < CHIAKI_TARGET_PS4_10 ? client_type_ps4_pre10 : client_type,
        account_id_b64);
  } else
    return CHIAKI_ERR_INVALID_DATA;
  if (inner_header_size < 0 ||
      inner_header_size >= buf_size_val - inner_header_off)
    return CHIAKI_ERR_BUF_TOO_SMALL;
  ChiakiErrorCode err =
      chiaki_rpcrypt_encrypt(crypt, 0, buf + inner_header_off,
                             buf + inner_header_off, inner_header_size);
  *buf_size = inner_header_off + inner_header_size;
  return err;
}

static void *regist_thread_func(void *user) {
  ChiakiRegist *regist = user;

  bool canceled = false;
  bool success = false;
  bool psn = false;
  // if holepunch info is filled out, this is a psn regist
#if !(defined(__SWITCH__) || defined(__PSVITA__))
  if (regist->info.holepunch_info) psn = true;
#endif

  ChiakiRPCrypt crypt;
  uint8_t ambassador[CHIAKI_RPCRYPT_KEY_SIZE];
  ChiakiErrorCode err =
      chiaki_random_bytes_crypt(ambassador, sizeof(ambassador));
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(regist->log, "Regist failed to generate random ambassador");
    goto fail;
  }

  uint8_t payload[0x400];
  size_t payload_size = sizeof(payload);
#if !(defined(__SWITCH__) || defined(__PSVITA__))
  err = chiaki_regist_request_payload_format(
      regist->info.target, ambassador, payload, &payload_size, &crypt,
      regist->info.psn_online_id, regist->info.psn_account_id, regist->info.pin,
      regist->info.holepunch_info);
#else
  err = chiaki_regist_request_payload_format(
      regist->info.target, ambassador, payload, &payload_size, &crypt,
      regist->info.psn_online_id, regist->info.psn_account_id,
      regist->info.pin);
#endif
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(regist->log, "Regist failed to format payload");
    log_error("Registration payload formatting failed with error: %d", err);
    goto fail;
  }
  log_info("Registration payload formatted successfully - %u bytes",
           (unsigned)payload_size);

  char request_header[0x100];
  // random local addr if our local addr is not provided
  char regist_local_addr[INET6_ADDRSTRLEN] = "10.0.2.15";
#if !(defined(__SWITCH__) || defined(__PSVITA__))
  if (regist->info.holepunch_info)
    memcpy(regist_local_addr, regist->info.holepunch_info->regist_local_ip,
           sizeof(regist_local_addr));
#endif
  int request_header_size = request_header_format(
      request_header, sizeof(request_header), payload_size, regist->info.target,
      regist_local_addr);

  if (request_header_size < 0 ||
      request_header_size >= sizeof(request_header)) {
    CHIAKI_LOGE(regist->log, "Regist failed to format request");
    log_error("Registration header formatting failed - size: %d",
              request_header_size);
    goto fail;
  }
  log_info("Registration header formatted successfully - %d bytes",
           request_header_size);

  // Log detailed request information for debugging
  CHIAKI_LOGI(regist->log, "=== PS5 REGISTRATION REQUEST DEBUG ===");
  CHIAKI_LOGI(regist->log, "Target: %d (PS5_1=%d, PS5_UNKNOWN=%d)",
              regist->info.target, CHIAKI_TARGET_PS5_1,
              CHIAKI_TARGET_PS5_UNKNOWN);
  CHIAKI_LOGI(regist->log, "Payload size: %u bytes", (unsigned)payload_size);
  CHIAKI_LOGI(regist->log, "Header size: %d bytes", request_header_size);

  // Log the complete HTTP request as readable text
  CHIAKI_LOGI(regist->log, "Complete HTTP request headers:");
  // Create a null-terminated copy for safe logging
  char *header_copy = malloc(request_header_size + 1);
  if (header_copy) {
    memcpy(header_copy, request_header, request_header_size);
    header_copy[request_header_size] = '\0';

    // Log each line separately to avoid log truncation
    char *line = strtok(header_copy, "\r\n");
    int line_num = 1;
    while (line != NULL) {
      CHIAKI_LOGI(regist->log, "  [%d] %s", line_num++, line);
      line = strtok(NULL, "\r\n");
    }
    free(header_copy);
  }

  CHIAKI_LOGV(regist->log, "Regist formatted request header (hexdump):");
  chiaki_log_hexdump(regist->log, CHIAKI_LOG_VERBOSE, (uint8_t *)request_header,
                     request_header_size);

  chiaki_socket_t sock = CHIAKI_INVALID_SOCKET;
  uint16_t remote_counter = 0;
  struct addrinfo *addrinfos;
  // PSN registration disabled for VitaRPS5
  // if(psn)
  // {
  //	CHIAKI_LOGI(regist->log, "REGIST - Starting RUDP session");
  //	... PSN code commented out ...
  // }
  // else
  {
    int r = getaddrinfo(regist->info.host, NULL, NULL, &addrinfos);
    if (r != 0) {
      CHIAKI_LOGE(regist->log, "Regist failed to getaddrinfo on %s",
                  regist->info.host);
      goto fail;
    }

    struct sockaddr_in6 recv_addr = {0};
    socklen_t recv_addr_size;
    recv_addr_size = sizeof(recv_addr);
    err = regist_search(regist, addrinfos, (struct sockaddr *)&recv_addr,
                        &recv_addr_size);
    if (err != CHIAKI_ERR_SUCCESS) {
      if (err == CHIAKI_ERR_CANCELED)
        canceled = true;
      else
        CHIAKI_LOGE(regist->log, "Regist search failed");
      goto fail_addrinfos;
    }

    err = chiaki_stop_pipe_sleep(
        &regist->stop_pipe,
        SEARCH_REQUEST_SLEEP_MS);  // PS4 doesn't accept requests immediately
    if (err != CHIAKI_ERR_TIMEOUT) {
      canceled = true;
      goto fail_addrinfos;
    }

    sock = regist_request_connect(regist, (struct sockaddr *)&recv_addr,
                                  recv_addr_size);
    if (CHIAKI_SOCKET_IS_INVALID(sock)) {
      CHIAKI_LOGE(regist->log,
                  "Regist eventually failed to connect for request");
      goto fail_addrinfos;
    }
    CHIAKI_LOGI(regist->log, "Regist connected to %s, sending request",
                regist->info.host);
  }
  if (!psn) {
    CHIAKI_LOGI(regist->log, "=== SENDING HTTP REQUEST TO PS5 ===");
    CHIAKI_LOGI(regist->log, "Sending request header: %d bytes",
                request_header_size);

    int s = send(sock, (CHIAKI_SOCKET_BUF_TYPE)request_header,
                 request_header_size, 0);
    if (s < 0) {
#ifdef _WIN32
      CHIAKI_LOGE(regist->log, "Regist failed to send request header: %u",
                  WSAGetLastError());
#elif defined(__PSVITA__)
      CHIAKI_LOGE(regist->log, "Regist failed to send request header: 0x%x",
                  errno);
#else
      CHIAKI_LOGE(regist->log, "Regist failed to send request header: %s",
                  strerror(errno));
#endif
      goto fail_socket;
    }
  }
  char *send_buf = NULL;
  size_t send_buf_size = 0;
  // PSN path disabled for VitaRPS5
  // if(psn)
  // {
  //	send_buf_size = request_header_size + payload_size;
  //	send_buf = calloc(send_buf_size, sizeof(char));
  //	memcpy(send_buf, request_header, request_header_size);
  //	memcpy(send_buf + request_header_size, payload, payload_size);
  // }
  // else
  {
    CHIAKI_LOGI(regist->log, "Sending request payload: %u bytes",
                (unsigned)payload_size);

    int s = send(sock, (CHIAKI_SOCKET_BUF_TYPE)payload, payload_size, 0);
    if (s < 0) {
#ifdef _WIN32
      CHIAKI_LOGE(regist->log, "Regist failed to send payload: %u",
                  WSAGetLastError());
#elif defined(__PSVITA__)
      CHIAKI_LOGE(regist->log, "Regist failed to send payload: 0x%x", errno);
#else
      CHIAKI_LOGE(regist->log, "Regist failed to send payload: %s",
                  strerror(errno));
#endif
      goto fail_socket;
    }
    CHIAKI_LOGI(regist->log, "Regist waiting for response");
  }

  ChiakiRegisteredHost host;
  err = regist_recv_response(regist, &host, sock, &crypt, remote_counter,
                             send_buf, send_buf_size);
  if (err != CHIAKI_ERR_SUCCESS) {
    if (err == CHIAKI_ERR_CANCELED)
      canceled = true;
    else
      CHIAKI_LOGE(regist->log, "Regist eventually failed");
    goto fail_socket;
  }

  CHIAKI_LOGI(regist->log, "Regist successfully received response");

  success = true;

fail_socket:
  // Always close socket for VitaRPS5 (non-PSN path)
  CHIAKI_SOCKET_CLOSE(sock);
  sock = CHIAKI_INVALID_SOCKET;
fail_addrinfos:
  // Always free addrinfos for VitaRPS5 (non-PSN path)
  freeaddrinfo(addrinfos);
fail:
  if (canceled) {
    CHIAKI_LOGI(regist->log, "Regist canceled");
    regist_event_simple(regist, CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED);
  } else if (success) {
    host.console_pin = regist->info.console_pin;
    ChiakiRegistEvent event = {0};
    event.type = CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS;
    event.registered_host = &host;
    regist->cb(&event, regist->cb_user);
  } else
    regist_event_simple(regist, CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED);
  return NULL;
}

static ChiakiErrorCode regist_search(ChiakiRegist *regist,
                                     struct addrinfo *addrinfos,
                                     struct sockaddr *recv_addr,
                                     socklen_t *recv_addr_size) {
  CHIAKI_LOGI(regist->log, "Regist starting search");
  struct sockaddr_in6 send_addr;
  socklen_t send_addr_len = sizeof(send_addr);
  chiaki_socket_t sock = regist_search_connect(
      regist, addrinfos, (struct sockaddr *)&send_addr, &send_addr_len);
  if (CHIAKI_SOCKET_IS_INVALID(sock)) {
    CHIAKI_LOGE(regist->log, "Regist eventually failed to connect for search");
    return CHIAKI_ERR_NETWORK;
  }

  ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

  const char *src = chiaki_target_is_ps5(regist->info.target) ? "SRC3" : "SRC2";
  const char *res = chiaki_target_is_ps5(regist->info.target) ? "RES3" : "RES2";
  size_t res_size = strlen(res);

  CHIAKI_LOGI(regist->log, "Regist sending search packet");
  int r;
  if (regist->info.broadcast)
    r = sendto_broadcast(regist->log, sock, src, strlen(src) + 1, 0,
                         (struct sockaddr *)&send_addr, send_addr_len);
  else
    // #ifdef __PSVITA__
    // 	r = sceNetSend(sock, src, strlen(src) + 1, 0);
    // #else
    r = send(sock, src, strlen(src) + 1, 0);
  // #endif
  if (r < 0) {
    CHIAKI_LOGE(regist->log, "Regist failed to send search: %s",
                strerror(errno));
    err = CHIAKI_ERR_NETWORK;
    goto done;
  }

  uint64_t timeout_abs_ms =
      chiaki_time_now_monotonic_ms() + REGIST_SEARCH_TIMEOUT_MS;
  while (true) {
    uint64_t now_ms = chiaki_time_now_monotonic_ms();
    if (now_ms > timeout_abs_ms)
      err = CHIAKI_ERR_TIMEOUT;
    else
      err = chiaki_stop_pipe_select_single(&regist->stop_pipe, sock, false,
                                           timeout_abs_ms - now_ms);
    if (err != CHIAKI_ERR_SUCCESS) {
      if (err == CHIAKI_ERR_TIMEOUT)
        CHIAKI_LOGE(regist->log,
                    "Regist timed out waiting for search response");
      break;
    }

    uint8_t buf[0x100];
    int n = recvfrom(sock, (CHIAKI_SOCKET_BUF_TYPE)buf, sizeof(buf) - 1, 0,
                     recv_addr, recv_addr_size);
    if (n <= 0) {
      if (n < 0)
        CHIAKI_LOGE(regist->log, "Regist failed to receive search response: %s",
                    strerror(errno));
      else
        CHIAKI_LOGE(regist->log, "Regist failed to receive search response");
      err = CHIAKI_ERR_NETWORK;
      goto done;
    }

    CHIAKI_LOGV(regist->log, "Regist received packet: %d >= %d", n, res_size);
    chiaki_log_hexdump(regist->log, CHIAKI_LOG_VERBOSE, buf, n);

    if (n >= res_size && !memcmp(buf, res, res_size)) {
      char addr[64];
      const char *addr_str = sockaddr_str(recv_addr, addr, sizeof(addr));
      CHIAKI_LOGI(regist->log, "Regist received search response from %s",
                  addr_str ? addr_str : "");
      break;
    }
  }

done:
  CHIAKI_SOCKET_CLOSE(sock);
  sock = CHIAKI_INVALID_SOCKET;
  return err;
}

static chiaki_socket_t regist_search_connect(ChiakiRegist *regist,
                                             struct addrinfo *addrinfos,
                                             struct sockaddr *send_addr,
                                             socklen_t *send_addr_len) {
  chiaki_socket_t sock = CHIAKI_INVALID_SOCKET;
  for (struct addrinfo *ai = addrinfos; ai; ai = ai->ai_next) {
    // if(ai->ai_protocol != IPPROTO_UDP)
    //	continue;

    if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) continue;

    if (ai->ai_addrlen > *send_addr_len) continue;
    memcpy(send_addr, ai->ai_addr, ai->ai_addrlen);
    *send_addr_len = ai->ai_addrlen;

    set_port(send_addr, htons(REGIST_PORT));

    sock = socket(send_addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
    if (CHIAKI_SOCKET_IS_INVALID(sock)) {
      CHIAKI_LOGE(regist->log, "Regist failed to create socket for search");
      continue;
    }
    int r = 0;
    if (regist->info.broadcast) {
      const int broadcast = 1;
#ifndef __PSVITA__
      if (send_addr->sa_family == AF_INET)
#endif
      {
        r = setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                       (const CHIAKI_SOCKET_BUF_TYPE)&broadcast,
                       sizeof(broadcast));
        if (r < 0) {
#ifdef _WIN32
          CHIAKI_LOGE(regist->log,
                      "Regist failed to setsockopt SO_BROADCAST, error %u",
                      WSAGetLastError());
#elif defined(__PSVITA__)
          CHIAKI_LOGE(regist->log,
                      "Regist failed to setsockopt SO_BROADCAST, error 0x%x",
                      r);
#else
          CHIAKI_LOGE(regist->log, "Regist failed to setsockopt SO_BROADCAST");
#endif
          goto connect_fail;
        }
        in_addr_t ip = ((struct sockaddr_in *)send_addr)->sin_addr.s_addr;
        ((struct sockaddr_in *)send_addr)->sin_addr.s_addr = htonl(INADDR_ANY);
        ((struct sockaddr_in *)send_addr)->sin_port = 0;
        ((struct sockaddr_in *)send_addr)->sin_family = AF_INET;
        r = bind(sock, send_addr, *send_addr_len);
        ((struct sockaddr_in *)send_addr)->sin_addr.s_addr = ip;
      }
#ifndef __PSVITA__
      else {
        struct sockaddr_in6 host_addr;
        host_addr.sin6_addr = in6addr_any;
        host_addr.sin6_port = 0;
        host_addr.sin6_family = AF_INET6;
        r = bind(sock, (struct sockaddr *)&host_addr, sizeof(host_addr));
      }
#endif
      if (r < 0) {
        CHIAKI_LOGE(regist->log, "Regist failed to bind socket");
        goto connect_fail;
      }
    } else {
      // #ifdef __PSVITA__
      // int r = sceNetConnect(sock, (SceNetSockaddr*) send_addr,
      // *send_addr_len); #else
      int r = connect(sock, send_addr, *send_addr_len);
      // #endif
      if (r < 0) {
#ifdef _WIN32
        CHIAKI_LOGE(regist->log, "Regist connect failed, error %u",
                    WSAGetLastError());
#elif defined(__PSVITA__)
        CHIAKI_LOGE(regist->log, "Regist connect failed, error 0x%x", r);
#else
        int errsv = errno;
        CHIAKI_LOGE(regist->log, "Regist connect failed: %s", strerror(errsv));
#endif
        goto connect_fail;
      }
    }
    break;

  connect_fail:
    CHIAKI_SOCKET_CLOSE(sock);
    sock = CHIAKI_INVALID_SOCKET;
  }

  return sock;
}

static chiaki_socket_t regist_request_connect(ChiakiRegist *regist,
                                              const struct sockaddr *addr,
                                              size_t addr_len) {
  // #ifdef __PSVITA__
  // 	chiaki_socket_t sock = sceNetSocket("", SCE_NET_AF_INET,
  // SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP); #else
  chiaki_socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  // #endif
  if (CHIAKI_SOCKET_IS_INVALID(sock)) {
    return CHIAKI_INVALID_SOCKET;
  }

  // #ifdef __PSVITA__
  // int r = sceNetConnect(sock, (SceNetSockaddr*) addr, addr_len);
  // #else
  int r = connect(sock, addr, addr_len);
  // #endif
  if (r < 0) {
#ifdef _WIN32
    CHIAKI_LOGE(regist->log, "Regist connect failed: %u", WSAGetLastError());
#elif defined(__PSVITA__)
    CHIAKI_LOGE(regist->log, "Regist connect failed: 0x%x", r);
#else
    int errsv = errno;
    CHIAKI_LOGE(regist->log, "Regist connect failed: %s", strerror(errsv));
#endif
    CHIAKI_SOCKET_CLOSE(sock);
    sock = CHIAKI_INVALID_SOCKET;
  }

  return sock;
}

static ChiakiErrorCode regist_recv_response(
    ChiakiRegist *regist, ChiakiRegisteredHost *host, chiaki_socket_t sock,
    ChiakiRPCrypt *rpcrypt, uint16_t remote_counter, char *send_buf,
    size_t send_buf_size) {
  uint8_t buf[1500];
  size_t buf_filled_size;
  size_t header_size;
  ChiakiErrorCode err;
// PSN holepunch disabled for VitaRPS5 integration
#if 0
	if(regist->info.holepunch_info)
	{
		err = chiaki_send_recv_http_header_psn(regist->info.rudp, regist->log, &remote_counter, send_buf, send_buf_size, (char *)buf, sizeof(buf), &header_size, &buf_filled_size);
		free(send_buf);
	}
	else
#endif
  err = chiaki_recv_http_header(sock, (char *)buf, sizeof(buf), &header_size,
                                &buf_filled_size, &regist->stop_pipe,
                                REGIST_REPONSE_TIMEOUT_MS);
  if (err == CHIAKI_ERR_CANCELED) return err;
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(regist->log, "Regist failed to receive response HTTP header");
    return err;
  }

  // Log PS5 HTTP response for debugging
  CHIAKI_LOGI(regist->log, "=== PS5 HTTP RESPONSE ===");
  CHIAKI_LOGI(regist->log, "Response header size: %u bytes",
              (unsigned)header_size);
  CHIAKI_LOGI(regist->log, "Response buffer filled: %u bytes",
              (unsigned)buf_filled_size);

  // Log the HTTP response headers as readable text
  char *response_copy = malloc(header_size + 1);
  if (response_copy) {
    memcpy(response_copy, buf, header_size);
    response_copy[header_size] = '\0';

    // Log each line separately
    char *line = strtok(response_copy, "\r\n");
    int line_num = 1;
    while (line != NULL) {
      CHIAKI_LOGI(regist->log, "  [%d] %s", line_num++, line);
      line = strtok(NULL, "\r\n");
    }
    free(response_copy);
  }

// PSN RUDP message handling disabled for VitaRPS5 integration
#if 0
	if(regist->info.holepunch_info)
	{
		RudpMessage message;
		err = chiaki_rudp_send_recv(regist->info.rudp, &message, NULL, 0, remote_counter, ACK, FINISH, 0, 3);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(regist->log, "REGIST - Failed to finish rudp");
			return err;
		}
		chiaki_rudp_message_pointers_free(&message);
	}
#endif

  CHIAKI_LOGV(regist->log, "Regist response HTTP header:");
  chiaki_log_hexdump(regist->log, CHIAKI_LOG_VERBOSE, buf, header_size);

  ChiakiHttpResponse http_response;
  err = chiaki_http_response_parse(&http_response, (char *)buf, header_size);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(regist->log, "Regist failed to pare response HTTP header");
    return err;
  }

  if (http_response.code != 200) {
    CHIAKI_LOGE(regist->log, "Regist received HTTP code %d",
                http_response.code);

    for (ChiakiHttpHeader *header = http_response.headers; header;
         header = header->next) {
      if (strcmp(header->key, "RP-Application-Reason") == 0) {
        uint32_t reason = strtoul(header->value, NULL, 0x10);
        CHIAKI_LOGE(regist->log, "Reported Application Reason: %#x (%s)",
                    (unsigned int)reason,
                    chiaki_rp_application_reason_string(reason));
        break;
      }
    }

    chiaki_http_response_fini(&http_response);
    return CHIAKI_ERR_UNKNOWN;
  }

  size_t content_size = 0;
  for (ChiakiHttpHeader *header = http_response.headers; header;
       header = header->next) {
    if (strcmp(header->key, "Content-Length") == 0) {
      content_size = (size_t)strtoull(header->value, NULL, 0);
    }
  }

  chiaki_http_response_fini(&http_response);

  if (!content_size) {
    CHIAKI_LOGE(
        regist->log,
        "Regist response does not contain or contains invalid Content-Length");
    return CHIAKI_ERR_INVALID_RESPONSE;
  }

  if (content_size + header_size > sizeof(buf)) {
    CHIAKI_LOGE(regist->log, "Regist response content too big");
    return CHIAKI_ERR_BUF_TOO_SMALL;
  }

#if !(defined(__SWITCH__) || defined(__PSVITA__))
  if (regist->info.holepunch_info) {
    if (buf_filled_size < content_size + header_size) {
      CHIAKI_LOGE(
          regist->log,
          "Received %lu which is less than content + header of size %lu",
          buf_filled_size, content_size + header_size);
      return CHIAKI_ERR_NETWORK;
    }
  } else
#endif
  {
    while (buf_filled_size < content_size + header_size) {
      err = chiaki_stop_pipe_select_single(&regist->stop_pipe, sock, false,
                                           REGIST_REPONSE_TIMEOUT_MS);
      if (err != CHIAKI_ERR_SUCCESS) {
        if (err == CHIAKI_ERR_TIMEOUT)
          CHIAKI_LOGE(regist->log,
                      "Regist timed out receiving response content");
        return err;
      }

      int received = recv(sock, (CHIAKI_SOCKET_BUF_TYPE)buf + buf_filled_size,
                          (content_size + header_size) - buf_filled_size, 0);
      if (received <= 0) {
        CHIAKI_LOGE(regist->log, "Regist failed to receive response content");
        return CHIAKI_ERR_NETWORK;
      }
      buf_filled_size += received;
    }
  }

  uint8_t *payload = buf + header_size;
  size_t payload_size = buf_filled_size - header_size;
  chiaki_rpcrypt_decrypt(rpcrypt, 0, payload, payload, payload_size);

  CHIAKI_LOGI(regist->log, "Regist response payload (decrypted):");
  chiaki_log_hexdump(regist->log, CHIAKI_LOG_VERBOSE, payload, payload_size);

  err = regist_parse_response_payload(regist, host, (char *)payload,
                                      payload_size);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(regist->log, "Regist failed to parse response payload");
    return err;
  }

  return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode regist_parse_response_payload(ChiakiRegist *regist,
                                                     ChiakiRegisteredHost *host,
                                                     char *buf,
                                                     size_t buf_size) {
  ChiakiHttpHeader *headers;
  ChiakiErrorCode err = chiaki_http_header_parse(&headers, buf, buf_size);
  if (err != CHIAKI_ERR_SUCCESS) {
    CHIAKI_LOGE(regist->log,
                "Regist failed to parse response payload HTTP header");
    return err;
  }

  memset(host, 0, sizeof(*host));
  host->target = regist->info.target;

  bool mac_found = false;
  bool regist_key_found = false;
  bool key_found = false;
  bool ps5 = chiaki_target_is_ps5(regist->info.target);

  CHIAKI_LOGI(regist->log, "REGISTRATION PATH: vitaki parser active (PS5=%s)",
              ps5 ? "yes" : "no");
  for (ChiakiHttpHeader *header = headers; header; header = header->next) {
#define COPY_STRING(name, key_str)                                            \
  if (strcmp(header->key, (key_str)) == 0) {                                  \
    size_t len = strlen(header->value);                                       \
    if (len >= sizeof(host->name)) {                                          \
      CHIAKI_LOGE(regist->log, "Regist value for %s in response is too long", \
                  (key_str));                                                 \
      continue;                                                               \
    }                                                                         \
    memcpy(host->name, header->value, len);                                   \
    host->name[len] = 0;                                                      \
    continue;                                                                 \
  }
    COPY_STRING(ap_ssid, "AP-Ssid")
    COPY_STRING(ap_bssid, "AP-Bssid")
    COPY_STRING(ap_key, "AP-Key")
    COPY_STRING(ap_name, "AP-Name")
    COPY_STRING(server_nickname, ps5 ? "PS5-Nickname" : "PS4-Nickname")
#undef COPY_STRING

    if (strcmp(header->key, ps5 ? "PS5-RegistKey" : "PS4-RegistKey") == 0) {
      CHIAKI_LOGI(regist->log, "RegistKey header received: len=%u, first8=%.8s",
                  (unsigned)strlen(header->value), header->value);
      // Step 1: hex-decode header->value to bytes (may yield ASCII digits)
      uint8_t tmp[16] = {0};
      size_t tmp_sz = sizeof(tmp);
      err = parse_hex(tmp, &tmp_sz, header->value, strlen(header->value));
      if (err != CHIAKI_ERR_SUCCESS) {
        CHIAKI_LOGE(regist->log,
                    "Regist received invalid RegistKey in response");
        memset(host->rp_regist_key, 0, sizeof(host->rp_regist_key));
      } else {
        // Step 2: detect double-hex (decoded bytes are ASCII hex digits)
        bool all_ascii_hex = true;
        for (size_t i = 0; i < tmp_sz; ++i) {
          uint8_t b = tmp[i];
          if (!((b >= '0' && b <= '9') || (b >= 'a' && b <= 'f') ||
                (b >= 'A' && b <= 'F'))) {
            all_ascii_hex = false;
            break;
          }
        }

        memset(host->rp_regist_key, 0, sizeof(host->rp_regist_key));
        if (all_ascii_hex && tmp_sz == 8) {
          // Step 3: second decode to 4 raw bytes, then pad to 16
          char ascii8[9];
          for (int i = 0; i < 8; ++i) ascii8[i] = (char)tmp[i];
          ascii8[8] = '\0';
          uint8_t raw4[4] = {0};
          size_t raw4_sz = sizeof(raw4);
          err = parse_hex(raw4, &raw4_sz, ascii8, 8);
          if (err != CHIAKI_ERR_SUCCESS || raw4_sz != 4) {
            CHIAKI_LOGE(regist->log, "RegistKey second decode failed");
            memset(host->rp_regist_key, 0, sizeof(host->rp_regist_key));
          } else {
            memcpy(host->rp_regist_key, raw4, 4);
            regist_key_found = true;
            CHIAKI_LOGI(
                regist->log,
                "RegistKey double-hex repaired to raw: %02X %02X %02X %02X",
                host->rp_regist_key[0], host->rp_regist_key[1],
                host->rp_regist_key[2], host->rp_regist_key[3]);
          }
        } else {
          // Already raw bytes; copy non-zero prefix (up to 16)
          size_t copy = tmp_sz < sizeof(host->rp_regist_key)
                            ? tmp_sz
                            : sizeof(host->rp_regist_key);
          memcpy(host->rp_regist_key, tmp, copy);
          regist_key_found = true;
        }
      }
    } else if (strcmp(header->key, "RP-KeyType") == 0) {
      host->rp_key_type = (uint32_t)strtoul(header->value, NULL, 0);
    } else if (strcmp(header->key, "RP-Key") == 0) {
      CHIAKI_LOGI(regist->log, "RP-Key header received: len=%u, first8=%.8s",
                  (unsigned)strlen(header->value), header->value);
      size_t buf_size = sizeof(host->rp_key);
      err = parse_hex((uint8_t *)host->rp_key, &buf_size, header->value,
                      strlen(header->value));
      if (err != CHIAKI_ERR_SUCCESS || buf_size != sizeof(host->rp_key)) {
        CHIAKI_LOGE(regist->log, "Regist received invalid key in response");
        memset(host->rp_key, 0, sizeof(host->rp_key));
      } else {
        CHIAKI_LOGI(
            regist->log,
            "RP-Key parsed: %02X %02X %02X %02X ... %02X %02X %02X %02X",
            (unsigned char)host->rp_key[0], (unsigned char)host->rp_key[1],
            (unsigned char)host->rp_key[2], (unsigned char)host->rp_key[3],
            (unsigned char)host->rp_key[12], (unsigned char)host->rp_key[13],
            (unsigned char)host->rp_key[14], (unsigned char)host->rp_key[15]);
        key_found = true;
      }
    } else if (strcmp(header->key, ps5 ? "PS5-Mac" : "PS4-Mac") == 0) {
      size_t buf_size = sizeof(host->server_mac);
      err = parse_hex((uint8_t *)host->server_mac, &buf_size, header->value,
                      strlen(header->value));
      if (err != CHIAKI_ERR_SUCCESS || buf_size != sizeof(host->server_mac)) {
        CHIAKI_LOGE(regist->log,
                    "Regist received invalid MAC Address in response");
        memset(host->server_mac, 0, sizeof(host->server_mac));
      } else {
        mac_found = true;
      }
    } else if (strcmp(header->key, "RP-SupportCmd") == 0) {
      uint32_t support_cmd = (uint32_t)strtoul(header->value, NULL, 0);
      CHIAKI_LOGI(regist->log, "RP-Support Cmd: %u", support_cmd);
    } else {
      CHIAKI_LOGI(regist->log,
                  "Regist received unknown key %s in response payload",
                  header->key);
    }
  }

  chiaki_http_header_free(headers);

  if (!regist_key_found) {
    CHIAKI_LOGE(regist->log,
                "Regist response is missing RegistKey (or it was invalid)");
    return CHIAKI_ERR_INVALID_RESPONSE;
  }

  if (!key_found) {
    CHIAKI_LOGE(regist->log,
                "Regist response is missing key (or it was invalid)");
    return CHIAKI_ERR_INVALID_RESPONSE;
  }

  if (!mac_found) {
    CHIAKI_LOGE(regist->log,
                "Regist response is missing MAC Adress (or it was invalid)");
    return CHIAKI_ERR_INVALID_RESPONSE;
  }

  return CHIAKI_ERR_SUCCESS;
}
