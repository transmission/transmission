// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <utility>

#include <QFileInfo>
#include <QPushButton>

#include <libtransmission/transmission.h>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>
#include <libtransmission/torrent-metainfo.h>

#include "AddData.h"
#include "FileTreeModel.h"
#include "FreeSpaceLabel.h"
#include "OptionsDialog.h"
#include "Prefs.h"
#include "Session.h"
#include "Torrent.h"
#include "Utils.h"
#include "VariantHelpers.h"

using ::trqt::variant_helpers::dict_add;
using ::trqt::variant_helpers::list_add;

/***
****
***/

OptionsDialog::OptionsDialog(Session& session, Prefs const& prefs, AddData addme, QWidget* parent)
    : BaseDialog{ parent }
    , add_{ std::move(addme) }
    , session_{ session }
    , is_local_{ session_.is_local() }
{
    ui_.setupUi(this);

    setWindowTitle(add_.type == AddData::FILENAME ? tr("Open Torrent from File") : tr("Open Torrent from URL or Magnet Link"));

    edit_timer_.setInterval(2000);
    edit_timer_.setSingleShot(true);
    connect(&edit_timer_, &QTimer::timeout, this, &OptionsDialog::on_destination_changed);

    if (add_.type == AddData::FILENAME)
    {
        ui_.sourceStack->setCurrentWidget(ui_.sourceButton);
        ui_.sourceButton->set_mode(PathButton::FileMode);
        ui_.sourceButton->set_title(tr("Open Torrent"));
        ui_.sourceButton->set_name_filter(tr("Torrent Files (*.torrent);;All Files (*.*)"));
        ui_.sourceButton->set_path(add_.filename);
        connect(ui_.sourceButton, &PathButton::path_changed, this, &OptionsDialog::on_source_changed);
    }
    else
    {
        ui_.sourceStack->setCurrentWidget(ui_.sourceEdit);
        ui_.sourceEdit->setText(add_.readable_name());
        ui_.sourceEdit->selectAll();
        connect(ui_.sourceEdit, &QLineEdit::editingFinished, this, &OptionsDialog::on_source_changed);
    }

    ui_.sourceStack->setFixedHeight(ui_.sourceStack->currentWidget()->sizeHint().height());
    ui_.sourceLabel->setBuddy(ui_.sourceStack->currentWidget());

    auto const font_metrics = QFontMetrics{ font() };
    int const width = font_metrics.size(0, QStringLiteral("This is a pretty long torrent filename indeed.torrent")).width();
    ui_.sourceStack->setMinimumWidth(width);

    auto const download_dir = Utils::remove_trailing_dir_separator(prefs.get<QString>(Prefs::DOWNLOAD_DIR));
    ui_.freeSpaceLabel->set_session(session_);
    ui_.freeSpaceLabel->set_path(download_dir);

    ui_.destinationButton->set_mode(PathButton::DirectoryMode);
    ui_.destinationButton->set_title(tr("Select Destination"));
    ui_.destinationButton->set_path(download_dir);
    ui_.destinationEdit->setText(download_dir);

    if (is_local_)
    {
        local_destination_.setPath(download_dir);
    }

    connect(ui_.destinationButton, &PathButton::path_changed, this, &OptionsDialog::on_destination_changed);
    connect(ui_.destinationEdit, &QLineEdit::textEdited, &edit_timer_, qOverload<>(&QTimer::start));
    connect(ui_.destinationEdit, &QLineEdit::editingFinished, this, &OptionsDialog::on_destination_changed);

    ui_.filesView->set_editable(false);
    ui_.priorityCombo->addItem(tr("High"), TR_PRI_HIGH);
    ui_.priorityCombo->addItem(tr("Normal"), TR_PRI_NORMAL);
    ui_.priorityCombo->addItem(tr("Low"), TR_PRI_LOW);
    ui_.priorityCombo->setCurrentIndex(1); // Normal

    ui_.startCheck->setChecked(prefs.get<bool>(Prefs::START));
    ui_.trashCheck->setChecked(prefs.get<bool>(Prefs::TRASH_ORIGINAL));

    connect(ui_.dialogButtons, &QDialogButtonBox::rejected, this, &QObject::deleteLater);
    connect(ui_.dialogButtons, &QDialogButtonBox::accepted, this, &OptionsDialog::on_accepted);

    connect(ui_.filesView, &FileTreeView::priority_changed, this, &OptionsDialog::on_priority_changed);
    connect(ui_.filesView, &FileTreeView::wanted_changed, this, &OptionsDialog::on_wanted_changed);

    connect(&session_, &Session::session_updated, this, &OptionsDialog::on_session_updated);

    update_widgets_locality();
    reload();
}

OptionsDialog::~OptionsDialog()
{
    clear_info();
}

/***
****
***/

void OptionsDialog::clear_info()
{
    metainfo_.reset();
    files_.clear();
}

void OptionsDialog::reload()
{
    clear_info();

    auto metainfo = tr_torrent_metainfo{};
    auto ok = bool{};

    switch (add_.type)
    {
    case AddData::MAGNET:
        ok = metainfo.parseMagnet(add_.magnet.toStdString());
        break;

    case AddData::FILENAME:
        ok = metainfo.parse_torrent_file(add_.filename.toStdString());
        break;

    case AddData::METAINFO:
        ok = metainfo.parse_benc(add_.metainfo.toStdString());
        break;

    default:
        break;
    }

    metainfo_.reset();
    ui_.filesView->clear();
    files_.clear();
    priorities_.clear();
    wanted_.clear();

    if (ok)
    {
        metainfo_ = metainfo;
    }

    bool const have_files_to_show = metainfo_ && !std::empty(*metainfo_);

    ui_.filesView->setVisible(have_files_to_show);
    layout()->setSizeConstraint(have_files_to_show ? QLayout::SetDefaultConstraint : QLayout::SetFixedSize);

    if (metainfo_)
    {
        auto const n_files = metainfo_->file_count();
        priorities_.assign(n_files, TR_PRI_NORMAL);
        wanted_.assign(n_files, true);

        for (tr_file_index_t i = 0; i < n_files; ++i)
        {
            auto f = TorrentFile{};
            f.index = i;
            f.priority = priorities_[i];
            f.wanted = wanted_[i];
            f.size = metainfo_->file_size(i);
            f.have = 0;
            f.filename = QString::fromStdString(metainfo_->file_subpath(i));
            files_.push_back(f);
        }
    }

    ui_.filesView->update(files_);
    ui_.filesView->hideColumn(FileTreeModel::COL_PROGRESS);
}

void OptionsDialog::update_widgets_locality()
{
    ui_.destinationStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui_.destinationButton) : ui_.destinationEdit);
    ui_.destinationStack->setFixedHeight(ui_.destinationStack->currentWidget()->sizeHint().height());
    ui_.destinationLabel->setBuddy(ui_.destinationStack->currentWidget());
}

void OptionsDialog::on_session_updated()
{
    bool const is_local = session_.is_local();

    if (is_local_ != is_local)
    {
        is_local_ = is_local;
        update_widgets_locality();
    }
}

void OptionsDialog::on_priority_changed(file_indices_t const& file_indices, int priority)
{
    for (int const i : file_indices)
    {
        priorities_[i] = priority;
    }
}

void OptionsDialog::on_wanted_changed(file_indices_t const& file_indices, bool is_wanted)
{
    for (int const i : file_indices)
    {
        wanted_[i] = is_wanted;
    }
}

void OptionsDialog::on_accepted()
{
    // rpc spec section 3.4 "adding a torrent"

    tr_variant args;
    tr_variantInitDict(&args, 10);
    QString download_dir;

    // "download-dir"
    if (ui_.destinationStack->currentWidget() == ui_.destinationButton)
    {
        download_dir = local_destination_.absolutePath();
    }
    else
    {
        download_dir = ui_.destinationEdit->text();
    }

    dict_add(&args, TR_KEY_download_dir, download_dir);

    // paused
    dict_add(&args, TR_KEY_paused, !ui_.startCheck->isChecked());

    // priority
    int const index = ui_.priorityCombo->currentIndex();
    int const priority = ui_.priorityCombo->itemData(index).toInt();
    dict_add(&args, TR_KEY_bandwidth_priority, priority);

    // files_unwanted
    auto count = std::count(wanted_.begin(), wanted_.end(), false);

    if (count > 0)
    {
        tr_variant* l = tr_variantDictAddList(&args, TR_KEY_files_unwanted, count);

        for (int i = 0, n = wanted_.size(); i < n; ++i)
        {
            if (!wanted_.at(i))
            {
                list_add(l, i);
            }
        }
    }

    // priority_low
    count = std::count(priorities_.begin(), priorities_.end(), TR_PRI_LOW);

    if (count > 0)
    {
        tr_variant* l = tr_variantDictAddList(&args, TR_KEY_priority_low, count);

        for (int i = 0, n = priorities_.size(); i < n; ++i)
        {
            if (priorities_.at(i) == TR_PRI_LOW)
            {
                list_add(l, i);
            }
        }
    }

    // priority_high
    count = std::count(priorities_.begin(), priorities_.end(), TR_PRI_HIGH);

    if (count > 0)
    {
        tr_variant* l = tr_variantDictAddList(&args, TR_KEY_priority_high, count);

        for (int i = 0, n = priorities_.size(); i < n; ++i)
        {
            if (priorities_.at(i) == TR_PRI_HIGH)
            {
                list_add(l, i);
            }
        }
    }

    auto const disposal = ui_.trashCheck->isChecked() ? AddData::FilenameDisposal::Delete : AddData::FilenameDisposal::NoAction;
    add_.set_file_disposal(disposal);

    session_.add_torrent(add_, &args);

    deleteLater();
}

void OptionsDialog::on_source_changed()
{
    if (ui_.sourceStack->currentWidget() == ui_.sourceButton)
    {
        add_.set(ui_.sourceButton->path());
    }
    else if (auto const text = ui_.sourceEdit->text(); text != add_.readable_name())
    {
        add_.set(text);
    }

    reload();
}

void OptionsDialog::on_destination_changed()
{
    if (ui_.destinationStack->currentWidget() == ui_.destinationButton)
    {
        local_destination_.setPath(ui_.destinationButton->path());
        ui_.freeSpaceLabel->set_path(local_destination_.absolutePath());
    }
    else
    {
        ui_.freeSpaceLabel->set_path(ui_.destinationEdit->text());
    }
}
