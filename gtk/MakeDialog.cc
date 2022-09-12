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
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Window& parent,
        tr_metainfo_builder& metainfo_builder,
        std::future<tr_error*> future,
        std::string_view const& target,
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
    Impl(MakeDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);

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
    set_response_sensitive(TR_GTK_RESPONSE_TYPE(CANCEL), !is_done);
    set_response_sensitive(TR_GTK_RESPONSE_TYPE(CLOSE), is_done);
    set_response_sensitive(TR_GTK_RESPONSE_TYPE(ACCEPT), is_done && success);

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
    case TR_GTK_RESPONSE_TYPE(CANCEL):
        builder_.cancelChecksums();
        hide();
        break;

    case TR_GTK_RESPONSE_TYPE(ACCEPT):
        addTorrent();
        [[fallthrough]];

    case TR_GTK_RESPONSE_TYPE(CLOSE):
        hide();
        break;

    default:
        g_assert(0 && "unhandled response");
    }
}

MakeProgressDialog::MakeProgressDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    tr_metainfo_builder& metainfo_builder,
    std::future<tr_error*> future,
    std::string_view const& target,
    Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(cast_item)
    , builder_(metainfo_builder)
    , future_{ std::move(future) }
    , target_(target)
    , core_(core)
    , progress_label_(gtr_get_widget<Gtk::Label>(builder, "progress_label"))
    , progress_bar_(gtr_get_widget<Gtk::ProgressBar>(builder, "progress_bar"))
{
    set_transient_for(parent);
    signal_response().connect(sigc::mem_fun(*this, &MakeProgressDialog::onProgressDialogResponse));

    progress_tag_ = Glib::signal_timeout().connect_seconds(
        sigc::mem_fun(*this, &MakeProgressDialog::onProgressDialogRefresh),
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);
    onProgressDialogRefresh();
}

void MakeDialog::Impl::makeProgressDialog(std::string_view target, std::future<tr_error*> future)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("MakeProgressDialog.ui"));
    progress_dialog_ = std::unique_ptr<MakeProgressDialog>(gtr_get_widget_derived<MakeProgressDialog>(
        builder,
        "MakeProgressDialog",
        dialog_,
        *builder_,
        std::move(future),
        target,
        core_));
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
    if (response == TR_GTK_RESPONSE_TYPE(ACCEPT))
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
    else if (response == TR_GTK_RESPONSE_TYPE(CLOSE))
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

    auto gstr = Glib::ustring();

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

    pieces_lb_->set_text(gstr);
}

void MakeDialog::Impl::configurePieceSizeScale()
{
    // the below lower & upper bounds would allow piece size selection between approx 1KiB - 64MiB
    auto adjustment = Gtk::Adjustment::create(log2(builder_->pieceSize()), 10, 26, 1.0, 1.0);
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

        if (Glib::file_test(filename, TR_GLIB_FILE_TEST(IS_DIR)))
        {
            /* a directory was dragged onto the dialog... */
            folder_radio_->set_active(true);
            folder_chooser_->set_current_folder(filename);
            success = true;
        }
        else if (Glib::file_test(filename, TR_GLIB_FILE_TEST(IS_REGULAR)))
        {
            /* a file was dragged on to the dialog... */
            file_radio_->set_active(true);
            file_chooser_->set_filename(filename);
            success = true;
        }
    }

    drag_context->drag_finish(success, false, time_);
}

MakeDialog::MakeDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core))
{
    set_transient_for(parent);
}

MakeDialog::~MakeDialog() = default;

std::unique_ptr<MakeDialog> MakeDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("MakeDialog.ui"));
    return std::unique_ptr<MakeDialog>(gtr_get_widget_derived<MakeDialog>(builder, "MakeDialog", parent, core));
}

MakeDialog::Impl::Impl(MakeDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
    , file_radio_(gtr_get_widget<Gtk::RadioButton>(builder, "source_file_radio"))
    , file_chooser_(gtr_get_widget<Gtk::FileChooserButton>(builder, "source_file_button"))
    , folder_radio_(gtr_get_widget<Gtk::RadioButton>(builder, "source_folder_radio"))
    , folder_chooser_(gtr_get_widget<Gtk::FileChooserButton>(builder, "source_folder_button"))
    , pieces_lb_(gtr_get_widget<Gtk::Label>(builder, "source_size_label"))
    , piece_size_scale_(gtr_get_widget<Gtk::Scale>(builder, "piece_size_scale"))
    , destination_chooser_(gtr_get_widget<Gtk::FileChooserButton>(builder, "destination_button"))
    , comment_check_(gtr_get_widget<Gtk::CheckButton>(builder, "comment_check"))
    , comment_entry_(gtr_get_widget<Gtk::Entry>(builder, "comment_entry"))
    , private_check_(gtr_get_widget<Gtk::CheckButton>(builder, "private_check"))
    , source_check_(gtr_get_widget<Gtk::CheckButton>(builder, "source_check"))
    , source_entry_(gtr_get_widget<Gtk::Entry>(builder, "source_entry"))
    , announce_text_buffer_(gtr_get_widget<Gtk::TextView>(builder, "trackers_view")->get_buffer())
{
    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::onResponse));

    destination_chooser_->set_current_folder(Glib::get_user_special_dir(TR_GLIB_USER_DIRECTORY(DESKTOP)));

    folder_radio_->set_active(false);
    folder_radio_->signal_toggled().connect([this]() { onSourceToggled2(folder_radio_, folder_chooser_); });
    folder_radio_->signal_toggled().connect([this]() { onSourceToggled(folder_radio_, folder_chooser_); });
    folder_chooser_->signal_selection_changed().connect([this]() { onChooserChosen(folder_chooser_); });
    folder_chooser_->set_sensitive(false);

    file_radio_->set_active(true);
    file_radio_->signal_toggled().connect([this]() { onSourceToggled2(file_radio_, file_chooser_); });
    file_radio_->signal_toggled().connect([this]() { onSourceToggled(file_radio_, file_chooser_); });
    file_chooser_->signal_selection_changed().connect([this]() { onChooserChosen(file_chooser_); });

    pieces_lb_->set_markup(fmt::format(FMT_STRING("<i>{:s}</i>"), _("No source selected")));

    piece_size_scale_->set_visible(false);
    piece_size_scale_->signal_value_changed().connect([this]() { onPieceSizeUpdated(); });

    comment_check_->set_active(false);
    comment_entry_->set_sensitive(false);
    comment_check_->signal_toggled().connect([this]() { onSourceToggled(comment_check_, comment_entry_); });

    source_check_->set_active(false);
    source_entry_->set_sensitive(false);
    source_check_->signal_toggled().connect([this]() { onSourceToggled(source_check_, source_entry_); });

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
