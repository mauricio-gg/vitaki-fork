// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef VITAKI_SOCK_H
#define VITAKI_SOCK_H

#include <stdbool.h>
#include <stdint.h>

// Include common.h for types - now safe with renamed enums
#include "common.h"

#define CHIAKI_EXPORT

#ifdef _WIN32
#include <errno.h>
#else
#include <errno.h>
#endif

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
#define CHIAKI_SOCKET_ERROR WSAGetLastError()
#define CHIAKI_SOCKET_EINPROGRESS (WSAGetLastError() == WSAEWOULDBLOCK)
#define CHIAKI_SOCKET_EINTR (WSAGetLastError() == WSAEINTR)
#define CHIAKI_SOCKET_BUF_TYPE const char*
#define CHIAKI_SSIZET_TYPE int
#elif defined(__PSVITA__)
typedef int chiaki_socket_t;
#define CHIAKI_SOCKET_IS_INVALID(s) ((s) < 0)
#define CHIAKI_INVALID_SOCKET (-1)
#define CHIAKI_SOCKET_CLOSE(s) close(s)
#define CHIAKI_SOCKET_ERROR_FMT "%d"
#define CHIAKI_SOCKET_ERROR errno
#define CHIAKI_SOCKET_EINPROGRESS (errno == EINPROGRESS)
#define CHIAKI_SOCKET_EINTR (errno == EINTR)
#define CHIAKI_SOCKET_BUF_TYPE const void*
#define CHIAKI_SSIZET_TYPE int
#else
typedef int chiaki_socket_t;
#define CHIAKI_SOCKET_IS_INVALID(s) ((s) < 0)
#define CHIAKI_INVALID_SOCKET (-1)
#define CHIAKI_SOCKET_CLOSE(s) close(s)
#define CHIAKI_SOCKET_ERROR_FMT "%d"
#define CHIAKI_SOCKET_ERROR errno
#define CHIAKI_SOCKET_EINPROGRESS (errno == EINPROGRESS)
#define CHIAKI_SOCKET_EINTR (errno == EINTR)
#define CHIAKI_SOCKET_BUF_TYPE const void*
#define CHIAKI_SSIZET_TYPE ssize_t
#endif

#ifdef __cplusplus
}
#endif

#endif  // VITAKI_SOCK_H