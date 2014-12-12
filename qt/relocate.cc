/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

#include "hig.h"
#include "relocate.h"
#include "session.h"
#include "torrent.h"
#include "torrent-model.h"
#include "utils.h"

bool RelocateDialog::myMoveFlag = true;

void
RelocateDialog::onSetLocation ()
{
  mySession.torrentSetLocation (myIds, myPath, myMoveFlag);
  deleteLater ();
}

void
RelocateDialog::onFileSelected (const QString& path)
{
  myPath = path;
  myDirButton->setText (myPath);
}

void
RelocateDialog::onDirButtonClicked ()
{
  const QString title = tr ("Select Location");
  const QString path = Utils::remoteFileChooser (this, title, myPath, true, mySession.isServer ());

  if (!path.isEmpty ())
    onFileSelected (path);
}

void
RelocateDialog::onMoveToggled (bool b)
{
  myMoveFlag = b;
}

RelocateDialog::RelocateDialog (Session          & session,
                                TorrentModel     & model,
                                const QSet<int>  & ids,
                                QWidget          * parent):
  QDialog (parent),
  mySession (session),
  myModel (model),
  myIds (ids)
{
  const int iconSize (style ()->pixelMetric (QStyle::PM_SmallIconSize));
  const QFileIconProvider iconProvider;
  const QIcon folderIcon = iconProvider.icon (QFileIconProvider::Folder);
  const QPixmap folderPixmap = folderIcon.pixmap (iconSize);

  QRadioButton * find_rb;
  setWindowTitle (tr ("Set Torrent Location"));

  foreach (int id, myIds)
    {
      const Torrent * tor = myModel.getTorrentFromId (id);

      if (myPath.isEmpty ())
        {
          myPath = tor->getPath ();
        }
      else if (myPath != tor->getPath ())
        {
          if (mySession.isServer ())
            myPath = QDir::homePath ();
          else
            myPath = QDir::rootPath ();
        }
    }

  HIG * hig = new HIG ();
  hig->addSectionTitle (tr ("Set Location"));
  hig->addRow (tr ("New &location:"), myDirButton = new QPushButton (folderPixmap, myPath));
  hig->addWideControl (myMoveRadio = new QRadioButton (tr ("&Move from the current folder"), this));
  hig->addWideControl (find_rb = new QRadioButton (tr ("Local data is &already there"), this));
  hig->finish ();

  if (myMoveFlag)
    myMoveRadio->setChecked (true);
  else
    find_rb->setChecked (true);

  connect (myMoveRadio, SIGNAL (toggled (bool)), this, SLOT (onMoveToggled (bool)));
  connect (myDirButton, SIGNAL (clicked (bool)), this, SLOT (onDirButtonClicked ()));

  QLayout * layout = new QVBoxLayout (this);
  layout->addWidget (hig);
  QDialogButtonBox * buttons = new QDialogButtonBox (QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
  connect (buttons, SIGNAL (rejected ()), this, SLOT (deleteLater ()));
  connect (buttons, SIGNAL (accepted ()), this, SLOT (onSetLocation ()));
  layout->addWidget (buttons);
  QWidget::setAttribute (Qt::WA_DeleteOnClose, true);
}
