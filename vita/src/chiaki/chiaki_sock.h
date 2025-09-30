// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SOCK_H
#define CHIAKI_SOCK_H

#include <stdbool.h>

#include "chiaki_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET chiaki_socket_t;
#define CHIAKI_SOCKET_IS_INVALID(s) ((s) == INVALID_SOCKET)
#define CHIAKI_INVALID_SOCKET INVALID_SOCKET
#define CHIAKI_SOCKET_CLOSE(s) closesocket(s)
#define CHIAKI_SOCKET_ERROR_FMT "%d"
#define CHIAKI_SOCKET_ERROR_VALUE (WSAGetLastError())
#define CHIAKI_SOCKET_EINPROGRESS (WSAGetLastError() == WSAEWOULDBLOCK)
#define CHIAKI_SOCKET_BUF_TYPE char*
#elif defined(__PSVITA__)
// VITAKI-FORK COMPATIBILITY: Disable PS Vita socket macros to avoid conflicts
// Use standard POSIX socket calls instead of sceNet* functions
// This prevents "Unknown error" when setting non-blocking mode
#include <errno.h>
#include <psp2/net/net.h>
#include <unistd.h>
typedef int chiaki_socket_t;
#define CHIAKI_SOCKET_IS_INVALID(s) ((s) < 0)
#define CHIAKI_INVALID_SOCKET (-1)
#define CHIAKI_SOCKET_CLOSE(s) \
  close(s)  // Use standard close() instead of sceNetSocketClose()
#ifndef CHIAKI_SOCKET_ERROR_FMT
#define CHIAKI_SOCKET_ERROR_FMT "%s"
#endif
#define CHIAKI_SOCKET_ERROR_VALUE \
  (strerror(errno))  // Use standard strerror() instead of errno
#define CHIAKI_SOCKET_EINPROGRESS \
  (errno == EINPROGRESS)  // Use standard EINPROGRESS
#ifndef CHIAKI_SOCKET_BUF_TYPE
#define CHIAKI_SOCKET_BUF_TYPE void*
#endif
#else
#include <errno.h>
#include <unistd.h>
typedef int chiaki_socket_t;
#define CHIAKI_SOCKET_IS_INVALID(s) ((s) < 0)
#define CHIAKI_INVALID_SOCKET (-1)
#define CHIAKI_SOCKET_CLOSE(s) close(s)
#ifndef CHIAKI_SOCKET_ERROR_FMT
#define CHIAKI_SOCKET_ERROR_FMT "%s"
#endif
#define CHIAKI_SOCKET_ERROR_VALUE (strerror(errno))
#define CHIAKI_SOCKET_EINPROGRESS (errno == EINPROGRESS)
#ifndef CHIAKI_SOCKET_BUF_TYPE
#define CHIAKI_SOCKET_BUF_TYPE void*
#endif
#endif

CHIAKI_EXPORT ChiakiErrorCode chiaki_socket_set_nonblock(chiaki_socket_t sock,
                                                         bool nonblock);

#ifdef __cplusplus
}
#endif

#endif  // CHIAKI_SOCK_H