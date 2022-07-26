// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <string>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <fmt/core.h>

#include <libtransmission/transmission.h>

#include "HigWorkarea.h"
#include "Prefs.h" /* gtr_pref_string_get */
#include "RelocateDialog.h"
#include "Session.h"
#include "Utils.h"

namespace
{

std::string previousLocation;

}

class RelocateDialog::Impl
{
public:
    Impl(RelocateDialog& dialog, Glib::RefPtr<Session> const& core, std::vector<tr_torrent_id_t> const& torrent_ids);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

private:
    void onResponse(int response);
    bool onTimer();

    void startMovingNextTorrent();

private:
    RelocateDialog& dialog_;
    Glib::RefPtr<Session> const core_;
    std::vector<tr_torrent_id_t> torrent_ids_;

    int done_ = 0;
    bool do_move_ = false;
    sigc::connection timer_;
    std::unique_ptr<Gtk::MessageDialog> message_dialog_;
    Gtk::FileChooserButton* chooser_ = nullptr;
    Gtk::RadioButton* move_tb_ = nullptr;
};

RelocateDialog::Impl::~Impl()
{
    timer_.disconnect();
}

/***
****
***/

void RelocateDialog::Impl::startMovingNextTorrent()
{
    auto* const tor = core_->find_torrent(torrent_ids_.back());

    if (tor != nullptr)
    {
        tr_torrentSetLocation(tor, previousLocation.c_str(), do_move_, nullptr, &done_);
    }

    torrent_ids_.pop_back();

    message_dialog_->set_message(
        fmt::format(_("Moving '{torrent_name}'"), fmt::arg("torrent_name", tr_torrentName(tor))),
        true);
}

/* every once in awhile, check to see if the move is done.
 * if so, delete the dialog */
bool RelocateDialog::Impl::onTimer()
{
    if (done_ == TR_LOC_ERROR)
    {
        Gtk::MessageDialog(*message_dialog_, _("Couldn't move torrent"), false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_CLOSE, true)
            .run();
        message_dialog_.reset();
    }
    else if (done_ == TR_LOC_DONE)
    {
        if (!torrent_ids_.empty())
        {
            startMovingNextTorrent();
        }
        else
        {
            dialog_.hide();
        }
    }

    return G_SOURCE_CONTINUE;
}

void RelocateDialog::Impl::onResponse(int response)
{
    if (response == Gtk::RESPONSE_APPLY)
    {
        auto const location = chooser_->get_filename();

        do_move_ = move_tb_->get_active();

        /* pop up a dialog saying that the work is in progress */
        message_dialog_ = std::make_unique<Gtk::MessageDialog>(
            dialog_,
            Glib::ustring(),
            false,
            Gtk::MESSAGE_INFO,
            Gtk::BUTTONS_CLOSE,
            true);
        message_dialog_->set_secondary_text(_("This may take a moment…"));
        message_dialog_->set_response_sensitive(Gtk::RESPONSE_CLOSE, false);
        message_dialog_->show();

        /* remember this location so that it can be the default next time */
        previousLocation = location;

        /* start the move and periodically check its status */
        done_ = TR_LOC_DONE;
        timer_ = Glib::signal_timeout().connect_seconds(sigc::mem_fun(*this, &Impl::onTimer), 1);
        onTimer();
    }
    else
    {
        dialog_.hide();
    }
}

std::unique_ptr<RelocateDialog> RelocateDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
{
    return std::unique_ptr<RelocateDialog>(new RelocateDialog(parent, core, torrent_ids));
}

RelocateDialog::RelocateDialog(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
    : Gtk::Dialog(_("Set Torrent Location"), parent, true)
    , impl_(std::make_unique<Impl>(*this, core, torrent_ids))
{
}

RelocateDialog::~RelocateDialog() = default;

RelocateDialog::Impl::Impl(
    RelocateDialog& dialog,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids)
    : dialog_(dialog)
    , core_(core)
    , torrent_ids_(torrent_ids)
{
    guint row;

    dialog_.add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
    dialog_.add_button(_("_Apply"), Gtk::RESPONSE_APPLY);
    dialog_.set_default_response(Gtk::RESPONSE_CANCEL);
    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::onResponse));

    row = 0;
    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Location"));

    if (previousLocation.empty())
    {
        previousLocation = gtr_pref_string_get(TR_KEY_download_dir);
    }

    chooser_ = Gtk::make_managed<Gtk::FileChooserButton>(_("Set Torrent Location"), Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    chooser_->set_current_folder(previousLocation);
    t->add_row(row, _("Torrent _location:"), *chooser_);

    Gtk::RadioButton::Group group;

    move_tb_ = Gtk::make_managed<Gtk::RadioButton>(group, _("_Move from the current folder"), true);
    t->add_wide_control(row, *move_tb_);

    t->add_wide_control(row, *Gtk::make_managed<Gtk::RadioButton>(group, _("Local data is _already there"), true));

    gtr_dialog_set_content(dialog_, *t);
}
