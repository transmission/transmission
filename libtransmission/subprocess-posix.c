/*
 * This file Copyright (C) 2010-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "transmission.h"
#include "error.h"
#include "subprocess.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "utils.h"


static volatile int *volatile child_pid_list = NULL;
static volatile int child_pid_list_len = 0;


static void handle_sigchld(int sig)
{
    TR_UNUSED(sig);

    int rc;

    for (int i = 0; i < child_pid_list_len; i++)
    {
        int pid = child_pid_list[i];
        if (pid != 0)
        {
            do
            {
                rc = waitpid(pid, NULL, WNOHANG);
            }
            while (rc == -1 && errno == EINTR);
            if (rc == pid)
            {
                child_pid_list[i] = 0;
            }
        }
    }

    /* FIXME: Call old handler, if any */
}

static void set_system_error(tr_error** error, int code, char const* what)
{
    if (error == NULL)
    {
        return;
    }

    if (what == NULL)
    {
        tr_error_set_literal(error, code, tr_strerror(code));
    }
    else
    {
        tr_error_set(error, code, "%s failed: %s", what, tr_strerror(code));
    }
}

static bool tr_spawn_async_in_child(char* const* cmd, char* const* env, char const* work_dir, int pipe_fd)
{
    if (env != NULL)
    {
        for (size_t i = 0; env[i] != NULL; ++i)
        {
            if (putenv(env[i]) != 0)
            {
                goto fail;
            }
        }
    }

    if (work_dir != NULL && chdir(work_dir) == -1)
    {
        goto fail;
    }

    if (execvp(cmd[0], cmd) == -1)
    {
        goto fail;
    }

    return true;

fail:
    (void)write(pipe_fd, &errno, sizeof(errno));
    return false;
}

static bool tr_spawn_async_in_parent(int pipe_fd, tr_error** error)
{
    int child_errno;
    ssize_t count;

    TR_STATIC_ASSERT(sizeof(child_errno) == sizeof(errno), "");

    do
    {
        count = read(pipe_fd, &child_errno, sizeof(child_errno));
    }
    while (count == -1 && errno == EINTR);

    close(pipe_fd);

    if (count == -1)
    {
        /* Read failed (what to do?) */
    }
    else if (count == 0)
    {
        /* Child successfully exec-ed */
    }
    else
    {
        TR_ASSERT((size_t)count == sizeof(child_errno));

        set_system_error(error, child_errno, "Child process setup");
        return false;
    }

    return true;
}

static void update_child_pid_list(int child_pid)
{
    sigset_t block_sigchld;

    sigemptyset(&block_sigchld);
    sigaddset(&block_sigchld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_sigchld, NULL);

    if (!child_pid_list)
    {
        child_pid_list = calloc(10, sizeof(int));
        child_pid_list_len = 10;
    }

    int i;
    for (i = 0; i < child_pid_list_len; i++)
    {
        if (child_pid_list[i] == 0)
        {
            child_pid_list[i] = child_pid;
            break;
        }
    }
    if (i == child_pid_list_len)
    {
        child_pid_list = realloc((int*)child_pid_list, child_pid_list_len * 2 * sizeof(int));
        for (; i < child_pid_list_len * 2; i++)
        {
            child_pid_list[i] = 0;
        }
        child_pid_list[child_pid_list_len] = child_pid;
        child_pid_list_len = child_pid_list_len*2;
    }

    sigprocmask(SIG_UNBLOCK, &block_sigchld, NULL);
}

bool tr_spawn_async(char* const* cmd, char* const* env, char const* work_dir, tr_error** error)
{
    static bool sigchld_handler_set = false;

    if (!sigchld_handler_set)
    {
        /* FIXME: "The effects of signal() in a multithreaded process are unspecified." (c) man 2 signal */
        if (signal(SIGCHLD, &handle_sigchld) == SIG_ERR)
        {
            set_system_error(error, errno, "Call to signal()");
            return false;
        }

        sigchld_handler_set = true;
    }

    int pipe_fds[2];

    if (pipe(pipe_fds) == -1)
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

    if (child_pid > 0)
    {
        update_child_pid_list(child_pid);
    }

    if (child_pid == 0)
    {
        close(pipe_fds[0]);

        if (!tr_spawn_async_in_child(cmd, env, work_dir, pipe_fds[1]))
        {
            _exit(0);
        }
    }

    close(pipe_fds[1]);

    return tr_spawn_async_in_parent(pipe_fds[0], error);
}
