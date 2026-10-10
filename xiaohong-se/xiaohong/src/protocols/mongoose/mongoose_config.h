#pragma once

// Base code: https://atomgit.com/xiaohong-ws63/mongoose  @ tag_7.20 branch

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


//#define MG_ARCH           0  // [0]MG_ARCH_CUSTOM  [8]MG_ARCH_ARMGCC  [13]MG_ARCH_CMSIS_RTOS2
//#define MG_TLS            0  // [0]MG_TLS_NONE     [1]MG_TLS_MBED      [3]MG_TLS_BUILTIN

#define MG_ENABLE_LOG                   1
#define MG_ENABLE_CUSTOM_LOG            1
#define MG_ENABLE_CUSTOM_MILLIS         1
/* 使用 mongoose_port_random.c 提供线程安全实现，禁止回落到 rand() */
#define MG_ENABLE_CUSTOM_RANDOM         1
#define MG_SOCK_WRITABLE_AFTER_CONNECT  1

#define MG_ENABLE_SOCKET      1  // 1-使用外部 TCP/IP 栈【这里是LWIP栈】, 0-使用Mongoose自带的轻量 TCP/IP 栈
// MG_ENABLE_SOCKET 是 Mongoose 中控制套接字（Socket）底层实现的核心宏定义，
// 它决定了 Mongoose 是否使用操作系统 / 硬件的原生 Socket API，以及如何适配嵌入式平台的网络驱动
// 启用（MG_ENABLE_SOCKET 1）：使用目标平台的原生 Socket API（如 POSIX socket、FreeRTOS+TCP socket、LWIP socket），这是绝大多数嵌入式系统的选择；需确保平台已实现标准 Socket 接口（如 socket()、connect()、recv()、send()），Mongoose 会直接调用这些接口，无需额外适配；
// 禁用（MG_ENABLE_SOCKET 0）：完全不依赖目标平台的原生 Socket，需用户自定义底层网络收发接口（仅用于无标准 Socket 封装的裸机 / 自研网络栈场景）。必须实现 Mongoose 提供的底层抽象接口（mg_socket_if），否则网络功能无法工作。


#if defined(MG_ENABLE_SOCKET) && MG_ENABLE_SOCKET

  #define MG_ENABLE_TCPIP   0  // 与 MG_ENABLE_SOCKET 定义相反
  // MG_ENABLE_TCPIP 是 Mongoose 框架中控制TCP/IP 网络核心功能的编译宏开关，是 Mongoose 实现网络通信的基础。
  // MG_ENABLE_TCPIP 是 Mongoose 中控制内置 TCP/IP 栈的核心宏定义，主要用于无操作系统 / 无第三方 TCP/IP 栈的嵌入式场景（如裸机 MCU）。
  // MG_ENABLE_SOCKET 决定的是 Mongoose 是 "[1]使用外部 TCP/IP 栈" 还是 "[0]自带轻量 TCP/IP 栈"，

  #define MG_ENABLE_LWIP    1
  #if MG_ENABLE_LWIP
    #include "lwip/pbuf.h"
    #include "lwip/api.h"
    #include "lwip/dns.h"
    #include "lwip/ip_addr.h"
    #include "lwip/inet.h"
    #include "lwip/sockets.h"
    #include "lwip/err.h"
     /* LiteOS musl 的 netinet/in.h 在 _GNU_SOURCE/_BSD_SOURCE 下已有 struct ip_mreq */
    #if !defined(_NETINET_IN_H) || (!defined(_GNU_SOURCE) && !defined(_BSD_SOURCE))
        typedef struct ip_mreq {
          struct in_addr imr_multiaddr;
          struct in_addr imr_interface;
        } ip_mreq;
    #endif
    /* LiteOS / lwIP：套接字 fd 应走 lwip_close；musl 的 close() 可能未链入导致 undefined reference */
    #ifndef closesocket
    #define closesocket(x) lwip_close(x)
    #endif
  #else
    struct sockaddr {
    u8_t        sa_len;
    sa_family_t sa_family;
    char        sa_data[14];
    };
  #endif
#else
  #define MG_ENABLE_LWIP    0
  #define MG_ENABLE_TCPIP   1  // 与 MG_ENABLE_SOCKET 定义相反
#endif

#define MG_ENABLE_PACKED_FS   1
// MG_ENABLE_PACKED_FS 是 mongoose 嵌入式网络库 中的一个核心编译宏（宏定义），主要作用是启用 mongoose 的打包文件系统（Packed Filesystem） 功能，简单来说就是把静态资源（如 HTML、CSS、JS、图片等）直接编译进固件 / 可执行文件中，无需外置文件系统。


#define MG_ENABLE_RL   0
// MG_ENABLE_RL 用于启用/禁用 Readline交互式命令行 功能（RL 即 Readline 的缩写）,用于需要交互式控制台的场景
// 如果禁用该宏（默认禁用），Mongoose 的命令行输入会退化为 “原始字符读取”，仅支持基础的回车确认，无行编辑和历史记录功能。

#define MG_ENABLE_POLL 0
// 用于启用 poll I/O 多路复用机制 的编译宏开关，是跨平台的基础 I/O 模型，作为 epoll/kqueue/IOCP 等平台专属模型的降级方案
// 兼容几乎所有类 Unix 系统（Linux/macOS/FreeBSD）和部分嵌入式系统，是 Mongoose 最基础的 I/O 模型

#define MG_ENABLE_EPOLL 0
// MG_ENABLE_EPOLL 仅在 Linux 平台生效，编译时可显式控制
// 用于启用 Linux 系统下 epoll I/O 多路复用机制 的编译宏开关，是 Mongoose 优化 Linux 平台网络性能的核心配置。


// OHOS RISC-V musl 工具链不提供 alloca 符号，使用 GCC 内置函数替代以解决链接错误
#if defined(__GNUC__) && !defined(alloca)
#define alloca __builtin_alloca
#endif

/* 收发缓冲按该粒度增长；16384 在堆紧张/碎片化时易 mg_iobuf_resize OOM，WS 客户端 4KB 通常足够 */
#ifndef MG_IO_SIZE
#define MG_IO_SIZE          4096
#endif
#define MG_ENABLE_POSIX_FS  0
#define MG_PATH_MAX         100
#define MG_ENABLE_DIRLIST   0


#if 0
mg_call 是 mongoose 库中用于在事件循环线程中安全执行自定义函数的核心工具函数，解决了「跨线程调用连接 / 事件管理器 API」的线程安全问题（mongoose 的事件循环是非线程安全的，所有连接操作必须在事件循环线程中执行）。


Built-in TCP/IP stack [Mongoose 内置的 TCP/IP 栈]
Mongoose works on any system that provides BSD socket API.
In other words, it works on top of any BSD-compatible TCP/IP stack.
That includes UNIX, Mac, Windows systems, as well as some embedded TCP/IP stacks like lwIP.
[这包括 UNIX、Mac、Windows 系统, 以及一些嵌入式 TCP/IP 栈，如 lwIP]

However, Mongoose provides its own TCP/IP stack that can be activated by the build option set to .
It can be done either by setting compiler flags or via :MG_ENABLE_TCPIP1mongoose_config.h
[不过，Mongoose 提供了自身的 TCP/IP 协议栈，可通过将编译选项设置为 MG_ENABLE_TCPIP=1 来激活.
既可以通过设置编译器标志，也可以通过 mongoose_config.h 配置文件实现。]


#endif