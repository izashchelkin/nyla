#include <fcntl.h>
#include <linux/close_range.h>
#include <unistd.h>

#include <cassert>

#include "internal.hpp"

namespace nyla {

bool spawn(std::span<const char* const> cmd) {
    assert(cmd.size() > 1 && !cmd.back());

    switch (fork()) {
        case -1:
            return false;
        case 0:
            break;
        default:
            return true;
    }

    int dev_null_fd = open("/dev/null", O_RDWR);
    if (dev_null_fd != -1) {
        dup2(dev_null_fd, STDIN_FILENO);
        dup2(dev_null_fd, STDOUT_FILENO);
        dup2(dev_null_fd, STDERR_FILENO);
    }

    if (close_range(3, ~0U, CLOSE_RANGE_UNSHARE) == 0) {
        execvp(cmd[0], const_cast<char* const*>(cmd.data()));
    }
    _exit(127);
}

}    // namespace nyla
