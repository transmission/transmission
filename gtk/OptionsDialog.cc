// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/file.h> /* tr_sys_path_is_same() */

#include "FileList.h"
#include "FreeSpaceLabel.h"
#include "HigWorkarea.h"
#include "OptionsDialog.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h" /* gtr_priority_combo_get_value() */

/****
*****
****/

namespace
{

auto const MaxRecentDestinations = size_t{ 4 };

std::list<std::string> get_recent_destinations()
{
    std::list<std::string> list;

    for (size_t i = 0; i < MaxRecentDestinations; ++i)
    {
        auto const key = gtr_sprintf("recent-download-dir-%d", i + 1);

        if (auto const val = gtr_pref_string_get(tr_quark_new({ key.c_str(), key.size() })); !val.empty())
        {
            list.push_back(val);
        }
    }

    return list;
}

void save_recent_destination(Glib::RefPtr<Session> const& core, std::string const& dir)
{
    if (dir.empty())
    {
        return;
    }

    auto list = get_recent_destinations();

    /* if it was already in the list, remove it */
    list.remove(dir);

    /* add it to the front of the list */
    list.push_front(dir);

    /* save the first MaxRecentDestinations directories */
    list.resize(MaxRecentDestinations);
    int i = 0;
    for (auto const& d : list)
    {
        auto const key = gtr_sprintf("recent-download-dir-%d", ++i);
        gtr_pref_string_set(tr_quark_new({ key.c_str(), key.size() }), d);
    }

    gtr_pref_save(core->get_session());
}

} // namespace

/****
*****
****/

class OptionsDialog::Impl
{
public:
    Impl(OptionsDialog& dialog, Glib::RefPtr<Session> const& core, std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor);

    TR_DISABLE_COPY_MOVE(Impl)

private:
    void sourceChanged(Gtk::FileChooserButton* b);
    void downloadDirChanged(Gtk::FileChooserButton* b);

    void removeOldTorrent();
    void updateTorrent();

    void addResponseCB(int response);

private:
    OptionsDialog& dialog_;

    Glib::RefPtr<Session> const core_;
    FileList* file_list_ = nullptr;
    Gtk::CheckButton* run_check_ = nullptr;
    Gtk::CheckButton* trash_check_ = nullptr;
    Gtk::ComboBox* priority_combo_ = nullptr;
    FreeSpaceLabel* freespace_label_ = nullptr;
    std::string filename_;
    std::string downloadDir_;
    tr_torrent* tor_ = nullptr;
    std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor_;
};

void OptionsDialog::Impl::removeOldTorrent()
{
    if (tor_ != nullptr)
    {
        file_list_->clear();
        tr_torrentRemove(tor_, false, nullptr);
        tor_ = nullptr;
    }
}

void OptionsDialog::Impl::addResponseCB(int response)
{
    if (tor_ != nullptr)
    {
        if (response != Gtk::RESPONSE_ACCEPT)
        {
            removeOldTorrent();
        }
        else
        {
            tr_torrentSetPriority(tor_, gtr_priority_combo_get_value(*priority_combo_));

            if (run_check_->get_active())
            {
                tr_torrentStart(tor_);
            }

            core_->add_torrent(tor_, false);

            if (trash_check_->get_active())
            {
                gtr_file_trash_or_remove(filename_, nullptr);
            }

            save_recent_destination(core_, downloadDir_);
        }
    }

    dialog_.hide();
}

void OptionsDialog::Impl::updateTorrent()
{
    bool const isLocalFile = tr_ctorGetSourceFile(ctor_.get()) != nullptr;
    trash_check_->set_sensitive(isLocalFile);

    if (tor_ == nullptr)
    {
        file_list_->clear();
        file_list_->set_sensitive(false);
    }
    else
    {
        tr_torrentSetDownloadDir(tor_, downloadDir_.c_str());
        file_list_->set_sensitive(tr_torrentHasMetadata(tor_));
        file_list_->set_torrent(tr_torrentId(tor_));
        tr_torrentVerify(tor_);
    }
}

/**
 * When the source torrent file is deleted
 * (such as, if it was a temp file that a web browser passed to us),
 * gtk invokes this callback and `filename' will be nullptr.
 * The `filename' tests here are to prevent us from losing the current
 * metadata when that happens.
 */
void OptionsDialog::Impl::sourceChanged(Gtk::FileChooserButton* b)
{
    auto const filename = b->get_filename();

    /* maybe instantiate a torrent */
    if (!filename.empty() || tor_ == nullptr)
    {
        bool new_file = false;

        if (!filename.empty() && (filename_.empty() || !tr_sys_path_is_same(filename, filename_)))
        {
            filename_ = filename;
            tr_ctorSetMetainfoFromFile(ctor_.get(), filename_.c_str(), nullptr);
            new_file = true;
        }

        tr_ctorSetDownloadDir(ctor_.get(), TR_FORCE, downloadDir_.c_str());
        tr_ctorSetPaused(ctor_.get(), TR_FORCE, true);
        tr_ctorSetDeleteSource(ctor_.get(), false);

        tr_torrent* duplicate_of = nullptr;
        if (tr_torrent* const torrent = tr_torrentNew(ctor_.get(), &duplicate_of); torrent != nullptr)
        {
            removeOldTorrent();
            tor_ = torrent;
        }
        else if (new_file)
        {
            gtr_add_torrent_error_dialog(*b, duplicate_of, filename_);
        }

        updateTorrent();
    }
}

void OptionsDialog::Impl::downloadDirChanged(Gtk::FileChooserButton* b)
{
    auto const fname = b->get_filename();

    if (!fname.empty() && (downloadDir_.empty() || !tr_sys_path_is_same(fname, downloadDir_)))
    {
        downloadDir_ = fname;
        updateTorrent();

        freespace_label_->set_dir(downloadDir_);
    }
}

namespace
{

void addTorrentFilters(Gtk::FileChooser* chooser)
{
    auto filter = Gtk::FileFilter::create();
    filter->set_name(_("Torrent files"));
    filter->add_pattern("*.torrent");
    chooser->add_filter(filter);

    filter = Gtk::FileFilter::create();
    filter->set_name(_("All files"));
    filter->add_pattern("*");
    chooser->add_filter(filter);
}

} // namespace

/****
*****
****/

std::unique_ptr<OptionsDialog> OptionsDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor)
{
    return std::unique_ptr<OptionsDialog>(new OptionsDialog(parent, core, std::move(ctor)));
}

OptionsDialog::OptionsDialog(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor)
    : Gtk::Dialog(_("Torrent Options"), parent, true /* modal */)
    , impl_(std::make_unique<Impl>(*this, core, std::move(ctor)))
{
}

OptionsDialog::~OptionsDialog() = default;

OptionsDialog::Impl::Impl(
    OptionsDialog& dialog,
    Glib::RefPtr<Session> const& core,
    std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor)
    : dialog_(dialog)
    , core_(core)
    , ctor_(std::move(ctor))
{
    int row = 0;

    /* make the dialog */
    dialog_.add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
    dialog_.add_button(_("_Open"), Gtk::RESPONSE_ACCEPT);
    dialog_.set_default_response(Gtk::RESPONSE_ACCEPT);

    char const* str = nullptr;
    if (!tr_ctorGetDownloadDir(ctor_.get(), TR_FORCE, &str))
    {
        g_assert_not_reached();
    }

    g_assert(str != nullptr);

    filename_ = tr_ctorGetSourceFile(ctor_.get()) != nullptr ? tr_ctorGetSourceFile(ctor_.get()) : "";
    downloadDir_ = str;
    file_list_ = Gtk::make_managed<FileList>(core_, 0);
    trash_check_ = Gtk::make_managed<Gtk::CheckButton>(_("Mo_ve torrent file to the trash"), true);
    run_check_ = Gtk::make_managed<Gtk::CheckButton>(_("_Start when added"), true);

    priority_combo_ = gtr_priority_combo_new();
    gtr_priority_combo_set_value(*priority_combo_, TR_PRI_NORMAL);

    dialog.signal_response().connect(sigc::mem_fun(*this, &Impl::addResponseCB));

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_border_width(GUI_PAD_BIG);
    grid->set_row_spacing(GUI_PAD);
    grid->set_column_spacing(GUI_PAD_BIG);

    /* "torrent file" row */
    auto* source_label = Gtk::make_managed<Gtk::Label>(_("_Torrent file:"), true);
    source_label->set_halign(Gtk::ALIGN_START);
    source_label->set_halign(Gtk::ALIGN_CENTER);
    grid->attach(*source_label, 0, row, 1, 1);
    auto* source_chooser = Gtk::make_managed<Gtk::FileChooserButton>(_("Select Source File"), Gtk::FILE_CHOOSER_ACTION_OPEN);
    source_chooser->set_hexpand(true);
    grid->attach_next_to(*source_chooser, *source_label, Gtk::POS_RIGHT);
    source_label->set_mnemonic_widget(*source_chooser);
    addTorrentFilters(source_chooser);
    source_chooser->signal_selection_changed().connect([this, source_chooser]() { sourceChanged(source_chooser); });

    /* "destination folder" row */
    row++;
    auto* destination_label = Gtk::make_managed<Gtk::Label>(_("_Destination folder:"), true);
    destination_label->set_halign(Gtk::ALIGN_START);
    destination_label->set_valign(Gtk::ALIGN_CENTER);
    grid->attach(*destination_label, 0, row, 1, 1);
    auto* destination_chooser = Gtk::make_managed<Gtk::FileChooserButton>(
        _("Select Destination Folder"),
        Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);

    if (!destination_chooser->set_current_folder(downloadDir_))
    {
        g_warning("couldn't select '%s'", downloadDir_.c_str());
    }

    for (auto const& folder : get_recent_destinations())
    {
        destination_chooser->add_shortcut_folder(folder);
    }

    grid->attach_next_to(*destination_chooser, *destination_label, Gtk::POS_RIGHT);
    destination_label->set_mnemonic_widget(*destination_chooser);
    destination_chooser->signal_selection_changed().connect([this, destination_chooser]()
                                                            { downloadDirChanged(destination_chooser); });

    row++;
    freespace_label_ = Gtk::make_managed<FreeSpaceLabel>(core_, downloadDir_);
    freespace_label_->set_margin_bottom(GUI_PAD_BIG);
    freespace_label_->set_halign(Gtk::ALIGN_END);
    freespace_label_->set_valign(Gtk::ALIGN_CENTER);
    grid->attach(*freespace_label_, 0, row, 2, 1);

    /* file list row */
    row++;
    file_list_->set_vexpand(true);
    file_list_->set_size_request(466U, 300U);
    grid->attach(*file_list_, 0, row, 2, 1);

    /* torrent priority row */
    row++;
    auto* priority_label = Gtk::make_managed<Gtk::Label>(_("Torrent _priority:"), true);
    priority_label->set_halign(Gtk::ALIGN_START);
    priority_label->set_valign(Gtk::ALIGN_CENTER);
    grid->attach(*priority_label, 0, row, 1, 1);
    priority_label->set_mnemonic_widget(*priority_combo_);
    grid->attach_next_to(*priority_combo_, *priority_label, Gtk::POS_RIGHT);

    /* torrent priority row */
    row++;

    bool flag;
    if (!tr_ctorGetPaused(ctor_.get(), TR_FORCE, &flag))
    {
        g_assert_not_reached();
    }

    run_check_->set_active(!flag);
    grid->attach(*run_check_, 0, row, 2, 1);

    /* "trash torrent file" row */
    row++;

    if (!tr_ctorGetDeleteSource(ctor_.get(), &flag))
    {
        g_assert_not_reached();
    }

    trash_check_->set_active(flag);
    grid->attach(*trash_check_, 0, row, 2, 1);

    /* trigger sourceChanged, either directly or indirectly,
     * so that it creates the tor/gtor objects */
    if (!filename_.empty())
    {
        source_chooser->set_filename(filename_);
    }
    else
    {
        sourceChanged(source_chooser);
    }

    gtr_dialog_set_content(dialog_, *grid);
    dialog_.get_widget_for_response(Gtk::RESPONSE_ACCEPT)->grab_focus();
}

/****
*****
****/

void TorrentFileChooserDialog::onOpenDialogResponse(int response, Glib::RefPtr<Session> const& core)
{
    /* remember this folder the next time we use this dialog */
    gtr_pref_string_set(TR_KEY_open_dialog_dir, get_current_folder());

    if (response == Gtk::RESPONSE_ACCEPT)
    {
        auto const* const tb = static_cast<Gtk::CheckButton*>(get_extra_widget());
        bool const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents);
        bool const do_prompt = tb->get_active();
        bool const do_notify = false;
        auto const files = get_files();

        core->add_files(files, do_start, do_prompt, do_notify);
    }

    hide();
}

std::unique_ptr<TorrentFileChooserDialog> TorrentFileChooserDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core)
{
    return std::unique_ptr<TorrentFileChooserDialog>(new TorrentFileChooserDialog(parent, core));
}

TorrentFileChooserDialog::TorrentFileChooserDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
    : Gtk::FileChooserDialog(parent, _("Open a Torrent"), Gtk::FILE_CHOOSER_ACTION_OPEN)
{
    set_modal(true);

    add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
    add_button(_("_Open"), Gtk::RESPONSE_ACCEPT);

    set_select_multiple(true);
    addTorrentFilters(this);
    signal_response().connect([this, core](int response) { onOpenDialogResponse(response, core); });

    if (auto const folder = gtr_pref_string_get(TR_KEY_open_dialog_dir); !folder.empty())
    {
        set_current_folder(folder);
    }

    auto* c = Gtk::make_managed<Gtk::CheckButton>(_("Show _options dialog"), true);
    c->set_active(gtr_pref_flag_get(TR_KEY_show_options_window));
    set_extra_widget(*c);
    c->show();
}

/***
****
***/

void TorrentUrlChooserDialog::onOpenURLResponse(int response, Glib::RefPtr<Session> const& core)
{

    if (response == Gtk::RESPONSE_CANCEL)
    {
        hide();
    }
    else if (response == Gtk::RESPONSE_ACCEPT)
    {
        auto const* const e = static_cast<Gtk::Entry*>(get_data("url-entry"));
        auto const url = gtr_str_strip(e->get_text());

        if (url.empty())
        {
            return;
        }

        if (core->add_from_url(url))
        {
            hide();
        }
        else
        {
            gtr_unrecognized_url_dialog(*this, url);
        }
    }
}

std::unique_ptr<TorrentUrlChooserDialog> TorrentUrlChooserDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    return std::unique_ptr<TorrentUrlChooserDialog>(new TorrentUrlChooserDialog(parent, core));
}

TorrentUrlChooserDialog::TorrentUrlChooserDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(_("Open URL"), parent, true /* modal  */)
{
    guint row;

    add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
    add_button(_("_Open"), Gtk::RESPONSE_ACCEPT);
    signal_response().connect([this, core](int response) { onOpenURLResponse(response, core); });

    row = 0;
    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Open torrent from URL"));
    auto* e = Gtk::make_managed<Gtk::Entry>();
    e->set_size_request(400, -1);
    gtr_paste_clipboard_url_into_entry(*e);
    set_data("url-entry", e);
    t->add_row(row, _("_URL"), *e);

    gtr_dialog_set_content(*this, *t);

    if (e->get_text_length() == 0)
    {
        e->grab_focus();
    }
    else
    {
        get_widget_for_response(Gtk::RESPONSE_ACCEPT)->grab_focus();
    }
}
