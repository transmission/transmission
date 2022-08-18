// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <fmt/core.h>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/utils.h> /* tr_formatter_mem_B() */

#include "HigWorkarea.h"
#include "MakeDialog.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

using namespace std::literals;

namespace
{

auto const FileChosenKey = Glib::Quark("file-is-chosen");

class MakeProgressDialog : public Gtk::Dialog
{
public:
    MakeProgressDialog(
        Gtk::Window& parent,
        tr_metainfo_builder& builder,
        std::future<tr_error*> future,
        std::string_view target,
        Glib::RefPtr<Session> const& core);
    ~MakeProgressDialog() override;

    TR_DISABLE_COPY_MOVE(MakeProgressDialog)

    [[nodiscard]] bool success() const
    {
        return success_;
    }

private:
    bool onProgressDialogRefresh();
    void onProgressDialogResponse(int response);

    void addTorrent();

private:
    tr_metainfo_builder& builder_;
    std::future<tr_error*> future_;
    std::string const target_;
    Glib::RefPtr<Session> const core_;
    bool success_ = false;

    sigc::connection progress_tag_;
    Gtk::Label* progress_label_ = nullptr;
    Gtk::ProgressBar* progress_bar_ = nullptr;
};

} // namespace

class MakeDialog::Impl
{
public:
    Impl(MakeDialog& dialog, Glib::RefPtr<Session> const& core);

    TR_DISABLE_COPY_MOVE(Impl)

private:
    void onSourceToggled2(Gtk::ToggleButton* tb, Gtk::FileChooserButton* chooser);
    void onChooserChosen(Gtk::FileChooserButton* chooser);
    void onResponse(int response);

    void on_drag_data_received(
        Glib::RefPtr<Gdk::DragContext> const& drag_context,
        int x,
        int y,
        Gtk::SelectionData const& selection_data,
        guint info,
        guint time_);

    void updatePiecesLabel();

    void setFilename(std::string_view filename);

    void makeProgressDialog(std::string_view target, std::future<tr_error*> future);
    void configurePieceSizeScale();
    void onPieceSizeUpdated();

private:
    MakeDialog& dialog_;
    Glib::RefPtr<Session> const core_;

    Gtk::RadioButton* file_radio_ = nullptr;
    Gtk::FileChooserButton* file_chooser_ = nullptr;
    Gtk::RadioButton* folder_radio_ = nullptr;
    Gtk::FileChooserButton* folder_chooser_ = nullptr;
    Gtk::Label* pieces_lb_ = nullptr;
    Gtk::Scale* piece_size_scale_ = nullptr;
    Gtk::FileChooserButton* destination_chooser_ = nullptr;
    Gtk::CheckButton* comment_check_ = nullptr;
    Gtk::Entry* comment_entry_ = nullptr;
    Gtk::CheckButton* private_check_ = nullptr;
    Gtk::CheckButton* source_check_ = nullptr;
    Gtk::Entry* source_entry_ = nullptr;
    std::unique_ptr<MakeProgressDialog> progress_dialog_;
    Glib::RefPtr<Gtk::TextBuffer> announce_text_buffer_;
    std::optional<tr_metainfo_builder> builder_;
};

bool MakeProgressDialog::onProgressDialogRefresh()
{
    auto const is_done = !future_.valid() || future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;

    if (is_done)
    {
        progress_tag_.disconnect();
    }

    // progress value
    auto percent_done = 1.0;
    auto piece_index = tr_piece_index_t{};

    if (!is_done)
    {
        auto const [current, total] = builder_.checksumStatus();
        percent_done = static_cast<double>(current) / total;
        piece_index = current;
    }

    // progress text
    auto str = std::string{};
    auto success = false;
    auto const base = Glib::path_get_basename(builder_.top());
    if (!is_done)
    {
        str = fmt::format(_("Creating '{path}'"), fmt::arg("path", base));
    }
    else
    {
        tr_error* error = future_.get();

        if (error == nullptr)
        {
            builder_.save(target_, &error);
        }

        if (error == nullptr)
        {
            str = fmt::format(_("Created '{path}'"), fmt::arg("path", base));
            success = true;
        }
        else
        {
            str = fmt::format(
                _("Couldn't create '{path}': {error} ({error_code})"),
                fmt::arg("path", base),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code));
            tr_error_free(error);
        }
    }

    gtr_label_set_text(*progress_label_, str);

    /* progress bar */
    if (piece_index == 0)
    {
        str.clear();
    }
    else
    {
        /* how much data we've scanned through to generate checksums */
        str = fmt::format(
            _("Scanned {file_size}"),
            fmt::arg("file_size", tr_strlsize(static_cast<uint64_t>(piece_index) * builder_.pieceSize())));
    }

    progress_bar_->set_fraction(percent_done);
    progress_bar_->set_text(str);

    /* buttons */
    set_response_sensitive(Gtk::RESPONSE_CANCEL, !is_done);
    set_response_sensitive(Gtk::RESPONSE_CLOSE, is_done);
    set_response_sensitive(Gtk::RESPONSE_ACCEPT, is_done && success);

    success_ = success;
    return true;
}

MakeProgressDialog::~MakeProgressDialog()
{
    progress_tag_.disconnect();
}

void MakeProgressDialog::addTorrent()
{
    tr_ctor* ctor = tr_ctorNew(core_->get_session());
    tr_ctorSetMetainfoFromFile(ctor, target_.c_str(), nullptr);
    tr_ctorSetDownloadDir(ctor, TR_FORCE, Glib::path_get_dirname(builder_.top()).c_str());
    core_->add_ctor(ctor);
}

void MakeProgressDialog::onProgressDialogResponse(int response)
{
    switch (response)
    {
    case Gtk::RESPONSE_CANCEL:
        builder_.cancelChecksums();
        hide();
        break;

    case Gtk::RESPONSE_ACCEPT:
        addTorrent();
        [[fallthrough]];

    case Gtk::RESPONSE_CLOSE:
        hide();
        break;

    default:
        g_assert(0 && "unhandled response");
    }
}

MakeProgressDialog::MakeProgressDialog(
    Gtk::Window& parent,
    tr_metainfo_builder& builder,
    std::future<tr_error*> future,
    std::string_view target,
    Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(_("New Torrent"), parent, true)
    , builder_{ builder }
    , future_{ std::move(future) }
    , target_{ target }
    , core_{ core }
{
    add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
    add_button(_("_Close"), Gtk::RESPONSE_CLOSE);
    add_button(_("_Add"), Gtk::RESPONSE_ACCEPT);
    signal_response().connect(sigc::mem_fun(*this, &MakeProgressDialog::onProgressDialogResponse));

    auto* fr = Gtk::make_managed<Gtk::Frame>();
    fr->set_border_width(GUI_PAD_BIG);
    fr->set_shadow_type(Gtk::SHADOW_NONE);
    auto* v = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, GUI_PAD);
    fr->add(*v);

    progress_label_ = Gtk::make_managed<Gtk::Label>(_("Creating torrent…"));
    progress_label_->set_halign(Gtk::ALIGN_START);
    progress_label_->set_valign(Gtk::ALIGN_CENTER);
    progress_label_->set_justify(Gtk::JUSTIFY_LEFT);
    v->pack_start(*progress_label_, false, false, 0);

    progress_bar_ = Gtk::make_managed<Gtk::ProgressBar>();
    v->pack_start(*progress_bar_, false, false, 0);

    progress_tag_ = Glib::signal_timeout().connect_seconds(
        sigc::mem_fun(*this, &MakeProgressDialog::onProgressDialogRefresh),
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);
    onProgressDialogRefresh();

    gtr_dialog_set_content(*this, *fr);
}

void MakeDialog::Impl::makeProgressDialog(std::string_view target, std::future<tr_error*> future)
{
    progress_dialog_ = std::make_unique<MakeProgressDialog>(dialog_, *builder_, std::move(future), target, core_);
    progress_dialog_->signal_hide().connect(
        [this]()
        {
            auto const success = progress_dialog_->success();
            progress_dialog_.reset();
            if (success)
            {
                dialog_.hide();
            }
        });
    progress_dialog_->show();
}

void MakeDialog::Impl::onResponse(int response)
{
    if (response == Gtk::RESPONSE_ACCEPT)
    {
        if (builder_)
        {
            // destination file
            auto const dir = destination_chooser_->get_filename();
            auto const base = Glib::path_get_basename(builder_->top());
            auto const target = fmt::format("{:s}/{:s}.torrent", dir, base);

            // build the announce list
            auto trackers = tr_announce_list{};
            trackers.parse(announce_text_buffer_->get_text(false).raw());
            builder_->setAnnounceList(std::move(trackers));

            // comment
            if (comment_check_->get_active())
            {
                builder_->setComment(comment_entry_->get_text().raw());
            }

            // source
            if (source_check_->get_active())
            {
                builder_->setSource(source_entry_->get_text().raw());
            }

            builder_->setPrivate(private_check_->get_active());

            // build the .torrent
            makeProgressDialog(target, builder_->makeChecksums());
        }
    }
    else if (response == Gtk::RESPONSE_CLOSE)
    {
        dialog_.hide();
    }
}

/***
****
***/

namespace
{

void onSourceToggled(Gtk::ToggleButton* tb, Gtk::Widget* widget)
{
    widget->set_sensitive(tb->get_active());
}

} // namespace

void MakeDialog::Impl::updatePiecesLabel()
{
    auto const filename = builder_ ? builder_->top() : ""sv;

    auto gstr = Glib::ustring{ "<i>" };

    if (std::empty(filename))
    {
        gstr += _("No source selected");
        piece_size_scale_->set_visible(false);
    }
    else
    {
        gstr += fmt::format(
            ngettext("{total_size} in {file_count:L} file", "{total_size} in {file_count:L} files", builder_->fileCount()),
            fmt::arg("total_size", tr_strlsize(builder_->totalSize())),
            fmt::arg("file_count", builder_->fileCount()));
        gstr += ' ';
        gstr += fmt::format(
            ngettext(
                "({piece_count} BitTorrent piece @ {piece_size})",
                "({piece_count} BitTorrent pieces @ {piece_size})",
                builder_->pieceCount()),
            fmt::arg("piece_count", builder_->pieceCount()),
            fmt::arg("piece_size", tr_formatter_mem_B(builder_->pieceSize())));
    }

    gstr += "</i>";
    pieces_lb_->set_markup(gstr);
}

void MakeDialog::Impl::configurePieceSizeScale()
{
    // the below lower & upper bounds would allow piece size selection between approx 1KiB - 16MiB
    auto adjustment = Gtk::Adjustment::create(log2(builder_->pieceSize()), 10, 24, 1.0, 1.0);
    piece_size_scale_->set_adjustment(adjustment);
    piece_size_scale_->set_visible(true);
}

void MakeDialog::Impl::setFilename(std::string_view filename)
{
    builder_.reset();

    if (!filename.empty())
    {
        builder_.emplace(filename);
        configurePieceSizeScale();
    }

    updatePiecesLabel();
}

void MakeDialog::Impl::onChooserChosen(Gtk::FileChooserButton* chooser)
{
    chooser->set_data(FileChosenKey, GINT_TO_POINTER(true));
    setFilename(chooser->get_filename());
}

void MakeDialog::Impl::onSourceToggled2(Gtk::ToggleButton* tb, Gtk::FileChooserButton* chooser)
{
    if (tb->get_active())
    {
        if (chooser->get_data(FileChosenKey) != nullptr)
        {
            onChooserChosen(chooser);
        }
        else
        {
            setFilename({});
        }
    }
}

void MakeDialog::Impl::on_drag_data_received(
    Glib::RefPtr<Gdk::DragContext> const& drag_context,
    int /*x*/,
    int /*y*/,
    Gtk::SelectionData const& selection_data,
    guint /*info*/,
    guint time_)
{
    bool success = false;

    if (auto const uris = selection_data.get_uris(); !uris.empty())
    {
        auto const& uri = uris.front();
        auto const filename = Glib::filename_from_uri(uri);

        if (Glib::file_test(filename, Glib::FILE_TEST_IS_DIR))
        {
            /* a directory was dragged onto the dialog... */
            folder_radio_->set_active(true);
            folder_chooser_->set_current_folder(filename);
            success = true;
        }
        else if (Glib::file_test(filename, Glib::FILE_TEST_IS_REGULAR))
        {
            /* a file was dragged on to the dialog... */
            file_radio_->set_active(true);
            file_chooser_->set_filename(filename);
            success = true;
        }
    }

    drag_context->drag_finish(success, false, time_);
}

std::unique_ptr<MakeDialog> MakeDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    return std::unique_ptr<MakeDialog>(new MakeDialog(parent, core));
}

MakeDialog::MakeDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(_("New Torrent"), parent)
    , impl_(std::make_unique<Impl>(*this, core))
{
}

MakeDialog::~MakeDialog() = default;

MakeDialog::Impl::Impl(MakeDialog& dialog, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
{
    guint row = 0;

    dialog_.add_button(_("_Close"), Gtk::RESPONSE_CLOSE);
    dialog_.add_button(_("_New"), Gtk::RESPONSE_ACCEPT);
    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::onResponse));

    auto* t = Gtk::make_managed<HigWorkarea>();

    t->add_section_title(row, _("Files"));

    destination_chooser_ = Gtk::make_managed<Gtk::FileChooserButton>(Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    destination_chooser_->set_current_folder(Glib::get_user_special_dir(Glib::USER_DIRECTORY_DESKTOP));
    t->add_row(row, _("Sa_ve to:"), *destination_chooser_);

    Gtk::RadioButton::Group slist;

    folder_radio_ = Gtk::make_managed<Gtk::RadioButton>(slist, _("Source F_older:"), true);
    folder_radio_->set_active(false);
    folder_chooser_ = Gtk::make_managed<Gtk::FileChooserButton>(Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    folder_radio_->signal_toggled().connect([this]() { onSourceToggled2(folder_radio_, folder_chooser_); });
    folder_radio_->signal_toggled().connect([this]() { onSourceToggled(folder_radio_, folder_chooser_); });
    folder_chooser_->signal_selection_changed().connect([this]() { onChooserChosen(folder_chooser_); });
    folder_chooser_->set_sensitive(false);
    t->add_row_w(row, *folder_radio_, *folder_chooser_);

    file_radio_ = Gtk::make_managed<Gtk::RadioButton>(slist, _("Source _File:"), true);
    file_radio_->set_active(true);
    file_chooser_ = Gtk::make_managed<Gtk::FileChooserButton>(Gtk::FILE_CHOOSER_ACTION_OPEN);
    file_radio_->signal_toggled().connect([this]() { onSourceToggled2(file_radio_, file_chooser_); });
    file_radio_->signal_toggled().connect([this]() { onSourceToggled(file_radio_, file_chooser_); });
    file_chooser_->signal_selection_changed().connect([this]() { onChooserChosen(file_chooser_); });
    t->add_row_w(row, *file_radio_, *file_chooser_);

    pieces_lb_ = Gtk::make_managed<Gtk::Label>();
    pieces_lb_->set_markup(fmt::format(FMT_STRING("<i>{:s}</i>"), _("No source selected")));
    t->add_row(row, {}, *pieces_lb_);

    piece_size_scale_ = Gtk::make_managed<Gtk::Scale>();
    piece_size_scale_->set_draw_value(false);
    piece_size_scale_->set_visible(false);
    piece_size_scale_->signal_value_changed().connect([this]() { onPieceSizeUpdated(); });
    t->add_row(row, _("Piece size:"), *piece_size_scale_);

    t->add_section_divider(row);
    t->add_section_title(row, _("Properties"));

    auto* v = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, GUI_PAD_SMALL);
    announce_text_buffer_ = Gtk::TextBuffer::create();
    auto* w = Gtk::make_managed<Gtk::TextView>(announce_text_buffer_);
    w->set_size_request(-1, 80);
    auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    sw->add(*w);
    auto* fr = Gtk::make_managed<Gtk::Frame>();
    fr->set_shadow_type(Gtk::SHADOW_IN);
    fr->add(*sw);
    v->pack_start(*fr, true, true, 0);
    auto* l = Gtk::make_managed<Gtk::Label>();
    l->set_markup(_(
        "To add a backup URL, add it on the next line after a primary URL.\nTo add a new primary URL, add it after a blank line."));
    l->set_justify(Gtk::JUSTIFY_LEFT);
    l->set_halign(Gtk::ALIGN_START);
    l->set_valign(Gtk::ALIGN_CENTER);
    v->pack_start(*l, false, false, 0);
    t->add_tall_row(row, _("_Trackers:"), *v);

    comment_check_ = Gtk::make_managed<Gtk::CheckButton>(_("Co_mment:"), true);
    comment_check_->set_active(false);
    comment_entry_ = Gtk::make_managed<Gtk::Entry>();
    comment_entry_->set_sensitive(false);
    comment_check_->signal_toggled().connect([this]() { onSourceToggled(comment_check_, comment_entry_); });
    t->add_row_w(row, *comment_check_, *comment_entry_);

    source_check_ = Gtk::make_managed<Gtk::CheckButton>(_("_Source:"), true);
    source_check_->set_active(false);
    source_entry_ = Gtk::make_managed<Gtk::Entry>();
    source_entry_->set_sensitive(false);
    source_check_->signal_toggled().connect([this]() { onSourceToggled(source_check_, source_entry_); });
    t->add_row_w(row, *source_check_, *source_entry_);

    private_check_ = t->add_wide_checkbutton(row, _("_Private torrent"), false);

    gtr_dialog_set_content(dialog_, *t);

    dialog_.drag_dest_set(Gtk::DEST_DEFAULT_ALL, Gdk::ACTION_COPY);
    dialog_.drag_dest_add_uri_targets();
    dialog_.signal_drag_data_received().connect(sigc::mem_fun(*this, &Impl::on_drag_data_received));
}

void MakeDialog::Impl::onPieceSizeUpdated()
{
    if (builder_)
    {
        builder_->setPieceSize(static_cast<uint32_t>(std::pow(2, piece_size_scale_->get_value())));
        updatePiecesLabel();
    }
}
