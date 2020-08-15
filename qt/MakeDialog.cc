/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "MakeDialog.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QPushButton>
#include <QTimer>

#include <libtransmission/makemeta.h>
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

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
    MakeProgressDialog(Session& session, tr_metainfo_builder& builder, QWidget* parent = nullptr);

private slots:
    void onButtonBoxClicked(QAbstractButton* button);
    void onProgress();

private:
    Session& session_;
    tr_metainfo_builder& builder_;
    Ui::MakeProgressDialog ui_ = {};
    QTimer timer_;
};

} // namespace

MakeProgressDialog::MakeProgressDialog(Session& session, tr_metainfo_builder& builder, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    builder_(builder)
{
    ui_.setupUi(this);

    connect(ui_.dialogButtons, SIGNAL(clicked(QAbstractButton*)), this, SLOT(onButtonBoxClicked(QAbstractButton*)));

    connect(&timer_, SIGNAL(timeout()), this, SLOT(onProgress()));
    timer_.start(100);

    onProgress();
}

void MakeProgressDialog::onButtonBoxClicked(QAbstractButton* button)
{
    switch (ui_.dialogButtons->standardButton(button))
    {
    case QDialogButtonBox::Open:
        session_.addNewlyCreatedTorrent(QString::fromUtf8(builder_.outputFile),
            QFileInfo(QString::fromUtf8(builder_.top)).dir().path());
        break;

    case QDialogButtonBox::Abort:
        builder_.abortFlag = true;
        break;

    default: // QDialogButtonBox::Ok:
        break;
    }

    close();
}

void MakeProgressDialog::onProgress()
{
    // progress bar
    tr_metainfo_builder const& b = builder_;
    double const denom = b.pieceCount != 0 ? b.pieceCount : 1;
    ui_.progressBar->setValue(static_cast<int>((100.0 * b.pieceIndex) / denom));

    // progress label
    QString const top = QString::fromUtf8(b.top);
    QString const base(QFileInfo(top).completeBaseName());
    QString str;

    if (!b.isDone)
    {
        str = tr("Creating \"%1\"").arg(base);
    }
    else if (b.result == TR_MAKEMETA_OK)
    {
        str = tr("Created \"%1\"!").arg(base);
    }
    else if (b.result == TR_MAKEMETA_URL)
    {
        str = tr("Error: invalid announce URL \"%1\"").arg(QString::fromUtf8(b.errfile));
    }
    else if (b.result == TR_MAKEMETA_CANCELLED)
    {
        str = tr("Cancelled");
    }
    else if (b.result == TR_MAKEMETA_IO_READ)
    {
        str = tr("Error reading \"%1\": %2").arg(QString::fromUtf8(b.errfile)).
            arg(QString::fromUtf8(tr_strerror(b.my_errno)));
    }
    else if (b.result == TR_MAKEMETA_IO_WRITE)
    {
        str = tr("Error writing \"%1\": %2").arg(QString::fromUtf8(b.errfile)).
            arg(QString::fromUtf8(tr_strerror(b.my_errno)));
    }

    ui_.progressLabel->setText(str);

    // buttons
    ui_.dialogButtons->button(QDialogButtonBox::Abort)->setEnabled(!b.isDone);
    ui_.dialogButtons->button(QDialogButtonBox::Ok)->setEnabled(b.isDone);
    ui_.dialogButtons->button(QDialogButtonBox::Open)->setEnabled(b.isDone && b.result == TR_MAKEMETA_OK);
}

#include "MakeDialog.moc"

/***
****
***/

void MakeDialog::makeTorrent()
{
    if (builder_ == nullptr)
    {
        return;
    }

    // get the tiers
    int tier = 0;
    QVector<tr_tracker_info> trackers;

    for (QString const& line : ui_.trackersEdit->toPlainText().split(QLatin1Char('\n')))
    {
        QString const announce_url = line.trimmed();

        if (announce_url.isEmpty())
        {
            ++tier;
        }
        else
        {
            tr_tracker_info tmp;
            tmp.announce = tr_strdup(announce_url.toUtf8().constData());
            tmp.tier = tier;
            trackers.append(tmp);
        }
    }

    // the file to create
    QString const path = QString::fromUtf8(builder_->top);
    auto const torrent_name = QFileInfo(path).completeBaseName() + QStringLiteral(".torrent");
    QString const target = QDir(ui_.destinationButton->path()).filePath(torrent_name);

    // comment
    QString comment;

    if (ui_.commentCheck->isChecked())
    {
        comment = ui_.commentEdit->text();
    }

    // start making the torrent
    tr_makeMetaInfo(builder_.get(), target.toUtf8().constData(), trackers.isEmpty() ? nullptr : trackers.data(),
        trackers.size(), comment.isEmpty() ? nullptr : comment.toUtf8().constData(), ui_.privateCheck->isChecked());

    // pop up the dialog
    auto* dialog = new MakeProgressDialog(session_, *builder_, this);
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

    QString const filename = getSource();

    if (!filename.isEmpty())
    {
        builder_.reset(tr_metaInfoBuilderCreate(filename.toUtf8().constData()));
    }

    QString text;

    if (builder_ == nullptr)
    {
        text = tr("<i>No source selected</i>");
    }
    else
    {
        QString files = tr("%Ln File(s)", nullptr, builder_->fileCount);
        QString pieces = tr("%Ln Piece(s)", nullptr, builder_->pieceCount);
        text = tr("%1 in %2; %3 @ %4").arg(Formatter::get().sizeToString(builder_->totalSize)).arg(files).arg(pieces).
            arg(Formatter::get().sizeToString(builder_->pieceSize));
    }

    ui_.sourceSizeLabel->setText(text);
}

MakeDialog::MakeDialog(Session& session, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    builder_(nullptr, &tr_metaInfoBuilderFree)
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

    connect(ui_.sourceFolderRadio, SIGNAL(toggled(bool)), this, SLOT(onSourceChanged()));
    connect(ui_.sourceFolderButton, SIGNAL(pathChanged(QString)), this, SLOT(onSourceChanged()));
    connect(ui_.sourceFileRadio, SIGNAL(toggled(bool)), this, SLOT(onSourceChanged()));
    connect(ui_.sourceFileButton, SIGNAL(pathChanged(QString)), this, SLOT(onSourceChanged()));

    connect(ui_.dialogButtons, SIGNAL(accepted()), this, SLOT(makeTorrent()));
    connect(ui_.dialogButtons, SIGNAL(rejected()), this, SLOT(close()));

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
