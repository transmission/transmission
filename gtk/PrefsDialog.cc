// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <climits> /* USHRT_MAX, INT_MAX */
#include <sstream>
#include <string>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <fmt/core.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>
#include <libtransmission/web-utils.h>

#include "FreeSpaceLabel.h"
#include "HigWorkarea.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

/**
***
**/

class PrefsDialog::Impl
{
public:
    Impl(PrefsDialog& dialog, Glib::RefPtr<Session> const& core);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

private:
    Gtk::Widget* speedPage();
    Gtk::Widget* downloadingPage();
    Gtk::Widget* seedingPage();
    Gtk::Widget* privacyPage();
    Gtk::Widget* networkPage();
    Gtk::Widget* desktopPage();
    Gtk::Widget* remotePage();

    void on_core_prefs_changed(tr_quark const key);

    void response_cb(int response);

private:
    PrefsDialog& dialog_;

    Glib::RefPtr<Session> const core_;
    sigc::connection core_prefs_tag_;

    FreeSpaceLabel* freespace_label_ = nullptr;

#if 0
    Gtk::Label* port_label_ = nullptr;
    Gtk::Button* port_button_ = nullptr;
    Gtk::SpinButton* port_spin_ = nullptr;
#endif
};

/**
***
**/

void PrefsDialog::Impl::response_cb(int response)
{
    if (response == Gtk::RESPONSE_HELP)
    {
        gtr_open_uri(gtr_get_help_uri() + "/html/preferences.html");
    }

    if (response == Gtk::RESPONSE_CLOSE)
    {
        dialog_.hide();
    }
}

namespace
{

Gtk::CheckButton* new_check_button(Glib::ustring const& mnemonic, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    auto* w = Gtk::make_managed<Gtk::CheckButton>(mnemonic, true);
    w->set_active(gtr_pref_flag_get(key));
    w->signal_toggled().connect([w, key, core]() { core->set_pref(key, w->get_active()); });
    return w;
}

auto const IdleDataKey = Glib::Quark("idle-data");

bool spun_cb_idle(Gtk::SpinButton* spin, tr_quark const key, Glib::RefPtr<Session> const& core, bool isDouble)
{
    bool keep_waiting = true;

    /* has the user stopped making changes? */
    if (auto* last_change = static_cast<Glib::Timer*>(spin->get_data(IdleDataKey)); last_change->elapsed() > 0.33)
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

Gtk::SpinButton* new_spin_button(tr_quark const key, Glib::RefPtr<Session> const& core, int low, int high, int step)
{
    auto* w = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(gtr_pref_int_get(key), low, high, step));
    w->set_digits(0);
    w->signal_value_changed().connect([w, key, core]() { spun_cb(w, key, core, false); });
    return w;
}

Gtk::SpinButton* new_spin_button_double(
    tr_quark const key,
    Glib::RefPtr<Session> const& core,
    double low,
    double high,
    double step)
{
    auto* w = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(gtr_pref_double_get(key), low, high, step));
    w->set_digits(2);
    w->signal_value_changed().connect([w, key, core]() { spun_cb(w, key, core, true); });
    return w;
}

void entry_changed_cb(Gtk::Entry* w, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    core->set_pref(key, w->get_text());
}

Gtk::Entry* new_entry(tr_quark const key, Glib::RefPtr<Session> const& core)
{
    auto* w = Gtk::make_managed<Gtk::Entry>();

    if (auto const value = gtr_pref_string_get(key); !value.empty())
    {
        w->set_text(value);
    }

    w->signal_changed().connect([w, key, core]() { entry_changed_cb(w, key, core); });
    return w;
}

Gtk::Widget* new_text_view(tr_quark const key, Glib::RefPtr<Session> const& core)
{
    auto* w = Gtk::make_managed<Gtk::TextView>();
    auto buffer = w->get_buffer();

    buffer->set_text(gtr_pref_string_get(key));

    /* set up the scrolled window and put the text view in it */
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::POLICY_AUTOMATIC, Gtk::PolicyType::POLICY_AUTOMATIC);
    scroll->set_shadow_type(Gtk::ShadowType::SHADOW_IN);
    scroll->add(*w);
    scroll->set_size_request(-1, 166);

    /* signal */
    w->add_events(Gdk::FOCUS_CHANGE_MASK);
    w->signal_focus_out_event().connect(
        [buffer, key, core](GdkEventFocus*)
        {
            core->set_pref(key, buffer->get_text());
            return false;
        });

    return scroll;
}

void chosen_cb(Gtk::FileChooser* w, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    core->set_pref(key, w->get_filename());
}

Gtk::FileChooserButton* new_path_chooser_button(tr_quark const key, Glib::RefPtr<Session> const& core)
{
    auto* w = Gtk::make_managed<Gtk::FileChooserButton>(Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);

    if (auto const path = gtr_pref_string_get(key); !path.empty())
    {
        w->set_filename(path);
    }

    w->signal_selection_changed().connect([w, key, core]() { chosen_cb(w, key, core); });
    return w;
}

Gtk::FileChooserButton* new_file_chooser_button(tr_quark const key, Glib::RefPtr<Session> const& core)
{
    auto* w = Gtk::make_managed<Gtk::FileChooserButton>(Gtk::FILE_CHOOSER_ACTION_OPEN);

    if (auto const path = gtr_pref_string_get(key); !path.empty())
    {
        w->set_filename(path);
    }

    w->signal_selection_changed().connect([w, key, core]() { chosen_cb(w, key, core); });
    return w;
}

void target_cb(Gtk::ToggleButton* tb, Gtk::Widget* target)
{
    target->set_sensitive(tb->get_active());
}

} // namespace

/****
*****  Download Tab
****/

Gtk::Widget* PrefsDialog::Impl::downloadingPage()
{
    guint row = 0;

    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, C_("Gerund", "Adding"));

    {
        auto* l = new_check_button(_("Automatically add torrent files _from:"), TR_KEY_watch_dir_enabled, core_);
        auto* w = new_path_chooser_button(TR_KEY_watch_dir, core_);
        w->set_sensitive(gtr_pref_flag_get(TR_KEY_watch_dir_enabled));
        l->signal_toggled().connect([l, w]() { target_cb(l, w); });
        t->add_row_w(row, *l, *w);
    }

    t->add_wide_control(row, *new_check_button(_("Show the Torrent Options _dialog"), TR_KEY_show_options_window, core_));

    t->add_wide_control(row, *new_check_button(_("_Start added torrents"), TR_KEY_start_added_torrents, core_));

    t->add_wide_control(
        row,
        *new_check_button(_("Mo_ve torrent file to the trash"), TR_KEY_trash_original_torrent_files, core_));

    t->add_row(row, _("Save to _Location:"), *new_path_chooser_button(TR_KEY_download_dir, core_));

    freespace_label_ = Gtk::make_managed<FreeSpaceLabel>(core_);
    freespace_label_->set_halign(Gtk::ALIGN_END);
    freespace_label_->set_valign(Gtk::ALIGN_CENTER);
    t->add_wide_control(row, *freespace_label_);

    t->add_section_divider(row);
    t->add_section_title(row, _("Download Queue"));

    t->add_row(row, _("Ma_ximum active downloads:"), *new_spin_button(TR_KEY_download_queue_size, core_, 0, INT_MAX, 1));

    t->add_row(
        row,
        _("Downloads sharing data in the last _N minutes are active:"),
        *new_spin_button(TR_KEY_queue_stalled_minutes, core_, 1, INT_MAX, 15));

    t->add_section_divider(row);
    t->add_section_title(row, _("Incomplete"));

    t->add_wide_control(
        row,
        *new_check_button(_("Append \"._part\" to incomplete files' names"), TR_KEY_rename_partial_files, core_));

    {
        auto* l = new_check_button(_("Keep _incomplete torrents in:"), TR_KEY_incomplete_dir_enabled, core_);
        auto* w = new_path_chooser_button(TR_KEY_incomplete_dir, core_);
        w->set_sensitive(gtr_pref_flag_get(TR_KEY_incomplete_dir_enabled));
        l->signal_toggled().connect([l, w]() { target_cb(l, w); });
        t->add_row_w(row, *l, *w);
    }

    {
        auto* l = new_check_button(_("Call scrip_t when done downloading:"), TR_KEY_script_torrent_done_enabled, core_);
        auto* w = new_file_chooser_button(TR_KEY_script_torrent_done_filename, core_);
        w->set_sensitive(gtr_pref_flag_get(TR_KEY_script_torrent_done_enabled));
        l->signal_toggled().connect([l, w]() { target_cb(l, w); });
        t->add_row_w(row, *l, *w);
    }

    return t;
}

/****
*****  Torrent Tab
****/

Gtk::Widget* PrefsDialog::Impl::seedingPage()
{
    guint row = 0;

    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Limits"));

    {
        auto* w = new_check_button(_("Stop seeding at _ratio:"), TR_KEY_ratio_limit_enabled, core_);
        auto* w2 = new_spin_button_double(TR_KEY_ratio_limit, core_, 0, 1000, .05);
        w2->set_sensitive(gtr_pref_flag_get(TR_KEY_ratio_limit_enabled));
        w->signal_toggled().connect([w, w2]() { target_cb(w, w2); });
        t->add_row_w(row, *w, *w2);
    }

    {
        auto* w = new_check_button(_("Stop seeding if idle for _N minutes:"), TR_KEY_idle_seeding_limit_enabled, core_);
        auto* w2 = new_spin_button(TR_KEY_idle_seeding_limit, core_, 1, 40320, 5);
        w2->set_sensitive(gtr_pref_flag_get(TR_KEY_idle_seeding_limit_enabled));
        w->signal_toggled().connect([w, w2]() { target_cb(w, w2); });
        t->add_row_w(row, *w, *w2);
    }

    {
        auto* l = new_check_button(_("Call scrip_t when done seeding:"), TR_KEY_script_torrent_done_seeding_enabled, core_);
        auto* w = new_file_chooser_button(TR_KEY_script_torrent_done_seeding_filename, core_);
        w->set_sensitive(gtr_pref_flag_get(TR_KEY_script_torrent_done_seeding_enabled));
        l->signal_toggled().connect([l, w]() { target_cb(l, w); });
        t->add_row_w(row, *l, *w);
    }

    return t;
}

/****
*****  Desktop Tab
****/

Gtk::Widget* PrefsDialog::Impl::desktopPage()
{
    guint row = 0;

    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Desktop"));

    t->add_wide_control(
        row,
        *new_check_button(_("_Inhibit hibernation when torrents are active"), TR_KEY_inhibit_desktop_hibernation, core_));

    t->add_wide_control(
        row,
        *new_check_button(_("Show Transmission icon in the _notification area"), TR_KEY_show_notification_area_icon, core_));

    t->add_section_divider(row);
    t->add_section_title(row, _("Notification"));

    t->add_wide_control(
        row,
        *new_check_button(_("Show a notification when torrents are a_dded"), TR_KEY_torrent_added_notification_enabled, core_));

    t->add_wide_control(
        row,
        *new_check_button(_("Show a notification when torrents _finish"), TR_KEY_torrent_complete_notification_enabled, core_));

    t->add_wide_control(
        row,
        *new_check_button(_("Play a _sound when torrents finish"), TR_KEY_torrent_complete_sound_enabled, core_));

    return t;
}

/****
*****  Peer Tab
****/

namespace
{

struct blocklist_data
{
    blocklist_data() = default;
    ~blocklist_data();

    TR_DISABLE_COPY_MOVE(blocklist_data)

    sigc::connection updateBlocklistTag;
    Gtk::Button* updateBlocklistButton = nullptr;
    std::unique_ptr<Gtk::MessageDialog> updateBlocklistDialog;
    Gtk::Label* label = nullptr;
    Gtk::CheckButton* check = nullptr;
    Glib::RefPtr<Session> core;
};

void updateBlocklistText(Gtk::Label* w, Glib::RefPtr<Session> const& core)
{
    int const n = tr_blocklistGetRuleCount(core->get_session());
    auto const msg = fmt::format(
        ngettext("Blocklist has {count} entry", "Blocklist has {count} entries", n),
        fmt::arg("count", n));
    w->set_markup(fmt::format(FMT_STRING("<i>{:s}</i>"), msg));
}

/* prefs dialog is being destroyed, so stop listening to blocklist updates */
blocklist_data::~blocklist_data()
{
    if (updateBlocklistTag.connected())
    {
        updateBlocklistTag.disconnect();
    }
}

/* user hit "close" in the blocklist-update dialog */
void onBlocklistUpdateResponse(std::shared_ptr<blocklist_data> const& data)
{
    data->updateBlocklistButton->set_sensitive(true);
    data->updateBlocklistDialog.reset();
    data->updateBlocklistTag.disconnect();
}

/* core says the blocklist was updated */
void onBlocklistUpdated(Glib::RefPtr<Session> const& core, int n, blocklist_data* data)
{
    bool const success = n >= 0;
    int const count = n >= 0 ? n : tr_blocklistGetRuleCount(core->get_session());
    auto const msg = fmt::format(
        ngettext("Blocklist has {count} entry", "Blocklist has {count} entries", count),
        fmt::arg("count", count));
    data->updateBlocklistButton->set_sensitive(true);
    data->updateBlocklistDialog->set_message(
        fmt::format(FMT_STRING("<b>{:s}</b>"), success ? _("Blocklist updated!") : _("Couldn't update blocklist")),
        true);
    data->updateBlocklistDialog->set_secondary_text(msg);
    updateBlocklistText(data->label, core);
}

/* user pushed a button to update the blocklist */
void onBlocklistUpdate(Gtk::Button* w, std::shared_ptr<blocklist_data> const& data)
{
    data->updateBlocklistDialog = std::make_unique<Gtk::MessageDialog>(
        *static_cast<Gtk::Window*>(w->get_toplevel()),
        _("Update Blocklist"),
        false,
        Gtk::MESSAGE_INFO,
        Gtk::BUTTONS_CLOSE);
    data->updateBlocklistButton->set_sensitive(false);
    data->updateBlocklistDialog->set_secondary_text(_("Getting new blocklist…"));
    data->updateBlocklistDialog->signal_response().connect([data](int /*response*/) { onBlocklistUpdateResponse(data); });
    data->updateBlocklistDialog->show();
    data->core->blocklist_update();
    data->updateBlocklistTag = data->core->signal_blocklist_updated().connect(
        [data](auto n) { onBlocklistUpdated(data->core, n, data.get()); });
}

void on_blocklist_url_changed(Gtk::Editable* e, Gtk::Button* button)
{
    auto const url = e->get_chars(0, -1);
    button->set_sensitive(tr_urlIsValid(url.c_str()));
}

void onIntComboChanged(Gtk::ComboBox* combo_box, tr_quark const key, Glib::RefPtr<Session> const& core)
{
    core->set_pref(key, gtr_combo_box_get_active_enum(*combo_box));
}

Gtk::ComboBox* new_encryption_combo(Glib::RefPtr<Session> const& core, tr_quark const key)
{
    auto* w = gtr_combo_box_new_enum({
        { _("Allow encryption"), TR_CLEAR_PREFERRED },
        { _("Prefer encryption"), TR_ENCRYPTION_PREFERRED },
        { _("Require encryption"), TR_ENCRYPTION_REQUIRED },
    });
    gtr_combo_box_set_active_enum(*w, gtr_pref_int_get(key));
    w->signal_changed().connect([w, key, core]() { onIntComboChanged(w, key, core); });
    return w;
}

} // namespace

Gtk::Widget* PrefsDialog::Impl::privacyPage()
{
    guint row = 0;

    auto const data = std::make_shared<blocklist_data>();
    data->core = core_;

    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Privacy"));

    t->add_row(row, _("_Encryption mode:"), *new_encryption_combo(core_, TR_KEY_encryption));

    t->add_section_divider(row);
    t->add_section_title(row, _("Blocklist"));

    data->check = new_check_button(_("Enable _blocklist:"), TR_KEY_blocklist_enabled, core_);
    auto* e = new_entry(TR_KEY_blocklist_url, core_);
    e->set_size_request(300, -1);
    t->add_row_w(row, *data->check, *e);
    data->check->signal_toggled().connect([data, e]() { target_cb(data->check, e); });
    target_cb(data->check, e);

    data->label = Gtk::make_managed<Gtk::Label>();
    data->label->set_halign(Gtk::ALIGN_START);
    data->label->set_valign(Gtk::ALIGN_CENTER);
    updateBlocklistText(data->label, core_);
    auto* h = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD_BIG);
    h->pack_start(*data->label, true, true, 0);
    data->updateBlocklistButton = Gtk::make_managed<Gtk::Button>(_("_Update"), true);
    data->updateBlocklistButton->set_data("session", core_->get_session());
    data->updateBlocklistButton->signal_clicked().connect([data]() { onBlocklistUpdate(data->updateBlocklistButton, data); });
    target_cb(data->check, data->updateBlocklistButton);
    h->pack_start(*data->updateBlocklistButton, false, false, 0);
    data->check->signal_toggled().connect([data]() { target_cb(data->check, data->label); });
    target_cb(data->check, data->label);
    t->add_wide_control(row, *h);
    e->signal_changed().connect([data, e]() { on_blocklist_url_changed(e, data->updateBlocklistButton); });
    on_blocklist_url_changed(e, data->updateBlocklistButton);

    auto* update_check = new_check_button(_("Enable _automatic updates"), TR_KEY_blocklist_updates_enabled, core_);
    t->add_wide_control(row, *update_check);
    data->check->signal_toggled().connect([data, update_check]() { target_cb(data->check, update_check); });
    target_cb(data->check, update_check);

    return t;
}

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

struct remote_page
{
    Glib::RefPtr<Session> core;
    Gtk::TreeView* view;
    Glib::RefPtr<Gtk::ListStore> store;
    Gtk::Button* remove_button;
    std::vector<Gtk::Widget*> widgets;
    std::vector<Gtk::Widget*> auth_widgets;
    std::vector<Gtk::Widget*> whitelist_widgets;
    Gtk::ToggleButton* rpc_tb;
    Gtk::ToggleButton* auth_tb;
    Gtk::ToggleButton* whitelist_tb;
};

void refreshWhitelist(std::shared_ptr<remote_page> const& page)
{
    std::ostringstream gstr;

    for (auto const& row : page->store->children())
    {
        gstr << row.get_value(whitelist_cols.address) << ",";
    }

    auto str = gstr.str();
    if (!str.empty())
    {
        str.resize(str.size() - 1); /* remove the trailing comma */
    }

    page->core->set_pref(TR_KEY_rpc_whitelist, str);
}

void onAddressEdited(Glib::ustring const& path, Glib::ustring const& address, std::shared_ptr<remote_page> const& page)
{
    if (auto const iter = page->store->get_iter(path); iter)
    {
        (*iter)[whitelist_cols.address] = address;
    }

    refreshWhitelist(page);
}

void onAddWhitelistClicked(std::shared_ptr<remote_page> const& page)
{
    auto const iter = page->store->append();
    (*iter)[whitelist_cols.address] = "0.0.0.0";

    page->view->set_cursor(page->store->get_path(iter), *page->view->get_column(0), true);
}

void onRemoveWhitelistClicked(std::shared_ptr<remote_page> const& page)
{
    auto const sel = page->view->get_selection();

    if (auto const iter = sel->get_selected(); iter)
    {
        page->store->erase(iter);
        refreshWhitelist(page);
    }
}

void refreshRPCSensitivity(std::shared_ptr<remote_page> const& page)
{
    bool const rpc_active = page->rpc_tb->get_active();
    bool const auth_active = page->auth_tb->get_active();
    bool const whitelist_active = page->whitelist_tb->get_active();
    auto const sel = page->view->get_selection();
    auto const have_addr = sel->get_selected();
    auto const n_rules = page->store->children().size();

    for (auto* const widget : page->widgets)
    {
        widget->set_sensitive(rpc_active);
    }

    for (auto* const widget : page->auth_widgets)
    {
        widget->set_sensitive(rpc_active && auth_active);
    }

    for (auto* const widget : page->whitelist_widgets)
    {
        widget->set_sensitive(rpc_active && whitelist_active);
    }

    page->remove_button->set_sensitive(rpc_active && whitelist_active && have_addr && n_rules > 1);
}

void onLaunchClutchCB()
{
    gtr_open_uri(gtr_sprintf("http://localhost:%d/", (int)gtr_pref_int_get(TR_KEY_rpc_port)));
}

} // namespace

Gtk::Widget* PrefsDialog::Impl::remotePage()
{
    guint row = 0;
    auto const page = std::make_shared<remote_page>();

    page->core = core_;

    auto* t = Gtk::make_managed<HigWorkarea>();

    t->add_section_title(row, _("Remote Control"));

    /* "enabled" checkbutton */
    page->rpc_tb = new_check_button(_("Allow _remote access"), TR_KEY_rpc_enabled, core_);
    page->rpc_tb->signal_clicked().connect([page]() { refreshRPCSensitivity(page); });
    auto* h1 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD_BIG);
    h1->pack_start(*page->rpc_tb, true, true, 0);
    auto* open_button = Gtk::make_managed<Gtk::Button>(_("_Open web client"), true);
    page->widgets.push_back(open_button);
    open_button->signal_clicked().connect(&onLaunchClutchCB);
    h1->pack_start(*open_button, false, false, 0);
    t->add_wide_control(row, *h1);

    /* port */
    auto* port_spin = new_spin_button(TR_KEY_rpc_port, core_, 0, USHRT_MAX, 1);
    page->widgets.push_back(port_spin);
    page->widgets.push_back(t->add_row(row, _("HTTP _port:"), *port_spin));

    /* require authentication */
    page->auth_tb = new_check_button(_("Use _authentication"), TR_KEY_rpc_authentication_required, core_);
    t->add_wide_control(row, *page->auth_tb);
    page->widgets.push_back(page->auth_tb);
    page->auth_tb->signal_clicked().connect([page]() { refreshRPCSensitivity(page); });

    /* username */
    auto* username_entry = new_entry(TR_KEY_rpc_username, core_);
    page->auth_widgets.push_back(username_entry);
    page->auth_widgets.push_back(t->add_row(row, _("_Username:"), *username_entry));

    /* password */
    auto* password_entry = new_entry(TR_KEY_rpc_password, core_);
    password_entry->set_visibility(false);
    page->auth_widgets.push_back(password_entry);
    page->auth_widgets.push_back(t->add_row(row, _("Pass_word:"), *password_entry));

    /* require authentication */
    page->whitelist_tb = new_check_button(_("Only allow these IP a_ddresses:"), TR_KEY_rpc_whitelist_enabled, core_);
    t->add_wide_control(row, *page->whitelist_tb);
    page->widgets.push_back(page->whitelist_tb);
    page->whitelist_tb->signal_clicked().connect([page]() { refreshRPCSensitivity(page); });

    /* access control list */
    {
        page->store = whitelist_tree_model_new(gtr_pref_string_get(TR_KEY_rpc_whitelist));

        page->view = Gtk::make_managed<Gtk::TreeView>(page->store);
        page->view->signal_button_release_event().connect([page](GdkEventButton* event)
                                                          { return on_tree_view_button_released(page->view, event); });

        page->whitelist_widgets.push_back(page->view);
        page->view->set_tooltip_text(_("IP addresses may use wildcards, such as 192.168.*.*"));
        auto const sel = page->view->get_selection();
        sel->signal_changed().connect([page]() { refreshRPCSensitivity(page); });
        page->view->set_headers_visible(true);
        auto* view_frame = Gtk::make_managed<Gtk::Frame>();
        view_frame->set_shadow_type(Gtk::SHADOW_IN);
        view_frame->add(*page->view);

        /* ip address column */
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->signal_edited().connect([page](auto const& path, auto const& new_text) { onAddressEdited(path, new_text, page); });
        r->property_editable() = true;
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>("", *r);
        c->add_attribute(r->property_text(), whitelist_cols.address);
        c->set_expand(true);
        page->view->append_column(*c);
        page->view->set_headers_visible(false);

        auto* w = t->add_row(row, _("Addresses:"), *view_frame);
        w->set_halign(Gtk::ALIGN_START);
        w->set_valign(Gtk::ALIGN_START);
        w->set_margin_top(GUI_PAD);
        w->set_margin_bottom(GUI_PAD);
        page->whitelist_widgets.push_back(w);

        auto* h2 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD);
        page->remove_button = Gtk::make_managed<Gtk::Button>(_("_Remove"), true);
        page->remove_button->signal_clicked().connect([page]() { onRemoveWhitelistClicked(page); });
        refreshRPCSensitivity(page);
        h2->pack_start(*page->remove_button, true, true, 0);
        auto* add_button = Gtk::make_managed<Gtk::Button>(_("_Add"), true);
        page->whitelist_widgets.push_back(add_button);
        add_button->signal_clicked().connect([page]() { onAddWhitelistClicked(page); });
        h2->set_halign(Gtk::ALIGN_END);
        h2->set_valign(Gtk::ALIGN_CENTER);
        h2->pack_start(*add_button, true, true, 0);
        t->add_wide_control(row, *h2);
    }

    refreshRPCSensitivity(page);
    return t;
}

/****
*****  Bandwidth Tab
****/

namespace
{

struct BandwidthPage
{
    std::vector<Gtk::Widget*> sched_widgets;
};

void refreshSchedSensitivity(std::shared_ptr<BandwidthPage> const& p)
{
    bool const sched_enabled = gtr_pref_flag_get(TR_KEY_alt_speed_time_enabled);

    for (auto* const w : p->sched_widgets)
    {
        w->set_sensitive(sched_enabled);
    }
}

Gtk::ComboBox* new_time_combo(Glib::RefPtr<Session> const& core, tr_quark const key)
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
        (*iter)[time_cols.title] = gtr_sprintf("%02d:%02d", i / 60, i % 60);
    }

    /* build the widget */
    auto* w = Gtk::make_managed<Gtk::ComboBox>(static_cast<Glib::RefPtr<Gtk::TreeModel> const&>(store));
    w->set_wrap_width(4);
    auto* r = Gtk::make_managed<Gtk::CellRendererText>();
    w->pack_start(*r, true);
    w->add_attribute(r->property_text(), time_cols.title);
    w->set_active(gtr_pref_int_get(key) / 15);
    w->signal_changed().connect(
        [w, key, core]()
        {
            if (auto const iter = w->get_active(); iter)
            {
                core->set_pref(key, iter->get_value(time_cols.offset));
            }
        });

    return w;
}

auto get_weekday_string(Glib::Date::Weekday weekday)
{
    auto date = Glib::Date{};
    date.set_time_current();
    date.add_days(weekday - date.get_weekday());
    return date.format_string("%A");
}

Gtk::ComboBox* new_week_combo(Glib::RefPtr<Session> const& core, tr_quark const key)
{
    auto* w = gtr_combo_box_new_enum({
        { _("Every Day"), TR_SCHED_ALL },
        { _("Weekdays"), TR_SCHED_WEEKDAY },
        { _("Weekends"), TR_SCHED_WEEKEND },
        { get_weekday_string(Glib::Date::MONDAY), TR_SCHED_MON },
        { get_weekday_string(Glib::Date::TUESDAY), TR_SCHED_TUES },
        { get_weekday_string(Glib::Date::WEDNESDAY), TR_SCHED_WED },
        { get_weekday_string(Glib::Date::THURSDAY), TR_SCHED_THURS },
        { get_weekday_string(Glib::Date::FRIDAY), TR_SCHED_FRI },
        { get_weekday_string(Glib::Date::SATURDAY), TR_SCHED_SAT },
        { get_weekday_string(Glib::Date::SUNDAY), TR_SCHED_SUN },
    });
    gtr_combo_box_set_active_enum(*w, gtr_pref_int_get(key));
    w->signal_changed().connect([w, key, core]() { onIntComboChanged(w, key, core); });
    return w;
}

} // namespace

Gtk::Widget* PrefsDialog::Impl::speedPage()
{
    guint row = 0;
    auto page = std::make_shared<BandwidthPage>();

    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Speed Limits"));

    {
        auto* w = new_check_button(
            // checkbox to limit upload speed
            fmt::format(_("_Upload ({speed_units}):"), fmt::arg("speed_units", speed_K_str)),
            TR_KEY_speed_limit_up_enabled,
            core_);
        auto* w2 = new_spin_button(TR_KEY_speed_limit_up, core_, 0, INT_MAX, 5);
        w2->set_sensitive(gtr_pref_flag_get(TR_KEY_speed_limit_up_enabled));
        w->signal_toggled().connect([w, w2]() { target_cb(w, w2); });
        t->add_row_w(row, *w, *w2);
    }

    {
        auto* w = new_check_button(
            // checkbox to limit download speed
            fmt::format(_("_Download ({speed_units}):"), fmt::arg("speed_units", speed_K_str)),
            TR_KEY_speed_limit_down_enabled,
            core_);
        auto* w2 = new_spin_button(TR_KEY_speed_limit_down, core_, 0, INT_MAX, 5);
        w2->set_sensitive(gtr_pref_flag_get(TR_KEY_speed_limit_down_enabled));
        w->signal_toggled().connect([w, w2]() { target_cb(w, w2); });
        t->add_row_w(row, *w, *w2);
    }

    t->add_section_divider(row);

    {
        auto* h = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD);
        auto* w = Gtk::make_managed<Gtk::Label>(fmt::format(FMT_STRING("<b>{:s}</b>"), _("Alternative Speed Limits")));
        w->set_halign(Gtk::ALIGN_START);
        w->set_valign(Gtk::ALIGN_CENTER);
        w->set_use_markup(true);
        h->pack_start(*w, false, false, 0);
        h->pack_start(*Gtk::make_managed<Gtk::Image>("alt-speed-on", Gtk::ICON_SIZE_MENU), false, false, 0);
        t->add_section_title_widget(row, *h);
    }

    {
        auto* w = Gtk::make_managed<Gtk::Label>(
            fmt::format(FMT_STRING("<small>{:s}</small>"), _("Override normal speed limits manually or at scheduled times")));
        w->set_use_markup(true);
        w->set_halign(Gtk::ALIGN_START);
        w->set_valign(Gtk::ALIGN_CENTER);
        t->add_wide_control(row, *w);
    }

    t->add_row(
        row,
        // labels a spinbutton for alternate upload speed limits
        fmt::format(_("U_pload ({speed_units}):"), fmt::arg("speed_units", speed_K_str)),
        *new_spin_button(TR_KEY_alt_speed_up, core_, 0, INT_MAX, 5));

    t->add_row(
        row,
        // labels a spinbutton for alternate download speed limits
        fmt::format(_("Do_wnload ({speed_units}):"), fmt::arg("speed_units", speed_K_str)),
        *new_spin_button(TR_KEY_alt_speed_down, core_, 0, INT_MAX, 5));

    {
        auto* h = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
        auto* start_combo = new_time_combo(core_, TR_KEY_alt_speed_time_begin);
        page->sched_widgets.push_back(start_combo);
        h->pack_start(*start_combo, true, true, 0);
        // label goes between two time selectors, e.g. "limit speeds from [time] to [time]"
        auto* to_label = Gtk::make_managed<Gtk::Label>(_(" _to "), true);
        page->sched_widgets.push_back(to_label);
        h->pack_start(*to_label, false, false, 0);
        auto* end_combo = new_time_combo(core_, TR_KEY_alt_speed_time_end);
        to_label->set_mnemonic_widget(*end_combo);
        page->sched_widgets.push_back(end_combo);
        h->pack_start(*end_combo, true, true, 0);
        auto* w = new_check_button(_("_Scheduled times:"), TR_KEY_alt_speed_time_enabled, core_);
        w->signal_toggled().connect([page]() { refreshSchedSensitivity(page); });
        t->add_row_w(row, *w, *h);
    }

    auto* week_combo = new_week_combo(core_, TR_KEY_alt_speed_time_day);
    page->sched_widgets.push_back(week_combo);
    page->sched_widgets.push_back(t->add_row(row, _("_On days:"), *week_combo));

    refreshSchedSensitivity(page);
    return t;
}

/****
*****  Network Tab
****/

namespace
{

struct network_page_data
{
    network_page_data() = default;
    ~network_page_data();

    TR_DISABLE_COPY_MOVE(network_page_data)

    Glib::RefPtr<Session> core;
    Gtk::Label* portLabel = nullptr;
    Gtk::Button* portButton = nullptr;
    Gtk::SpinButton* portSpin = nullptr;
    sigc::connection portTag;
    sigc::connection prefsTag;
};

void onCorePrefsChanged(tr_quark const key, network_page_data* data)
{
    if (key == TR_KEY_peer_port)
    {
        gtr_label_set_text(*data->portLabel, _("Status unknown"));
        data->portButton->set_sensitive(true);
        data->portSpin->set_sensitive(true);
    }
}

network_page_data::~network_page_data()
{
    if (prefsTag.connected())
    {
        prefsTag.disconnect();
    }

    if (portTag.connected())
    {
        portTag.disconnect();
    }
}

void onPortTested(bool isOpen, network_page_data* data)
{
    data->portLabel->set_markup(fmt::format(
        isOpen ? _("Port is {markup_begin}open{markup_end}") : _("Port is {markup_begin}closed{markup_end}"),
        fmt::arg("markup_begin", "<b>"),
        fmt::arg("markup_end", "</b>")));
    data->portButton->set_sensitive(true);
    data->portSpin->set_sensitive(true);
}

void onPortTest(std::shared_ptr<network_page_data> const& data)
{
    data->portButton->set_sensitive(false);
    data->portSpin->set_sensitive(false);
    data->portLabel->set_markup(fmt::format(FMT_STRING("<i>{:s}</i>"), _("Testing TCP port…")));

    if (!data->portTag.connected())
    {
        data->portTag = data->core->signal_port_tested().connect([data](bool is_open) { onPortTested(is_open, data.get()); });
    }

    data->core->port_test();
}

} // namespace

Gtk::Widget* PrefsDialog::Impl::networkPage()
{
    guint row = 0;

    /* register to stop listening to core prefs changes when the page is destroyed */
    auto const data = std::make_shared<network_page_data>();
    data->core = core_;

    /* build the page */
    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Listening Port"));

    data->portSpin = new_spin_button(TR_KEY_peer_port, core_, 1, USHRT_MAX, 1);
    t->add_row(row, _("_Port used for incoming connections:"), *data->portSpin);

    auto* h = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD_BIG);
    data->portLabel = Gtk::make_managed<Gtk::Label>(_("Status unknown"));
    data->portLabel->set_halign(Gtk::ALIGN_START);
    data->portLabel->set_valign(Gtk::ALIGN_CENTER);
    h->pack_start(*data->portLabel, true, true, 0);
    data->portButton = Gtk::make_managed<Gtk::Button>(_("Te_st Port"), true);
    h->pack_end(*data->portButton, false, false, 0);
    data->portButton->signal_clicked().connect([data]() { onPortTest(data); });
    t->add_row(row, {}, *h);
    data->prefsTag = core_->signal_prefs_changed().connect([data](auto key) { onCorePrefsChanged(key, data.get()); });

    t->add_wide_control(
        row,
        *new_check_button(
            _("Pick a _random port every time Transmission is started"),
            TR_KEY_peer_port_random_on_start,
            core_));

    t->add_wide_control(
        row,
        *new_check_button(_("Use UPnP or NAT-PMP port _forwarding from my router"), TR_KEY_port_forwarding_enabled, core_));

    t->add_section_divider(row);
    t->add_section_title(row, _("Peer Limits"));

    t->add_row(row, _("Maximum peers per _torrent:"), *new_spin_button(TR_KEY_peer_limit_per_torrent, core_, 1, FD_SETSIZE, 5));
    t->add_row(row, _("Maximum peers _overall:"), *new_spin_button(TR_KEY_peer_limit_global, core_, 1, FD_SETSIZE, 5));

    t->add_section_divider(row);
    t->add_section_title(row, _("Options"));

    Gtk::CheckButton* w;

#ifdef WITH_UTP
    w = new_check_button(_("Enable _uTP for peer communication"), TR_KEY_utp_enabled, core_);
    w->set_tooltip_text(_("uTP is a tool for reducing network congestion."));
    t->add_wide_control(row, *w);
#endif

    w = new_check_button(_("Use PE_X to find more peers"), TR_KEY_pex_enabled, core_);
    w->set_tooltip_text(_("PEX is a tool for exchanging peer lists with the peers you're connected to."));
    t->add_wide_control(row, *w);

    w = new_check_button(_("Use _DHT to find more peers"), TR_KEY_dht_enabled, core_);
    w->set_tooltip_text(_("DHT is a tool for finding peers without a tracker."));
    t->add_wide_control(row, *w);

    w = new_check_button(_("Use _Local Peer Discovery to find more peers"), TR_KEY_lpd_enabled, core_);
    w->set_tooltip_text(_("LPD is a tool for finding peers on your local network."));
    t->add_wide_control(row, *w);

    t->add_section_divider(row);
    t->add_section_title(row, _("Default Public Trackers"));

    auto tv = new_text_view(TR_KEY_default_trackers, core_);
    tv->set_tooltip_text(
        _("Trackers to use on all public torrents.\n\n"
          "To add a backup URL, add it on the next line after a primary URL.\n"
          "To add a new primary URL, add it after a blank line."));
    t->add_wide_control(row, *tv);

    return t;
}

/****
*****
****/

PrefsDialog::Impl::~Impl()
{
    if (core_prefs_tag_.connected())
    {
        core_prefs_tag_.disconnect();
    }
}

void PrefsDialog::Impl::on_core_prefs_changed(tr_quark const key)
{
#if 0

    if (key == TR_KEY_peer_port)
    {
        port_label_->set_text(_("Status unknown"));
        port_button_->set_sensitive(true);
        port_spin_->set_sensitive(true);
    }

#endif

    if (key == TR_KEY_download_dir)
    {
        char const* downloadDir = tr_sessionGetDownloadDir(core_->get_session());
        freespace_label_->set_dir(downloadDir);
    }
}

std::unique_ptr<PrefsDialog> PrefsDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    return std::unique_ptr<PrefsDialog>(new PrefsDialog(parent, core));
}

PrefsDialog::PrefsDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(_("Transmission Preferences"), parent)
    , impl_(std::make_unique<Impl>(*this, core))
{
    set_modal(true);
}

PrefsDialog::~PrefsDialog() = default;

PrefsDialog::Impl::Impl(PrefsDialog& dialog, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
{
    static tr_quark const prefs_quarks[] = { TR_KEY_peer_port, TR_KEY_download_dir };

    core_prefs_tag_ = core_->signal_prefs_changed().connect(sigc::mem_fun(*this, &Impl::on_core_prefs_changed));

    dialog_.add_button(_("_Help"), Gtk::RESPONSE_HELP);
    dialog_.add_button(_("_Close"), Gtk::RESPONSE_CLOSE);
    dialog_.set_role("transmission-preferences-dialog");
    dialog_.set_border_width(GUI_PAD);

    auto* n = Gtk::make_managed<Gtk::Notebook>();
    n->set_border_width(GUI_PAD);

    n->append_page(*speedPage(), _("Speed"));
    n->append_page(*downloadingPage(), C_("Gerund", "Downloading"));
    n->append_page(*seedingPage(), C_("Gerund", "Seeding"));
    n->append_page(*privacyPage(), _("Privacy"));
    n->append_page(*networkPage(), _("Network"));
    n->append_page(*desktopPage(), _("Desktop"));
    n->append_page(*remotePage(), _("Remote"));

    /* init from prefs keys */
    for (auto const key : prefs_quarks)
    {
        on_core_prefs_changed(key);
    }

    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::response_cb));
    gtr_dialog_set_content(dialog_, *n);
}
