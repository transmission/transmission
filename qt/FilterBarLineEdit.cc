/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QToolButton>
#include <QStyle>

#include "FilterBarLineEdit.h"

FilterBarLineEdit::FilterBarLineEdit (QWidget * parent):
  QLineEdit (parent),
  myClearButton (nullptr)
{
#if QT_VERSION < QT_VERSION_CHECK(5, 2, 0)
  const QIcon icon = QIcon::fromTheme (QLatin1String ("edit-clear"), style ()->standardIcon (QStyle::SP_DialogCloseButton));
  const int iconSize = style ()->pixelMetric (QStyle::PM_SmallIconSize);

  myClearButton = new QToolButton (this);
  myClearButton->setStyleSheet (QLatin1String ("QToolButton{border:0;padding:0;margin:0}"));
  myClearButton->setToolButtonStyle (Qt::ToolButtonIconOnly);
  myClearButton->setFocusPolicy (Qt::NoFocus);
  myClearButton->setCursor (Qt::ArrowCursor);
  myClearButton->setIconSize (QSize (iconSize, iconSize));
  myClearButton->setIcon (icon);
  myClearButton->setFixedSize (myClearButton->iconSize () + QSize (2, 2));
  myClearButton->hide ();

  const int frameWidth = style ()->pixelMetric (QStyle::PM_DefaultFrameWidth);
  const QSize minSizeHint = minimumSizeHint ();
  const QSize buttonSize = myClearButton->size ();

  setStyleSheet (QString::fromLatin1 ("QLineEdit{padding-right:%1px}").arg (buttonSize.width () + frameWidth + 1));
  setMinimumSize (qMax (minSizeHint.width (), buttonSize.width () + frameWidth * 2 + 2),
                  qMax (minSizeHint.height (), buttonSize.height () + frameWidth * 2 + 2));

  connect (this, SIGNAL (textChanged (QString)), this, SLOT (updateClearButtonVisibility ()));
  connect (myClearButton, SIGNAL (clicked ()), this, SLOT (clear ()));
#else
  setClearButtonEnabled (true);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(4, 7, 0)
  setPlaceholderText (tr ("Search..."));
#endif
}

void
FilterBarLineEdit::resizeEvent (QResizeEvent * event)
{
  QLineEdit::resizeEvent (event);

#if QT_VERSION < QT_VERSION_CHECK(5, 2, 0)
  const int frameWidth = style ()->pixelMetric (QStyle::PM_DefaultFrameWidth);
  const QRect editRect = rect();
  const QSize buttonSize = myClearButton->size ();

  myClearButton->move (editRect.right () - frameWidth - buttonSize.width (),
                       editRect.top () + (editRect.height () - buttonSize.height ()) / 2);
#endif
}

void
FilterBarLineEdit::updateClearButtonVisibility ()
{
#if QT_VERSION < QT_VERSION_CHECK(5, 2, 0)
  myClearButton->setVisible (!text ().isEmpty ());
#endif
}
