/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QFileInfo>
#include <QPushButton>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* mime64 */
#include <libtransmission/variant.h>

#include "AddData.h"
#include "FreeSpaceLabel.h"
#include "OptionsDialog.h"
#include "Prefs.h"
#include "Session.h"
#include "Torrent.h"
#include "Utils.h"
#include "VariantHelpers.h"

using ::trqt::variant_helpers::dictAdd;
using ::trqt::variant_helpers::listAdd;

/***
****
***/

OptionsDialog::OptionsDialog(Session& session, Prefs const& prefs, AddData addme, QWidget* parent) :
    BaseDialog(parent),
    add_(std::move(addme)),
    verify_button_(new QPushButton(tr("&Verify Local Data"), this)),
    session_(session),
    is_local_(session_.isLocal())
{
    ui_.setupUi(this);

    QString title;

    if (add_.type == AddData::FILENAME)
    {
        title = tr("Open Torrent from File");
    }
    else
    {
        title = tr("Open Torrent from URL or Magnet Link");
    }

    setWindowTitle(title);

    edit_timer_.setInterval(2000);
    edit_timer_.setSingleShot(true);
    connect(&edit_timer_, &QTimer::timeout, this, &OptionsDialog::onDestinationChanged);

    if (add_.type == AddData::FILENAME)
    {
        ui_.sourceStack->setCurrentWidget(ui_.sourceButton);
        ui_.sourceButton->setMode(PathButton::FileMode);
        ui_.sourceButton->setTitle(tr("Open Torrent"));
        ui_.sourceButton->setNameFilter(tr("Torrent Files (*.torrent);;All Files (*.*)"));
        ui_.sourceButton->setPath(add_.filename);
        connect(ui_.sourceButton, &PathButton::pathChanged, this, &OptionsDialog::onSourceChanged);
    }
    else
    {
        ui_.sourceStack->setCurrentWidget(ui_.sourceEdit);
        ui_.sourceEdit->setText(add_.readableName());
        ui_.sourceEdit->selectAll();
        connect(ui_.sourceEdit, &QLineEdit::editingFinished, this, &OptionsDialog::onSourceChanged);
    }

    ui_.sourceStack->setFixedHeight(ui_.sourceStack->currentWidget()->sizeHint().height());
    ui_.sourceLabel->setBuddy(ui_.sourceStack->currentWidget());

    QFontMetrics const font_metrics(font());
    int const width = font_metrics.size(0, QStringLiteral("This is a pretty long torrent filename indeed.torrent")).width();
    ui_.sourceStack->setMinimumWidth(width);

    QString const download_dir(Utils::removeTrailingDirSeparator(prefs.getString(Prefs::DOWNLOAD_DIR)));
    ui_.freeSpaceLabel->setSession(session_);
    ui_.freeSpaceLabel->setPath(download_dir);

    ui_.destinationButton->setMode(PathButton::DirectoryMode);
    ui_.destinationButton->setTitle(tr("Select Destination"));
    ui_.destinationButton->setPath(download_dir);
    ui_.destinationEdit->setText(download_dir);

    if (is_local_)
    {
        local_destination_.setPath(download_dir);
    }

    connect(ui_.destinationButton, &PathButton::pathChanged, this, &OptionsDialog::onDestinationChanged);
    connect(ui_.destinationEdit, &QLineEdit::textEdited, &edit_timer_, qOverload<>(&QTimer::start));
    connect(ui_.destinationEdit, &QLineEdit::editingFinished, this, &OptionsDialog::onDestinationChanged);

    ui_.filesView->setEditable(false);

    ui_.priorityCombo->addItem(tr("High"), TR_PRI_HIGH);
    ui_.priorityCombo->addItem(tr("Normal"), TR_PRI_NORMAL);
    ui_.priorityCombo->addItem(tr("Low"), TR_PRI_LOW);
    ui_.priorityCombo->setCurrentIndex(1); // Normal

    ui_.dialogButtons->addButton(verify_button_, QDialogButtonBox::ActionRole);
    connect(verify_button_, &QAbstractButton::clicked, this, &OptionsDialog::onVerify);

    ui_.startCheck->setChecked(prefs.getBool(Prefs::START));
    ui_.trashCheck->setChecked(prefs.getBool(Prefs::TRASH_ORIGINAL));

    connect(ui_.dialogButtons, &QDialogButtonBox::rejected, this, &QObject::deleteLater);
    connect(ui_.dialogButtons, &QDialogButtonBox::accepted, this, &OptionsDialog::onAccepted);

    connect(ui_.filesView, &FileTreeView::priorityChanged, this, &OptionsDialog::onPriorityChanged);
    connect(ui_.filesView, &FileTreeView::wantedChanged, this, &OptionsDialog::onWantedChanged);

    connect(&verify_timer_, &QTimer::timeout, this, &OptionsDialog::onTimeout);

    connect(&session_, &Session::sessionUpdated, this, &OptionsDialog::onSessionUpdated);

    updateWidgetsLocality();
    reload();
}

OptionsDialog::~OptionsDialog()
{
    clearInfo();
}

/***
****
***/

void OptionsDialog::clearInfo()
{
    if (have_info_)
    {
        tr_metainfoFree(&info_);
    }

    have_info_ = false;
    files_.clear();
}

void OptionsDialog::reload()
{
    clearInfo();
    clearVerify();

    tr_ctor* ctor = tr_ctorNew(nullptr);

    switch (add_.type)
    {
    case AddData::MAGNET:
        tr_ctorSetMetainfoFromMagnetLink(ctor, add_.magnet.toUtf8().constData());
        break;

    case AddData::FILENAME:
        tr_ctorSetMetainfoFromFile(ctor, add_.filename.toUtf8().constData());
        break;

    case AddData::METAINFO:
        tr_ctorSetMetainfo(ctor, add_.metainfo.constData(), add_.metainfo.size());
        break;

    default:
        break;
    }

    int const err = tr_torrentParse(ctor, &info_);
    have_info_ = err == 0;
    tr_ctorFree(ctor);

    ui_.filesView->clear();
    files_.clear();
    priorities_.clear();
    wanted_.clear();

    bool const have_files_to_show = have_info_ && info_.fileCount > 0;

    ui_.filesView->setVisible(have_files_to_show);
    verify_button_->setEnabled(have_files_to_show);
    layout()->setSizeConstraint(have_files_to_show ? QLayout::SetDefaultConstraint : QLayout::SetFixedSize);

    if (have_info_)
    {
        priorities_.insert(0, info_.fileCount, TR_PRI_NORMAL);
        wanted_.insert(0, info_.fileCount, true);

        for (tr_file_index_t i = 0; i < info_.fileCount; ++i)
        {
            TorrentFile file;
            file.index = i;
            file.priority = priorities_[i];
            file.wanted = wanted_[i];
            file.size = info_.files[i].length;
            file.have = 0;
            file.filename = QString::fromUtf8(info_.files[i].name);
            files_.append(file);
        }
    }

    ui_.filesView->update(files_);
}

void OptionsDialog::updateWidgetsLocality()
{
    ui_.destinationStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui_.destinationButton) : ui_.destinationEdit);
    ui_.destinationStack->setFixedHeight(ui_.destinationStack->currentWidget()->sizeHint().height());
    ui_.destinationLabel->setBuddy(ui_.destinationStack->currentWidget());

    // hide the % done when non-local, since we've no way of knowing
    (ui_.filesView->*(is_local_ ? &QTreeView::showColumn : &QTreeView::hideColumn))(2);

    verify_button_->setVisible(is_local_);
}

void OptionsDialog::onSessionUpdated()
{
    bool const is_local = session_.isLocal();

    if (is_local_ != is_local)
    {
        is_local_ = is_local;
        updateWidgetsLocality();
    }
}

void OptionsDialog::onPriorityChanged(QSet<int> const& file_indices, int priority)
{
    for (int const i : file_indices)
    {
        priorities_[i] = priority;
    }
}

void OptionsDialog::onWantedChanged(QSet<int> const& file_indices, bool is_wanted)
{
    for (int const i : file_indices)
    {
        wanted_[i] = is_wanted;
    }
}

void OptionsDialog::onAccepted()
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

    dictAdd(&args, TR_KEY_download_dir, download_dir);

    // paused
    dictAdd(&args, TR_KEY_paused, !ui_.startCheck->isChecked());

    // priority
    int const index = ui_.priorityCombo->currentIndex();
    int const priority = ui_.priorityCombo->itemData(index).toInt();
    dictAdd(&args, TR_KEY_bandwidthPriority, priority);

    // files-unwanted
    int count = wanted_.count(false);

    if (count > 0)
    {
        tr_variant* l = tr_variantDictAddList(&args, TR_KEY_files_unwanted, count);

        for (int i = 0, n = wanted_.size(); i < n; ++i)
        {
            if (!wanted_.at(i))
            {
                listAdd(l, i);
            }
        }
    }

    // priority-low
    count = priorities_.count(TR_PRI_LOW);

    if (count > 0)
    {
        tr_variant* l = tr_variantDictAddList(&args, TR_KEY_priority_low, count);

        for (int i = 0, n = priorities_.size(); i < n; ++i)
        {
            if (priorities_.at(i) == TR_PRI_LOW)
            {
                listAdd(l, i);
            }
        }
    }

    // priority-high
    count = priorities_.count(TR_PRI_HIGH);

    if (count > 0)
    {
        tr_variant* l = tr_variantDictAddList(&args, TR_KEY_priority_high, count);

        for (int i = 0, n = priorities_.size(); i < n; ++i)
        {
            if (priorities_.at(i) == TR_PRI_HIGH)
            {
                listAdd(l, i);
            }
        }
    }

    session_.addTorrent(add_, &args, ui_.trashCheck->isChecked());

    deleteLater();
}

void OptionsDialog::onSourceChanged()
{
    if (ui_.sourceStack->currentWidget() == ui_.sourceButton)
    {
        add_.set(ui_.sourceButton->path());
    }
    else
    {
        add_.set(ui_.sourceEdit->text());
    }

    reload();
}

void OptionsDialog::onDestinationChanged()
{
    if (ui_.destinationStack->currentWidget() == ui_.destinationButton)
    {
        local_destination_.setPath(ui_.destinationButton->path());
        ui_.freeSpaceLabel->setPath(local_destination_.absolutePath());
    }
    else
    {
        ui_.freeSpaceLabel->setPath(ui_.destinationEdit->text());
    }
}

/***
****
****  VERIFY
****
***/

void OptionsDialog::clearVerify()
{
    verify_hash_.reset();
    verify_file_.close();
    verify_file_pos_ = 0;
    verify_flags_.clear();
    verify_file_index_ = 0;
    verify_piece_index_ = 0;
    verify_piece_pos_ = 0;
    verify_timer_.stop();

    for (TorrentFile& f : files_)
    {
        f.have = 0;
    }

    ui_.filesView->update(files_);
}

void OptionsDialog::onVerify()
{
    clearVerify();
    verify_flags_.insert(0, info_.pieceCount, false);
    verify_timer_.setSingleShot(false);
    verify_timer_.start(0);
}

namespace
{

uint64_t getPieceSize(tr_info const* info, tr_piece_index_t piece_index)
{
    if (piece_index != info->pieceCount - 1)
    {
        return info->pieceSize;
    }

    return info->totalSize % info->pieceSize;
}

} // namespace

void OptionsDialog::onTimeout()
{
    if (files_.isEmpty())
    {
        verify_timer_.stop();
        return;
    }

    tr_file const* file = &info_.files[verify_file_index_];

    if (verify_file_pos_ == 0 && !verify_file_.isOpen())
    {
        QFileInfo const file_info(local_destination_, QString::fromUtf8(file->name));
        verify_file_.setFileName(file_info.absoluteFilePath());
        verify_file_.open(QIODevice::ReadOnly);
    }

    int64_t left_in_piece = getPieceSize(&info_, verify_piece_index_) - verify_piece_pos_;
    int64_t left_in_file = file->length - verify_file_pos_;
    int64_t bytes_this_pass = qMin(left_in_file, left_in_piece);
    bytes_this_pass = qMin(bytes_this_pass, static_cast<int64_t>(sizeof(verify_buf_)));

    if (verify_file_.isOpen() && verify_file_.seek(verify_file_pos_))
    {
        int64_t num_read = verify_file_.read(verify_buf_, bytes_this_pass);

        if (num_read == bytes_this_pass)
        {
            verify_hash_.addData(verify_buf_, num_read);
        }
    }

    left_in_piece -= bytes_this_pass;
    left_in_file -= bytes_this_pass;
    verify_piece_pos_ += bytes_this_pass;
    verify_file_pos_ += bytes_this_pass;

    verify_bins_[verify_file_index_] += bytes_this_pass;

    if (left_in_piece == 0)
    {
        QByteArray const result(verify_hash_.result());
        bool const matches = memcmp(result.constData(), info_.pieces[verify_piece_index_].hash, SHA_DIGEST_LENGTH) == 0;
        verify_flags_[verify_piece_index_] = matches;
        verify_piece_pos_ = 0;
        ++verify_piece_index_;
        verify_hash_.reset();

        FileList changed_files;

        if (matches)
        {
            for (auto i = verify_bins_.begin(), end = verify_bins_.end(); i != end; ++i)
            {
                TorrentFile& f(files_[i.key()]);
                f.have += i.value();
                changed_files.append(f);
            }
        }

        ui_.filesView->update(changed_files);
        verify_bins_.clear();
    }

    if (left_in_file == 0)
    {
        verify_file_.close();
        ++verify_file_index_;
        verify_file_pos_ = 0;
    }

    bool done = verify_piece_index_ >= info_.pieceCount;

    if (done)
    {
        uint64_t have = 0;

        for (TorrentFile const& f : files_)
        {
            have += f.have;
        }

        if (have == 0) // everything failed
        {
            // did the user accidentally specify the child directory instead of the parent?
            QStringList const tokens = QString::fromUtf8(file->name).split(QLatin1Char('/'));

            if (!tokens.empty() && local_destination_.dirName() == tokens.at(0))
            {
                // move up one directory and try again
                local_destination_.cdUp();
                onVerify();
                done = false;
            }
        }
    }

    if (done)
    {
        verify_timer_.stop();
    }
}
