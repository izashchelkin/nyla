#include "nyla/commons/macros.h"

#include <asm/unistd.h>
#include <cstdint>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace nyla
{

namespace sys
{

//

INLINE int64_t SysCall0(int64_t num);
INLINE int64_t SysCall1(int64_t num, int64_t arg1);
INLINE int64_t SysCall2(int64_t num, int64_t arg1, int64_t arg2);
INLINE int64_t SysCall3(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3);
INLINE int64_t SysCall4(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4);
INLINE int64_t SysCall5(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5);
INLINE int64_t SysCall6(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5,
                        int64_t arg6);

//

INLINE int64_t socket(int32_t domain, int32_t type, int32_t protocol)
{
    return SysCall3(__NR_socket, domain, type, protocol);
}

INLINE int64_t connect(int32_t fd, const struct sockaddr *addr, socklen_t len)
{
    return SysCall3(__NR_connect, fd, (int64_t)addr, (int64_t)len);
}

INLINE int64_t read(int32_t fd, void *buf, size_t count)
{
    return SysCall3(__NR_read, fd, (int64_t)buf, (int64_t)count);
}

INLINE int64_t write(int32_t fd, const void *buf, size_t count)
{
    return SysCall3(__NR_write, fd, (int64_t)buf, (int64_t)count);
}

INLINE int64_t close(int32_t fd)
{
    return SysCall1(__NR_close, fd);
}

//

INLINE int64_t SysCall0(int64_t num)
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);

    __asm__ volatile("syscall\n" : "=a"(ret) : "0"(_num) : "rcx", "r11", "memory", "cc");
    return ret;
}

INLINE int64_t SysCall1(int64_t num, int64_t arg1)
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);
    register int64_t _arg1 __asm__("rdi") = (int64_t)(arg1);

    __asm__ volatile("syscall\n" : "=a"(ret) : "r"(_arg1), "0"(_num) : "rcx", "r11", "memory", "cc");
    return ret;
}

INLINE int64_t SysCall2(int64_t num, int64_t arg1, int64_t arg2)
{
    int64_t ret;
    register int64_t _num __asm__("rax") = (num);
    register int64_t _arg1 __asm__("rdi") = (int64_t)(arg1);
    register int64_t _arg2 __asm__("rsi") = (int64_t)(arg2);

    __asm__ volatile("syscall\n" : "=a"(ret) : "r"(_arg1), "r"(_arg2), "0"(_num) : "rcx", "r11", "memory", "cc");
    return ret;
}

INLINE int64_t SysCall3(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3)
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
    return ret;
}

INLINE int64_t SysCall4(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4)
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
    return ret;
}

INLINE int64_t SysCall5(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5)
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
    return ret;
}

INLINE int64_t SysCall6(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int64_t arg6)
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
    return ret;
}

} // namespace sys

} // namespace nyla
