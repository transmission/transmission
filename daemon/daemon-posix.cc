// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#ifdef HAVE_SYS_SIGNALFD_H
#include <sys/signalfd.h>
#endif /* signalfd API */
#include <event2/event.h>
#include <signal.h>
#include <stdlib.h> /* abort(), daemon(), exit() */
#include <fcntl.h> /* open() */
#include <unistd.h> /* fork(), setsid(), chdir(), dup2(), close(), pipe() */

#include <string_view>

#include <fmt/format.h>

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/utils.h>

#include "daemon.h"

static dtr_callbacks const* callbacks = nullptr;
static void* callback_arg = nullptr;

static void set_system_error(tr_error** error, int code, std::string_view message)
{
    tr_error_set(error, code, fmt::format(FMT_STRING("{:s}: {:s} ({:d}"), message, tr_strerror(code), code));
}

#ifdef HAVE_SYS_SIGNALFD_H

static int sigfd = -1;

static void handle_signal(int sig)
{
    switch (sig)
    {
    case SIGHUP:
        callbacks->on_reconfigure(callback_arg);
        break;

    case SIGINT:
    case SIGTERM:
        callbacks->on_stop(callback_arg);
        break;

    default:
        assert("Unexpected signal" && 0);
    }
}

static void handle_signals(evutil_socket_t fd, short /*what*/, void* /*arg*/)
{
    struct signalfd_siginfo fdsi;

    if (read(fd, &fdsi, sizeof(fdsi)) != sizeof(fdsi))
        assert("Error reading signal descriptor" && 0);
    else
        handle_signal(fdsi.ssi_signo);
}

static bool setup_signals(void* arg)
{
    sigset_t mask = {};
    struct event* sigev = nullptr;
    struct event_base* base = static_cast<struct event_base*>(arg);

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0)
        return false;

    sigfd = signalfd(-1, &mask, 0);
    if (sigfd < 0)
        return false;

    sigev = event_new(base, sigfd, EV_READ | EV_PERSIST, handle_signals, nullptr);
    if (sigev == nullptr)
    {
        close(sigfd);
        return false;
    }

    if (event_add(sigev, nullptr) < 0)
    {
        event_del(sigev);
        close(sigfd);
        return false;
    }

    return true;
}

#else /* no signalfd API, use evsignal */

static void reconfigure(evutil_socket_t /*fd*/, short /*events*/, void* /*arg*/)
{
    callbacks->on_reconfigure(callback_arg);
}

static void stop(evutil_socket_t /*fd*/, short /*events*/, void* /*arg*/)
{
    callbacks->on_stop(callback_arg);
}

static bool setup_signal(struct event_base* base, int sig, void (*callback)(evutil_socket_t, short, void*))
{
    struct event* sigev = evsignal_new(base, sig, callback, nullptr);

    if (sigev == nullptr)
        return false;

    if (evsignal_add(sigev, nullptr) < 0)
    {
        event_free(sigev);
        return false;
    }

    return true;
}

static bool setup_signals(void* arg)
{
    struct event_base* base = static_cast<struct event_base*>(arg);

    return (setup_signal(base, SIGHUP, reconfigure) && setup_signal(base, SIGINT, stop) && setup_signal(base, SIGTERM, stop));
}

#endif /* HAVE_SYS_SIGNALFD_H */

bool dtr_daemon(dtr_callbacks const* cb, void* cb_arg, bool foreground, int* exit_code, tr_error** error)
{
    callbacks = cb;
    callback_arg = cb_arg;

    *exit_code = 1;

    if (!foreground)
    {
#if defined(HAVE_DAEMON) && !defined(__APPLE__) && !defined(__UCLIBC__)

        if (daemon(true, false) == -1)
        {
            set_system_error(error, errno, "daemon() failed");
            return false;
        }

#else

        /* this is loosely based off of glibc's daemon() implementation
         * https://sourceware.org/git/?p=glibc.git;a=blob_plain;f=misc/daemon.c */

        switch (fork())
        {
        case -1:
            set_system_error(error, errno, "fork() failed");
            return false;

        case 0:
            break;

        default:
            *exit_code = 0;
            return true;
        }

        if (setsid() == -1)
        {
            set_system_error(error, errno, "setsid() failed");
            return false;
        }

        /*
        if (chdir("/") == -1)
        {
            set_system_error(error, errno, "chdir() failed");
            return false;
        }
        */

        {
            int const fd = open("/dev/null", O_RDWR, 0);
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

#endif
    }

    *exit_code = cb->on_start(cb_arg, setup_signals, foreground);

    return true;
}
