// 桌面桩：lwip/sockets.h → 直接用 POSIX socket
#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
typedef int SOCKET_T;
inline int lwip_close(int fd) { return close(fd); }
#define LWIP_SOCKET 1
