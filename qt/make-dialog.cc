/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <cassert>
#include <iostream>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/utils.h>

#include "formatter.h"
#include "hig.h"
#include "make-dialog.h"
#include "session.h"
#include "utils.h"

/***
****
***/

void
MakeDialog::onNewDialogDestroyed (QObject * o)
{
  Q_UNUSED (o);

  myTimer.stop ();
}

void
MakeDialog::onNewButtonBoxClicked (QAbstractButton * button)
{
  switch (myNewButtonBox->standardButton (button))
    {
      case QDialogButtonBox::Open:
        mySession.addNewlyCreatedTorrent (myTarget, QFileInfo(QString::fromUtf8(myBuilder->top)).dir().path());
        break;

      case QDialogButtonBox::Abort:
        myBuilder->abortFlag = true;
        break;

      default: // QDialogButtonBox::Ok:
        break;
    }

  myNewDialog->deleteLater ();
}

void
MakeDialog::onProgress ()
{
  // progress bar
  const tr_metainfo_builder * b = myBuilder;
  const double denom = b->pieceCount ? b->pieceCount : 1;
  myNewProgress->setValue ((int) ((100.0 * b->pieceIndex) / denom));

  // progress label
  const QString top = QString::fromLocal8Bit (myBuilder->top);
  const QString base (QFileInfo(top).completeBaseName());
  QString str;
  if (!b->isDone)
    str = tr ("Creating \"%1\"").arg (base);
  else if (b->result == TR_MAKEMETA_OK)
    str = tr ("Created \"%1\"!").arg (base);
  else if (b->result == TR_MAKEMETA_URL)
    str = tr ("Error: invalid announce URL \"%1\"").arg (QString::fromLocal8Bit (b->errfile));
  else if (b->result == TR_MAKEMETA_CANCELLED)
    str = tr ("Cancelled");
  else if (b->result == TR_MAKEMETA_IO_READ)
    str = tr ("Error reading \"%1\": %2").arg (QString::fromLocal8Bit(b->errfile)).arg (QString::fromLocal8Bit(strerror(b->my_errno)));
  else if (b->result == TR_MAKEMETA_IO_WRITE)
    str = tr ("Error writing \"%1\": %2").arg (QString::fromLocal8Bit(b->errfile)).arg (QString::fromLocal8Bit(strerror(b->my_errno)));
  myNewLabel->setText (str);

  // buttons
  (myNewButtonBox->button(QDialogButtonBox::Abort))->setEnabled (!b->isDone);
  (myNewButtonBox->button(QDialogButtonBox::Ok))->setEnabled (b->isDone);
  (myNewButtonBox->button(QDialogButtonBox::Open))->setEnabled (b->isDone && !b->result);
}


void
MakeDialog::makeTorrent ()
{
  if (!myBuilder)
    return;

  // get the tiers
  int tier = 0;
  QVector<tr_tracker_info> trackers;
  foreach (QString line, myTrackerEdit->toPlainText().split("\n"))
    {
      line = line.trimmed ();
      if (line.isEmpty ())
        {
          ++tier;
        }
      else
        {
          tr_tracker_info tmp;
          tmp.announce = tr_strdup (line.toUtf8().constData ());
          tmp.tier = tier;
          trackers.append (tmp);
        }
    }

  // pop up the dialog
  QDialog * dialog = new QDialog (this);
  dialog->setWindowTitle (tr ("New Torrent"));
  myNewDialog = dialog;
  QVBoxLayout * top = new QVBoxLayout (dialog);
  top->addWidget( (myNewLabel = new QLabel));
  top->addWidget( (myNewProgress = new QProgressBar));
  QDialogButtonBox * buttons = new QDialogButtonBox (QDialogButtonBox::Ok
                                                   | QDialogButtonBox::Open
                                                   | QDialogButtonBox::Abort);
  myNewButtonBox = buttons;
  connect (buttons, SIGNAL(clicked(QAbstractButton*)),
           this, SLOT(onNewButtonBoxClicked(QAbstractButton*)));
  top->addWidget (buttons);
  onProgress ();
  dialog->show ();
  connect (dialog, SIGNAL(destroyed(QObject*)),
           this, SLOT(onNewDialogDestroyed(QObject*)));
  myTimer.start (100);

  // the file to create
  const QString path = QString::fromUtf8 (myBuilder->top);
  const QString torrentName = QFileInfo(path).completeBaseName() + ".torrent";
  myTarget = QDir (myDestination).filePath (torrentName);

  // comment
  QString comment;
  if (myCommentCheck->isChecked())
    comment = myCommentEdit->text();

  // start making the torrent
  tr_makeMetaInfo (myBuilder,
                   myTarget.toUtf8().constData(),
                   (trackers.isEmpty() ? NULL : trackers.data()),
                   trackers.size(),
                   (comment.isEmpty() ? NULL : comment.toUtf8().constData()),
                   myPrivateCheck->isChecked());
}

/***
****
***/

void
MakeDialog::onFileClicked ()
{
  QFileDialog * d = new QFileDialog (this, tr ("Select File"));
  d->setFileMode (QFileDialog::ExistingFile);
  d->setAttribute (Qt::WA_DeleteOnClose);
  connect (d, SIGNAL(filesSelected(QStringList)),
           this, SLOT(onFileSelected(QStringList)));
  d->show ();
}
void
MakeDialog::onFileSelected (const QStringList& list)
{
  if (!list.empty ())
    onFileSelected (list.front ());
}
void
MakeDialog::onFileSelected (const QString& filename)
{
  myFile = Utils::removeTrailingDirSeparator (filename);
  myFileButton->setText (QFileInfo(myFile).fileName());
  onSourceChanged ();
}

void
MakeDialog::onFolderClicked ()
{
  QFileDialog * d = new QFileDialog (this, tr ("Select Folder"));
  d->setFileMode (QFileDialog::Directory);
  d->setOption (QFileDialog::ShowDirsOnly);
  d->setAttribute (Qt::WA_DeleteOnClose);
  connect (d, SIGNAL(filesSelected(QStringList)),
           this, SLOT(onFolderSelected(QStringList)));
  d->show ();
}

void
MakeDialog::onFolderSelected (const QStringList& list)
{
  if (!list.empty ())
    onFolderSelected (list.front ());
}

void
MakeDialog::onFolderSelected (const QString& filename)
{
  myFolder = Utils::removeTrailingDirSeparator (filename);
  myFolderButton->setText (QFileInfo(myFolder).fileName());
  onSourceChanged ();
}

void
MakeDialog::onDestinationClicked ()
{
  QFileDialog * d = new QFileDialog (this, tr ("Select Folder"));
  d->setFileMode (QFileDialog::Directory);
  d->setOption (QFileDialog::ShowDirsOnly);
  d->setAttribute (Qt::WA_DeleteOnClose);
  connect (d, SIGNAL(filesSelected(QStringList)),
           this, SLOT(onDestinationSelected(QStringList)));
  d->show ();
}
void
MakeDialog::onDestinationSelected (const QStringList& list)
{
  if (!list.empty ())
    onDestinationSelected (list.front());
}
void
MakeDialog::onDestinationSelected (const QString& filename)
{
  myDestination = Utils::removeTrailingDirSeparator (filename);
  myDestinationButton->setText (QFileInfo(myDestination).fileName());
}

void
MakeDialog::enableBuddyWhenChecked (QRadioButton * box, QWidget * buddy)
{
  connect (box, SIGNAL(toggled(bool)), buddy, SLOT(setEnabled(bool)));
  buddy->setEnabled (box->isChecked ());
}
void
MakeDialog::enableBuddyWhenChecked (QCheckBox * box, QWidget * buddy)
{
  connect (box, SIGNAL(toggled(bool)), buddy, SLOT(setEnabled(bool)));
  buddy->setEnabled (box->isChecked ());
}

QString
MakeDialog::getSource () const
{
  return myFileRadio->isChecked () ? myFile : myFolder;
}

void
MakeDialog::onButtonBoxClicked (QAbstractButton * button)
{
  switch (myButtonBox->standardButton (button))
    {
      case QDialogButtonBox::Ok:
        makeTorrent ();
        break;

      default: // QDialogButtonBox::Close:
        deleteLater ();
        break;
    }
}

/***
****
***/

void
MakeDialog::onSourceChanged ()
{
  if (myBuilder)
    {
      tr_metaInfoBuilderFree (myBuilder);
      myBuilder = 0;
    }

  const QString filename = getSource ();
  if (!filename.isEmpty ())
    myBuilder = tr_metaInfoBuilderCreate (filename.toUtf8().constData());

  QString text;
  if (!myBuilder)
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

  mySourceLabel->setText (text);
}


// bah, there doesn't seem to be any cleaner way to override
// QPlainTextEdit's default desire to be 12 lines tall
class ShortPlainTextEdit: public QPlainTextEdit
{
  public:
    virtual ~ShortPlainTextEdit () {}
    ShortPlainTextEdit (QWidget * parent = 0): QPlainTextEdit(parent) {}
    virtual QSize sizeHint  () const { return QSize (256, 50); }
};

MakeDialog::MakeDialog (Session& session, QWidget * parent):
  QDialog (parent, Qt::Dialog),
  mySession (session),
  myBuilder (0)
{
  setAcceptDrops (true);

  connect (&myTimer, SIGNAL(timeout()), this, SLOT(onProgress()));

  setWindowTitle (tr ("New Torrent"));
  QVBoxLayout * top = new QVBoxLayout (this);
  top->setSpacing (HIG::PAD);

  HIG * hig = new HIG;
  hig->setContentsMargins (0, 0, 0, 0);
  hig->addSectionTitle (tr ("Files"));

    QFileIconProvider iconProvider;
    const int iconSize (style()->pixelMetric (QStyle::PM_SmallIconSize));
    const QIcon folderIcon = iconProvider.icon (QFileIconProvider::Folder);
    const QPixmap folderPixmap = folderIcon.pixmap (iconSize);
    QPushButton * b = new QPushButton;
    b->setIcon (folderPixmap);
    b->setStyleSheet (QString::fromUtf8 ("text-align: left; padding-left: 5; padding-right: 5"));
    myDestination = QDir::homePath();
    b->setText (myDestination);
    connect (b, SIGNAL(clicked(bool)),
             this, SLOT(onDestinationClicked()));
    myDestinationButton = b;
    hig->addRow (tr ("Sa&ve to:"), b);

    myFolderRadio = new QRadioButton (tr ("Source F&older:"));
    connect (myFolderRadio, SIGNAL(toggled(bool)),
             this, SLOT(onSourceChanged()));
    myFolderButton = new QPushButton;
    myFolderButton->setIcon (folderPixmap);
    myFolderButton->setText (tr ("(None)"));
    myFolderButton->setStyleSheet (QString::fromUtf8 ("text-align: left; padding-left: 5; padding-right: 5"));
    connect (myFolderButton, SIGNAL(clicked(bool)),
             this, SLOT(onFolderClicked()));
    hig->addRow (myFolderRadio, myFolderButton);
    enableBuddyWhenChecked (myFolderRadio, myFolderButton);

    const QIcon fileIcon = iconProvider.icon (QFileIconProvider::File);
    const QPixmap filePixmap = fileIcon.pixmap (iconSize);
    myFileRadio = new QRadioButton (tr ("Source &File:"));
    myFileRadio->setChecked (true);
    connect (myFileRadio, SIGNAL(toggled(bool)),
             this, SLOT(onSourceChanged()));
    myFileButton = new QPushButton;
    myFileButton->setText (tr ("(None)"));
    myFileButton->setIcon (filePixmap);
    myFileButton->setStyleSheet (QString::fromUtf8 ("text-align: left; padding-left: 5; padding-right: 5"));
    connect (myFileButton, SIGNAL(clicked(bool)),
             this, SLOT(onFileClicked()));
    hig->addRow (myFileRadio, myFileButton);
    enableBuddyWhenChecked (myFileRadio, myFileButton);

    mySourceLabel = new QLabel (this);
    hig->addRow (tr (""), mySourceLabel);

  hig->addSectionDivider ();
  hig->addSectionTitle (tr ("Properties"));

    hig->addWideControl (myTrackerEdit = new ShortPlainTextEdit);
    const int height = fontMetrics().size (0, QString::fromUtf8("\n\n\n\n")).height ();
    myTrackerEdit->setMinimumHeight (height);
    hig->addTallRow (tr ("&Trackers:"), myTrackerEdit);
    QLabel * l = new QLabel (tr ("To add a backup URL, add it on the line after the primary URL.\nTo add another primary URL, add it after a blank line."));
    l->setAlignment (Qt::AlignLeft);
    hig->addRow (tr (""), l);
    myTrackerEdit->resize (500, height);

    myCommentCheck = new QCheckBox (tr ("Co&mment"));
    myCommentEdit = new QLineEdit ();
    hig->addRow (myCommentCheck, myCommentEdit);
    enableBuddyWhenChecked (myCommentCheck, myCommentEdit);

    myPrivateCheck = hig->addWideCheckBox (tr ("&Private torrent"), false);

  hig->finish ();
  top->addWidget (hig, 1);

  myButtonBox = new QDialogButtonBox (QDialogButtonBox::Ok
                                    | QDialogButtonBox::Close);
  connect (myButtonBox, SIGNAL(clicked(QAbstractButton*)),
           this, SLOT(onButtonBoxClicked(QAbstractButton*)));

  top->addWidget (myButtonBox);
  onSourceChanged ();
}

MakeDialog::~MakeDialog ()
{
  if (myBuilder)
    tr_metaInfoBuilderFree (myBuilder);
}

/***
****
***/

void
MakeDialog::dragEnterEvent (QDragEnterEvent * event)
{
  const QMimeData * mime = event->mimeData ();

  if (mime->urls().size() && QFile(mime->urls().front().path()).exists ())
    event->acceptProposedAction();
}

void
MakeDialog::dropEvent (QDropEvent * event)
{
  const QString filename = event->mimeData()->urls().front().path();
  const QFileInfo fileInfo (filename);

  if (fileInfo.exists ())
    {
      if (fileInfo.isDir ())
        {
          myFolderRadio->setChecked (true);
          onFolderSelected (filename );
        }
      else // it's a file
        {
          myFileRadio->setChecked (true);
          onFileSelected (filename);
        }
    }
}
