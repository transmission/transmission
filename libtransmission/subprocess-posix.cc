// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fmt/core.h>

#include "libtransmission/error.h"
#include "libtransmission/subprocess.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{
void handle_sigchld(int /*i*/)
{
    for (;;)
    {
        // FIXME: only check for our own PIDs
        auto const res = waitpid(-1, nullptr, WNOHANG);

        if ((res == 0) || (res == -1 && errno != EINTR))
        {
            break;
        }
    }

    // FIXME: Call old handler, if any
}

void set_system_error(tr_error* error, int code, std::string_view what)
{
    if (error != nullptr)
    {
        error->set(code, fmt::format("{:s} failed: {:s} ({:d})", what, tr_strerror(code), code));
    }
}

[[nodiscard]] bool tr_spawn_async_in_child(
    char const* const* cmd,
    std::map<std::string_view, std::string_view> const& env,
    std::string_view work_dir)
{
    auto key_sz = std::string{};
    auto val_sz = std::string{};

    for (auto const& [key_sv, val_sv] : env)
    {
        key_sz = key_sv;
        val_sz = val_sv;

        if (setenv(key_sz.c_str(), val_sz.c_str(), 1) != 0)
        {
            return false;
        }
    }

    if (!std::empty(work_dir) && chdir(tr_pathbuf{ work_dir }) == -1)
    {
        return false;
    }

    if (execvp(cmd[0], const_cast<char* const*>(cmd)) == -1)
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool tr_spawn_async_in_parent(int pipe_fd, tr_error* error)
{
    for (;;)
    {
        int child_errno = 0;
        auto const n_read = read(pipe_fd, &child_errno, sizeof(child_errno));

        if (n_read == 0) // child successfully exec'ed
        {
            return true;
        }

        if (n_read > 0)
        {
            // child errno was set
            TR_ASSERT(static_cast<size_t>(count) == sizeof(child_errno));
            set_system_error(error, child_errno, "Child process setup");
            return false;
        }

        if (errno != EINTR) // read failed (what to do?)
        {
            return true;
        }

        // got EINTR; try again
    }
}
} // namespace

bool tr_spawn_async(
    char const* const* cmd,
    std::map<std::string_view, std::string_view> const& env,
    std::string_view work_dir,
    tr_error* error)
{
    static bool sigchld_handler_set = false;

    if (!sigchld_handler_set)
    {
        /* FIXME: "The effects of signal() in a multithreaded process are unspecified." © man 2 signal */
        if (signal(SIGCHLD, &handle_sigchld) == SIG_ERR) // NOLINT(performance-no-int-to-ptr)
        {
            set_system_error(error, errno, "Call to signal()");
            return false;
        }

        sigchld_handler_set = true;
    }

    auto pipe_fds = std::array<int, 2>{};

    if (pipe(std::data(pipe_fds)) == -1)
    {
        set_system_error(error, errno, "Call to pipe()");
        return false;
    }

    if (fcntl(pipe_fds[1], F_SETFD, fcntl(pipe_fds[1], F_GETFD) | FD_CLOEXEC) == -1)
    {
        set_system_error(error, errno, "Call to fcntl()");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    int const child_pid = fork();

    if (child_pid == -1)
    {
        set_system_error(error, errno, "Call to fork()");
        return false;
    }

    if (child_pid == 0)
    {
        close(pipe_fds[0]);

        if (!tr_spawn_async_in_child(cmd, env, work_dir))
        {
            auto const ok = write(pipe_fds[1], &errno, sizeof(errno)) != -1;
            _exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
        }
    }

    close(pipe_fds[1]);

    return tr_spawn_async_in_parent(pipe_fds[0], error);
}
