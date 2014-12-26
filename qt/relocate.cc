/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>

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
  close ();
}

void
RelocateDialog::onFileSelected (const QString& path)
{
  myPath = path;

  const QFileInfo pathInfo (path);
  const QFileIconProvider iconProvider;

  ui.newLocationButton->setIcon (mySession.isLocal () ?
                                 iconProvider.icon (pathInfo) :
                                 iconProvider.icon (QFileIconProvider::Folder));
  ui.newLocationButton->setText (pathInfo.baseName ());
  ui.newLocationButton->setToolTip (path);
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

RelocateDialog::RelocateDialog (Session            & session,
                                const TorrentModel & model,
                                const QSet<int>    & ids,
                                QWidget            * parent):
  QDialog (parent),
  mySession (session),
  myIds (ids)
{
  ui.setupUi (this);

  foreach (int id, myIds)
    {
      const Torrent * tor = model.getTorrentFromId (id);

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
          break;
        }
    }

  onFileSelected (myPath);

  if (myMoveFlag)
    ui.moveDataRadio->setChecked (true);
  else
    ui.findDataRadio->setChecked (true);

  connect (ui.moveDataRadio, SIGNAL (toggled (bool)), this, SLOT (onMoveToggled (bool)));
  connect (ui.newLocationButton, SIGNAL (clicked ()), this, SLOT (onDirButtonClicked ()));
  connect (ui.dialogButtons, SIGNAL (rejected ()), this, SLOT (close ()));
  connect (ui.dialogButtons, SIGNAL (accepted ()), this, SLOT (onSetLocation ()));

  setAttribute (Qt::WA_DeleteOnClose, true);
}
