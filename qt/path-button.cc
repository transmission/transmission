/*
 * This file Copyright (C) 2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QStyle>

#include "path-button.h"
#include "utils.h"

TrPathButton::TrPathButton (QWidget * parent):
  QToolButton (parent),
  myMode (DirectoryMode),
  myTitle (),
  myNameFilter (),
  myPath ()
{
  setSizePolicy(QSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed));

  updateAppearance ();

  connect (this, SIGNAL (clicked ()), this, SLOT (onClicked ()));
}

void
TrPathButton::setMode (Mode mode)
{
  if (myMode == mode)
    return;

  myMode = mode;

  updateAppearance ();
}

void
TrPathButton::setTitle (const QString& title)
{
  myTitle = title;
}

void
TrPathButton::setNameFilter (const QString& nameFilter)
{
  myNameFilter = nameFilter;
}

void
TrPathButton::setPath (const QString& path)
{
  if (myPath == path)
    return;

  myPath = Utils::removeTrailingDirSeparator (path);

  updateAppearance ();

  emit pathChanged (myPath);
}

const QString&
TrPathButton::path () const
{
  return myPath;
}

void
TrPathButton::onClicked ()
{
  QFileDialog * dialog = new QFileDialog (window (), effectiveTitle ());
  dialog->setFileMode (isDirMode () ? QFileDialog::Directory : QFileDialog::ExistingFile);
  if (isDirMode ())
    dialog->setOption (QFileDialog::ShowDirsOnly);
  dialog->setNameFilter (myNameFilter);
  dialog->selectFile (myPath);

  connect (dialog, SIGNAL (fileSelected (QString)), this, SLOT (onFileSelected (QString)));

  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->open ();
}

void
TrPathButton::onFileSelected (const QString& path)
{
  if (!path.isEmpty ())
    setPath (path);
}

void
TrPathButton::updateAppearance ()
{
  const QFileInfo pathInfo (myPath);

  const int iconSize (style ()->pixelMetric (QStyle::PM_SmallIconSize));
  const QFileIconProvider iconProvider;

  QIcon icon;
  if (!myPath.isEmpty () && pathInfo.exists ())
    icon = iconProvider.icon (myPath);
  if (icon.isNull ())
    icon = iconProvider.icon (isDirMode () ? QFileIconProvider::Folder : QFileIconProvider::File);

  setIconSize (QSize (iconSize, iconSize));
  setIcon (icon);
  setText (myPath.isEmpty () ? tr ("(None)") : (pathInfo.fileName ().isEmpty () ? myPath : pathInfo.fileName ()));
  setToolTip (myPath == text () ? QString () : myPath);
}

bool
TrPathButton::isDirMode () const
{
  return myMode == DirectoryMode;
}

QString
TrPathButton::effectiveTitle () const
{
  if (!myTitle.isEmpty ())
    return myTitle;

  return isDirMode () ? tr ("Select Folder") : tr ("Select File");
}
