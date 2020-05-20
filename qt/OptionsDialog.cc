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

/***
****
***/

OptionsDialog::OptionsDialog(Session& session, Prefs const& prefs, AddData const& addme, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    add_(addme),
    is_local_(session_.isLocal()),
    have_info_(false),
    verify_button_(new QPushButton(tr("&Verify Local Data"), this)),
    verify_file_(nullptr),
    verify_hash_(QCryptographicHash::Sha1),
    edit_timer_(this)
{
    ui.setupUi(this);

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
    connect(&edit_timer_, SIGNAL(timeout()), this, SLOT(onDestinationChanged()));

    if (add_.type == AddData::FILENAME)
    {
        ui.sourceStack->setCurrentWidget(ui.sourceButton);
        ui.sourceButton->setMode(PathButton::FileMode);
        ui.sourceButton->setTitle(tr("Open Torrent"));
        ui.sourceButton->setNameFilter(tr("Torrent Files (*.torrent);;All Files (*.*)"));
        ui.sourceButton->setPath(add_.filename);
        connect(ui.sourceButton, SIGNAL(pathChanged(QString)), this, SLOT(onSourceChanged()));
    }
    else
    {
        ui.sourceStack->setCurrentWidget(ui.sourceEdit);
        ui.sourceEdit->setText(add_.readableName());
        ui.sourceEdit->selectAll();
        connect(ui.sourceEdit, SIGNAL(editingFinished()), this, SLOT(onSourceChanged()));
    }

    ui.sourceStack->setFixedHeight(ui.sourceStack->currentWidget()->sizeHint().height());
    ui.sourceLabel->setBuddy(ui.sourceStack->currentWidget());

    QFontMetrics const fontMetrics(font());
    int const width = fontMetrics.size(0, QString::fromUtf8("This is a pretty long torrent filename indeed.torrent")).width();
    ui.sourceStack->setMinimumWidth(width);

    QString const downloadDir(Utils::removeTrailingDirSeparator(prefs.getString(Prefs::DOWNLOAD_DIR)));
    ui.freeSpaceLabel->setSession(session_);
    ui.freeSpaceLabel->setPath(downloadDir);

    ui.destinationButton->setMode(PathButton::DirectoryMode);
    ui.destinationButton->setTitle(tr("Select Destination"));
    ui.destinationButton->setPath(downloadDir);
    ui.destinationEdit->setText(downloadDir);

    if (is_local_)
    {
        local_destination_.setPath(downloadDir);
    }

    connect(ui.destinationButton, SIGNAL(pathChanged(QString)), this, SLOT(onDestinationChanged()));
    connect(ui.destinationEdit, SIGNAL(textEdited(QString)), &edit_timer_, SLOT(start()));
    connect(ui.destinationEdit, SIGNAL(editingFinished()), this, SLOT(onDestinationChanged()));

    ui.filesView->setEditable(false);

    ui.priorityCombo->addItem(tr("High"), TR_PRI_HIGH);
    ui.priorityCombo->addItem(tr("Normal"), TR_PRI_NORMAL);
    ui.priorityCombo->addItem(tr("Low"), TR_PRI_LOW);
    ui.priorityCombo->setCurrentIndex(1); // Normal

    ui.dialogButtons->addButton(verify_button_, QDialogButtonBox::ActionRole);
    connect(verify_button_, SIGNAL(clicked(bool)), this, SLOT(onVerify()));

    ui.startCheck->setChecked(prefs.getBool(Prefs::START));
    ui.trashCheck->setChecked(prefs.getBool(Prefs::TRASH_ORIGINAL));

    connect(ui.dialogButtons, SIGNAL(rejected()), this, SLOT(deleteLater()));
    connect(ui.dialogButtons, SIGNAL(accepted()), this, SLOT(onAccepted()));

    connect(ui.filesView, SIGNAL(priorityChanged(QSet<int>, int)), this, SLOT(onPriorityChanged(QSet<int>, int)));
    connect(ui.filesView, SIGNAL(wantedChanged(QSet<int>, bool)), this, SLOT(onWantedChanged(QSet<int>, bool)));

    connect(&verify_timer_, SIGNAL(timeout()), this, SLOT(onTimeout()));

    connect(&session_, SIGNAL(sessionUpdated()), SLOT(onSessionUpdated()));

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
        tr_ctorSetMetainfo(ctor, reinterpret_cast<quint8 const*>(add_.metainfo.constData()), add_.metainfo.size());
        break;

    default:
        break;
    }

    int const err = tr_torrentParse(ctor, &info_);
    have_info_ = err == 0;
    tr_ctorFree(ctor);

    ui.filesView->clear();
    files_.clear();
    priorities_.clear();
    wanted_.clear();

    bool const haveFilesToShow = have_info_ && info_.fileCount > 0;

    ui.filesView->setVisible(haveFilesToShow);
    verify_button_->setEnabled(haveFilesToShow);
    layout()->setSizeConstraint(haveFilesToShow ? QLayout::SetDefaultConstraint : QLayout::SetFixedSize);

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

    ui.filesView->update(files_);
}

void OptionsDialog::updateWidgetsLocality()
{
    ui.destinationStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui.destinationButton) : ui.destinationEdit);
    ui.destinationStack->setFixedHeight(ui.destinationStack->currentWidget()->sizeHint().height());
    ui.destinationLabel->setBuddy(ui.destinationStack->currentWidget());

    // hide the % done when non-local, since we've no way of knowing
    (ui.filesView->*(is_local_ ? &QTreeView::showColumn : &QTreeView::hideColumn))(2);

    verify_button_->setVisible(is_local_);
}

void OptionsDialog::onSessionUpdated()
{
    bool const isLocal = session_.isLocal();

    if (is_local_ != isLocal)
    {
        is_local_ = isLocal;
        updateWidgetsLocality();
    }
}

void OptionsDialog::onPriorityChanged(QSet<int> const& fileIndices, int priority)
{
    for (int const i : fileIndices)
    {
        priorities_[i] = priority;
    }
}

void OptionsDialog::onWantedChanged(QSet<int> const& fileIndices, bool isWanted)
{
    for (int const i : fileIndices)
    {
        wanted_[i] = isWanted;
    }
}

void OptionsDialog::onAccepted()
{
    // rpc spec section 3.4 "adding a torrent"

    tr_variant args;
    tr_variantInitDict(&args, 10);
    QString downloadDir;

    // "download-dir"
    if (ui.destinationStack->currentWidget() == ui.destinationButton)
    {
        downloadDir = local_destination_.absolutePath();
    }
    else
    {
        downloadDir = ui.destinationEdit->text();
    }

    tr_variantDictAddStr(&args, TR_KEY_download_dir, downloadDir.toUtf8().constData());

    // paused
    tr_variantDictAddBool(&args, TR_KEY_paused, !ui.startCheck->isChecked());

    // priority
    int const index = ui.priorityCombo->currentIndex();
    int const priority = ui.priorityCombo->itemData(index).toInt();
    tr_variantDictAddInt(&args, TR_KEY_bandwidthPriority, priority);

    // files-unwanted
    int count = wanted_.count(false);

    if (count > 0)
    {
        tr_variant* l = tr_variantDictAddList(&args, TR_KEY_files_unwanted, count);

        for (int i = 0, n = wanted_.size(); i < n; ++i)
        {
            if (!wanted_.at(i))
            {
                tr_variantListAddInt(l, i);
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
                tr_variantListAddInt(l, i);
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
                tr_variantListAddInt(l, i);
            }
        }
    }

    session_.addTorrent(add_, &args, ui.trashCheck->isChecked());

    deleteLater();
}

void OptionsDialog::onSourceChanged()
{
    if (ui.sourceStack->currentWidget() == ui.sourceButton)
    {
        add_.set(ui.sourceButton->path());
    }
    else
    {
        add_.set(ui.sourceEdit->text());
    }

    reload();
}

void OptionsDialog::onDestinationChanged()
{
    if (ui.destinationStack->currentWidget() == ui.destinationButton)
    {
        local_destination_.setPath(ui.destinationButton->path());
        ui.freeSpaceLabel->setPath(local_destination_.absolutePath());
    }
    else
    {
        ui.freeSpaceLabel->setPath(ui.destinationEdit->text());
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

    ui.filesView->update(files_);
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

uint64_t getPieceSize(tr_info const* info, tr_piece_index_t pieceIndex)
{
    if (pieceIndex != info->pieceCount - 1)
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
        QFileInfo const fileInfo(local_destination_, QString::fromUtf8(file->name));
        verify_file_.setFileName(fileInfo.absoluteFilePath());
        verify_file_.open(QIODevice::ReadOnly);
    }

    int64_t leftInPiece = getPieceSize(&info_, verify_piece_index_) - verify_piece_pos_;
    int64_t leftInFile = file->length - verify_file_pos_;
    int64_t bytesThisPass = qMin(leftInFile, leftInPiece);
    bytesThisPass = qMin(bytesThisPass, static_cast<int64_t>(sizeof(verify_buf_)));

    if (verify_file_.isOpen() && verify_file_.seek(verify_file_pos_))
    {
        int64_t numRead = verify_file_.read(verify_buf_, bytesThisPass);

        if (numRead == bytesThisPass)
        {
            verify_hash_.addData(verify_buf_, numRead);
        }
    }

    leftInPiece -= bytesThisPass;
    leftInFile -= bytesThisPass;
    verify_piece_pos_ += bytesThisPass;
    verify_file_pos_ += bytesThisPass;

    verify_bins_[verify_file_index_] += bytesThisPass;

    if (leftInPiece == 0)
    {
        QByteArray const result(verify_hash_.result());
        bool const matches = memcmp(result.constData(), info_.pieces[verify_piece_index_].hash, SHA_DIGEST_LENGTH) == 0;
        verify_flags_[verify_piece_index_] = matches;
        verify_piece_pos_ = 0;
        ++verify_piece_index_;
        verify_hash_.reset();

        FileList changedFiles;

        if (matches)
        {
            for (auto i = verify_bins_.begin(), end = verify_bins_.end(); i != end; ++i)
            {
                TorrentFile& f(files_[i.key()]);
                f.have += i.value();
                changedFiles.append(f);
            }
        }

        ui.filesView->update(changedFiles);
        verify_bins_.clear();
    }

    if (leftInFile == 0)
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
