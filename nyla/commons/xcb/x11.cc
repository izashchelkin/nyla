/* Copyright (C) 2001-2004 Bart Massey and Jamey Sharp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the names of the authors or their
 * institutions shall not be used in advertising or otherwise to promote the
 * sale, use or other dealings in this Software without prior written
 * authorization from the authors.
 */

#include <cstdint>
#include <linux/un.h>

#include "nyla/commons/fmt.h"
#include "nyla/commons/linux_abi.h"
#include "nyla/commons/span.h"

namespace nyla
{

namespace
{

auto _xcb_socket(int family, int type, int proto) -> syscall_result
{
    return socket(family, type | SOCK_CLOEXEC, proto);
}

auto _xcb_open_unix(byteview file) -> syscall_result
{
    ASSERT(file.size < UNIX_PATH_MAX);

    struct sockaddr_un addr{
        .sun_family = AF_UNIX,
    };

    MemCpy(addr.sun_path, file.data, file.size);
    addr.sun_path[file.size] = '\0';

    auto fd = _xcb_socket(AF_UNIX, SOCK_STREAM, 0);
    if (sys::IsError(fd, nullptr))
        return fd;

    __socklen_t len = sizeof(int32_t);
    int32_t val;

    auto res = getsockopt(fd.raw, SOL_SOCKET, SO_SNDBUF, &val, &len);
    if (!sys::IsError(res, nullptr) && val < 64 * 1024)
    {
    }

    if (== 0 &&)
    {
        val = 64 * 1024;
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(int));
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        close(fd);
        return -1;
    }

    return fd;
}

} // namespace

static int _xcb_open(const char *host, char *protocol, const int display)
{
#ifndef _WIN32
    int fd;
#ifdef __hpux
    static const char unix_base[] = "/usr/spool/sockets/X11/";
#else
    static const char unix_base[] = "/tmp/.X11-unix/X";
#endif
    const char *base = unix_base;
    size_t filelen;
    char *file = NULL;

    if (protocol && strcmp("unix", protocol) == 0 && host && host[0] == '/')
    {
        /* Full path to socket provided, ignore everything else */
        filelen = strlen(host) + 1;
        if (filelen > INT_MAX)
            return -1;
        file = malloc(filelen);
        if (file == NULL)
            return -1;
        memcpy(file, host, filelen);
    }
    else
    {
#endif /* _WIN32 */

        /* If protocol or host is "unix", fall through to Unix socket code below */
        if ((!protocol || (strcmp("unix", protocol) != 0)) && (*host != '\0') && (strcmp("unix", host) != 0))
        {
            /* display specifies TCP */
            unsigned short port = X_TCP_PORT + display;
            return _xcb_open_tcp(host, protocol, port);
        }

#ifndef _WIN32
#if defined(HAVE_TSOL_LABEL_H) && defined(HAVE_IS_SYSTEM_LABELED)
        /* Check special path for Unix sockets under Solaris Trusted Extensions */
        if (is_system_labeled())
        {
            const char *tsol_base = "/var/tsol/doors/.X11-unix/X";
            char tsol_socket[PATH_MAX];
            struct stat sbuf;

            snprintf(tsol_socket, sizeof(tsol_socket), "%s%d", tsol_base, display);

            if (stat(tsol_socket, &sbuf) == 0)
                base = tsol_base;
            else if (errno != ENOENT)
                return 0;
        }
#endif

        filelen = strlen(base) + 1 + sizeof(display) * 3 + 1;
        file = malloc(filelen);
        if (file == NULL)
            return -1;

        /* display specifies Unix socket */
        if (snprintf(file, filelen, "%s%d", base, display) < 0)
        {
            free(file);
            return -1;
        }
    }

    if (protocol && strcmp("unix", protocol))
        return -1;
    fd = _xcb_open_unix(protocol, file);
    free(file);

    if (fd < 0 && !protocol && *host == '\0')
    {
        unsigned short port = X_TCP_PORT + display;
        fd = _xcb_open_tcp(host, protocol, port);
    }

    return fd;
#endif         /* !_WIN32 */
    return -1; /* if control reaches here then something has gone wrong */
}

} // namespace nyla