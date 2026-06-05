#include "nyla/commons/subprocess.h"

#include "nyla/commons/mem.h"
#include "nyla/commons/time.h"

#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <linux/close_range.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nyla
{

constexpr uint64_t kOutputCap = 0x200000; // 2 MB per stream (SSE responses can be large)

auto API SubprocessRun(span<const char *const> cmd, region_alloc &alloc, byteview stdin_data, uint32_t timeout_ms)
    -> subprocess_result
{
    subprocess_result result = {};
    result.exit_code = -1;

    // Validate args
    if (cmd.size <= 1 || Span::Back(cmd) != nullptr)
        return result;

    int stdoutPipe[2];
    int stderrPipe[2];
    int stdinPipe[2];

    if (pipe(stdoutPipe) == -1)
        return result;
    if (pipe(stderrPipe) == -1)
    {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return result;
    }
    if (pipe(stdinPipe) == -1)
    {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);
        return result;
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        return result;
    }

    if (pid == 0)
    {
        // ── Child ──
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        close(stdinPipe[1]);

        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);

        close(stdinPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);

        if (close_range(3, ~0U, CLOSE_RANGE_UNSHARE) != 0)
            _exit(127);

        execvp(cmd[0], const_cast<char *const *>(cmd.data));
        _exit(127);
    }

    // ── Parent ──
    close(stdoutPipe[1]);
    close(stderrPipe[1]);
    close(stdinPipe[0]);

    // Write stdin data
    if (stdin_data.size > 0)
    {
        uint8_t const *p = stdin_data.data;
        uint64_t remaining = stdin_data.size;
        while (remaining > 0)
        {
            ssize_t n = write(stdinPipe[1], p, (size_t)remaining);
            if (n <= 0)
                break;
            p += n;
            remaining -= (uint64_t)n;
        }
    }
    close(stdinPipe[1]);

    // Read stdout + stderr with optional timeout
    span<uint8_t> outBuf = RegionAlloc::AllocArray<uint8_t>(alloc, kOutputCap);
    span<uint8_t> errBuf = RegionAlloc::AllocArray<uint8_t>(alloc, kOutputCap);
    uint64_t outLen = 0;
    uint64_t errLen = 0;
    bool outDone = false;
    bool errDone = false;

    deadline const dl = (timeout_ms > 0) ? deadline::FromMillis(timeout_ms) : deadline::Never();
    bool timedOut = false;

    // Set non-blocking for poll-based read
    fcntl(stdoutPipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderrPipe[0], F_SETFL, O_NONBLOCK);

    while (!outDone || !errDone)
    {
        struct pollfd fds[2];
        nfds_t nfds = 0;

        if (!outDone)
        {
            fds[nfds].fd = stdoutPipe[0];
            fds[nfds].events = POLLIN;
            nfds++;
        }
        if (!errDone)
        {
            fds[nfds].fd = stderrPipe[0];
            fds[nfds].events = POLLIN;
            nfds++;
        }
        if (nfds == 0)
            break;

        int pollTimeout = -1;
        if (dl.IsActive())
        {
            if (dl.IsExpired())
            {
                timedOut = true;
                break;
            }
            uint64_t remaining = dl.RemainingMs();
            pollTimeout = (int)(remaining > INT32_MAX ? INT32_MAX : remaining);
        }
        int rc = poll(fds, nfds, pollTimeout);
        if (rc == 0)
        {
            // poll timed out
            timedOut = true;
            break;
        }
        if (rc < 0)
            break;

        for (nfds_t i = 0; i < nfds; ++i)
        {
            if (fds[i].revents == 0)
                continue;

            int fd = fds[i].fd;
            bool isStdout = (fd == stdoutPipe[0]);

            // Read data first, then check hangup (both may be set simultaneously)
            span<uint8_t> &buf = isStdout ? outBuf : errBuf;
            uint64_t &len = isStdout ? outLen : errLen;

            if ((fds[i].revents & POLLIN) && len < kOutputCap)
            {
                uint64_t avail = kOutputCap - len;
                ssize_t n = read(fd, buf.data + len, (size_t)avail);
                if (n > 0)
                    len += (uint64_t)n;
            }

            // Hangup or error — mark this stream as done
            if (fds[i].revents & (POLLHUP | POLLERR))
            {
                if (isStdout)
                    outDone = true;
                else
                    errDone = true;
            }
        }
    }

    close(stdoutPipe[0]);
    close(stderrPipe[0]);

    // Kill if timed out
    if (timedOut)
    {
        kill(pid, SIGKILL);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == -1)
    {
        result.exit_code = -1;
        result.stdout_data = byteview{outBuf.data, outLen};
        result.stderr_data = byteview{errBuf.data, errLen};
        result.timed_out = timedOut;
        return result;
    }

    if (WIFEXITED(status))
        result.exit_code = (int32_t)WEXITSTATUS(status);
    else
        result.exit_code = -1;

    result.stdout_data = byteview{outBuf.data, outLen};
    result.stderr_data = byteview{errBuf.data, errLen};
    result.timed_out = timedOut;
    return result;
}

} // namespace nyla
