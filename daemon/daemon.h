// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>

#ifdef HAVE_SYS_SIGNALFD_H
#include <unistd.h>
#endif

#include <libtransmission/variant.h>
#include <libtransmission/quark.h>
#include <libtransmission/file.h>

struct tr_error;
struct tr_session;

class tr_daemon
{
public:
    tr_daemon() = default;

    ~tr_daemon()
    {
#ifdef HAVE_SYS_SIGNALFD_H
        if (sigfd_ != -1)
        {
            close(sigfd_);
        }
#endif /* signalfd API */
    }

    bool spawn(bool foreground, int* exit_code, tr_error& error);
    bool init(int argc, char const* const argv[], bool* foreground, int* ret);
    void handle_error(tr_error const&) const;
    int start(bool foreground);
    void periodic_update();
    void reconfigure();
    void stop();

private:
#ifdef HAVE_SYS_SIGNALFD_H
    int sigfd_ = -1;
#endif /* signalfd API */
    bool paused_ = false;
    bool seen_hup_ = false;
    std::string config_dir_;
    tr_variant settings_ = {};
    bool logfile_flush_ = false;
    tr_session* my_session_ = nullptr;
    char const* log_file_name_ = nullptr;
    event_base* ev_base_ = nullptr;
    tr_sys_file_t logfile_ = TR_BAD_SYS_FILE;

    tr_quark const key_pidfile_ = tr_quark_new("pidfile");
    tr_quark const key_watch_dir_force_generic_ = tr_quark_new("watch-dir-force-generic");

    bool parse_args(int argc, char const* const* argv, bool* dump_settings, bool* foreground, int* exit_code);
    bool reopen_log_file(char const* filename);
    bool setup_signals(struct event*& sig_ev);
    void cleanup_signals(struct event* sig_ev) const;
    void report_status();

    tr_variant load_settings(char const* config_dir) const;
};
