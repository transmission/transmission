/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QPushButton>
#include <QTimer>

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/utils.h>

#include "ColumnResizer.h"
#include "Formatter.h"
#include "MakeDialog.h"
#include "Session.h"
#include "Utils.h"

#include "ui_MakeProgressDialog.h"

namespace
{
  class MakeProgressDialog: public BaseDialog
  {
      Q_OBJECT

    public:
      MakeProgressDialog (Session& session, tr_metainfo_builder& builder, QWidget * parent = nullptr);

    private slots:
      void onButtonBoxClicked (QAbstractButton *);
      void onProgress ();

    private:
      Session& mySession;
      tr_metainfo_builder& myBuilder;
      Ui::MakeProgressDialog ui;
      QTimer myTimer;
  };
}

MakeProgressDialog::MakeProgressDialog (Session& session, tr_metainfo_builder& builder, QWidget * parent):
  BaseDialog (parent),
  mySession (session),
  myBuilder (builder)
{
  ui.setupUi (this);

  connect (ui.dialogButtons, SIGNAL (clicked (QAbstractButton *)),
           this, SLOT (onButtonBoxClicked (QAbstractButton *)));

  connect (&myTimer, SIGNAL (timeout ()), this, SLOT (onProgress ()));
  myTimer.start (100);

  onProgress ();
}

void
MakeProgressDialog::onButtonBoxClicked (QAbstractButton * button)
{
  switch (ui.dialogButtons->standardButton (button))
    {
      case QDialogButtonBox::Open:
        mySession.addNewlyCreatedTorrent (QString::fromUtf8 (myBuilder.outputFile),
                                          QFileInfo (QString::fromUtf8 (myBuilder.top)).dir ().path ());
        break;

      case QDialogButtonBox::Abort:
        myBuilder.abortFlag = true;
        break;

      default: // QDialogButtonBox::Ok:
        break;
    }

  close ();
}

void
MakeProgressDialog::onProgress ()
{
  // progress bar
  const tr_metainfo_builder& b = myBuilder;
  const double denom = b.pieceCount ? b.pieceCount : 1;
  ui.progressBar->setValue (static_cast<int> ((100.0 * b.pieceIndex) / denom));

  // progress label
  const QString top = QString::fromUtf8 (b.top);
  const QString base (QFileInfo (top).completeBaseName ());
  QString str;
  if (!b.isDone)
    str = tr ("Creating \"%1\"").arg (base);
  else if (b.result == TR_MAKEMETA_OK)
    str = tr ("Created \"%1\"!").arg (base);
  else if (b.result == TR_MAKEMETA_URL)
    str = tr ("Error: invalid announce URL \"%1\"").arg (QString::fromUtf8 (b.errfile));
  else if (b.result == TR_MAKEMETA_CANCELLED)
    str = tr ("Cancelled");
  else if (b.result == TR_MAKEMETA_IO_READ)
    str = tr ("Error reading \"%1\": %2").arg (QString::fromUtf8 (b.errfile)).
                                          arg (QString::fromLocal8Bit (tr_strerror (b.my_errno)));
  else if (b.result == TR_MAKEMETA_IO_WRITE)
    str = tr ("Error writing \"%1\": %2").arg (QString::fromUtf8 (b.errfile)).
                                          arg (QString::fromLocal8Bit (tr_strerror (b.my_errno)));
  ui.progressLabel->setText (str);

  // buttons
  ui.dialogButtons->button (QDialogButtonBox::Abort)->setEnabled (!b.isDone);
  ui.dialogButtons->button (QDialogButtonBox::Ok)->setEnabled (b.isDone);
  ui.dialogButtons->button (QDialogButtonBox::Open)->setEnabled (b.isDone && b.result == TR_MAKEMETA_OK);
}

#include "MakeDialog.moc"

/***
****
***/

void
MakeDialog::makeTorrent ()
{
  if (myBuilder == nullptr)
    return;

  // get the tiers
  int tier = 0;
  QVector<tr_tracker_info> trackers;
  for (const QString& line: ui.trackersEdit->toPlainText ().split (QLatin1Char ('\n')))
    {
      const QString announceUrl = line.trimmed ();
      if (announceUrl.isEmpty ())
        {
          ++tier;
        }
      else
        {
          tr_tracker_info tmp;
          tmp.announce = tr_strdup (announceUrl.toUtf8 ().constData ());
          tmp.tier = tier;
          trackers.append (tmp);
        }
    }

  // the file to create
  const QString path = QString::fromUtf8 (myBuilder->top);
  const QString torrentName = QFileInfo (path).completeBaseName () + QLatin1String (".torrent");
  const QString target = QDir (ui.destinationButton->path ()).filePath (torrentName);

  // comment
  QString comment;
  if (ui.commentCheck->isChecked ())
    comment = ui.commentEdit->text ();

  // start making the torrent
  tr_makeMetaInfo (myBuilder.get (),
                   target.toUtf8 ().constData (),
                   trackers.isEmpty () ? NULL : trackers.data (),
                   trackers.size (),
                   comment.isEmpty () ? NULL : comment.toUtf8 ().constData (),
                   ui.privateCheck->isChecked ());

  // pop up the dialog
  MakeProgressDialog * dialog = new MakeProgressDialog (mySession, *myBuilder, this);
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->open ();
}

/***
****
***/

QString
MakeDialog::getSource () const
{
  return (ui.sourceFileRadio->isChecked () ? ui.sourceFileButton : ui.sourceFolderButton)->path ();
}

/***
****
***/

void
MakeDialog::onSourceChanged ()
{
  myBuilder.reset ();

  const QString filename = getSource ();
  if (!filename.isEmpty ())
    myBuilder.reset (tr_metaInfoBuilderCreate (filename.toUtf8 ().constData ()));

  QString text;
  if (myBuilder == nullptr)
    {
      text = tr ("<i>No source selected<i>");
    }
  else
    {
      QString files = tr ("%Ln File(s)", 0, myBuilder->fileCount);
      QString pieces = tr ("%Ln Piece(s)", 0, myBuilder->pieceCount);
      text = tr ("%1 in %2; %3 @ %4")
               .arg (Formatter::sizeToString (myBuilder->totalSize))
               .arg (files)
               .arg (pieces)
               .arg (Formatter::sizeToString (myBuilder->pieceSize));
    }

  ui.sourceSizeLabel->setText (text);
}

MakeDialog::MakeDialog (Session& session, QWidget * parent):
  BaseDialog (parent),
  mySession (session),
  myBuilder (nullptr, &tr_metaInfoBuilderFree)
{
  ui.setupUi (this);

  ui.destinationButton->setMode (PathButton::DirectoryMode);
  ui.destinationButton->setPath (QDir::homePath ());

  ui.sourceFolderButton->setMode (PathButton::DirectoryMode);
  ui.sourceFileButton->setMode (PathButton::FileMode);

  ColumnResizer * cr (new ColumnResizer (this));
  cr->addLayout (ui.filesSectionLayout);
  cr->addLayout (ui.propertiesSectionLayout);
  cr->update ();

  resize (minimumSizeHint ());

  connect (ui.sourceFolderRadio, SIGNAL (toggled (bool)), this, SLOT (onSourceChanged ()));
  connect (ui.sourceFolderButton, SIGNAL (pathChanged (QString)), this, SLOT (onSourceChanged ()));
  connect (ui.sourceFileRadio, SIGNAL (toggled (bool)), this, SLOT (onSourceChanged ()));
  connect (ui.sourceFileButton, SIGNAL (pathChanged (QString)), this, SLOT (onSourceChanged ()));

  connect (ui.dialogButtons, SIGNAL (accepted ()), this, SLOT (makeTorrent ()));
  connect (ui.dialogButtons, SIGNAL (rejected ()), this, SLOT (close ()));

  onSourceChanged ();
}

MakeDialog::~MakeDialog ()
{
}

/***
****
***/

void
MakeDialog::dragEnterEvent (QDragEnterEvent * event)
{
  const QMimeData * mime = event->mimeData ();

  if (!mime->urls ().isEmpty () && QFileInfo (mime->urls ().front ().path ()).exists ())
    event->acceptProposedAction ();
}

void
MakeDialog::dropEvent (QDropEvent * event)
{
  const QString filename = event->mimeData ()->urls ().front ().path ();
  const QFileInfo fileInfo (filename);

  if (fileInfo.exists ())
    {
      if (fileInfo.isDir ())
        {
          ui.sourceFolderRadio->setChecked (true);
          ui.sourceFolderButton->setPath (filename);
        }
      else // it's a file
        {
          ui.sourceFileRadio->setChecked (true);
          ui.sourceFileButton->setPath (filename);
        }
    }
}
