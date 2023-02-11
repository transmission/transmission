// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "MakeDialog.h"

#include <chrono>
#include <future>
#include <utility>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QPushButton>
#include <QTimer>

#include <libtransmission/error.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/transmission.h>

#include "ColumnResizer.h"
#include "Formatter.h"
#include "Session.h"
#include "Utils.h"

#include "ui_MakeProgressDialog.h"

namespace
{

class MakeProgressDialog : public BaseDialog
{
    Q_OBJECT

public:
    MakeProgressDialog(
        Session& session,
        tr_metainfo_builder& builder,
        std::future<tr_error*> future,
        QString outfile,
        QWidget* parent = nullptr);

private slots:
    void onButtonBoxClicked(QAbstractButton* button);
    void onProgress();

private:
    Session& session_;
    tr_metainfo_builder& builder_;
    std::future<tr_error*> future_;
    QString const outfile_;
    Ui::MakeProgressDialog ui_ = {};
    QTimer timer_;
};

} // namespace

MakeProgressDialog::MakeProgressDialog(
    Session& session,
    tr_metainfo_builder& builder,
    std::future<tr_error*> future,
    QString outfile,
    QWidget* parent)
    : BaseDialog(parent)
    , session_(session)
    , builder_(builder)
    , future_(std::move(future))
    , outfile_(std::move(outfile))
{
    ui_.setupUi(this);

    connect(ui_.dialogButtons, &QDialogButtonBox::clicked, this, &MakeProgressDialog::onButtonBoxClicked);

    connect(&timer_, &QTimer::timeout, this, &MakeProgressDialog::onProgress);
    timer_.start(100);

    onProgress();
}

void MakeProgressDialog::onButtonBoxClicked(QAbstractButton* button)
{
    switch (ui_.dialogButtons->standardButton(button))
    {
    case QDialogButtonBox::Open:
        session_.addNewlyCreatedTorrent(outfile_, QFileInfo(QString::fromStdString(builder_.top())).dir().path());
        break;

    case QDialogButtonBox::Abort:
        builder_.cancelChecksums();
        break;

    default: // QDialogButtonBox::Ok:
        break;
    }

    close();
}

void MakeProgressDialog::onProgress()
{
    auto const is_done = !future_.valid() || future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;

    if (is_done)
    {
        timer_.stop();
    }

    // progress bar
    auto progress = int{ 100 }; // [0..100]
    if (!is_done)
    {
        auto const [current, total] = builder_.checksumStatus();
        progress = static_cast<int>((100.0 * current) / total);
    }
    ui_.progressBar->setValue(progress);

    // progress label
    auto const top = QString::fromStdString(builder_.top());
    auto const base = QFileInfo(top).completeBaseName();
    QString str;

    auto success = false;
    if (!is_done)
    {
        str = tr("Creating \"%1\"").arg(base);
    }
    else
    {
        tr_error* error = future_.get();

        if (error == nullptr)
        {
            builder_.save(outfile_.toStdString(), &error);
        }

        if (error == nullptr)
        {
            str = tr("Created \"%1\"!").arg(base);
            success = true;
        }
        else
        {
            str = tr("Couldn't create \"%1\": %2 (%3)").arg(base).arg(QString::fromUtf8(error->message)).arg(error->code);
            tr_error_free(error);
        }
    }

    ui_.progressLabel->setText(str);

    // buttons
    ui_.dialogButtons->button(QDialogButtonBox::Abort)->setEnabled(!is_done);
    ui_.dialogButtons->button(QDialogButtonBox::Ok)->setEnabled(is_done);
    ui_.dialogButtons->button(QDialogButtonBox::Open)->setEnabled(is_done && success);
}

#include "MakeDialog.moc"

/***
****
***/

void MakeDialog::makeTorrent()
{
    if (!builder_)
    {
        return;
    }

    // get the announce list
    auto trackers = tr_announce_list();
    trackers.parse(ui_.trackersEdit->toPlainText().toStdString());
    builder_->setAnnounceList(std::move(trackers));

    // the file to create
    auto const path = QString::fromStdString(builder_->top());
    auto const torrent_name = QFileInfo(path).completeBaseName() + QStringLiteral(".torrent");
    auto const outfile = QDir(ui_.destinationButton->path()).filePath(torrent_name);

    // comment
    if (ui_.commentCheck->isChecked())
    {
        builder_->setComment(ui_.commentEdit->text().toStdString());
    }

    // source
    if (ui_.sourceCheck->isChecked())
    {
        builder_->setSource(ui_.sourceEdit->text().toStdString());
    }

    builder_->setPrivate(ui_.privateCheck->isChecked());

    // pop up the dialog
    auto* dialog = new MakeProgressDialog(session_, *builder_, builder_->makeChecksums(), outfile, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

/***
****
***/

QString MakeDialog::getSource() const
{
    return (ui_.sourceFileRadio->isChecked() ? ui_.sourceFileButton : ui_.sourceFolderButton)->path();
}

/***
****
***/

void MakeDialog::onSourceChanged()
{
    builder_.reset();

    if (auto const filename = getSource(); !filename.isEmpty())
    {
        builder_.emplace(filename.toStdString());
    }

    if (!builder_)
    {
        updatePiecesLabel();
    }
    else
    {
        ui_.pieceSizeSlider->setValue(log2(builder_->pieceSize()));
    }
}

MakeDialog::MakeDialog(Session& session, QWidget* parent)
    : BaseDialog(parent)
    , session_(session)
{
    ui_.setupUi(this);

    ui_.destinationButton->setMode(PathButton::DirectoryMode);
    ui_.destinationButton->setPath(QDir::homePath());

    ui_.sourceFolderButton->setMode(PathButton::DirectoryMode);
    ui_.sourceFileButton->setMode(PathButton::FileMode);

    auto* cr = new ColumnResizer(this);
    cr->addLayout(ui_.filesSectionLayout);
    cr->addLayout(ui_.propertiesSectionLayout);
    cr->update();

    resize(minimumSizeHint());

    connect(ui_.sourceFolderRadio, &QAbstractButton::toggled, this, &MakeDialog::onSourceChanged);
    connect(ui_.sourceFolderButton, &PathButton::pathChanged, this, &MakeDialog::onSourceChanged);
    connect(ui_.sourceFileRadio, &QAbstractButton::toggled, this, &MakeDialog::onSourceChanged);
    connect(ui_.sourceFileButton, &PathButton::pathChanged, this, &MakeDialog::onSourceChanged);

    connect(ui_.dialogButtons, &QDialogButtonBox::accepted, this, &MakeDialog::makeTorrent);
    connect(ui_.dialogButtons, &QDialogButtonBox::rejected, this, &MakeDialog::close);
    connect(ui_.pieceSizeSlider, &QSlider::valueChanged, this, &MakeDialog::onPieceSizeUpdated);

    onSourceChanged();
}

/***
****
***/

void MakeDialog::dragEnterEvent(QDragEnterEvent* event)
{
    QMimeData const* mime = event->mimeData();

    if (!mime->urls().isEmpty() && QFileInfo(mime->urls().front().path()).exists())
    {
        event->acceptProposedAction();
    }
}

void MakeDialog::dropEvent(QDropEvent* event)
{
    QString const filename = event->mimeData()->urls().front().path();
    QFileInfo const file_info(filename);

    if (file_info.exists())
    {
        if (file_info.isDir())
        {
            ui_.sourceFolderRadio->setChecked(true);
            ui_.sourceFolderButton->setPath(filename);
        }
        else // it's a file
        {
            ui_.sourceFileRadio->setChecked(true);
            ui_.sourceFileButton->setPath(filename);
        }
    }
}

void MakeDialog::updatePiecesLabel()
{
    QString text;

    if (!builder_)
    {
        text = tr("<i>No source selected</i>");
        ui_.pieceSizeSlider->setEnabled(false);
    }
    else
    {
        auto const files = tr("%Ln File(s)", nullptr, builder_->fileCount());
        auto const pieces = tr("%Ln Piece(s)", nullptr, builder_->pieceCount());
        text = tr("%1 in %2; %3 @ %4")
                   .arg(Formatter::get().sizeToString(builder_->totalSize()))
                   .arg(files)
                   .arg(pieces)
                   .arg(Formatter::get().memToString(static_cast<uint64_t>(builder_->pieceSize())));
        ui_.pieceSizeSlider->setEnabled(true);
    }

    ui_.sourceSizeLabel->setText(text);
}

void MakeDialog::onPieceSizeUpdated(int value)
{
    auto new_size = static_cast<uint64_t>(pow(2, value));

    if (builder_)
    {
        builder_->setPieceSize(new_size);
    }

    updatePiecesLabel();
}
