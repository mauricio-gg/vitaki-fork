// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "vitaki_stoppipe.h"

#include <fcntl.h>
#include <string.h>

#include "../utils/logger.h"  // VitaRPS5 logging system
#include "vitaki_log.h"
#include "vitaki_sock.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#elif defined(__PSVITA__)
#include <psp2/net/net.h>
#else
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

CHIAKI_EXPORT ChiakiErrorCode chiaki_stop_pipe_init(ChiakiStopPipe *stop_pipe) {
#ifdef _WIN32
  stop_pipe->event = WSACreateEvent();
  if (stop_pipe->event == WSA_INVALID_EVENT) return CHIAKI_ERR_UNKNOWN;
#elif defined(__SWITCH__) || defined(__PSVITA__)
  // currently pipe or socketpare are not available on switch or vita
  // use a custom udp socket as pipe

  // struct sockaddr_in addr;
  socklen_t addr_size = sizeof(stop_pipe->addr);

  log_debug("StopPipe: Initializing UDP socket-based stop pipe for PS Vita");
  stop_pipe->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (stop_pipe->fd < 0) {
    log_error("StopPipe: Failed to create UDP socket: %d", stop_pipe->fd);
    return CHIAKI_ERR_UNKNOWN;
  }

  stop_pipe->addr.sin_family = AF_INET;
  // bind to localhost
  stop_pipe->addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  // use a random port (dedicate one socket per object)
  stop_pipe->addr.sin_port = htons(0);

  log_debug("StopPipe: Binding UDP socket to localhost");
  // bind on localhost - CREATE UDP socket
  int bind_result =
      bind(stop_pipe->fd, (struct sockaddr *)&stop_pipe->addr, addr_size);
  if (bind_result < 0) {
    log_error("StopPipe: Failed to bind UDP socket: result=%d", bind_result);
    close(stop_pipe->fd);
    return CHIAKI_ERR_UNKNOWN;
  }

  log_debug("StopPipe: Getting socket name after bind");
  // Get the actual port assigned by the system
  int getsockname_result = getsockname(
      stop_pipe->fd, (struct sockaddr *)&stop_pipe->addr, &addr_size);
  if (getsockname_result < 0) {
    log_error("StopPipe: Failed to get socket name: result=%d",
              getsockname_result);
    close(stop_pipe->fd);
    return CHIAKI_ERR_UNKNOWN;
  }

  log_debug("StopPipe: Socket bound successfully to port %d",
            ntohs(stop_pipe->addr.sin_port));
#ifdef __PSVITA__
  // PS Vita: SO_NONBLOCK via setsockopt() corrupts socket descriptor (causes
  // EBADF) Use blocking socket mode for stop pipe to prevent socket corruption
  // This is consistent with our main socket connection workaround
  log_debug(
      "StopPipe: Using blocking socket mode on PS Vita to avoid EBADF "
      "corruption");
  int r = 0;  // Success - no operation needed for blocking mode
#else
  int r = fcntl(stop_pipe->fd, F_SETFL, O_NONBLOCK);
  if (r == -1) {
    close(stop_pipe->fd);
    return CHIAKI_ERR_UNKNOWN;
  }
#endif
// #elif defined(__PSVITA__)
// 	int addr_size = sizeof(stop_pipe->addr);
// 	stop_pipe->fd = sceNetSocket("", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM,
// SCE_NET_IPPROTO_UDP); 	if (stop_pipe->fd < 0) 		return
// CHIAKI_ERR_UNKNOWN; 	stop_pipe->addr.sin_family = AF_INET;
// 	stop_pipe->addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
// 	stop_pipe->addr.sin_port = htons(0);
// 	int r = sceNetBind(stop_pipe->fd, (SceNetSockaddr *) &stop_pipe->addr,
// (unsigned int) addr_size); 	if (r < 0) { 		return
// CHIAKI_ERR_UNKNOWN;
// 	}
// 	sceNetGetsockname(stop_pipe->fd, (SceNetSockaddr *) &stop_pipe->addr,
// (unsigned int*) &addr_size); 	int val = 1; 	r =
// sceNetSetsockopt(stop_pipe->fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &val,
// sizeof(val)); 	if(r < 0) { sceNetSocketClose(stop_pipe->fd);
// return CHIAKI_ERR_UNKNOWN;
// 	}

// 	stop_pipe->epoll_fd = sceNetEpollCreate("chiaki-epoll", 0);
// 	if (stop_pipe->epoll_fd < 0) {
// 		return CHIAKI_ERR_UNKNOWN;
// 	}
#else
  int r = pipe(stop_pipe->fds);
  if (r < 0) return CHIAKI_ERR_UNKNOWN;
  r = fcntl(stop_pipe->fds[0], F_SETFL, O_NONBLOCK);
  if (r == -1) {
    close(stop_pipe->fds[0]);
    close(stop_pipe->fds[1]);
    return CHIAKI_ERR_UNKNOWN;
  }
#endif
  return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_stop_pipe_fini(ChiakiStopPipe *stop_pipe) {
#ifdef _WIN32
  WSACloseEvent(stop_pipe->event);
#elif defined(__SWITCH__)
  close(stop_pipe->fd);
#elif defined(__PSVITA__)
  close(stop_pipe->fd);
  // sceNetEpollDestroy(stop_pipe->epoll_fd);
#else
  close(stop_pipe->fds[0]);
  close(stop_pipe->fds[1]);
#endif
}

CHIAKI_EXPORT void chiaki_stop_pipe_stop(ChiakiStopPipe *stop_pipe) {
#ifdef _WIN32
  WSASetEvent(stop_pipe->event);
#elif defined(__SWITCH__) || defined(__PSVITA__)
  // send to local socket (FIXME MSG_CONFIRM)
  sendto(stop_pipe->fd, "\x00", 1, 0, (struct sockaddr *)&stop_pipe->addr,
         sizeof(struct sockaddr_in));
// #elif defined(__PSVITA__)
// 	int r = sceNetSendto(stop_pipe->fd, "\x00", 1, 0,
// (SceNetSockaddr*)&stop_pipe->addr, sizeof(stop_pipe->addr)); 	if (r <
// 0) {
// 	}
#else
  write(stop_pipe->fds[1], "\x00", 1);
#endif
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_stop_pipe_select_single(ChiakiStopPipe *stop_pipe, chiaki_socket_t fd,
                               bool write, uint64_t timeout_ms) {
#ifdef _WIN32
  WSAEVENT events[2];
  DWORD events_count = 1;
  events[0] = stop_pipe->event;

  if (!CHIAKI_SOCKET_IS_INVALID(fd)) {
    events_count = 2;
    events[1] = WSACreateEvent();
    if (events[1] == WSA_INVALID_EVENT) return CHIAKI_ERR_UNKNOWN;
    WSAEventSelect(fd, events[1], write ? FD_WRITE : FD_READ);
  }

  DWORD r = WSAWaitForMultipleEvents(
      events_count, events, FALSE,
      timeout_ms == UINT64_MAX ? WSA_INFINITE : (DWORD)timeout_ms, FALSE);

  if (events_count == 2) WSACloseEvent(events[1]);

  switch (r) {
    case WSA_WAIT_EVENT_0:
      return CHIAKI_ERR_CANCELED;
    case WSA_WAIT_EVENT_0 + 1:
      return CHIAKI_ERR_SUCCESS;
    case WSA_WAIT_TIMEOUT:
      return CHIAKI_ERR_TIMEOUT;
    default:
      return CHIAKI_ERR_UNKNOWN;
  }
// #elif defined(__PSVITA__)
// 	SceNetEpollEvent ev = {0};
// 	ev.events = SCE_NET_EPOLLIN | SCE_NET_EPOLLHUP;
// 	ev.data.fd = stop_pipe->fd;
// 	int r = sceNetEpollControl(stop_pipe->epoll_fd, SCE_NET_EPOLL_CTL_ADD,
// stop_pipe->fd, &ev); 	if (r < 0) { 		return
// CHIAKI_ERR_UNKNOWN;
// 	}

// 	if (!CHIAKI_SOCKET_IS_INVALID(fd)) {
// 		ev.events = (write ? SCE_NET_EPOLLOUT : SCE_NET_EPOLLIN) |
// SCE_NET_EPOLLHUP; 		ev.data.fd = fd; 		r =
// sceNetEpollControl(stop_pipe->epoll_fd, SCE_NET_EPOLL_CTL_ADD, fd, &ev);
// if (r < 0) { 			return CHIAKI_ERR_UNKNOWN;
// 		}
// 	}

// 	if (r == 0) {
// 		r = sceNetEpollWait(stop_pipe->epoll_fd, &ev, 1, timeout_ms *
// 1000);
// 	}

// 	sceNetEpollControl(stop_pipe->epoll_fd, SCE_NET_EPOLL_CTL_DEL,
// stop_pipe->fd, NULL); 	if (!CHIAKI_SOCKET_IS_INVALID(fd)) {
// 		sceNetEpollControl(stop_pipe->epoll_fd, SCE_NET_EPOLL_CTL_DEL,
// fd, NULL);
// 	}

// 	if(r < 0) {
// 		return CHIAKI_ERR_UNKNOWN;
// 	}

// 	if (ev.data.fd == stop_pipe->fd) {
// 		return CHIAKI_ERR_CANCELED;
// 	} else if (ev.data.fd == fd) {
// 		return CHIAKI_ERR_SUCCESS;
// 	}

// 	return CHIAKI_ERR_TIMEOUT;
#else
  fd_set rfds;
  FD_ZERO(&rfds);
#if defined(__SWITCH__) || defined(__PSVITA__)
  // push udp local socket as fd
  int stop_fd = stop_pipe->fd;
#else
  int stop_fd = stop_pipe->fds[0];
#endif
  FD_SET(stop_fd, &rfds);
  int nfds = stop_fd;

  fd_set wfds;
  FD_ZERO(&wfds);
  if (!CHIAKI_SOCKET_IS_INVALID(fd)) {
    FD_SET(fd, write ? &wfds : &rfds);
    if (fd > nfds) nfds = fd;
  }
  nfds++;

  struct timeval timeout_s;
  struct timeval *timeout = NULL;
  if (timeout_ms != UINT64_MAX) {
    timeout_s.tv_sec = timeout_ms / 1000;
    timeout_s.tv_usec = (timeout_ms % 1000) * 1000;
    timeout = &timeout_s;
  }
#ifdef __PSVITA__
  // workaround crash in newlib
  else {
    timeout_s.tv_sec = 999999999;
    timeout_s.tv_usec = 0;
    timeout = &timeout_s;
  }
#endif

  int r;
  do {
    r = select(nfds, &rfds, write ? &wfds : NULL, NULL, timeout);
#ifdef _WIN32
  } while (r < 0 && WSAGetLastError() == WSAEINTR)
#else
  } while (r < 0 && errno == EINTR);
#endif

      if (r < 0) return CHIAKI_ERR_UNKNOWN;

  if (FD_ISSET(stop_fd, &rfds)) return CHIAKI_ERR_CANCELED;

  if (!CHIAKI_SOCKET_IS_INVALID(fd) && FD_ISSET(fd, write ? &wfds : &rfds))
    return CHIAKI_ERR_SUCCESS;

  return CHIAKI_ERR_TIMEOUT;
#endif
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_stop_pipe_connect(ChiakiStopPipe *stop_pipe, chiaki_socket_t fd,
                         struct sockaddr *addr, size_t addrlen) {
  // Get target address info for diagnostics
  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  char addr_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
  int target_port = ntohs(sin->sin_port);

  log_info("[SOCKET_CONNECT] Attempting connection to %s:%d (socket fd=%d)",
           addr_str, target_port, fd);

#ifdef __PSVITA__
  // Validate socket descriptor before attempting connection using POSIX API
  // (vitaki-fork approach)
  int socket_valid = 1;
  socklen_t optlen = sizeof(socket_valid);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socket_valid, &optlen) < 0) {
    log_error(
        "[SOCKET_CONNECT] PS Vita socket validation failed - descriptor may be "
        "corrupted (fd=%d)",
        fd);
    return CHIAKI_ERR_UNKNOWN;
  }
  log_debug(
      "[SOCKET_CONNECT] PS Vita socket validation passed (fd=%d, type=%d)", fd,
      socket_valid);

  int r = connect(fd, addr, (socklen_t)addrlen);
  int errno_backup = errno;  // Save errno before potential overwrite
#else
  int r = connect(fd, addr, (socklen_t)addrlen);
#endif

  log_info("[SOCKET_CONNECT] connect() returned: %d", r);
  if (r >= 0) {
    log_info("[SOCKET_CONNECT] Connection successful to %s:%d", addr_str,
             target_port);
    return CHIAKI_ERR_SUCCESS;
  }

  // Log detailed error information with platform-specific details
#ifdef __PSVITA__
  log_error("[SOCKET_CONNECT] PS Vita connect() failed to %s:%d - errno: %d",
            addr_str, target_port, errno_backup);
#else
  int error_code = CHIAKI_SOCKET_ERROR;
  log_error("[SOCKET_CONNECT] connect() failed to %s:%d - errno: %d", addr_str,
            target_port, error_code);
#endif

  if (CHIAKI_SOCKET_EINPROGRESS) {
    log_debug(
        "[SOCKET_CONNECT] Connection in progress, waiting for completion");
    ChiakiErrorCode err =
        chiaki_stop_pipe_select_single(stop_pipe, fd, true, UINT64_MAX);
    log_debug("[SOCKET_CONNECT] select_single returned: %d", err);
    if (err != CHIAKI_ERR_SUCCESS) return err;
  } else {
#ifdef _WIN32
    int err = WSAGetLastError();
    log_error("[SOCKET_CONNECT] Connection failed to %s:%d - Windows error: %d",
              addr_str, target_port, err);
    if (err == WSAECONNREFUSED)
      return CHIAKI_ERR_CONNECTION_REFUSED;
    else
      return CHIAKI_ERR_NETWORK;
#elif defined(__PSVITA__)
    log_error("[SOCKET_CONNECT] PS Vita connection failed to %s:%d - errno: %d",
              addr_str, target_port, errno_backup);
    if (errno_backup == ECONNREFUSED) {
      log_error("[SOCKET_CONNECT] Connection refused by %s:%d", addr_str,
                target_port);
      return CHIAKI_ERR_CONNECTION_REFUSED;
    } else {
      log_error("[SOCKET_CONNECT] PS Vita network error connecting to %s:%d",
                addr_str, target_port);
      return CHIAKI_ERR_NETWORK;
    }
#else
    log_error("[SOCKET_CONNECT] Connection failed to %s:%d - errno: %d (%s)",
              addr_str, target_port, errno, strerror(errno));
    if (errno == ECONNREFUSED) {
      log_error("[SOCKET_CONNECT] Connection refused by %s:%d", addr_str,
                target_port);
      return CHIAKI_ERR_CONNECTION_REFUSED;
    } else {
      log_error("[SOCKET_CONNECT] Network error connecting to %s:%d", addr_str,
                target_port);
      return CHIAKI_ERR_NETWORK;
    }
#endif
  }

  struct sockaddr peer;
  socklen_t peerlen = sizeof(peer);
  if (getpeername(fd, &peer, &peerlen) == 0) return CHIAKI_ERR_SUCCESS;

#ifdef __PSVITA__
  if (errno != ENOTCONN) {
    log_error("[SOCKET_CONNECT] PS Vita getpeername check failed: errno=%d",
              errno);
    return CHIAKI_ERR_UNKNOWN;
  }
#else
  if (errno != ENOTCONN) {
    log_error("[SOCKET_CONNECT] getpeername check failed: errno=%d", errno);
    return CHIAKI_ERR_UNKNOWN;
  }
#endif

#ifdef _WIN32
  int sockerr;
  socklen_t sockerr_sz = sizeof(sockerr);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)(&sockerr), &sockerr_sz) < 0)
    return CHIAKI_ERR_UNKNOWN;
// #elif defined(__PSVITA__)
//	int sockerr;
// 	socklen_t sockerr_sz = sizeof(sockerr);
// 	if(sceNetGetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &sockerr,
// &sockerr_sz) < 0) 		return CHIAKI_ERR_UNKNOWN;
#else
  int sockerr;
  socklen_t sockerr_sz = sizeof(sockerr);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &sockerr_sz) < 0)
    return CHIAKI_ERR_UNKNOWN;
#endif

#ifdef _WIN32
  switch (sockerr) {
    case WSAETIMEDOUT:
      return CHIAKI_ERR_TIMEOUT;
    case WSAECONNREFUSED:
      return CHIAKI_ERR_CONNECTION_REFUSED;
    case WSAEHOSTDOWN:
      return CHIAKI_ERR_HOST_DOWN;
    case WSAEHOSTUNREACH:
      return CHIAKI_ERR_HOST_UNREACH;
    default:
      return CHIAKI_ERR_UNKNOWN;
  }
// #elif defined(__PSVITA__)
// 	switch (sockerr) {
// 		case 0:
// 			return CHIAKI_ERR_SUCCESS; // I have no idea.
// 		case SCE_NET_ERROR_ETIMEDOUT:
// 			return CHIAKI_ERR_TIMEOUT;
// 		case SCE_NET_ERROR_ECONNREFUSED:
// 			return CHIAKI_ERR_CONNECTION_REFUSED;
// 		case SCE_NET_ERROR_EHOSTDOWN:
// 			return CHIAKI_ERR_HOST_DOWN;
// 		case SCE_NET_ERROR_EHOSTUNREACH:
// 			return CHIAKI_ERR_HOST_UNREACH;
// 		default:
// 			return CHIAKI_ERR_UNKNOWN;
// 	}
#else
  switch (sockerr) {
    case ETIMEDOUT:
      return CHIAKI_ERR_TIMEOUT;
    case ECONNREFUSED:
      return CHIAKI_ERR_CONNECTION_REFUSED;
    case EHOSTDOWN:
      return CHIAKI_ERR_HOST_DOWN;
    case EHOSTUNREACH:
      return CHIAKI_ERR_HOST_UNREACH;
    default:
      return CHIAKI_ERR_UNKNOWN;
  }
#endif
}

CHIAKI_EXPORT ChiakiErrorCode
chiaki_stop_pipe_reset(ChiakiStopPipe *stop_pipe) {
#ifdef _WIN32
  BOOL r = WSAResetEvent(stop_pipe->event);
  return r ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_UNKNOWN;
#elif defined(__SWITCH__) || defined(__PSVITA__)
  // FIXME
  uint8_t v;
  int r;
  while ((r = read(stop_pipe->fd, &v, sizeof(v))) > 0)
    ;
  return r < 0 ? CHIAKI_ERR_UNKNOWN : CHIAKI_ERR_SUCCESS;
// #elif defined(__PSVITA__)
// 	int r;
// 	uint8_t v;
// 	while((r = /*sceNetRecv*/read(stop_pipe->fd, &v, sizeof(v)/*, 0*/)) >
// 0); 	return r < 0 ? CHIAKI_ERR_UNKNOWN : CHIAKI_ERR_SUCCESS;
#else
  uint8_t v;
  int r;
  while ((r = read(stop_pipe->fds[0], &v, sizeof(v))) > 0)
    ;
  return r < 0 ? CHIAKI_ERR_UNKNOWN : CHIAKI_ERR_SUCCESS;
#endif
}
