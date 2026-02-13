/*
 * Platform compatibility header
 */

#ifndef COMPAT_H
#define COMPAT_H

#ifdef G_OS_WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#define compat_close_socket(s) closesocket(s)
#define compat_close_fd(fd)    _close(fd)

static inline void compat_winsock_init(void) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}
static inline void compat_winsock_cleanup(void) {
    WSACleanup();
}

#else /* POSIX */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define compat_close_socket(s) close(s)
#define compat_close_fd(fd)    close(fd)

static inline void compat_winsock_init(void) {}
static inline void compat_winsock_cleanup(void) {}

#endif /* G_OS_WIN32 */

#endif /* COMPAT_H */
