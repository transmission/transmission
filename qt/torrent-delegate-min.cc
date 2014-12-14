/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <iostream>

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QStyleOptionProgressBar>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "torrent.h"
#include "torrent-delegate-min.h"
#include "torrent-model.h"

enum
{
  GUI_PAD = 6,
  BAR_WIDTH = 50,
  BAR_HEIGHT = 12,
  LINE_SPACING = 4
};

/***
****
****   +---------+-----------------------------------------------+
****   |  Icon   |   Title      shortStatusString [Progressbar]  |
****   +-------- +-----------------------------------------------+
****
***/

QSize
TorrentDelegateMin::sizeHint (const QStyleOptionViewItem & option,
                              const Torrent              & tor) const
{
  const QStyle* style (QApplication::style());
  static const int iconSize (style->pixelMetric (QStyle::PM_SmallIconSize));

  QFont nameFont (option.font);
  const QFontMetrics nameFM (nameFont);
  const bool isMagnet (!tor.hasMetadata());
  const QString nameStr = (isMagnet ? progressString (tor) : tor.name());
  const int nameWidth = nameFM.width (nameStr);

  QFont statusFont (option.font);
  statusFont.setPointSize (int (option.font.pointSize() * 0.85));
  const QFontMetrics statusFM (statusFont);
  const QString statusStr (shortStatusString (tor));
  const int statusWidth = statusFM.width (statusStr);

  const QSize m (margin (*style));

  return QSize (m.width()*2 + iconSize + GUI_PAD + nameWidth
                                       + GUI_PAD + statusWidth
                                       + GUI_PAD + BAR_WIDTH,
                m.height()*2 + std::max (nameFM.height(), (int)BAR_HEIGHT));
}

void
TorrentDelegateMin::drawTorrent (QPainter                   * painter,
                                 const QStyleOptionViewItem & option,
                                 const Torrent              & tor) const
{
  const bool isPaused (tor.isPaused());
  const QStyle * style (QApplication::style());
  static const int iconSize (style->pixelMetric (QStyle::PM_SmallIconSize));

  QFont nameFont (option.font);
  const QFontMetrics nameFM (nameFont);
  const bool isMagnet (!tor.hasMetadata());
  const QString nameStr = (isMagnet ? progressString (tor) : tor.name());

  QFont statusFont (option.font);
  statusFont.setPointSize (int (option.font.pointSize() * 0.85));
  const QFontMetrics statusFM (statusFont);
  const QString statusStr (shortStatusString (tor));
  const QSize statusSize (statusFM.size (0, statusStr));

  painter->save();

  if (option.state & QStyle::State_Selected)
    {
      QPalette::ColorGroup cg = option.state & QStyle::State_Enabled
                              ? QPalette::Normal : QPalette::Disabled;
      if (cg == QPalette::Normal && !(option.state & QStyle::State_Active))
        cg = QPalette::Inactive;

      painter->fillRect(option.rect, option.palette.brush(cg, QPalette::Highlight));
    }

  QIcon::Mode im;
  if (isPaused || !(option.state & QStyle::State_Enabled))
    im = QIcon::Disabled;
  else if (option.state & QStyle::State_Selected)
    im = QIcon::Selected;
  else
    im = QIcon::Normal;

  QIcon::State qs;
  if (isPaused)
    qs = QIcon::Off;
  else
    qs = QIcon::On;

  QPalette::ColorGroup cg = QPalette::Normal;
  if (isPaused || !(option.state & QStyle::State_Enabled))
    cg = QPalette::Disabled;
  if (cg == QPalette::Normal && !(option.state & QStyle::State_Active))
    cg = QPalette::Inactive;

  QPalette::ColorRole cr;
  if (option.state & QStyle::State_Selected)
    cr = QPalette::HighlightedText;
  else
    cr = QPalette::Text;

  QStyle::State progressBarState (option.state);
  if (isPaused)
    progressBarState = QStyle::State_None;
  progressBarState |= QStyle::State_Small;

  // layout
  const QSize m (margin (*style));
  QRect fillArea (option.rect);
  fillArea.adjust (m.width(), m.height(), -m.width(), -m.height());
  const QRect iconArea (fillArea.x(),
                        fillArea.y() +  (fillArea.height() - iconSize) / 2,
                        iconSize,
                        iconSize);
  const QRect barArea (fillArea.x() + fillArea.width() - BAR_WIDTH,
                       fillArea.y() +  (fillArea.height() - BAR_HEIGHT) / 2,
                       BAR_WIDTH,
                       BAR_HEIGHT);
  const QRect statusArea (barArea.x() - GUI_PAD - statusSize.width(),
                          fillArea.y() +  (fillArea.height() - statusSize.height()) / 2,
                          fillArea.width(),
                          fillArea.height());
  const QRect nameArea (iconArea.x() + iconArea.width() + GUI_PAD,
                        fillArea.y(),
                        statusArea.x() -  (iconArea.x() + iconArea.width() + GUI_PAD * 2),
                        fillArea.height());

  // render
  if (tor.hasError())
    painter->setPen (QColor ("red"));
  else
    painter->setPen (option.palette.color (cg, cr));
  tor.getMimeTypeIcon().paint (painter, iconArea, Qt::AlignCenter, im, qs);
  painter->setFont (nameFont);
  painter->drawText (nameArea, 0, nameFM.elidedText (nameStr, Qt::ElideRight, nameArea.width()));
  painter->setFont (statusFont);
  painter->drawText (statusArea, 0, statusStr);
  myProgressBarStyle->rect = barArea;
  if (tor.isDownloading())
    {
      myProgressBarStyle->palette.setBrush (QPalette::Highlight, blueBrush);
      myProgressBarStyle->palette.setColor (QPalette::Base, blueBack);
      myProgressBarStyle->palette.setColor (QPalette::Window, blueBack);
    }
  else if (tor.isSeeding())
    {
      myProgressBarStyle->palette.setBrush (QPalette::Highlight, greenBrush);
      myProgressBarStyle->palette.setColor (QPalette::Base, greenBack);
      myProgressBarStyle->palette.setColor (QPalette::Window, greenBack);
    }
  else
    {
      myProgressBarStyle->palette.setBrush (QPalette::Highlight, silverBrush);
      myProgressBarStyle->palette.setColor (QPalette::Base, silverBack);
      myProgressBarStyle->palette.setColor (QPalette::Window, silverBack);
    }
  myProgressBarStyle->state = progressBarState;
  myProgressBarStyle->text = QString::fromLatin1 ("%1%").arg (static_cast<int> (tr_truncd (100.0 * tor.percentDone (), 0)));
  myProgressBarStyle->textVisible = true;
  myProgressBarStyle->textAlignment = Qt::AlignCenter;
  setProgressBarPercentDone (option, tor);
  style->drawControl (QStyle::CE_ProgressBar, myProgressBarStyle, painter);

  painter->restore();
}
