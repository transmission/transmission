// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cassert>
#include <cerrno>
#include <csignal>
#include <string_view>

#include <fcntl.h>
#include <unistd.h> /* fork(), setsid(), chdir(), dup2(), close(), pipe() */

#ifdef HAVE_SYS_SIGNALFD_H
#include <sys/signalfd.h>
#endif /* signalfd API */

#include <event2/event.h>

#include <fmt/core.h>

#include <libtransmission/error.h>
#include <libtransmission/utils.h>

#include "daemon.h"

static void set_system_error(tr_error& error, int code, std::string_view message)
{
    error.set(code, fmt::format("{:s}: {:s} ({:d}", message, tr_strerror(code), code));
}

#ifdef HAVE_SYS_SIGNALFD_H

static void handle_signals(evutil_socket_t fd, short /*what*/, void* arg)
{
    struct signalfd_siginfo fdsi;
    auto* const daemon = static_cast<tr_daemon*>(arg);

    if (read(fd, &fdsi, sizeof(fdsi)) != sizeof(fdsi))
        assert("Error reading signal descriptor" && 0);
    else
    {
        switch (fdsi.ssi_signo)
        {
        case SIGHUP:
            daemon->reconfigure();
            break;
        case SIGINT:
        case SIGTERM:
            daemon->stop();
            break;
        default:
            assert("Unexpected signal" && 0);
        }
    }
}

bool tr_daemon::setup_signals(struct event*& sig_ev)
{
    sigset_t mask = {};
    struct event_base* base = ev_base_;

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0)
        return false;

    sigfd_ = signalfd(-1, &mask, 0);
    if (sigfd_ < 0)
        return false;

    sig_ev = event_new(base, sigfd_, EV_READ | EV_PERSIST, handle_signals, this);

    return sig_ev != nullptr && event_add(sig_ev, nullptr) >= 0;
}

#else /* no signalfd API, use evsignal */

namespace
{

static void reconfigureMarshall(evutil_socket_t /*fd*/, short /*events*/, void* arg)
{
    static_cast<tr_daemon*>(arg)->reconfigure();
}

static void stopMarshall(evutil_socket_t /*fd*/, short /*events*/, void* arg)
{
    static_cast<tr_daemon*>(arg)->stop();
}

static bool setup_signal(
    struct event_base* base,
    struct event*& sig_ev,
    int sig,
    void (*callback)(evutil_socket_t, short, void*),
    void* arg)
{
    sig_ev = evsignal_new(base, sig, callback, arg);

    return sig_ev != nullptr && evsignal_add(sig_ev, nullptr) >= 0;
}

} // anonymous namespace

bool tr_daemon::setup_signals(struct event*& sig_ev)
{
    return setup_signal(ev_base_, sig_ev, SIGHUP, reconfigureMarshall, this) &&
        setup_signal(ev_base_, sig_ev, SIGINT, stopMarshall, this) &&
        setup_signal(ev_base_, sig_ev, SIGTERM, stopMarshall, this);
}

#endif /* HAVE_SYS_SIGNALFD_H */

void tr_daemon::cleanup_signals(struct event* sig_ev) const
{
    if (sig_ev != nullptr)
    {
        event_del(sig_ev);
        event_free(sig_ev);
    }

#ifdef HAVE_SYS_SIGNALFD_H
    if (sigfd_ >= 0)
    {
        close(sigfd_);
    }
#endif /* HAVE_SYS_SIGNALFD_H */
}

bool tr_daemon::spawn(bool foreground, int* exit_code, tr_error& error)
{
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

    *exit_code = start(foreground);

    return true;
}
