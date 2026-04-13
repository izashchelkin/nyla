/*
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/* Copyright (C) 1991-2026 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#pragma once

#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"

#include <cstdint>

#include <asm/socket.h>
#include <asm/unistd.h>
#include <bits/types/time_t.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <linux/un.h>

/* Older Linux headers do not define these constants.  */
#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif

#ifndef ECANCELED
#define ECANCELED 125
#endif

#ifndef EOWNERDEAD
#define EOWNERDEAD 130
#endif

#ifndef ENOTRECOVERABLE
#define ENOTRECOVERABLE 131
#endif

#ifndef ERFKILL
#define ERFKILL 132
#endif

#ifndef EHWPOISON
#define EHWPOISON 133
#endif

/* Types of sockets.  */
enum __socket_type
{
    SOCK_STREAM = 1, /* Sequenced, reliable, connection-based
                byte streams.  */
#define SOCK_STREAM SOCK_STREAM
    SOCK_DGRAM = 2, /* Connectionless, unreliable datagrams
               of fixed maximum length.  */
#define SOCK_DGRAM SOCK_DGRAM
    SOCK_RAW = 3, /* Raw protocol interface.  */
#define SOCK_RAW SOCK_RAW
    SOCK_RDM = 4, /* Reliably-delivered messages.  */
#define SOCK_RDM SOCK_RDM
    SOCK_SEQPACKET = 5, /* Sequenced, reliable, connection-based,
               datagrams of fixed maximum length.  */
#define SOCK_SEQPACKET SOCK_SEQPACKET
    SOCK_DCCP = 6, /* Datagram Congestion Control Protocol.  */
#define SOCK_DCCP SOCK_DCCP
    SOCK_PACKET = 10, /* Linux specific way of getting packets
                 at the dev level.  For writing rarp and
                 other similar things on the user level. */
#define SOCK_PACKET SOCK_PACKET

    /* Flags to be ORed into the type parameter of socket and socketpair and
       used for the flags parameter of paccept.  */

    SOCK_CLOEXEC = 02000000, /* Atomically set close-on-exec flag for the
                    new descriptor(s).  */
#define SOCK_CLOEXEC SOCK_CLOEXEC
    SOCK_NONBLOCK = 00004000 /* Atomically mark descriptor(s) as
                    non-blocking.  */
#define SOCK_NONBLOCK SOCK_NONBLOCK
};

/* Protocol families.  */
#define PF_UNSPEC 0      /* Unspecified.  */
#define PF_LOCAL 1       /* Local to host (pipes and file-domain).  */
#define PF_UNIX PF_LOCAL /* POSIX name for PF_LOCAL.  */
#define PF_FILE PF_LOCAL /* Another non-standard name for PF_LOCAL.  */
#define PF_INET 2        /* IP protocol family.  */
#define PF_AX25 3        /* Amateur Radio AX.25.  */
#define PF_IPX 4         /* Novell Internet Protocol.  */
#define PF_APPLETALK 5   /* Appletalk DDP.  */
#define PF_NETROM 6      /* Amateur radio NetROM.  */
#define PF_BRIDGE 7      /* Multiprotocol bridge.  */
#define PF_ATMPVC 8      /* ATM PVCs.  */
#define PF_X25 9         /* Reserved for X.25 project.  */
#define PF_INET6 10      /* IP version 6.  */
#define PF_ROSE 11       /* Amateur Radio X.25 PLP.  */
#define PF_DECnet 12     /* Reserved for DECnet project.  */
#define PF_NETBEUI 13    /* Reserved for 802.2LLC project.  */
#define PF_SECURITY 14   /* Security callback pseudo AF.  */
#define PF_KEY 15        /* PF_KEY key management API.  */
#define PF_NETLINK 16
#define PF_ROUTE PF_NETLINK /* Alias to emulate 4.4BSD.  */
#define PF_PACKET 17        /* Packet family.  */
#define PF_ASH 18           /* Ash.  */
#define PF_ECONET 19        /* Acorn Econet.  */
#define PF_ATMSVC 20        /* ATM SVCs.  */
#define PF_RDS 21           /* RDS sockets.  */
#define PF_SNA 22           /* Linux SNA Project */
#define PF_IRDA 23          /* IRDA sockets.  */
#define PF_PPPOX 24         /* PPPoX sockets.  */
#define PF_WANPIPE 25       /* Wanpipe API sockets.  */
#define PF_LLC 26           /* Linux LLC.  */
#define PF_IB 27            /* Native InfiniBand address.  */
#define PF_MPLS 28          /* MPLS.  */
#define PF_CAN 29           /* Controller Area Network.  */
#define PF_TIPC 30          /* TIPC sockets.  */
#define PF_BLUETOOTH 31     /* Bluetooth sockets.  */
#define PF_IUCV 32          /* IUCV sockets.  */
#define PF_RXRPC 33         /* RxRPC sockets.  */
#define PF_ISDN 34          /* mISDN sockets.  */
#define PF_PHONET 35        /* Phonet sockets.  */
#define PF_IEEE802154 36    /* IEEE 802.15.4 sockets.  */
#define PF_CAIF 37          /* CAIF sockets.  */
#define PF_ALG 38           /* Algorithm sockets.  */
#define PF_NFC 39           /* NFC sockets.  */
#define PF_VSOCK 40         /* vSockets.  */
#define PF_KCM 41           /* Kernel Connection Multiplexor.  */
#define PF_QIPCRTR 42       /* Qualcomm IPC Router.  */
#define PF_SMC 43           /* SMC sockets.  */
#define PF_XDP 44           /* XDP sockets.  */
#define PF_MCTP 45          /* Management component transport protocol.  */
#define PF_MAX 46           /* For now..  */

/* Address families.  */
#define AF_UNSPEC PF_UNSPEC
#define AF_LOCAL PF_LOCAL
#define AF_UNIX PF_UNIX
#define AF_FILE PF_FILE
#define AF_INET PF_INET
#define AF_AX25 PF_AX25
#define AF_IPX PF_IPX
#define AF_APPLETALK PF_APPLETALK
#define AF_NETROM PF_NETROM
#define AF_BRIDGE PF_BRIDGE
#define AF_ATMPVC PF_ATMPVC
#define AF_X25 PF_X25
#define AF_INET6 PF_INET6
#define AF_ROSE PF_ROSE
#define AF_DECnet PF_DECnet
#define AF_NETBEUI PF_NETBEUI
#define AF_SECURITY PF_SECURITY
#define AF_KEY PF_KEY
#define AF_NETLINK PF_NETLINK
#define AF_ROUTE PF_ROUTE
#define AF_PACKET PF_PACKET
#define AF_ASH PF_ASH
#define AF_ECONET PF_ECONET
#define AF_ATMSVC PF_ATMSVC
#define AF_RDS PF_RDS
#define AF_SNA PF_SNA
#define AF_IRDA PF_IRDA
#define AF_PPPOX PF_PPPOX
#define AF_WANPIPE PF_WANPIPE
#define AF_LLC PF_LLC
#define AF_IB PF_IB
#define AF_MPLS PF_MPLS
#define AF_CAN PF_CAN
#define AF_TIPC PF_TIPC
#define AF_BLUETOOTH PF_BLUETOOTH
#define AF_IUCV PF_IUCV
#define AF_RXRPC PF_RXRPC
#define AF_ISDN PF_ISDN
#define AF_PHONET PF_PHONET
#define AF_IEEE802154 PF_IEEE802154
#define AF_CAIF PF_CAIF
#define AF_ALG PF_ALG
#define AF_NFC PF_NFC
#define AF_VSOCK PF_VSOCK
#define AF_KCM PF_KCM
#define AF_QIPCRTR PF_QIPCRTR
#define AF_SMC PF_SMC
#define AF_XDP PF_XDP
#define AF_MCTP PF_MCTP
#define AF_MAX PF_MAX

/* Socket level values.  Others are defined in the appropriate headers. */
#define SOL_RAW 255
#define SOL_DECNET 261
#define SOL_X25 262
#define SOL_PACKET 263
#define SOL_ATM 264 /* ATM layer (cell level).  */
#define SOL_AAL 265 /* ATM Adaption Layer (packet level).  */
#define SOL_IRDA 266
#define SOL_NETBEUI 267
#define SOL_LLC 268
#define SOL_DCCP 269
#define SOL_NETLINK 270
#define SOL_TIPC 271
#define SOL_RXRPC 272
#define SOL_PPPOL2TP 273
#define SOL_BLUETOOTH 274
#define SOL_PNPIPE 275
#define SOL_RDS 276
#define SOL_IUCV 277
#define SOL_CAIF 278
#define SOL_ALG 279
#define SOL_NFC 280
#define SOL_KCM 281
#define SOL_TLS 282
#define SOL_XDP 283
#define SOL_MPTCP 284
#define SOL_MCTP 285
#define SOL_SMC 286
#define SOL_VSOCK 287

/* Maximum queue length specifiable by listen.  */
#define SOMAXCONN 4096

/* POSIX.1g specifies this type name for the `sa_family' member.  */
typedef unsigned short int sa_family_t;

/* This macro is used to declare the initial common members
   of the data types used for socket addresses, `struct sockaddr',
   `struct sockaddr_in', `struct sockaddr_un', etc.  */

#define __SOCKADDR_COMMON(sa_prefix) sa_family_t sa_prefix##family

#define __SOCKADDR_COMMON_SIZE (sizeof(unsigned short int))

/* Size of struct sockaddr_storage.  */
#define _SS_SIZE 128

/* Structure describing a generic socket address.  */
struct __attribute_struct_may_alias__ sockaddr
{
    __SOCKADDR_COMMON(sa_); /* Common data: address family and length.  */
    char sa_data[14];       /* Address data.  */
};

/* Structure large enough to hold any socket address (with the historical
   exception of AF_UNIX).  */
#define __ss_aligntype unsigned long int
#define _SS_PADSIZE (_SS_SIZE - __SOCKADDR_COMMON_SIZE - sizeof(__ss_aligntype))

struct __attribute_struct_may_alias__ sockaddr_storage
{
    __SOCKADDR_COMMON(ss_); /* Address family, etc.  */
    char __ss_padding[_SS_PADSIZE];
    __ss_aligntype __ss_align; /* Force desired alignment.  */
};

/* Bits in the FLAGS argument to `send', `recv', et al.  */
enum
{
    MSG_OOB = 0x01, /* Process out-of-band data.  */
#define MSG_OOB MSG_OOB
    MSG_PEEK = 0x02, /* Peek at incoming messages.  */
#define MSG_PEEK MSG_PEEK
    MSG_DONTROUTE = 0x04, /* Don't use local routing.  */
#define MSG_DONTROUTE MSG_DONTROUTE
#ifdef __USE_GNU
    /* DECnet uses a different name.  */
    MSG_TRYHARD = MSG_DONTROUTE,
#define MSG_TRYHARD MSG_DONTROUTE
#endif
    MSG_CTRUNC = 0x08, /* Control data lost before delivery.  */
#define MSG_CTRUNC MSG_CTRUNC
    MSG_PROXY = 0x10, /* Supply or ask second address.  */
#define MSG_PROXY MSG_PROXY
    MSG_TRUNC = 0x20,
#define MSG_TRUNC MSG_TRUNC
    MSG_DONTWAIT = 0x40, /* Nonblocking IO.  */
#define MSG_DONTWAIT MSG_DONTWAIT
    MSG_EOR = 0x80, /* End of record.  */
#define MSG_EOR MSG_EOR
    MSG_WAITALL = 0x100, /* Wait for a full request.  */
#define MSG_WAITALL MSG_WAITALL
    MSG_FIN = 0x200,
#define MSG_FIN MSG_FIN
    MSG_SYN = 0x400,
#define MSG_SYN MSG_SYN
    MSG_CONFIRM = 0x800, /* Confirm path validity.  */
#define MSG_CONFIRM MSG_CONFIRM
    MSG_RST = 0x1000,
#define MSG_RST MSG_RST
    MSG_ERRQUEUE = 0x2000, /* Fetch message from error queue.  */
#define MSG_ERRQUEUE MSG_ERRQUEUE
    MSG_NOSIGNAL = 0x4000, /* Do not generate SIGPIPE.  */
#define MSG_NOSIGNAL MSG_NOSIGNAL
    MSG_MORE = 0x8000, /* Sender will send more.  */
#define MSG_MORE MSG_MORE
    MSG_WAITFORONE = 0x10000, /* Wait for at least one packet to return.*/
#define MSG_WAITFORONE MSG_WAITFORONE
    MSG_BATCH = 0x40000, /* sendmmsg: more messages coming.  */
#define MSG_BATCH MSG_BATCH
    MSG_SOCK_DEVMEM = 0x2000000, /* Receive devmem skbs as cmsg.  */
#define MSG_SOCK_DEVMEM MSG_SOCK_DEVMEM
    MSG_ZEROCOPY = 0x4000000, /* Use user data in kernel path.  */
#define MSG_ZEROCOPY MSG_ZEROCOPY
    MSG_FASTOPEN = 0x20000000, /* Send data in TCP SYN.  */
#define MSG_FASTOPEN MSG_FASTOPEN

    MSG_CMSG_CLOEXEC = 0x40000000 /* Set close_on_exit for file
                 descriptor received through
                 SCM_RIGHTS.  */
#define MSG_CMSG_CLOEXEC MSG_CMSG_CLOEXEC
};

/* Structure describing messages sent by
   `sendmsg' and received by `recvmsg'.  */
struct msghdr
{
    void *msg_name;          /* Address to send to/receive from.  */
    __socklen_t msg_namelen; /* Length of address data.  */

    struct iovec *msg_iov; /* Vector of data to send/receive into.  */
    uint64_t msg_iovlen;   /* Number of elements in the vector.  */

    void *msg_control;       /* Ancillary data (eg BSD filedesc passing). */
    uint64_t msg_controllen; /* Ancillary data buffer length.
                  !! The type should be socklen_t but the
                  definition of the kernel is incompatible
                  with this.  */

    int msg_flags; /* Flags on received message.  */
};

/* Structure used for storage of ancillary data object information.  */
struct cmsghdr
{
    uint64_t cmsg_len; /* Length of data in cmsg_data plus length
            of cmsghdr structure.
            !! The type should be socklen_t but the
            definition of the kernel is incompatible
            with this.  */
    int cmsg_level;    /* Originating protocol.  */
    int cmsg_type;     /* Protocol specific type.  */
#if __glibc_c99_flexarr_available
    __extension__ unsigned char __cmsg_data __flexarr; /* Ancillary data.  */
#endif
};

/* Ancillary data object manipulation macros.  */
#if __glibc_c99_flexarr_available
#define CMSG_DATA(cmsg) ((cmsg)->__cmsg_data)
#else
#define CMSG_DATA(cmsg) ((unsigned char *)((struct cmsghdr *)(cmsg) + 1))
#endif
#define CMSG_NXTHDR(mhdr, cmsg) __cmsg_nxthdr(mhdr, cmsg)
#define CMSG_FIRSTHDR(mhdr)                                                                                            \
    ((uint64_t)(mhdr)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(mhdr)->msg_control                \
                                                                : (struct cmsghdr *)0)
#define CMSG_ALIGN(len) (((len) + sizeof(uint64_t) - 1) & (uint64_t)~(sizeof(uint64_t) - 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(len) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))

/* Given a length, return the additional padding necessary such that
   len + __CMSG_PADDING(len) == CMSG_ALIGN (len).  */
#define __CMSG_PADDING(len) ((sizeof(uint64_t) - ((len) & (sizeof(uint64_t) - 1))) & (sizeof(uint64_t) - 1))

extern struct cmsghdr *__cmsg_nxthdr(struct msghdr *__mhdr, struct cmsghdr *__cmsg) __THROW;
#ifdef __USE_EXTERN_INLINES
#ifndef _EXTERN_INLINE
#define _EXTERN_INLINE __extern_inline
#endif
_EXTERN_INLINE struct cmsghdr *__NTH(__cmsg_nxthdr(struct msghdr *__mhdr, struct cmsghdr *__cmsg))
{
    /* We may safely assume that __cmsg lies between __mhdr->msg_control and
       __mhdr->msg_controllen because the user is required to obtain the first
       cmsg via CMSG_FIRSTHDR, set its length, then obtain subsequent cmsgs
       via CMSG_NXTHDR, setting lengths along the way.  However, we don't yet
       trust the value of __cmsg->cmsg_len and therefore do not use it in any
       pointer arithmetic until we check its value.  */

    unsigned char *__msg_control_ptr = (unsigned char *)__mhdr->msg_control;
    unsigned char *__cmsg_ptr = (unsigned char *)__cmsg;

    uint64_t __size_needed = sizeof(struct cmsghdr) + __CMSG_PADDING(__cmsg->cmsg_len);

    /* The current header is malformed, too small to be a full header.  */
    if ((uint64_t)__cmsg->cmsg_len < sizeof(struct cmsghdr))
        return (struct cmsghdr *)0;

    /* There isn't enough space between __cmsg and the end of the buffer to
    hold the current cmsg *and* the next one.  */
    if (((uint64_t)(__msg_control_ptr + __mhdr->msg_controllen - __cmsg_ptr) < __size_needed) ||
        ((uint64_t)(__msg_control_ptr + __mhdr->msg_controllen - __cmsg_ptr - __size_needed) < __cmsg->cmsg_len))

        return (struct cmsghdr *)0;

    /* Now, we trust cmsg_len and can use it to find the next header.  */
    __cmsg = (struct cmsghdr *)((unsigned char *)__cmsg + CMSG_ALIGN(__cmsg->cmsg_len));
    return __cmsg;
}
#endif /* Use `extern inline'.  */

/* Socket level message types.  This must match the definitions in
   <linux/socket.h>.  */
enum
{
    SCM_RIGHTS = 0x01 /* Transfer file descriptors.  */
#define SCM_RIGHTS SCM_RIGHTS
#ifdef __USE_GNU
        ,
    SCM_CREDENTIALS = 0x02 /* Credentials passing.  */
#define SCM_CREDENTIALS SCM_CREDENTIALS
        ,
    SCM_SECURITY = 0x03 /* Security label.  */
#define SCM_SECURITY SCM_SECURITY
        ,
    SCM_PIDFD = 0x04 /* Pidfd.  */
#define SCM_PIDFD SCM_PIDFD
#endif
};

#ifdef __USE_GNU
/* User visible structure for SCM_CREDENTIALS message */
struct ucred
{
    __pid_t pid; /* PID of sending process.  */
    __uid_t uid; /* UID of sending process.  */
    __gid_t gid; /* GID of sending process.  */
};
#endif

/* Structure used to manipulate the SO_LINGER option.  */
struct linger
{
    int l_onoff;  /* Nonzero to linger on close.  */
    int l_linger; /* Time to linger.  */
};

//
//
//

namespace nyla
{

struct syscall_result
{
    int64_t raw;
};

namespace sys
{

INLINE auto IsError(syscall_result res, int *err) -> bool
{
    if ((uint64_t)res.raw >= (uint64_t)-4095)
    {
        if (err)
            *err = -res.raw;
        return true;
    }
    return false;
}

INLINE syscall_result SysCall0(int64_t num);
INLINE syscall_result SysCall1(int64_t num, int64_t arg1);
INLINE syscall_result SysCall2(int64_t num, int64_t arg1, int64_t arg2);
INLINE syscall_result SysCall3(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3);
INLINE syscall_result SysCall4(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4);
INLINE syscall_result SysCall5(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5);
INLINE syscall_result SysCall6(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5,
                               int64_t arg6);

} // namespace sys

INLINE syscall_result open(const char *path, int flags)
{
    return sys::SysCall4(__NR_openat, AT_FDCWD, (int64_t)path, flags, 0);
}

INLINE syscall_result open(const char *path, int flags, __mode_t mode)
{
    return sys::SysCall4(__NR_openat, AT_FDCWD, (int64_t)path, flags, mode);
}

INLINE syscall_result socket(int32_t domain, int32_t type, int32_t protocol)
{
    return sys::SysCall3(__NR_socket, domain, type, protocol);
}

INLINE syscall_result connect(int32_t fd, const struct sockaddr *addr, __socklen_t len)
{
    return sys::SysCall3(__NR_connect, fd, (int64_t)addr, (int64_t)len);
}

INLINE syscall_result getsockopt(int32_t fd, int32_t level, int32_t optname, void *optval, __socklen_t *optlen)
{
    return sys::SysCall5(__NR_getsockopt, fd, level, optname, (int64_t)optval, (int64_t)optlen);
}

INLINE syscall_result setsockopt(int32_t fd, int32_t level, int32_t optname, const void *optval, __socklen_t optlen)
{
    return sys::SysCall5(__NR_setsockopt, fd, level, optname, (int64_t)optval, (int64_t)optlen);
}

INLINE syscall_result read(int32_t fd, void *buf, uint64_t count)
{
    return sys::SysCall3(__NR_read, fd, (int64_t)buf, (int64_t)count);
}

INLINE syscall_result write(int32_t fd, const void *buf, uint64_t count)
{
    return sys::SysCall3(__NR_write, fd, (int64_t)buf, (int64_t)count);
}

INLINE syscall_result close(int32_t fd)
{
    return sys::SysCall1(__NR_close, fd);
}

INLINE syscall_result fcntl(int32_t fd, int32_t cmd)
{
    return sys::SysCall3(__NR_fcntl, fd, cmd, 0);
}

INLINE syscall_result fcntl(int32_t fd, int32_t cmd, int64_t arg)
{
    return sys::SysCall3(__NR_fcntl, fd, cmd, arg);
}

INLINE syscall_result fcntl(int32_t fd, int32_t cmd, const void *arg)
{
    return sys::SysCall3(__NR_fcntl, fd, cmd, (int64_t)arg);
}

INLINE syscall_result fcntl(int32_t fd, int32_t cmd, void *arg)
{
    return sys::SysCall3(__NR_fcntl, fd, cmd, (int64_t)arg);
}

namespace sys
{

INLINE auto SysCall0(int64_t num) -> syscall_result
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);

    __asm__ volatile("syscall\n" : "=a"(ret) : "0"(_num) : "rcx", "r11", "memory", "cc");
    return {ret};
}

INLINE auto SysCall1(int64_t num, int64_t arg1) -> syscall_result
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);
    register int64_t _arg1 __asm__("rdi") = (int64_t)(arg1);

    __asm__ volatile("syscall\n" : "=a"(ret) : "r"(_arg1), "0"(_num) : "rcx", "r11", "memory", "cc");
    return {ret};
}

INLINE auto SysCall2(int64_t num, int64_t arg1, int64_t arg2) -> syscall_result
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);
    register int64_t _arg1 __asm__("rdi") = (int64_t)(arg1);
    register int64_t _arg2 __asm__("rsi") = (int64_t)(arg2);

    __asm__ volatile("syscall\n" : "=a"(ret) : "r"(_arg1), "r"(_arg2), "0"(_num) : "rcx", "r11", "memory", "cc");
    return {ret};
}

INLINE auto SysCall3(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3) -> syscall_result
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);
    register int64_t _arg1 __asm__("rdi") = (int64_t)(arg1);
    register int64_t _arg2 __asm__("rsi") = (int64_t)(arg2);
    register int64_t _arg3 __asm__("rdx") = (int64_t)(arg3);

    __asm__ volatile("syscall\n"
                     : "=a"(ret)
                     : "r"(_arg1), "r"(_arg2), "r"(_arg3), "0"(_num)
                     : "rcx", "r11", "memory", "cc");
    return {ret};
}

INLINE auto SysCall4(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4) -> syscall_result
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);
    register int64_t _arg1 __asm__("rdi") = (int64_t)(arg1);
    register int64_t _arg2 __asm__("rsi") = (int64_t)(arg2);
    register int64_t _arg3 __asm__("rdx") = (int64_t)(arg3);
    register int64_t _arg4 __asm__("r10") = (int64_t)(arg4);

    __asm__ volatile("syscall\n"
                     : "=a"(ret)
                     : "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "0"(_num)
                     : "rcx", "r11", "memory", "cc");
    return {ret};
}

INLINE auto SysCall5(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5)
    -> syscall_result
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);
    register int64_t _arg1 __asm__("rdi") = (int64_t)(arg1);
    register int64_t _arg2 __asm__("rsi") = (int64_t)(arg2);
    register int64_t _arg3 __asm__("rdx") = (int64_t)(arg3);
    register int64_t _arg4 __asm__("r10") = (int64_t)(arg4);
    register int64_t _arg5 __asm__("r8") = (int64_t)(arg5);

    __asm__ volatile("syscall\n"
                     : "=a"(ret)
                     : "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), "0"(_num)
                     : "rcx", "r11", "memory", "cc");
    return {ret};
}

INLINE auto SysCall6(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int64_t arg6)
    -> syscall_result
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);
    register int64_t _arg1 __asm__("rdi") = (int64_t)(arg1);
    register int64_t _arg2 __asm__("rsi") = (int64_t)(arg2);
    register int64_t _arg3 __asm__("rdx") = (int64_t)(arg3);
    register int64_t _arg4 __asm__("r10") = (int64_t)(arg4);
    register int64_t _arg5 __asm__("r8") = (int64_t)(arg5);
    register int64_t _arg6 __asm__("r9") = (int64_t)(arg6);

    __asm__ volatile("syscall\n"
                     : "=a"(ret)
                     : "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), "r"(_arg6), "0"(_num)
                     : "rcx", "r11", "memory", "cc");
    return {ret};
}

} // namespace sys

} // namespace nyla