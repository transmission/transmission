// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#ifdef HAVE_SYS_SIGNALFD_H
#include <event2/event.h>
#include <sys/signalfd.h>
#endif /* signalfd API */
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

using namespace std::literals;

/***
****
***/

static dtr_callbacks const* callbacks = nullptr;
static void* callback_arg = nullptr;

#ifdef HAVE_SYS_SIGNALFD_H
static int sigfd = -1;
#else
static int signal_pipe[2];
#endif /* signalfd API */

/***
****
***/

static void set_system_error(tr_error** error, int code, std::string_view message)
{
    tr_error_set(error, code, fmt::format(FMT_STRING("{:s}: {:s} ({:d}"), message, tr_strerror(code), code));
}

/***
****
***/

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

#ifdef HAVE_SYS_SIGNALFD_H

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

#else /* no signalfd API */

static void send_signal_to_pipe(int sig)
{
    int const old_errno = errno;

    if (write(signal_pipe[1], &sig, sizeof(sig)) == -1)
    {
        abort();
    }

    errno = old_errno;
}

static void* signal_handler_thread_main(void* /*arg*/)
{
    int sig;

    while (read(signal_pipe[0], &sig, sizeof(sig)) == sizeof(sig) && sig != 0)
    {
        handle_signal(sig);
    }

    return nullptr;
}

static bool create_signal_pipe(tr_error** error)
{
    if (pipe(signal_pipe) == -1)
    {
        set_system_error(error, errno, "pipe() failed");
        return false;
    }

    return true;
}

static void destroy_signal_pipe(void)
{
    close(signal_pipe[0]);
    close(signal_pipe[1]);
}

static bool create_signal_handler_thread(pthread_t* thread, tr_error** error)
{
    if (!create_signal_pipe(error))
    {
        return false;
    }

    if ((errno = pthread_create(thread, nullptr, &signal_handler_thread_main, nullptr)) != 0)
    {
        set_system_error(error, errno, "pthread_create() failed");
        destroy_signal_pipe();
        return false;
    }

    return true;
}

static void destroy_signal_handler_thread(pthread_t thread)
{
    send_signal_to_pipe(0);
    pthread_join(thread, nullptr);

    destroy_signal_pipe();
}

static bool setup_signal_handler(int sig, tr_error** error)
{
    assert(sig != 0);

    if (signal(sig, &send_signal_to_pipe) == SIG_ERR)
    {
        set_system_error(error, errno, "signal() failed");
        return false;
    }

    return true;
}

#endif /* signalfd API */

/***
****
***/

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

#ifndef HAVE_SYS_SIGNALFD_H
    pthread_t signal_thread;

    if (!create_signal_handler_thread(&signal_thread, error))
    {
        return false;
    }

    if (!setup_signal_handler(SIGINT, error) || !setup_signal_handler(SIGTERM, error) || !setup_signal_handler(SIGHUP, error))
    {
        destroy_signal_handler_thread(signal_thread);
        return false;
    }

    *exit_code = cb->on_start(cb_arg, nullptr, foreground);
#else
    *exit_code = cb->on_start(cb_arg, setup_signals, foreground);
#endif /* signalfd API */

#ifndef HAVE_SYS_SIGNALFD_H
    destroy_signal_handler_thread(signal_thread);
#endif /* no signalfd API */

    return true;
}
