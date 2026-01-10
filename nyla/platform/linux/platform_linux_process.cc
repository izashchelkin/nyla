#include "nyla/commons/assert.h"
#include "nyla/platform/linux/platform_linux.h"

#include <fcntl.h>
#include <linux/close_range.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <span>

namespace nyla
{

auto Platform::Impl::Spawn(std::span<const char *const> cmd) -> bool
{
    { // TODO: move this somewhere
        static bool installed = false;
        if (!installed)
        {
            struct sigaction sa;
            sa.sa_handler = [](int signum) -> void {
                pid_t pid;
                int status;
                while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
                    ;
            };
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESTART;
            if (sigaction(SIGCHLD, &sa, NULL) == -1)
            {
                NYLA_LOG("sigaction failed");
                return false;
            }
            else
            {
                installed = true;
            }
        }
    }

    if (cmd.size() <= 1)
        return false;
    if (cmd.back() != nullptr)
        return false;

    switch (fork())
    {
    case -1:
        return false;
    case 0:
        break;
    default:
        return true;
    }

    int devNullFd = open("/dev/null", O_RDWR);
    if (devNullFd != -1)
    {
        dup2(devNullFd, STDIN_FILENO);
        dup2(devNullFd, STDOUT_FILENO);
        dup2(devNullFd, STDERR_FILENO);
    }

    if (close_range(3, ~0U, CLOSE_RANGE_UNSHARE) != 0)
    {
        goto failure;
    }

    execvp(cmd[0], const_cast<char *const *>(cmd.data()));

failure:
    _exit(127);
}

} // namespace nyla
