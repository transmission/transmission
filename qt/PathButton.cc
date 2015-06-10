/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QStylePainter>

#include "PathButton.h"
#include "Utils.h"

PathButton::PathButton (QWidget * parent):
  QToolButton (parent),
  myMode (DirectoryMode),
  myTitle (),
  myNameFilter (),
  myPath ()
{
  setSizePolicy(QSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed));
  setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
  setText (tr ("(None)")); // for minimum width

  updateAppearance ();

  connect (this, SIGNAL (clicked ()), this, SLOT (onClicked ()));
}

void
PathButton::setMode (Mode mode)
{
  if (myMode == mode)
    return;

  myMode = mode;

  updateAppearance ();
}

void
PathButton::setTitle (const QString& title)
{
  myTitle = title;
}

void
PathButton::setNameFilter (const QString& nameFilter)
{
  myNameFilter = nameFilter;
}

void
PathButton::setPath (const QString& path)
{
  if (myPath == path)
    return;

  myPath = QDir::toNativeSeparators (Utils::removeTrailingDirSeparator (path));

  updateAppearance ();

  emit pathChanged (myPath);
}

const QString&
PathButton::path () const
{
  return myPath;
}

QSize
PathButton::sizeHint () const
{
  const QSize sh (QToolButton::sizeHint ());
  return QSize (qMin (sh.width (), 150), sh.height ());
}

void
PathButton::paintEvent (QPaintEvent * /*event*/)
{
  QStylePainter painter(this);
  QStyleOptionToolButton option;
  initStyleOption (&option);

  const QSize fakeContentSize (qMax (100, qApp->globalStrut ().width ()),
                        qMax (100, qApp->globalStrut ().height ()));
  const QSize fakeSizeHint = style ()->sizeFromContents (QStyle::CT_ToolButton, &option, fakeContentSize, this);

  int textWidth = width () - (fakeSizeHint.width () - fakeContentSize.width ()) - iconSize ().width () - 6;
  if (popupMode () == MenuButtonPopup)
    textWidth -= style ()->pixelMetric (QStyle::PM_MenuButtonIndicator, &option, this);

  const QFileInfo pathInfo (myPath);
  option.text = myPath.isEmpty () ? tr ("(None)") : (pathInfo.fileName ().isEmpty () ? myPath : pathInfo.fileName ());
  option.text = fontMetrics ().elidedText (option.text, Qt::ElideMiddle, textWidth);

  painter.drawComplexControl(QStyle::CC_ToolButton, option);
}

void
PathButton::onClicked ()
{
  QFileDialog * dialog = new QFileDialog (window (), effectiveTitle ());
  dialog->setFileMode (isDirMode () ? QFileDialog::Directory : QFileDialog::ExistingFile);
  if (isDirMode ())
    dialog->setOption (QFileDialog::ShowDirsOnly);
  if (!myNameFilter.isEmpty ())
    dialog->setNameFilter (myNameFilter);

  const QFileInfo pathInfo (myPath);
  if (!myPath.isEmpty () && pathInfo.exists ())
    {
      if (pathInfo.isDir ())
        {
          dialog->setDirectory (pathInfo.absoluteFilePath ());
        }
      else
        {
          dialog->setDirectory (pathInfo.absolutePath ());
          dialog->selectFile (pathInfo.fileName ());
        }
    }

  connect (dialog, SIGNAL (fileSelected (QString)), this, SLOT (onFileSelected (QString)));

  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->open ();
}

void
PathButton::onFileSelected (const QString& path)
{
  if (!path.isEmpty ())
    setPath (path);
}

void
PathButton::updateAppearance ()
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
  setToolTip (myPath == text () ? QString () : myPath);

  update ();
}

bool
PathButton::isDirMode () const
{
  return myMode == DirectoryMode;
}

QString
PathButton::effectiveTitle () const
{
  if (!myTitle.isEmpty ())
    return myTitle;

  return isDirMode () ? tr ("Select Folder") : tr ("Select File");
}
