// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "chiaki_sock.h"

#include <errno.h>
#include <fcntl.h>

#ifdef __PSVITA__
#include <psp2/net/net.h>
#include <sys/socket.h>

#include "../utils/logger.h"  // VitaRPS5 logging
#endif

CHIAKI_EXPORT ChiakiErrorCode chiaki_socket_set_nonblock(chiaki_socket_t sock,
                                                         bool nonblock) {
#ifdef _WIN32
  u_long nbio = nonblock ? 1 : 0;
  if (ioctlsocket(sock, FIONBIO, &nbio) != NO_ERROR) return CHIAKI_ERR_UNKNOWN;
#elif defined(__PSVITA__)
  // PS Vita: SO_NONBLOCK via setsockopt() corrupts socket descriptor (causes
  // EBADF) Use blocking sockets as workaround - connection will be handled by
  // stop_pipe timeout
  (void)nonblock;  // Suppress unused parameter warning
  log_debug(
      "PS Vita: Using blocking socket mode to avoid EBADF socket corruption");
#else
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1) return CHIAKI_ERR_UNKNOWN;
  flags = nonblock ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  if (fcntl(sock, F_SETFL, flags) == -1) return CHIAKI_ERR_UNKNOWN;
#endif
  return CHIAKI_ERR_SUCCESS;
}