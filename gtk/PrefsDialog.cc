// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <limits>
#include <memory>
#include <sstream>
#include <string>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <fmt/core.h>

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>
#include <libtransmission/web-utils.h>

#include "FreeSpaceLabel.h"
#include "PathButton.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "SystemTrayIcon.h"
#include "Utils.h"

/**
***
**/

class PrefsDialog::Impl
{
public:
    Impl(PrefsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);

    TR_DISABLE_COPY_MOVE(Impl)

private:
    void response_cb(int response);

private:
    PrefsDialog& dialog_;
    Glib::RefPtr<Session> const core_;
};

/**
***
**/

void PrefsDialog::Impl::response_cb(int response)
{
    if (response == TR_GTK_RESPONSE_TYPE(HELP))
    {
        gtr_open_uri(gtr_get_help_uri() + "/html/preferences.html");
    }

    if (response == TR_GTK_RESPONSE_TYPE(CLOSE))
    {
        dialog_.close();
    }
}

namespace
{

void init_check_button(Gtk::CheckButton& button, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    button.set_active(gtr_pref_flag_get(key));
    button.signal_toggled().connect([&button, key, core]() { core->set_pref(key, button.get_active()); });
}

auto const IdleDataKey = Glib::Quark("idle-data");

bool spun_cb_idle(Gtk::SpinButton* spin, tr_quark const key, Glib::RefPtr<Session> const& core, bool isDouble)
{
    bool keep_waiting = true;

    /* has the user stopped making changes? */
    if (auto const* const last_change = static_cast<Glib::Timer*>(spin->get_data(IdleDataKey)); last_change->elapsed() > 0.33)
    {
        /* update the core */
        if (isDouble)
        {
            double const value = spin->get_value();
            core->set_pref(key, value);
        }
        else
        {
            int const value = spin->get_value_as_int();
            core->set_pref(key, value);
        }

        /* cleanup */
        spin->set_data(IdleDataKey, nullptr);
        keep_waiting = false;
        spin->unreference();
    }

    return keep_waiting;
}

void spun_cb(Gtk::SpinButton* w, tr_quark const key, Glib::RefPtr<Session> const& core, bool isDouble)
{
    /* user may be spinning through many values, so let's hold off
       for a moment to keep from flooding the core with changes */
    auto* last_change = static_cast<Glib::Timer*>(w->get_data(IdleDataKey));

    if (last_change == nullptr)
    {
        last_change = new Glib::Timer();
        w->set_data(IdleDataKey, last_change, [](gpointer p) { delete static_cast<Glib::Timer*>(p); });
        w->reference();
        Glib::signal_timeout().connect_seconds([w, key, core, isDouble]() { return spun_cb_idle(w, key, core, isDouble); }, 1);
    }

    last_change->start();
}

void init_spin_button(
    Gtk::SpinButton& button,
    tr_quark const key,
    Glib::RefPtr<Session> const& core,
    int low,
    int high,
    int step)
{
    button.set_adjustment(Gtk::Adjustment::create(gtr_pref_int_get(key), low, high, step));
    button.set_digits(0);
    button.signal_value_changed().connect([&button, key, core]() { spun_cb(&button, key, core, false); });
}

void init_spin_button_double(
    Gtk::SpinButton& button,
    tr_quark const key,
    Glib::RefPtr<Session> const& core,
    double low,
    double high,
    double step)
{
    button.set_adjustment(Gtk::Adjustment::create(gtr_pref_double_get(key), low, high, step));
    button.set_digits(2);
    button.signal_value_changed().connect([&button, key, core]() { spun_cb(&button, key, core, true); });
}

void entry_changed_cb(Gtk::Entry* w, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    core->set_pref(key, w->get_text());
}

void init_entry(Gtk::Entry& entry, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    if (auto const value = gtr_pref_string_get(key); !value.empty())
    {
        entry.set_text(value);
    }

    entry.signal_changed().connect([&entry, key, core]() { entry_changed_cb(&entry, key, core); });
}

void init_text_view(Gtk::TextView& view, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    auto buffer = view.get_buffer();
    buffer->set_text(gtr_pref_string_get(key));

    auto const save_buffer = [buffer, key, core]()
    {
        core->set_pref(key, buffer->get_text());
    };

#if GTKMM_CHECK_VERSION(4, 0, 0)
    auto focus_controller = Gtk::EventControllerFocus::create();
    focus_controller->signal_leave().connect(save_buffer);
    view.add_controller(focus_controller);
#else
    view.add_events(Gdk::FOCUS_CHANGE_MASK);
    view.signal_focus_out_event().connect_notify(sigc::hide<0>(save_buffer));
#endif
}

void chosen_cb(PathButton* w, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    core->set_pref(key, w->get_filename());
}

void init_chooser_button(PathButton& button, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    if (auto const path = gtr_pref_string_get(key); !path.empty())
    {
        button.set_filename(path);
    }

    button.signal_selection_changed().connect([&button, key, core]() { chosen_cb(&button, key, core); });
}

} // namespace

/****
*****  Download Tab
****/

namespace
{

class DownloadingPage : public Gtk::Box
{
public:
    DownloadingPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    ~DownloadingPage() override;

    TR_DISABLE_COPY_MOVE(DownloadingPage)

private:
    void on_core_prefs_changed(tr_quark key);

private:
    Glib::RefPtr<Session> const core_;

    FreeSpaceLabel* freespace_label_ = nullptr;

    sigc::connection core_prefs_tag_;
};

void DownloadingPage::on_core_prefs_changed(tr_quark const key)
{
    if (key == TR_KEY_download_dir)
    {
        char const* downloadDir = tr_sessionGetDownloadDir(core_->get_session());
        freespace_label_->set_dir(downloadDir);
    }
}

DownloadingPage::~DownloadingPage()
{
    if (core_prefs_tag_.connected())
    {
        core_prefs_tag_.disconnect();
    }
}

DownloadingPage::DownloadingPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : Gtk::Box(cast_item)
    , core_(core)
    , freespace_label_(gtr_get_widget_derived<FreeSpaceLabel>(builder, "download_dir_stats_label", core))
{
    core_prefs_tag_ = core_->signal_prefs_changed().connect(sigc::mem_fun(*this, &DownloadingPage::on_core_prefs_changed));

    {
        auto* l = gtr_get_widget<Gtk::CheckButton>(builder, "watch_dir_check");
        init_check_button(*l, TR_KEY_watch_dir_enabled, core_);
        auto* w = gtr_get_widget_derived<PathButton>(builder, "watch_dir_chooser");
        init_chooser_button(*w, TR_KEY_watch_dir, core_);
    }

    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "show_options_dialog_check"),
        TR_KEY_show_options_window,
        core);

    init_check_button(*gtr_get_widget<Gtk::CheckButton>(builder, "start_on_add_check"), TR_KEY_start_added_torrents, core_);

    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "trash_on_add_check"),
        TR_KEY_trash_original_torrent_files,
        core_);

    init_chooser_button(*gtr_get_widget_derived<PathButton>(builder, "download_dir_chooser"), TR_KEY_download_dir, core_);

    init_spin_button(
        *gtr_get_widget<Gtk::SpinButton>(builder, "max_active_downloads_spin"),
        TR_KEY_download_queue_size,
        core_,
        0,
        std::numeric_limits<int>::max(),
        1);

    init_spin_button(
        *gtr_get_widget<Gtk::SpinButton>(builder, "max_inactive_time_spin"),
        TR_KEY_queue_stalled_minutes,
        core_,
        1,
        std::numeric_limits<int>::max(),
        15);

    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "append_suffix_to_incomplete_check"),
        TR_KEY_rename_partial_files,
        core_);

    {
        auto* l = gtr_get_widget<Gtk::CheckButton>(builder, "incomplete_dir_check");
        init_check_button(*l, TR_KEY_incomplete_dir_enabled, core_);
        auto* w = gtr_get_widget_derived<PathButton>(builder, "incomplete_dir_chooser");
        init_chooser_button(*w, TR_KEY_incomplete_dir, core_);
    }

    {
        auto* l = gtr_get_widget<Gtk::CheckButton>(builder, "download_done_script_check");
        init_check_button(*l, TR_KEY_script_torrent_done_enabled, core_);
        auto* w = gtr_get_widget_derived<PathButton>(builder, "download_done_script_chooser");
        init_chooser_button(*w, TR_KEY_script_torrent_done_filename, core_);
    }

    on_core_prefs_changed(TR_KEY_download_dir);
}

} // namespace

/****
*****  Torrent Tab
****/

namespace
{

class SeedingPage : public Gtk::Box
{
public:
    SeedingPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);

    TR_DISABLE_COPY_MOVE(SeedingPage)

private:
    Glib::RefPtr<Session> const core_;
};

SeedingPage::SeedingPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : Gtk::Box(cast_item)
    , core_(core)
{
    {
        auto* w = gtr_get_widget<Gtk::CheckButton>(builder, "stop_seeding_ratio_check");
        init_check_button(*w, TR_KEY_ratio_limit_enabled, core_);
        auto* w2 = gtr_get_widget<Gtk::SpinButton>(builder, "stop_seeding_ratio_spin");
        init_spin_button_double(*w2, TR_KEY_ratio_limit, core_, 0, 1000, .05);
    }

    {
        auto* w = gtr_get_widget<Gtk::CheckButton>(builder, "stop_seeding_timeout_check");
        init_check_button(*w, TR_KEY_idle_seeding_limit_enabled, core_);
        auto* w2 = gtr_get_widget<Gtk::SpinButton>(builder, "stop_seeding_timeout_spin");
        init_spin_button(*w2, TR_KEY_idle_seeding_limit, core_, 1, 40320, 5);
    }

    {
        auto* l = gtr_get_widget<Gtk::CheckButton>(builder, "seeding_done_script_check");
        init_check_button(*l, TR_KEY_script_torrent_done_seeding_enabled, core_);
        auto* w = gtr_get_widget_derived<PathButton>(builder, "seeding_done_script_chooser");
        init_chooser_button(*w, TR_KEY_script_torrent_done_seeding_filename, core_);
    }
}

} // namespace

/****
*****  Desktop Tab
****/

namespace
{

class DesktopPage : public Gtk::Box
{
public:
    DesktopPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);

    TR_DISABLE_COPY_MOVE(DesktopPage)

private:
    Glib::RefPtr<Session> const core_;
};

DesktopPage::DesktopPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : Gtk::Box(cast_item)
    , core_(core)
{
    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "inhibit_hibernation_check"),
        TR_KEY_inhibit_desktop_hibernation,
        core_);

    if (auto* const show_systray_icon_check = gtr_get_widget<Gtk::CheckButton>(builder, "show_systray_icon_check");
        SystemTrayIcon::is_available())
    {
        init_check_button(*show_systray_icon_check, TR_KEY_show_notification_area_icon, core_);
    }
    else
    {
        show_systray_icon_check->hide();
    }

    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "notify_on_torrent_add_check"),
        TR_KEY_torrent_added_notification_enabled,
        core_);

    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "notify_on_torrent_finish_check"),
        TR_KEY_torrent_complete_notification_enabled,
        core_);

    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "ding_no_torrent_finish_check"),
        TR_KEY_torrent_complete_sound_enabled,
        core_);
}

} // namespace

/****
*****  Peer Tab
****/

namespace
{

class PrivacyPage : public Gtk::Box
{
public:
    PrivacyPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    ~PrivacyPage() override;

    TR_DISABLE_COPY_MOVE(PrivacyPage)

private:
    void updateBlocklistText();
    void onBlocklistUpdateResponse();
    void onBlocklistUpdated(int n);
    void onBlocklistUpdate();
    void on_blocklist_url_changed(Gtk::Editable* e);

    static void init_encryption_combo(Gtk::ComboBox& combo, Glib::RefPtr<Session> const& core, tr_quark key);

private:
    Glib::RefPtr<Session> core_;

    Gtk::Button* updateBlocklistButton_ = nullptr;
    Gtk::Label* label_ = nullptr;
    Gtk::CheckButton* check_ = nullptr;

    sigc::connection updateBlocklistTag_;
    std::unique_ptr<Gtk::MessageDialog> updateBlocklistDialog_;
};

void PrivacyPage::updateBlocklistText()
{
    int const n = tr_blocklistGetRuleCount(core_->get_session());
    auto const msg = fmt::format(
        ngettext("Blocklist has {count:L} entry", "Blocklist has {count:L} entries", n),
        fmt::arg("count", n));
    label_->set_markup(fmt::format(FMT_STRING("<i>{:s}</i>"), msg));
}

/* prefs dialog is being destroyed, so stop listening to blocklist updates */
PrivacyPage::~PrivacyPage()
{
    if (updateBlocklistTag_.connected())
    {
        updateBlocklistTag_.disconnect();
    }
}

/* user hit "close" in the blocklist-update dialog */
void PrivacyPage::onBlocklistUpdateResponse()
{
    updateBlocklistButton_->set_sensitive(true);
    updateBlocklistDialog_.reset();
    updateBlocklistTag_.disconnect();
}

/* core says the blocklist was updated */
void PrivacyPage::onBlocklistUpdated(int n)
{
    bool const success = n >= 0;
    int const count = n >= 0 ? n : tr_blocklistGetRuleCount(core_->get_session());
    auto const msg = fmt::format(
        ngettext("Blocklist has {count:L} entry", "Blocklist has {count:L} entries", count),
        fmt::arg("count", count));
    updateBlocklistButton_->set_sensitive(true);
    updateBlocklistDialog_->set_message(
        fmt::format(FMT_STRING("<b>{:s}</b>"), success ? _("Blocklist updated!") : _("Couldn't update blocklist")),
        true);
    updateBlocklistDialog_->set_secondary_text(msg);
    updateBlocklistText();
}

/* user pushed a button to update the blocklist */
void PrivacyPage::onBlocklistUpdate()
{
    updateBlocklistDialog_ = std::make_unique<Gtk::MessageDialog>(
        *static_cast<Gtk::Window*>(TR_GTK_WIDGET_GET_ROOT(*this)),
        _("Update Blocklist"),
        false,
        TR_GTK_MESSAGE_TYPE(INFO),
        TR_GTK_BUTTONS_TYPE(CLOSE));
    updateBlocklistButton_->set_sensitive(false);
    updateBlocklistDialog_->set_secondary_text(_("Getting new blocklist…"));
    updateBlocklistDialog_->signal_response().connect([this](int /*response*/) { onBlocklistUpdateResponse(); });
    updateBlocklistDialog_->show();
    core_->blocklist_update();
    updateBlocklistTag_ = core_->signal_blocklist_updated().connect([this](auto n) { onBlocklistUpdated(n); });
}

void PrivacyPage::on_blocklist_url_changed(Gtk::Editable* e)
{
    auto const url = e->get_chars(0, -1);
    updateBlocklistButton_->set_sensitive(tr_urlIsValid(url.c_str()));
}

void onIntComboChanged(Gtk::ComboBox* combo_box, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    core->set_pref(key, gtr_combo_box_get_active_enum(*combo_box));
}

void PrivacyPage::init_encryption_combo(Gtk::ComboBox& combo, Glib::RefPtr<Session> const& core, tr_quark const key)
{
    gtr_combo_box_set_enum(
        combo,
        {
            { _("Allow encryption"), TR_CLEAR_PREFERRED },
            { _("Prefer encryption"), TR_ENCRYPTION_PREFERRED },
            { _("Require encryption"), TR_ENCRYPTION_REQUIRED },
        });
    gtr_combo_box_set_active_enum(combo, gtr_pref_int_get(key));
    combo.signal_changed().connect([&combo, key, core]() { onIntComboChanged(&combo, key, core); });
}

PrivacyPage::PrivacyPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : Gtk::Box(cast_item)
    , core_(core)
    , updateBlocklistButton_(gtr_get_widget<Gtk::Button>(builder, "update_blocklist_button"))
    , label_(gtr_get_widget<Gtk::Label>(builder, "blocklist_stats_label"))
    , check_(gtr_get_widget<Gtk::CheckButton>(builder, "blocklist_check"))
{
    init_encryption_combo(*gtr_get_widget<Gtk::ComboBox>(builder, "encryption_mode_combo"), core_, TR_KEY_encryption);

    init_check_button(*check_, TR_KEY_blocklist_enabled, core_);
    auto* const e = gtr_get_widget<Gtk::Entry>(builder, "blocklist_url_entry");
    init_entry(*e, TR_KEY_blocklist_url, core_);

    updateBlocklistText();
    updateBlocklistButton_->set_data("session", core_->get_session());
    updateBlocklistButton_->signal_clicked().connect([this]() { onBlocklistUpdate(); });
    updateBlocklistButton_->set_sensitive(check_->get_active());
    e->signal_changed().connect([this, e]() { on_blocklist_url_changed(e); });
    on_blocklist_url_changed(e);

    auto* update_check = gtr_get_widget<Gtk::CheckButton>(builder, "blocklist_autoupdate_check");
    init_check_button(*update_check, TR_KEY_blocklist_updates_enabled, core_);
}

} // namespace

/****
*****  Remote Tab
****/

namespace
{

class WhitelistModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    WhitelistModelColumns()
    {
        add(address);
    }

    Gtk::TreeModelColumn<Glib::ustring> address;
};

WhitelistModelColumns const whitelist_cols;

Glib::RefPtr<Gtk::ListStore> whitelist_tree_model_new(std::string const& whitelist)
{
    auto const store = Gtk::ListStore::create(whitelist_cols);

    std::istringstream stream(whitelist);
    std::string s;

    while (std::getline(stream, s, ','))
    {
        s = gtr_str_strip(s);

        if (s.empty())
        {
            continue;
        }

        auto const iter = store->append();
        (*iter)[whitelist_cols.address] = s;
    }

    return store;
}

class RemotePage : public Gtk::Box
{
public:
    RemotePage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);

    TR_DISABLE_COPY_MOVE(RemotePage)

private:
    void refreshWhitelist();
    void onAddressEdited(Glib::ustring const& path, Glib::ustring const& address);
    void onAddWhitelistClicked();
    void onRemoveWhitelistClicked();
    void refreshRPCSensitivity();

private:
    Glib::RefPtr<Session> core_;

    Gtk::TreeView* view_;
    Gtk::Button* remove_button_;
    Gtk::CheckButton* rpc_tb_;
    Gtk::CheckButton* auth_tb_;
    Gtk::CheckButton* whitelist_tb_;

    Glib::RefPtr<Gtk::ListStore> store_;
    std::vector<Gtk::Widget*> auth_widgets_;
    std::vector<Gtk::Widget*> whitelist_widgets_;
};

void RemotePage::refreshWhitelist()
{
    std::ostringstream gstr;

    for (auto const& row : store_->children())
    {
        gstr << row.get_value(whitelist_cols.address) << ",";
    }

    auto str = gstr.str();
    if (!str.empty())
    {
        str.resize(str.size() - 1); /* remove the trailing comma */
    }

    core_->set_pref(TR_KEY_rpc_whitelist, str);
}

void RemotePage::onAddressEdited(Glib::ustring const& path, Glib::ustring const& address)
{
    if (auto const iter = store_->get_iter(path); iter)
    {
        (*iter)[whitelist_cols.address] = address;
    }

    refreshWhitelist();
}

void RemotePage::onAddWhitelistClicked()
{
    auto const iter = store_->append();
    (*iter)[whitelist_cols.address] = "0.0.0.0";

    view_->set_cursor(store_->get_path(iter), *view_->get_column(0), true);
}

void RemotePage::onRemoveWhitelistClicked()
{
    auto const sel = view_->get_selection();

    if (auto const iter = sel->get_selected(); iter)
    {
        store_->erase(iter);
        refreshWhitelist();
    }
}

void RemotePage::refreshRPCSensitivity()
{
    bool const rpc_active = rpc_tb_->get_active();
    bool const auth_active = auth_tb_->get_active();
    bool const whitelist_active = whitelist_tb_->get_active();
    auto const sel = view_->get_selection();
    auto const have_addr = sel->get_selected();
    auto const n_rules = store_->children().size();

    for (auto* const widget : auth_widgets_)
    {
        widget->set_sensitive(rpc_active && auth_active);
    }

    for (auto* const widget : whitelist_widgets_)
    {
        widget->set_sensitive(rpc_active && whitelist_active);
    }

    remove_button_->set_sensitive(rpc_active && whitelist_active && have_addr && n_rules > 1);
}

void onLaunchClutchCB()
{
    gtr_open_uri(fmt::format("http://localhost:{}/", gtr_pref_int_get(TR_KEY_rpc_port)));
}

} // namespace

RemotePage::RemotePage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : Gtk::Box(cast_item)
    , core_(core)
    , view_(gtr_get_widget<Gtk::TreeView>(builder, "rpc_whitelist_view"))
    , remove_button_(gtr_get_widget<Gtk::Button>(builder, "remove_from_rpc_whistlist_button"))
    , rpc_tb_(gtr_get_widget<Gtk::CheckButton>(builder, "enable_rpc_check"))
    , auth_tb_(gtr_get_widget<Gtk::CheckButton>(builder, "enable_rpc_auth_check"))
    , whitelist_tb_(gtr_get_widget<Gtk::CheckButton>(builder, "rpc_whitelist_check"))
{
    /* "enabled" checkbutton */
    init_check_button(*rpc_tb_, TR_KEY_rpc_enabled, core_);
    rpc_tb_->signal_toggled().connect([this]() { refreshRPCSensitivity(); });
    auto* const open_button = gtr_get_widget<Gtk::Button>(builder, "open_web_client_button");
    open_button->signal_clicked().connect(&onLaunchClutchCB);

    /* port */
    auto* port_spin = gtr_get_widget<Gtk::SpinButton>(builder, "rpc_port_spin");
    init_spin_button(*port_spin, TR_KEY_rpc_port, core_, 0, std::numeric_limits<uint16_t>::max(), 1);

    /* require authentication */
    init_check_button(*auth_tb_, TR_KEY_rpc_authentication_required, core_);
    auth_tb_->signal_toggled().connect([this]() { refreshRPCSensitivity(); });

    /* username */
    auto* username_entry = gtr_get_widget<Gtk::Entry>(builder, "rpc_username_entry");
    init_entry(*username_entry, TR_KEY_rpc_username, core_);
    auth_widgets_.push_back(username_entry);
    auth_widgets_.push_back(gtr_get_widget<Gtk::Label>(builder, "rpc_username_label"));

    /* password */
    auto* password_entry = gtr_get_widget<Gtk::Entry>(builder, "rpc_password_entry");
    init_entry(*password_entry, TR_KEY_rpc_password, core_);
    auth_widgets_.push_back(password_entry);
    auth_widgets_.push_back(gtr_get_widget<Gtk::Label>(builder, "rpc_password_label"));

    /* require authentication */
    init_check_button(*whitelist_tb_, TR_KEY_rpc_whitelist_enabled, core_);
    whitelist_tb_->signal_toggled().connect([this]() { refreshRPCSensitivity(); });

    /* access control list */
    {
        store_ = whitelist_tree_model_new(gtr_pref_string_get(TR_KEY_rpc_whitelist));

        view_->set_model(store_);
        setup_tree_view_button_event_handling(
            *view_,
            {},
            [this](double view_x, double view_y) { return on_tree_view_button_released(*view_, view_x, view_y); });

        whitelist_widgets_.push_back(view_);
        auto const sel = view_->get_selection();
        sel->signal_changed().connect([this]() { refreshRPCSensitivity(); });

        /* ip address column */
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->signal_edited().connect([this](auto const& path, auto const& new_text) { onAddressEdited(path, new_text); });
        r->property_editable() = true;
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>("", *r);
        c->add_attribute(r->property_text(), whitelist_cols.address);
        c->set_expand(true);
        view_->append_column(*c);

        whitelist_widgets_.push_back(gtr_get_widget<Gtk::Label>(builder, "rpc_whitelist_label"));

        remove_button_->signal_clicked().connect([this]() { onRemoveWhitelistClicked(); });
        refreshRPCSensitivity();
        auto* add_button = gtr_get_widget<Gtk::Button>(builder, "add_to_rpc_whitelist_button");
        whitelist_widgets_.push_back(add_button);
        add_button->signal_clicked().connect([this]() { onAddWhitelistClicked(); });
    }

    refreshRPCSensitivity();
}

/****
*****  Bandwidth Tab
****/

namespace
{

class SpeedPage : public Gtk::Box
{
public:
    SpeedPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);

    TR_DISABLE_COPY_MOVE(SpeedPage)

private:
    static void init_time_combo(Gtk::ComboBox& combo, Glib::RefPtr<Session> const& core, tr_quark key);
    static void init_week_combo(Gtk::ComboBox& combo, Glib::RefPtr<Session> const& core, tr_quark key);

    static auto get_weekday_string(Glib::Date::Weekday weekday);

private:
    Glib::RefPtr<Session> core_;
};

void SpeedPage::init_time_combo(Gtk::ComboBox& combo, Glib::RefPtr<Session> const& core, tr_quark const key)
{
    class TimeModelColumns : public Gtk::TreeModelColumnRecord
    {
    public:
        TimeModelColumns()
        {
            add(offset);
            add(title);
        }

        Gtk::TreeModelColumn<int> offset;
        Gtk::TreeModelColumn<Glib::ustring> title;
    };

    static TimeModelColumns const time_cols;

    /* build a store at 15 minute intervals */
    auto store = Gtk::ListStore::create(time_cols);

    for (int i = 0; i < 60 * 24; i += 15)
    {
        auto const iter = store->append();
        (*iter)[time_cols.offset] = i;
        (*iter)[time_cols.title] = fmt::format("{:02}:{:02}", i / 60, i % 60);
    }

    /* build the widget */
    combo.set_model(store);
    auto* r = Gtk::make_managed<Gtk::CellRendererText>();
    combo.pack_start(*r, true);
    combo.add_attribute(r->property_text(), time_cols.title);
    combo.set_active(gtr_pref_int_get(key) / 15);
    combo.signal_changed().connect(
        [&combo, key, core]()
        {
            if (auto const iter = combo.get_active(); iter)
            {
                core->set_pref(key, iter->get_value(time_cols.offset));
            }
        });
}

auto SpeedPage::get_weekday_string(Glib::Date::Weekday weekday)
{
    auto date = Glib::Date{};
    date.set_time_current();
    date.add_days(static_cast<int>(weekday) - static_cast<int>(date.get_weekday()));
    return date.format_string("%A");
}

void SpeedPage::init_week_combo(Gtk::ComboBox& combo, Glib::RefPtr<Session> const& core, tr_quark const key)
{
    gtr_combo_box_set_enum(
        combo,
        {
            { _("Every Day"), TR_SCHED_ALL },
            { _("Weekdays"), TR_SCHED_WEEKDAY },
            { _("Weekends"), TR_SCHED_WEEKEND },
            { get_weekday_string(Glib::Date::Weekday::MONDAY), TR_SCHED_MON },
            { get_weekday_string(Glib::Date::Weekday::TUESDAY), TR_SCHED_TUES },
            { get_weekday_string(Glib::Date::Weekday::WEDNESDAY), TR_SCHED_WED },
            { get_weekday_string(Glib::Date::Weekday::THURSDAY), TR_SCHED_THURS },
            { get_weekday_string(Glib::Date::Weekday::FRIDAY), TR_SCHED_FRI },
            { get_weekday_string(Glib::Date::Weekday::SATURDAY), TR_SCHED_SAT },
            { get_weekday_string(Glib::Date::Weekday::SUNDAY), TR_SCHED_SUN },
        });
    gtr_combo_box_set_active_enum(combo, gtr_pref_int_get(key));
    combo.signal_changed().connect([&combo, key, core]() { onIntComboChanged(&combo, key, core); });
}

SpeedPage::SpeedPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : Gtk::Box(cast_item)
    , core_(core)
{
    {
        auto* const w = gtr_get_widget<Gtk::CheckButton>(builder, "upload_limit_check");
        init_check_button(*w, TR_KEY_speed_limit_up_enabled, core_);
        w->set_label(fmt::format(w->get_label().raw(), fmt::arg("speed_units", speed_K_str)));

        auto* const w2 = gtr_get_widget<Gtk::SpinButton>(builder, "upload_limit_spin");
        init_spin_button(*w2, TR_KEY_speed_limit_up, core_, 0, std::numeric_limits<int>::max(), 5);
    }

    {
        auto* const w = gtr_get_widget<Gtk::CheckButton>(builder, "download_limit_check");
        init_check_button(*w, TR_KEY_speed_limit_down_enabled, core_);
        w->set_label(fmt::format(w->get_label().raw(), fmt::arg("speed_units", speed_K_str)));

        auto* const w2 = gtr_get_widget<Gtk::SpinButton>(builder, "download_limit_spin");
        init_spin_button(*w2, TR_KEY_speed_limit_down, core_, 0, std::numeric_limits<int>::max(), 5);
    }

    {
        auto* const w = gtr_get_widget<Gtk::Label>(builder, "alt_upload_limit_label");
        w->set_label(fmt::format(w->get_label().raw(), fmt::arg("speed_units", speed_K_str)));

        auto* const w2 = gtr_get_widget<Gtk::SpinButton>(builder, "alt_upload_limit_spin");
        init_spin_button(*w2, TR_KEY_alt_speed_up, core_, 0, std::numeric_limits<int>::max(), 5);
    }

    {
        auto* const w = gtr_get_widget<Gtk::Label>(builder, "alt_download_limit_label");
        w->set_label(fmt::format(w->get_label().raw(), fmt::arg("speed_units", speed_K_str)));

        auto* const w2 = gtr_get_widget<Gtk::SpinButton>(builder, "alt_download_limit_spin");
        init_spin_button(*w2, TR_KEY_alt_speed_down, core_, 0, std::numeric_limits<int>::max(), 5);
    }

    {
        auto* start_combo = gtr_get_widget<Gtk::ComboBox>(builder, "alt_speed_start_time_combo");
        init_time_combo(*start_combo, core_, TR_KEY_alt_speed_time_begin);

        auto* end_combo = gtr_get_widget<Gtk::ComboBox>(builder, "alt_speed_end_time_combo");
        init_time_combo(*end_combo, core_, TR_KEY_alt_speed_time_end);

        auto* w = gtr_get_widget<Gtk::CheckButton>(builder, "alt_schedule_time_check");
        init_check_button(*w, TR_KEY_alt_speed_time_enabled, core_);
    }

    auto* week_combo = gtr_get_widget<Gtk::ComboBox>(builder, "alt_speed_days_combo");
    init_week_combo(*week_combo, core_, TR_KEY_alt_speed_time_day);
}

} // namespace

/****
*****  Network Tab
****/

namespace
{

class NetworkPage : public Gtk::Box
{
public:
    NetworkPage(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    ~NetworkPage() override;

    TR_DISABLE_COPY_MOVE(NetworkPage)

private:
    void onCorePrefsChanged(tr_quark key);
    void onPortTested(bool isOpen);
    void onPortTest();

private:
    Glib::RefPtr<Session> core_;

    Gtk::Label* portLabel_ = nullptr;
    Gtk::Button* portButton_ = nullptr;
    Gtk::SpinButton* portSpin_ = nullptr;

    sigc::connection portTag_;
    sigc::connection prefsTag_;
};

void NetworkPage::onCorePrefsChanged(tr_quark const key)
{
    if (key == TR_KEY_peer_port)
    {
        gtr_label_set_text(*portLabel_, _("Status unknown"));
        portButton_->set_sensitive(true);
        portSpin_->set_sensitive(true);
    }
}

NetworkPage::~NetworkPage()
{
    if (prefsTag_.connected())
    {
        prefsTag_.disconnect();
    }

    if (portTag_.connected())
    {
        portTag_.disconnect();
    }
}

void NetworkPage::onPortTested(bool isOpen)
{
    portLabel_->set_markup(fmt::format(
        isOpen ? _("Port is {markup_begin}open{markup_end}") : _("Port is {markup_begin}closed{markup_end}"),
        fmt::arg("markup_begin", "<b>"),
        fmt::arg("markup_end", "</b>")));
    portButton_->set_sensitive(true);
    portSpin_->set_sensitive(true);
}

void NetworkPage::onPortTest()
{
    portButton_->set_sensitive(false);
    portSpin_->set_sensitive(false);
    portLabel_->set_text(_("Testing TCP port…"));

    if (!portTag_.connected())
    {
        portTag_ = core_->signal_port_tested().connect([this](bool is_open) { onPortTested(is_open); });
    }

    core_->port_test();
}

NetworkPage::NetworkPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : Gtk::Box(cast_item)
    , core_(core)
    , portLabel_(gtr_get_widget<Gtk::Label>(builder, "listening_port_status_label"))
    , portButton_(gtr_get_widget<Gtk::Button>(builder, "test_listening_port_button"))
    , portSpin_(gtr_get_widget<Gtk::SpinButton>(builder, "listening_port_spin"))
{
    init_spin_button(*portSpin_, TR_KEY_peer_port, core_, 1, std::numeric_limits<uint16_t>::max(), 1);

    portButton_->signal_clicked().connect([this]() { onPortTest(); });

    prefsTag_ = core_->signal_prefs_changed().connect([this](auto key) { onCorePrefsChanged(key); });

    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "pick_random_listening_port_at_start_check"),
        TR_KEY_peer_port_random_on_start,
        core_);
    init_check_button(
        *gtr_get_widget<Gtk::CheckButton>(builder, "enable_listening_port_forwarding_check"),
        TR_KEY_port_forwarding_enabled,
        core_);

    init_spin_button(
        *gtr_get_widget<Gtk::SpinButton>(builder, "max_torrent_peers_spin"),
        TR_KEY_peer_limit_per_torrent,
        core_,
        1,
        FD_SETSIZE,
        5);
    init_spin_button(
        *gtr_get_widget<Gtk::SpinButton>(builder, "max_total_peers_spin"),
        TR_KEY_peer_limit_global,
        core_,
        1,
        FD_SETSIZE,
        5);

#ifdef WITH_UTP
    init_check_button(*gtr_get_widget<Gtk::CheckButton>(builder, "enable_utp_check"), TR_KEY_utp_enabled, core_);
#else
    gtr_get_widget<Gtk::CheckButton>(builder, "enable_utp_check")->hide();
#endif

    init_check_button(*gtr_get_widget<Gtk::CheckButton>(builder, "enable_pex_check"), TR_KEY_pex_enabled, core_);

    init_check_button(*gtr_get_widget<Gtk::CheckButton>(builder, "enable_dht_check"), TR_KEY_dht_enabled, core_);

    init_check_button(*gtr_get_widget<Gtk::CheckButton>(builder, "enable_lpd_check"), TR_KEY_lpd_enabled, core_);

    init_text_view(*gtr_get_widget<Gtk::TextView>(builder, "default_trackers_view"), TR_KEY_default_trackers, core_);
}

} // namespace

/****
*****
****/

std::unique_ptr<PrefsDialog> PrefsDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("PrefsDialog.ui"));
    return std::unique_ptr<PrefsDialog>(gtr_get_widget_derived<PrefsDialog>(builder, "PrefsDialog", parent, core));
}

PrefsDialog::PrefsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core))
{
    set_transient_for(parent);
}

PrefsDialog::~PrefsDialog() = default;

PrefsDialog::Impl::Impl(PrefsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
{
    gtr_get_widget_derived<SpeedPage>(builder, "speed_page_layout", core_);
    gtr_get_widget_derived<DownloadingPage>(builder, "downloading_page_layout", core_);
    gtr_get_widget_derived<SeedingPage>(builder, "seeding_page_layout", core_);
    gtr_get_widget_derived<PrivacyPage>(builder, "privacy_page_layout", core_);
    gtr_get_widget_derived<NetworkPage>(builder, "network_page_layout", core_);
    gtr_get_widget_derived<DesktopPage>(builder, "desktop_page_layout", core_);
    gtr_get_widget_derived<RemotePage>(builder, "remote_page_layout", core_);

    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::response_cb));
}
