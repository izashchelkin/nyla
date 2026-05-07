#include "nyla/commons/platform_pty.h"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "nyla/commons/fmt.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

struct platform_pty
{
    int masterFd;
    pid_t childPid;
    bool alive;
};

namespace PlatformPty
{

auto API Create(region_alloc &alloc, const platform_pty_spawn_desc &desc) -> platform_pty *
{
    auto &self = RegionAlloc::Alloc<platform_pty>(alloc);
    self.masterFd = -1;
    self.childPid = -1;
    self.alive = false;

    winsize ws{};
    ws.ws_col = (uint16_t)desc.cols;
    ws.ws_row = (uint16_t)desc.rows;

    int master = -1;
    pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0)
    {
        LOG("forkpty failed: errno=%d"_s, errno);
        return &self;
    }

    if (pid == 0)
    {
        // Child: exec the shell. forkpty has already set up stdio + made it the controlling tty.
        const char *shell = "/bin/bash";
        if (desc.shellPath.size > 0)
            shell = (const char *)desc.shellPath.data; // assumed null-terminated by caller

        setenv("TERM", "xterm-256color", 1);

        execl(shell, shell, (char *)nullptr);
        _exit(127);
    }

    int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);

    self.masterFd = master;
    self.childPid = pid;
    self.alive = true;
    return &self;
}

void API Destroy(platform_pty &self)
{
    if (self.childPid > 0)
    {
        kill(self.childPid, SIGHUP);
        int status = 0;
        waitpid(self.childPid, &status, 0);
        self.childPid = -1;
    }
    if (self.masterFd >= 0)
    {
        close(self.masterFd);
        self.masterFd = -1;
    }
    self.alive = false;
}

auto API Read(platform_pty &self, span<uint8_t> out) -> uint32_t
{
    if (self.masterFd < 0 || out.size == 0)
        return 0;

    int64_t n = read(self.masterFd, out.data, out.size);
    if (n > 0)
        return (uint32_t)n;
    return 0;
}

auto API Write(platform_pty &self, byteview bytes) -> uint32_t
{
    if (self.masterFd < 0 || bytes.size == 0)
        return 0;

    int64_t n = write(self.masterFd, bytes.data, bytes.size);
    if (n > 0)
        return (uint32_t)n;
    return 0;
}

void API Resize(platform_pty &self, uint32_t cols, uint32_t rows)
{
    if (self.masterFd < 0)
        return;
    winsize ws{};
    ws.ws_col = (uint16_t)cols;
    ws.ws_row = (uint16_t)rows;
    ioctl(self.masterFd, TIOCSWINSZ, &ws);
}

auto API IsAlive(platform_pty &self) -> bool
{
    if (!self.alive)
        return false;
    if (self.childPid <= 0)
        return false;
    int status = 0;
    pid_t r = waitpid(self.childPid, &status, WNOHANG);
    if (r == self.childPid)
        self.alive = false;
    return self.alive;
}

} // namespace PlatformPty

} // namespace nyla
