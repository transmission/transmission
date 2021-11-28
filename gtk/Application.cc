/******************************************************************************
 * Copyright (c) Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <algorithm>
#include <locale.h>
#include <map>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h> /* exit() */
#include <string.h>
#include <thread>
#include <time.h>
#include <vector>
#include <string_view>

#include <giomm.h>
#include <glib/gmessages.h>
#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "Actions.h"
#include "Application.h"
#include "DetailsDialog.h"
#include "Dialogs.h"
#include "HigWorkarea.h"
#include "MainWindow.h"
#include "MakeDialog.h"
#include "MessageLogWindow.h"
#include "OptionsDialog.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "RelocateDialog.h"
#include "Session.h"
#include "StatsDialog.h"
#include "SystemTrayIcon.h"
#include "Utils.h"

#define MY_CONFIG_NAME "transmission"

using namespace std::literals;

#define SHOW_LICENSE

namespace
{

char const* LICENSE =
    "Copyright 2005-2020. All code is copyrighted by the respective authors.\n"
    "\n"
    "Transmission can be redistributed and/or modified under the terms of the "
    "GNU GPL versions 2 or 3 or by any future license endorsed by Mnemosyne LLC.\n"
    "\n"
    "In addition, linking to and/or using OpenSSL is allowed.\n"
    "\n"
    "This program is distributed in the hope that it will be useful, "
    "but WITHOUT ANY WARRANTY; without even the implied warranty of "
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    "\n"
    "Some of Transmission's source files have more permissive licenses. "
    "Those files may, of course, be used on their own under their own terms.\n";

}

class Application::Impl
{
public:
    Impl(Application& app, std::string const& config_dir, bool start_paused, bool is_iconified);

    void open_files(std::vector<Glib::RefPtr<Gio::File>> const& files);

    void on_startup();
    void on_activate();

    void actions_handler(Glib::ustring const& action_name);

private:
    struct counts_data
    {
        int total_count = 0;
        int queued_count = 0;
        int stopped_count = 0;
    };

private:
    void show_details_dialog_for_selected_torrents();
    void show_about_dialog();

    bool refresh_actions();
    void refresh_actions_soon();

    void on_main_window_size_allocated(Gtk::Allocation& alloc);
    bool on_main_window_focus_in(GdkEventFocus* event);

    void on_drag_data_received(
        Glib::RefPtr<Gdk::DragContext> const& drag_context,
        gint x,
        gint y,
        Gtk::SelectionData const& selection_data,
        guint info,
        guint time_);

    bool on_rpc_changed_idle(tr_rpc_callback_type type, int torrent_id);

    void placeWindowFromPrefs();
    void presentMainWindow();
    void hideMainWindow();
    void toggleMainWindow();

    bool winclose(GdkEventAny* event);
    void rowChangedCB(Gtk::TreePath const& path, Gtk::TreeModel::iterator const& iter);

    void app_setup();
    void main_window_setup();

    bool on_session_closed();
    void on_app_exit();

    void show_torrent_errors(Glib::ustring const& primary, std::vector<std::string>& files);
    void flush_torrent_errors();

    bool update_model_once();
    void update_model_soon();
    bool update_model_loop();

    void on_core_busy(bool busy);
    void on_core_error(Session::ErrorCode code, Glib::ustring const& msg);
    void on_add_torrent(tr_ctor* ctor);
    void on_prefs_changed(tr_quark key);

    std::vector<int> get_selected_torrent_ids() const;
    tr_torrent* get_first_selected_torrent() const;
    counts_data get_selected_torrent_counts() const;

    void start_all_torrents();
    void pause_all_torrents();
    void copy_magnet_link_to_clipboard(tr_torrent* tor) const;
    bool call_rpc_for_selected_torrents(std::string const& method);
    void remove_selected(bool delete_files);

    static tr_rpc_callback_status on_rpc_changed(tr_session* session, tr_rpc_callback_type type, tr_torrent* tor, void* gdata);

private:
    Application& app_;

    std::string config_dir_;
    bool start_paused_ = false;
    bool is_iconified_ = false;
    bool is_closing_ = false;

    Glib::RefPtr<Gtk::Builder> ui_builder_;

    unsigned int activation_count_ = 0;
    sigc::connection timer_;
    sigc::connection update_model_soon_tag_;
    sigc::connection refresh_actions_tag_;
    std::unique_ptr<SystemTrayIcon> icon_;
    std::unique_ptr<MainWindow> wind_;
    Glib::RefPtr<Session> core_;
    std::unique_ptr<MessageLogWindow> msgwin_;
    std::unique_ptr<PrefsDialog> prefs_;
    std::vector<std::string> error_list_;
    std::vector<std::string> duplicates_list_;
    std::map<std::string, std::unique_ptr<DetailsDialog>> details_;
    Glib::RefPtr<Gtk::TreeSelection> sel_;
};

namespace
{

template<typename T>
void gtr_window_present(T const& window)
{
    window->present(gtk_get_current_event_time());
}

/***
****
****  DETAILS DIALOGS MANAGEMENT
****
***/

std::string get_details_dialog_key(std::vector<int> const& id_list)
{
    auto tmp = id_list;
    std::sort(tmp.begin(), tmp.end());

    std::ostringstream gstr;

    for (auto const id : tmp)
    {
        gstr << id << ' ';
    }

    return gstr.str();
}

} // namespace

std::vector<int> Application::Impl::get_selected_torrent_ids() const
{
    std::vector<int> ids;
    sel_->selected_foreach([&ids](auto const& /*path*/, auto const& iter)
                           { ids.push_back(iter->get_value(torrent_cols.torrent_id)); });
    return ids;
}

void Application::Impl::show_details_dialog_for_selected_torrents()
{
    auto const ids = get_selected_torrent_ids();
    auto const key = get_details_dialog_key(ids);

    auto dialog_it = details_.find(key);

    if (dialog_it == details_.end())
    {
        auto dialog = DetailsDialog::create(*wind_, core_);
        dialog->set_torrents(ids);
        dialog->signal_hide().connect([this, key]() { details_.erase(key); });
        dialog_it = details_.emplace(key, std::move(dialog)).first;
        dialog_it->second->show();
    }

    gtr_window_present(dialog_it->second);
}

/****
*****
*****  ON SELECTION CHANGED
*****
****/

Application::Impl::counts_data Application::Impl::get_selected_torrent_counts() const
{
    counts_data counts;

    sel_->selected_foreach(
        [&counts](auto const& /*path*/, auto const& iter)
        {
            ++counts.total_count;

            auto const activity = iter->get_value(torrent_cols.activity);

            if (activity == TR_STATUS_DOWNLOAD_WAIT || activity == TR_STATUS_SEED_WAIT)
            {
                ++counts.queued_count;
            }

            if (activity == TR_STATUS_STOPPED)
            {
                ++counts.stopped_count;
            }
        });

    return counts;
}

bool Application::Impl::refresh_actions()
{
    if (!is_closing_)
    {
        size_t const total = core_->get_torrent_count();
        size_t const active = core_->get_active_torrent_count();
        auto const torrent_count = core_->get_model()->children().size();
        bool has_selection;

        auto const sel_counts = get_selected_torrent_counts();
        has_selection = sel_counts.total_count > 0;

        gtr_action_set_sensitive("select-all", torrent_count != 0);
        gtr_action_set_sensitive("deselect-all", torrent_count != 0);
        gtr_action_set_sensitive("pause-all-torrents", active != 0);
        gtr_action_set_sensitive("start-all-torrents", active != total);

        gtr_action_set_sensitive("torrent-stop", (sel_counts.stopped_count < sel_counts.total_count));
        gtr_action_set_sensitive("torrent-start", (sel_counts.stopped_count) > 0);
        gtr_action_set_sensitive("torrent-start-now", (sel_counts.stopped_count + sel_counts.queued_count) > 0);
        gtr_action_set_sensitive("torrent-verify", has_selection);
        gtr_action_set_sensitive("remove-torrent", has_selection);
        gtr_action_set_sensitive("delete-torrent", has_selection);
        gtr_action_set_sensitive("relocate-torrent", has_selection);
        gtr_action_set_sensitive("queue-move-top", has_selection);
        gtr_action_set_sensitive("queue-move-up", has_selection);
        gtr_action_set_sensitive("queue-move-down", has_selection);
        gtr_action_set_sensitive("queue-move-bottom", has_selection);
        gtr_action_set_sensitive("show-torrent-properties", has_selection);
        gtr_action_set_sensitive("open-torrent-folder", sel_counts.total_count == 1);
        gtr_action_set_sensitive("copy-magnet-link-to-clipboard", sel_counts.total_count == 1);

        bool canUpdate = false;
        sel_->selected_foreach(
            [&canUpdate](auto const& /*path*/, auto const& iter)
            {
                auto* tor = static_cast<tr_torrent*>(iter->get_value(torrent_cols.torrent));
                canUpdate = canUpdate || tr_torrentCanManualUpdate(tor);
            });
        gtr_action_set_sensitive("torrent-reannounce", canUpdate);
    }

    refresh_actions_tag_.disconnect();
    return false;
}

void Application::Impl::refresh_actions_soon()
{
    if (!is_closing_ && !refresh_actions_tag_.connected())
    {
        refresh_actions_tag_ = Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::refresh_actions));
    }
}

/***
****
***/

namespace
{

bool has_magnet_link_handler()
{
    return bool{ Gio::AppInfo::get_default_for_uri_scheme("magnet") };
}

void register_magnet_link_handler()
{
    std::string const content_type = "x-scheme-handler/magnet";

    try
    {
        auto const app = Gio::AppInfo::create_from_commandline(
            "transmission-gtk",
            "transmission-gtk",
            Gio::APP_INFO_CREATE_SUPPORTS_URIS);
        app->set_as_default_for_type(content_type);
    }
    catch (Gio::Error const& e)
    {
        g_warning(_("Error registering Transmission as a %s handler: %s"), content_type.c_str(), e.what().c_str());
    }
}

void ensure_magnet_handler_exists()
{
    if (!has_magnet_link_handler())
    {
        register_magnet_link_handler();
    }
}

} // namespace

void Application::Impl::on_main_window_size_allocated(Gtk::Allocation& /*alloc*/)
{
    auto const gdk_window = wind_->get_window();
    bool const is_maximized = gdk_window != nullptr && (gdk_window->get_state() & Gdk::WINDOW_STATE_MAXIMIZED) != 0;

    gtr_pref_int_set(TR_KEY_main_window_is_maximized, is_maximized);

    if (!is_maximized)
    {
        int x;
        int y;
        int w;
        int h;
        wind_->get_position(x, y);
        wind_->get_size(w, h);
        gtr_pref_int_set(TR_KEY_main_window_x, x);
        gtr_pref_int_set(TR_KEY_main_window_y, y);
        gtr_pref_int_set(TR_KEY_main_window_width, w);
        gtr_pref_int_set(TR_KEY_main_window_height, h);
    }
}

/***
**** listen to changes that come from RPC
***/

bool Application::Impl::on_rpc_changed_idle(tr_rpc_callback_type type, int torrent_id)
{
    switch (type)
    {
    case TR_RPC_SESSION_CLOSE:
        gtr_action_activate("quit");
        break;

    case TR_RPC_TORRENT_ADDED:
        if (auto* tor = core_->find_torrent(torrent_id); tor != nullptr)
        {
            core_->add_torrent(tor, true);
        }

        break;

    case TR_RPC_TORRENT_REMOVING:
        core_->remove_torrent(torrent_id, false);
        break;

    case TR_RPC_TORRENT_TRASHING:
        core_->remove_torrent(torrent_id, true);
        break;

    case TR_RPC_SESSION_CHANGED:
        {
            tr_variant tmp;
            tr_variant* newval;
            tr_variant* oldvals = gtr_pref_get_all();
            tr_quark key;
            std::vector<tr_quark> changed_keys;
            auto* session = core_->get_session();
            tr_variantInitDict(&tmp, 100);
            tr_sessionGetSettings(session, &tmp);

            for (int i = 0; tr_variantDictChild(&tmp, i, &key, &newval); ++i)
            {
                bool changed;
                tr_variant const* oldval = tr_variantDictFind(oldvals, key);

                if (oldval == nullptr)
                {
                    changed = true;
                }
                else
                {
                    char* a = tr_variantToStr(oldval, TR_VARIANT_FMT_BENC, nullptr);
                    char* b = tr_variantToStr(newval, TR_VARIANT_FMT_BENC, nullptr);
                    changed = g_strcmp0(a, b) != 0;
                    tr_free(b);
                    tr_free(a);
                }

                if (changed)
                {
                    changed_keys.push_back(key);
                }
            }

            tr_sessionGetSettings(session, oldvals);

            for (auto const changed_key : changed_keys)
            {
                core_->signal_prefs_changed().emit(changed_key);
            }

            tr_variantFree(&tmp);
            break;
        }

    case TR_RPC_TORRENT_CHANGED:
    case TR_RPC_TORRENT_MOVED:
    case TR_RPC_TORRENT_STARTED:
    case TR_RPC_TORRENT_STOPPED:
    case TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED:
        /* nothing interesting to do here */
        break;
    }

    return false;
}

tr_rpc_callback_status Application::Impl::on_rpc_changed(
    tr_session* /*session*/,
    tr_rpc_callback_type type,
    tr_torrent* tor,
    void* gdata)
{
    auto* impl = static_cast<Impl*>(gdata);
    auto const torrent_id = tr_torrentId(tor);

    Glib::signal_idle().connect([impl, type, torrent_id]() { return impl->on_rpc_changed_idle(type, torrent_id); });

    return TR_RPC_NOREMOVE;
}

/***
****  signal handling
***/

namespace
{

sig_atomic_t global_sigcount = 0;
void* sighandler_cbdata = nullptr;

void signal_handler(int sig)
{
    if (++global_sigcount > 1)
    {
        signal(sig, SIG_DFL);
        raise(sig);
    }
    else if (sig == SIGINT || sig == SIGTERM)
    {
        g_message(_("Got signal %d; trying to shut down cleanly. Do it again if it gets stuck."), sig);
        gtr_actions_handler("quit", sighandler_cbdata);
    }
}

} // namespace

/****
*****
*****
****/

void Application::on_startup()
{
    Gtk::Application::on_startup();

    impl_->on_startup();
}

void Application::Impl::on_startup()
{
    tr_session* session;

    ::signal(SIGINT, signal_handler);
    ::signal(SIGTERM, signal_handler);

    sighandler_cbdata = this;

    /* ensure the directories are created */
    if (auto const str = gtr_pref_string_get(TR_KEY_download_dir); !str.empty())
    {
        g_mkdir_with_parents(str.c_str(), 0777);
    }

    if (auto const str = gtr_pref_string_get(TR_KEY_incomplete_dir); !str.empty())
    {
        g_mkdir_with_parents(str.c_str(), 0777);
    }

    /* initialize the libtransmission session */
    session = tr_sessionInit(config_dir_.c_str(), true, gtr_pref_get_all());

    gtr_pref_flag_set(TR_KEY_alt_speed_enabled, tr_sessionUsesAltSpeed(session));
    gtr_pref_int_set(TR_KEY_peer_port, tr_sessionGetPeerPort(session));
    core_ = Session::create(session);

    /* init the ui manager */
    ui_builder_ = Gtk::Builder::create_from_resource(TR_RESOURCE_PATH "transmission-ui.xml");
    auto const actions = gtr_actions_init(ui_builder_, this);

    app_.set_menubar(gtr_action_get_object<Gio::Menu>("main-window-menu"));

    /* create main window now to be a parent to any error dialogs */
    wind_ = MainWindow::create(app_, actions, core_);
    wind_->signal_size_allocate().connect(sigc::mem_fun(*this, &Impl::on_main_window_size_allocated));
    app_.hold();
    app_setup();
    tr_sessionSetRPCCallback(session, &Impl::on_rpc_changed, this);

    /* check & see if it's time to update the blocklist */
    if (gtr_pref_flag_get(TR_KEY_blocklist_enabled) && gtr_pref_flag_get(TR_KEY_blocklist_updates_enabled))
    {
        int64_t const last_time = gtr_pref_int_get(TR_KEY_blocklist_date);
        int const SECONDS_IN_A_WEEK = 7 * 24 * 60 * 60;
        time_t const now = time(nullptr);

        if (last_time + SECONDS_IN_A_WEEK < now)
        {
            core_->blocklist_update();
        }
    }

    /* if there's no magnet link handler registered, register us */
    ensure_magnet_handler_exists();
}

void Application::on_activate()
{
    Gtk::Application::on_activate();

    impl_->on_activate();
}

void Application::Impl::on_activate()
{
    activation_count_++;

    /* GApplication emits an 'activate' signal when bootstrapping the primary.
     * Ordinarily we handle that by presenting the main window, but if the user
     * user started Transmission minimized, ignore that initial signal... */
    if (is_iconified_ && activation_count_ == 1)
    {
        return;
    }

    gtr_action_activate("present-main-window");
}

void Application::Impl::open_files(std::vector<Glib::RefPtr<Gio::File>> const& files)
{
    bool const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents) && !start_paused_;
    bool const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);
    bool const do_notify = true;

    core_->add_files(files, do_start, do_prompt, do_notify);
}

void Application::on_open(std::vector<Glib::RefPtr<Gio::File>> const& files, Glib::ustring const& hint)
{
    Gtk::Application::on_open(files, hint);

    impl_->open_files(files);
}

namespace
{

std::string get_application_id(std::string const& config_dir)
{
    struct stat sb;
    ::stat(config_dir.c_str(), &sb);

    std::ostringstream id;
    id << "com.transmissionbt.transmission_" << sb.st_dev << '_' << sb.st_ino;

    return id.str();
}

} // namespace

Application::Application(std::string const& config_dir, bool start_paused, bool is_iconified)
    : Gtk::Application(get_application_id(config_dir), Gio::APPLICATION_HANDLES_OPEN)
    , impl_(std::make_unique<Impl>(*this, config_dir, start_paused, is_iconified))
{
}

Application::~Application() = default;

Application::Impl::Impl(Application& app, std::string const& config_dir, bool start_paused, bool is_iconified)
    : app_(app)
    , config_dir_(config_dir)
    , start_paused_(start_paused)
    , is_iconified_(is_iconified)
{
}

void Application::Impl::on_core_busy(bool busy)
{
    wind_->set_busy(busy);
}

void Application::Impl::app_setup()
{
    if (is_iconified_)
    {
        gtr_pref_flag_set(TR_KEY_show_notification_area_icon, true);
    }

    gtr_actions_set_core(core_);

    /* set up core handlers */
    core_->signal_busy().connect(sigc::mem_fun(*this, &Impl::on_core_busy));
    core_->signal_add_error().connect(sigc::mem_fun(*this, &Impl::on_core_error));
    core_->signal_add_prompt().connect(sigc::mem_fun(*this, &Impl::on_add_torrent));
    core_->signal_prefs_changed().connect(sigc::mem_fun(*this, &Impl::on_prefs_changed));

    /* add torrents from command-line and saved state */
    core_->load(start_paused_);
    core_->torrents_added();

    /* set up main window */
    main_window_setup();

    /* set up the icon */
    on_prefs_changed(TR_KEY_show_notification_area_icon);

    /* start model update timer */
    timer_ = Glib::signal_timeout().connect_seconds(
        sigc::mem_fun(*this, &Impl::update_model_loop),
        MAIN_WINDOW_REFRESH_INTERVAL_SECONDS);
    update_model_once();

    /* either show the window or iconify it */
    if (!is_iconified_)
    {
        wind_->show();
        gtr_action_set_toggled("toggle-main-window", true);
    }
    else
    {
        wind_->set_skip_taskbar_hint(icon_ != nullptr);
        is_iconified_ = false; // ensure that the next toggle iconifies
        gtr_action_set_toggled("toggle-main-window", false);
    }

    if (!gtr_pref_flag_get(TR_KEY_user_has_given_informed_consent))
    {
        Gtk::MessageDialog w(
            *wind_,
            _("Transmission is a file sharing program. When you run a torrent, its data will be "
              "made available to others by means of upload. Any content you share is your sole responsibility."),
            false,
            Gtk::MESSAGE_OTHER,
            Gtk::BUTTONS_NONE,
            true);
        w.add_button(_("_Cancel"), Gtk::RESPONSE_REJECT);
        w.add_button(_("I _Agree"), Gtk::RESPONSE_ACCEPT);
        w.set_default_response(Gtk::RESPONSE_ACCEPT);

        switch (w.run())
        {
        case Gtk::RESPONSE_ACCEPT:
            /* only show it once */
            gtr_pref_flag_set(TR_KEY_user_has_given_informed_consent, true);
            break;

        default:
            exit(0);
        }
    }
}

void Application::Impl::placeWindowFromPrefs()
{
    wind_->resize((int)gtr_pref_int_get(TR_KEY_main_window_width), (int)gtr_pref_int_get(TR_KEY_main_window_height));
    wind_->move((int)gtr_pref_int_get(TR_KEY_main_window_x), (int)gtr_pref_int_get(TR_KEY_main_window_y));
}

void Application::Impl::presentMainWindow()
{
    gtr_action_set_toggled("toggle-main-window", true);

    if (is_iconified_)
    {
        is_iconified_ = false;

        wind_->set_skip_taskbar_hint(false);
    }

    if (!wind_->get_visible())
    {
        placeWindowFromPrefs();
        gtr_widget_set_visible(*wind_, true);
    }

    gtr_window_present(wind_);
    wind_->get_window()->raise();
}

void Application::Impl::hideMainWindow()
{
    gtr_action_set_toggled("toggle-main-window", false);

    wind_->set_skip_taskbar_hint(true);
    gtr_widget_set_visible(*wind_, false);
    is_iconified_ = true;
}

void Application::Impl::toggleMainWindow()
{
    if (is_iconified_)
    {
        presentMainWindow();
    }
    else
    {
        hideMainWindow();
    }
}

bool Application::Impl::winclose(GdkEventAny* /*event*/)
{
    if (icon_ != nullptr)
    {
        gtr_action_activate("toggle-main-window");
    }
    else
    {
        on_app_exit();
    }

    return true; /* don't propagate event further */
}

void Application::Impl::rowChangedCB(Gtk::TreePath const& path, Gtk::TreeModel::iterator const& /*iter*/)
{
    if (sel_->is_selected(path))
    {
        refresh_actions_soon();
    }
}

void Application::Impl::on_drag_data_received(
    Glib::RefPtr<Gdk::DragContext> const& drag_context,
    gint /*x*/,
    gint /*y*/,
    Gtk::SelectionData const& selection_data,
    guint /*info*/,
    guint time_)
{
    if (auto const uris = selection_data.get_uris(); !uris.empty())
    {
        auto files = std::vector<Glib::RefPtr<Gio::File>>();
        files.reserve(uris.size());
        std::transform(uris.begin(), uris.end(), std::back_inserter(files), &Gio::File::create_for_uri);

        open_files(files);
    }
    else
    {
        auto const text = gtr_str_strip(selection_data.get_text());

        if (!text.empty())
        {
            core_->add_from_url(text);
        }
    }

    drag_context->drag_finish(true, false, time_);
}

void Application::Impl::main_window_setup()
{
    // g_assert(nullptr == cbdata->wind);
    // cbdata->wind = wind;
    sel_ = wind_->get_selection();

    sel_->signal_changed().connect(sigc::mem_fun(*this, &Impl::refresh_actions_soon));
    refresh_actions_soon();
    auto const model = core_->get_model();
    model->signal_row_changed().connect(sigc::mem_fun(*this, &Impl::rowChangedCB));
    wind_->signal_delete_event().connect(sigc::mem_fun(*this, &Impl::winclose));
    refresh_actions();

    /* register to handle URIs that get dragged onto our main window */
    wind_->drag_dest_set(Gtk::DEST_DEFAULT_ALL, Gdk::ACTION_COPY);
    wind_->drag_dest_add_uri_targets();
    wind_->drag_dest_add_text_targets(); /* links dragged from browsers are text */
    wind_->signal_drag_data_received().connect(sigc::mem_fun(*this, &Impl::on_drag_data_received));
}

bool Application::Impl::on_session_closed()
{
    details_.clear();

    prefs_.reset();
    wind_.reset();

    core_.reset();

    icon_.reset();

    error_list_.clear();
    duplicates_list_.clear();

    app_.release();
    return false;
}

void Application::Impl::on_app_exit()
{
    if (is_closing_)
    {
        return;
    }

    is_closing_ = true;

    /* stop the update timer */
    timer_.disconnect();

    /* stop the refresh-actions timer */
    refresh_actions_tag_.disconnect();

    auto* c = static_cast<Gtk::Container*>(wind_.get());
    c->remove(*static_cast<Gtk::Bin*>(c)->get_child());

    auto* p = Gtk::make_managed<Gtk::Grid>();
    p->set_column_spacing(GUI_PAD_BIG);
    p->set_halign(Gtk::ALIGN_CENTER);
    p->set_valign(Gtk::ALIGN_CENTER);
    c->add(*p);

    auto* icon = Gtk::make_managed<Gtk::Image>("network-workgroup", Gtk::ICON_SIZE_DIALOG);
    p->attach(*icon, 0, 0, 1, 2);

    auto* top_label = Gtk::make_managed<Gtk::Label>();
    top_label->set_markup(_("<b>Closing Connections</b>"));
    top_label->set_halign(Gtk::ALIGN_START);
    top_label->set_valign(Gtk::ALIGN_CENTER);
    p->attach(*top_label, 1, 0, 1, 1);

    auto* bottom_label = Gtk::make_managed<Gtk::Label>(_("Sending upload/download totals to tracker…"));
    bottom_label->set_halign(Gtk::ALIGN_START);
    bottom_label->set_valign(Gtk::ALIGN_CENTER);
    p->attach(*bottom_label, 1, 1, 1, 1);

    auto* button = Gtk::make_managed<Gtk::Button>(_("_Quit Now"), true);
    button->set_margin_top(GUI_PAD);
    button->set_halign(Gtk::ALIGN_START);
    button->set_valign(Gtk::ALIGN_END);
    button->signal_clicked().connect([]() { ::exit(0); });
    p->attach(*button, 1, 2, 1, 1);

    p->show_all();
    button->grab_focus();

    /* clear the UI */
    core_->clear();

    /* ensure the window is in its previous position & size.
     * this seems to be necessary because changing the main window's
     * child seems to unset the size */
    placeWindowFromPrefs();

    /* shut down libT */
    /* since tr_sessionClose () is a blocking function,
     * delegate its call to another thread here... when it's done,
     * punt the GUI teardown back to the GTK+ thread */
    std::thread(
        [this, session = core_->close()]()
        {
            tr_sessionClose(session);
            Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::on_session_closed));
        })
        .detach();
}

void Application::Impl::show_torrent_errors(Glib::ustring const& primary, std::vector<std::string>& files)
{
    std::ostringstream s;
    auto const leader = files.size() > 1 ? gtr_get_unicode_string(GTR_UNICODE_BULLET) : "";

    for (auto const& f : files)
    {
        s << leader << ' ' << f << '\n';
    }

    Gtk::MessageDialog w(*wind_, primary, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_CLOSE);
    w.set_secondary_text(s.str());
    w.run();

    files.clear();
}

void Application::Impl::flush_torrent_errors()
{
    if (!error_list_.empty())
    {
        show_torrent_errors(
            ngettext("Couldn't add corrupt torrent", "Couldn't add corrupt torrents", error_list_.size()),
            error_list_);
    }

    if (!duplicates_list_.empty())
    {
        show_torrent_errors(
            ngettext("Couldn't add duplicate torrent", "Couldn't add duplicate torrents", duplicates_list_.size()),
            duplicates_list_);
    }
}

void Application::Impl::on_core_error(Session::ErrorCode code, Glib::ustring const& msg)
{
    switch (code)
    {
    case Session::ERR_ADD_TORRENT_ERR:
        error_list_.push_back(Glib::path_get_basename(msg));
        break;

    case Session::ERR_ADD_TORRENT_DUP:
        duplicates_list_.push_back(msg);
        break;

    case Session::ERR_NO_MORE_TORRENTS:
        flush_torrent_errors();
        break;

    default:
        g_assert_not_reached();
        break;
    }
}

bool Application::Impl::on_main_window_focus_in(GdkEventFocus* /*event*/)
{
    if (wind_ != nullptr)
    {
        wind_->set_urgency_hint(false);
    }

    return false;
}

void Application::Impl::on_add_torrent(tr_ctor* ctor)
{
    auto w = std::shared_ptr<OptionsDialog>(
        OptionsDialog::create(*wind_, core_, std::unique_ptr<tr_ctor, decltype(&tr_ctorFree)>(ctor, &tr_ctorFree)));

    w->signal_hide().connect([w]() mutable { w.reset(); });
    w->signal_focus_in_event().connect(sigc::mem_fun(*this, &Impl::on_main_window_focus_in));

    if (wind_ != nullptr)
    {
        wind_->set_urgency_hint(true);
    }

    w->show();
}

void Application::Impl::on_prefs_changed(tr_quark const key)
{
    auto* tr = core_->get_session();

    switch (key)
    {
    case TR_KEY_encryption:
        tr_sessionSetEncryption(tr, static_cast<tr_encryption_mode>(gtr_pref_int_get(key)));
        break;

    case TR_KEY_download_dir:
        tr_sessionSetDownloadDir(tr, gtr_pref_string_get(key).c_str());
        break;

    case TR_KEY_message_level:
        tr_logSetLevel(static_cast<tr_log_level>(gtr_pref_int_get(key)));
        break;

    case TR_KEY_peer_port:
        tr_sessionSetPeerPort(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_blocklist_enabled:
        tr_blocklistSetEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_blocklist_url:
        tr_blocklistSetURL(tr, gtr_pref_string_get(key).c_str());
        break;

    case TR_KEY_show_notification_area_icon:
        {
            bool const show = gtr_pref_flag_get(key);

            if (show && icon_ == nullptr)
            {
                icon_ = std::make_unique<SystemTrayIcon>(*wind_, core_);
            }
            else if (!show && icon_ != nullptr)
            {
                icon_.reset();
            }

            break;
        }

    case TR_KEY_speed_limit_down_enabled:
        tr_sessionLimitSpeed(tr, TR_DOWN, gtr_pref_flag_get(key));
        break;

    case TR_KEY_speed_limit_down:
        tr_sessionSetSpeedLimit_KBps(tr, TR_DOWN, gtr_pref_int_get(key));
        break;

    case TR_KEY_speed_limit_up_enabled:
        tr_sessionLimitSpeed(tr, TR_UP, gtr_pref_flag_get(key));
        break;

    case TR_KEY_speed_limit_up:
        tr_sessionSetSpeedLimit_KBps(tr, TR_UP, gtr_pref_int_get(key));
        break;

    case TR_KEY_ratio_limit_enabled:
        tr_sessionSetRatioLimited(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_ratio_limit:
        tr_sessionSetRatioLimit(tr, gtr_pref_double_get(key));
        break;

    case TR_KEY_idle_seeding_limit:
        tr_sessionSetIdleLimit(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_idle_seeding_limit_enabled:
        tr_sessionSetIdleLimited(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_port_forwarding_enabled:
        tr_sessionSetPortForwardingEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_pex_enabled:
        tr_sessionSetPexEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_rename_partial_files:
        tr_sessionSetIncompleteFileNamingEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_download_queue_size:
        tr_sessionSetQueueSize(tr, TR_DOWN, gtr_pref_int_get(key));
        break;

    case TR_KEY_queue_stalled_minutes:
        tr_sessionSetQueueStalledMinutes(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_dht_enabled:
        tr_sessionSetDHTEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_utp_enabled:
        tr_sessionSetUTPEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_lpd_enabled:
        tr_sessionSetLPDEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_rpc_port:
        tr_sessionSetRPCPort(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_rpc_enabled:
        tr_sessionSetRPCEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_rpc_whitelist:
        tr_sessionSetRPCWhitelist(tr, gtr_pref_string_get(key).c_str());
        break;

    case TR_KEY_rpc_whitelist_enabled:
        tr_sessionSetRPCWhitelistEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_rpc_username:
        tr_sessionSetRPCUsername(tr, gtr_pref_string_get(key).c_str());
        break;

    case TR_KEY_rpc_password:
        tr_sessionSetRPCPassword(tr, gtr_pref_string_get(key).c_str());
        break;

    case TR_KEY_rpc_authentication_required:
        tr_sessionSetRPCPasswordEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_alt_speed_up:
        tr_sessionSetAltSpeed_KBps(tr, TR_UP, gtr_pref_int_get(key));
        break;

    case TR_KEY_alt_speed_down:
        tr_sessionSetAltSpeed_KBps(tr, TR_DOWN, gtr_pref_int_get(key));
        break;

    case TR_KEY_alt_speed_enabled:
        {
            bool const b = gtr_pref_flag_get(key);
            tr_sessionUseAltSpeed(tr, b);
            gtr_action_set_toggled(tr_quark_get_string(key), b);
            break;
        }

    case TR_KEY_alt_speed_time_begin:
        tr_sessionSetAltSpeedBegin(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_alt_speed_time_end:
        tr_sessionSetAltSpeedEnd(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_alt_speed_time_enabled:
        tr_sessionUseAltSpeedTime(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_alt_speed_time_day:
        tr_sessionSetAltSpeedDay(tr, static_cast<tr_sched_day>(gtr_pref_int_get(key)));
        break;

    case TR_KEY_peer_port_random_on_start:
        tr_sessionSetPeerPortRandomOnStart(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_incomplete_dir:
        tr_sessionSetIncompleteDir(tr, gtr_pref_string_get(key).c_str());
        break;

    case TR_KEY_incomplete_dir_enabled:
        tr_sessionSetIncompleteDirEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_script_torrent_done_enabled:
        tr_sessionSetScriptEnabled(tr, TR_SCRIPT_ON_TORRENT_DONE, gtr_pref_flag_get(key));
        break;

    case TR_KEY_script_torrent_done_filename:
        tr_sessionSetScript(tr, TR_SCRIPT_ON_TORRENT_DONE, gtr_pref_string_get(key).c_str());
        break;

    case TR_KEY_start_added_torrents:
        tr_sessionSetPaused(tr, !gtr_pref_flag_get(key));
        break;

    case TR_KEY_trash_original_torrent_files:
        tr_sessionSetDeleteSource(tr, gtr_pref_flag_get(key));
        break;

    default:
        break;
    }
}

bool Application::Impl::update_model_once()
{
    /* update the torrent data in the model */
    core_->update();

    /* refresh the main window's statusbar and toolbar buttons */
    if (wind_ != nullptr)
    {
        wind_->refresh();
    }

    /* update the actions */
    refresh_actions();

    /* update the status tray icon */
    if (icon_ != nullptr)
    {
        icon_->refresh();
    }

    update_model_soon_tag_.disconnect();
    return false;
}

void Application::Impl::update_model_soon()
{
    if (!update_model_soon_tag_.connected())
    {
        update_model_soon_tag_ = Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::update_model_once));
    }
}

bool Application::Impl::update_model_loop()
{
    bool const done = global_sigcount != 0;

    if (!done)
    {
        update_model_once();
    }

    return !done;
}

void Application::Impl::show_about_dialog()
{
    auto const uri = Glib::ustring("https://transmissionbt.com/");
    auto const authors = std::vector<Glib::ustring>({
        "Charles Kerr (Backend; GTK+)",
        "Mitchell Livingston (Backend; OS X)",
        "Mike Gelfand",
    });

    Gtk::AboutDialog d;
    d.set_authors(authors);
    d.set_comments(_("A fast and easy BitTorrent client"));
    d.set_copyright(_("Copyright (c) The Transmission Project"));
    d.set_logo_icon_name(MY_CONFIG_NAME);
    d.set_name(Glib::get_application_name());
    /* Translators: translate "translator-credits" as your name
       to have it appear in the credits in the "About"
       dialog */
    d.set_translator_credits(_("translator-credits"));
    d.set_version(LONG_VERSION_STRING);
    d.set_website(uri);
    d.set_website_label(uri);
#ifdef SHOW_LICENSE
    d.set_license(LICENSE);
    d.set_wrap_license(true);
#endif
    d.set_transient_for(*wind_);
    d.run();
}

bool Application::Impl::call_rpc_for_selected_torrents(std::string const& method)
{
    tr_variant top;
    tr_variant* args;
    tr_variant* ids;
    bool invoked = false;
    auto* session = core_->get_session();

    tr_variantInitDict(&top, 2);
    tr_variantDictAddStrView(&top, TR_KEY_method, method);
    args = tr_variantDictAddDict(&top, TR_KEY_arguments, 1);
    ids = tr_variantDictAddList(args, TR_KEY_ids, 0);
    sel_->selected_foreach(
        [ids](auto const& /*path*/, auto const& iter)
        {
            auto* tor = static_cast<tr_torrent*>(iter->get_value(torrent_cols.torrent));
            tr_variantListAddInt(ids, tr_torrentId(tor));
        });

    if (tr_variantListSize(ids) != 0)
    {
        tr_rpc_request_exec_json(session, &top, nullptr, nullptr);
        invoked = true;
    }

    tr_variantFree(&top);
    return invoked;
}

void Application::Impl::remove_selected(bool delete_files)
{
    std::vector<int> l;

    sel_->selected_foreach([&l](auto const& /*path*/, auto const& iter)
                           { l.push_back(iter->get_value(torrent_cols.torrent_id)); });

    if (!l.empty())
    {
        gtr_confirm_remove(*wind_, core_, l, delete_files);
    }
}

void Application::Impl::start_all_torrents()
{
    auto* session = core_->get_session();
    tr_variant request;

    tr_variantInitDict(&request, 1);
    tr_variantDictAddStrView(&request, TR_KEY_method, "torrent-start"sv);
    tr_rpc_request_exec_json(session, &request, nullptr, nullptr);
    tr_variantFree(&request);
}

void Application::Impl::pause_all_torrents()
{
    auto* session = core_->get_session();
    tr_variant request;

    tr_variantInitDict(&request, 1);
    tr_variantDictAddStrView(&request, TR_KEY_method, "torrent-stop"sv);
    tr_rpc_request_exec_json(session, &request, nullptr, nullptr);
    tr_variantFree(&request);
}

tr_torrent* Application::Impl::get_first_selected_torrent() const
{
    tr_torrent* tor = nullptr;
    Glib::RefPtr<Gtk::TreeModel> m;
    auto const l = sel_->get_selected_rows(m);

    if (!l.empty())
    {
        if (auto iter = m->get_iter(l.front()); iter)
        {
            tor = static_cast<tr_torrent*>(iter->get_value(torrent_cols.torrent));
        }
    }

    return tor;
}

void Application::Impl::copy_magnet_link_to_clipboard(tr_torrent* tor) const
{
    char* magnet = tr_torrentGetMagnetLink(tor);
    auto const display = wind_->get_display();
    GdkAtom selection;
    Glib::RefPtr<Gtk::Clipboard> clipboard;

    /* this is The Right Thing for copy/paste... */
    selection = GDK_SELECTION_CLIPBOARD;
    clipboard = Gtk::Clipboard::get_for_display(display, selection);
    clipboard->set_text(magnet);

    /* ...but people using plain ol' X need this instead */
    selection = GDK_SELECTION_PRIMARY;
    clipboard = Gtk::Clipboard::get_for_display(display, selection);
    clipboard->set_text(magnet);

    /* cleanup */
    tr_free(magnet);
}

void gtr_actions_handler(Glib::ustring const& action_name, void* user_data)
{
    static_cast<Application::Impl*>(user_data)->actions_handler(action_name);
}

void Application::Impl::actions_handler(Glib::ustring const& action_name)
{
    bool changed = false;

    if (action_name == "open-torrent-from-url")
    {
        auto w = std::shared_ptr<TorrentUrlChooserDialog>(TorrentUrlChooserDialog::create(*wind_, core_));
        w->signal_hide().connect([w]() mutable { w.reset(); });
        w->show();
    }
    else if (action_name == "open-torrent")
    {
        auto w = std::shared_ptr<TorrentFileChooserDialog>(TorrentFileChooserDialog::create(*wind_, core_));
        w->signal_hide().connect([w]() mutable { w.reset(); });
        w->show();
    }
    else if (action_name == "show-stats")
    {
        auto dialog = std::shared_ptr<StatsDialog>(StatsDialog::create(*wind_, core_));
        dialog->signal_hide().connect([dialog]() mutable { dialog.reset(); });
        dialog->show();
    }
    else if (action_name == "donate")
    {
        gtr_open_uri("https://transmissionbt.com/donate/");
    }
    else if (action_name == "pause-all-torrents")
    {
        pause_all_torrents();
    }
    else if (action_name == "start-all-torrents")
    {
        start_all_torrents();
    }
    else if (action_name == "copy-magnet-link-to-clipboard")
    {
        tr_torrent* tor = get_first_selected_torrent();

        if (tor != nullptr)
        {
            copy_magnet_link_to_clipboard(tor);
        }
    }
    else if (action_name == "relocate-torrent")
    {
        auto const ids = get_selected_torrent_ids();

        if (!ids.empty())
        {
            auto w = std::shared_ptr<RelocateDialog>(RelocateDialog::create(*wind_, core_, ids));
            w->signal_hide().connect([w]() mutable { w.reset(); });
            w->show();
        }
    }
    else if (
        action_name == "torrent-start" || action_name == "torrent-start-now" || action_name == "torrent-stop" ||
        action_name == "torrent-reannounce" || action_name == "torrent-verify" || action_name == "queue-move-top" ||
        action_name == "queue-move-up" || action_name == "queue-move-down" || action_name == "queue-move-bottom")
    {
        changed = call_rpc_for_selected_torrents(action_name);
    }
    else if (action_name == "open-torrent-folder")
    {
        sel_->selected_foreach([this](auto const& /*path*/, auto const& iter)
                               { core_->open_folder(iter->get_value(torrent_cols.torrent_id)); });
    }
    else if (action_name == "show-torrent-properties")
    {
        show_details_dialog_for_selected_torrents();
    }
    else if (action_name == "new-torrent")
    {
        auto w = std::shared_ptr<MakeDialog>(MakeDialog::create(*wind_, core_));
        w->signal_hide().connect([w]() mutable { w.reset(); });
        w->show();
    }
    else if (action_name == "remove-torrent")
    {
        remove_selected(false);
    }
    else if (action_name == "delete-torrent")
    {
        remove_selected(true);
    }
    else if (action_name == "quit")
    {
        on_app_exit();
    }
    else if (action_name == "select-all")
    {
        sel_->select_all();
    }
    else if (action_name == "deselect-all")
    {
        sel_->unselect_all();
    }
    else if (action_name == "edit-preferences")
    {
        if (prefs_ == nullptr)
        {
            prefs_ = PrefsDialog::create(*wind_, core_);
            prefs_->signal_hide().connect([this]() { prefs_.reset(); });
        }

        gtr_window_present(prefs_);
    }
    else if (action_name == "toggle-message-log")
    {
        if (msgwin_ == nullptr)
        {
            msgwin_ = MessageLogWindow::create(*wind_, core_);
            msgwin_->signal_hide().connect([this]() { msgwin_.reset(); });
            msgwin_->show();
        }
        else
        {
            msgwin_->hide();
        }
    }
    else if (action_name == "show-about-dialog")
    {
        show_about_dialog();
    }
    else if (action_name == "help")
    {
        gtr_open_uri(gtr_get_help_uri());
    }
    else if (action_name == "toggle-main-window")
    {
        toggleMainWindow();
    }
    else if (action_name == "present-main-window")
    {
        presentMainWindow();
    }
    else
    {
        g_error("Unhandled action: %s", action_name.c_str());
    }

    if (changed)
    {
        update_model_soon();
    }
}
