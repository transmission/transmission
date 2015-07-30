/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

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

#include "Torrent.h"
#include "TorrentDelegateMin.h"
#include "TorrentModel.h"
#include "Utils.h"

enum
{
  GUI_PAD = 6,
  BAR_WIDTH = 50,
  BAR_HEIGHT = 16,
  LINE_SPACING = 4
};

/***
****
****   +---------+-----------------------------------------------+
****   |  Icon   |   Title      shortStatusString [Progressbar]  |
****   +-------- +-----------------------------------------------+
****
***/

namespace
{
  class ItemLayout
  {
    private:
      QString myNameText;
      QString myStatusText;

    public:
      QFont nameFont;
      QFont statusFont;

      QRect iconRect;
      QRect emblemRect;
      QRect nameRect;
      QRect statusRect;
      QRect barRect;

    public:
      ItemLayout(const QString& nameText, const QString& statusText, const QIcon& emblemIcon,
                 const QFont& baseFont, Qt::LayoutDirection direction, const QPoint& topLeft, int width);

      QSize size () const
      {
        return (iconRect | nameRect | statusRect | barRect).size ();
      }

      QString nameText () const { return elidedText (nameFont, myNameText, nameRect.width ()); }
      QString statusText () const { return myStatusText; }

    private:
      QString elidedText (const QFont& font, const QString& text, int width) const
      {
        return QFontMetrics (font).elidedText (text, Qt::ElideRight, width);
      }
  };

  ItemLayout::ItemLayout(const QString& nameText, const QString& statusText, const QIcon& emblemIcon,
                         const QFont& baseFont, Qt::LayoutDirection direction, const QPoint& topLeft, int width):
    myNameText (nameText),
    myStatusText (statusText),
    nameFont (baseFont),
    statusFont (baseFont)
  {
    const QStyle * style (qApp->style ());
    const int iconSize (style->pixelMetric (QStyle::PM_SmallIconSize));

    const QFontMetrics nameFM (nameFont);
    const QSize nameSize (nameFM.size (0, myNameText));

    statusFont.setPointSize (static_cast<int> (statusFont.pointSize () * 0.85));
    const QFontMetrics statusFM (statusFont);
    const QSize statusSize (statusFM.size (0, myStatusText));

    QRect baseRect (topLeft, QSize (width, qMax (iconSize, qMax (nameSize.height (), qMax (statusSize.height (), static_cast<int>(BAR_HEIGHT))))));

    iconRect = style->alignedRect (direction, Qt::AlignLeft | Qt::AlignVCenter, QSize (iconSize, iconSize), baseRect);
    emblemRect = style->alignedRect (direction, Qt::AlignRight | Qt::AlignBottom,
                                     emblemIcon.actualSize (iconRect.size () / 2, QIcon::Normal, QIcon::On),
                                     iconRect);
    barRect = style->alignedRect (direction, Qt::AlignRight | Qt::AlignVCenter, QSize (BAR_WIDTH, BAR_HEIGHT), baseRect);
    Utils::narrowRect (baseRect, iconRect.width () + GUI_PAD, barRect.width () + GUI_PAD, direction);
    statusRect = style->alignedRect (direction, Qt::AlignRight | Qt::AlignVCenter, QSize (statusSize.width (), baseRect.height ()), baseRect);
    Utils::narrowRect (baseRect, 0, statusRect.width () + GUI_PAD, direction);
    nameRect = baseRect;
  }
}

QSize
TorrentDelegateMin::sizeHint (const QStyleOptionViewItem & option,
                              const Torrent              & tor) const
{
  const bool isMagnet (!tor.hasMetadata());
  const QSize m (margin (*qApp->style()));
  const ItemLayout layout (isMagnet ? progressString (tor) : tor.name(), shortStatusString (tor), QIcon (),
                           option.font, option.direction, QPoint (0, 0), option.rect.width () - m.width () * 2);
  return layout.size () + m * 2;
}

void
TorrentDelegateMin::drawTorrent (QPainter                   * painter,
                                 const QStyleOptionViewItem & option,
                                 const Torrent              & tor) const
{
  const QStyle * style (qApp->style());

  const bool isPaused (tor.isPaused());
  const bool isMagnet (!tor.hasMetadata());

  const bool isItemSelected ((option.state & QStyle::State_Selected) != 0);
  const bool isItemEnabled ((option.state & QStyle::State_Enabled) != 0);
  const bool isItemActive ((option.state & QStyle::State_Active) != 0);

  painter->save();

  if (isItemSelected)
    {
      QPalette::ColorGroup cg = isItemEnabled ? QPalette::Normal : QPalette::Disabled;
      if (cg == QPalette::Normal && !isItemActive)
        cg = QPalette::Inactive;

      painter->fillRect(option.rect, option.palette.brush(cg, QPalette::Highlight));
    }

  QIcon::Mode im;
  if (isPaused || !isItemEnabled)
    im = QIcon::Disabled;
  else if (isItemSelected)
    im = QIcon::Selected;
  else
    im = QIcon::Normal;

  QIcon::State qs;
  if (isPaused)
    qs = QIcon::Off;
  else
    qs = QIcon::On;

  QPalette::ColorGroup cg = QPalette::Normal;
  if (isPaused || !isItemEnabled)
    cg = QPalette::Disabled;
  if (cg == QPalette::Normal && !isItemActive)
    cg = QPalette::Inactive;

  QPalette::ColorRole cr;
  if (isItemSelected)
    cr = QPalette::HighlightedText;
  else
    cr = QPalette::Text;

  QStyle::State progressBarState (option.state);
  if (isPaused)
    progressBarState = QStyle::State_None;
  progressBarState |= QStyle::State_Small;

  const QIcon::Mode emblemIm = isItemSelected ? QIcon::Selected : QIcon::Normal;
  const QIcon emblemIcon = tor.hasError () ? QIcon::fromTheme (QLatin1String ("emblem-important"), style->standardIcon (QStyle::SP_MessageBoxWarning)) : QIcon ();

  // layout
  const QSize m (margin (*style));
  const QRect contentRect (option.rect.adjusted (m.width(), m.height(), -m.width(), -m.height()));
  const ItemLayout layout (isMagnet ? progressString (tor) : tor.name(), shortStatusString (tor), emblemIcon,
                           option.font, option.direction, contentRect.topLeft (), contentRect.width ());

  // render
  if (tor.hasError() && !isItemSelected)
    painter->setPen (QColor ("red"));
  else
    painter->setPen (option.palette.color (cg, cr));
  tor.getMimeTypeIcon().paint (painter, layout.iconRect, Qt::AlignCenter, im, qs);
  if (!emblemIcon.isNull ())
    emblemIcon.paint (painter, layout.emblemRect, Qt::AlignCenter, emblemIm, qs);
  painter->setFont (layout.nameFont);
  painter->drawText (layout.nameRect, Qt::AlignLeft | Qt::AlignVCenter, layout.nameText ());
  painter->setFont (layout.statusFont);
  painter->drawText (layout.statusRect, Qt::AlignLeft | Qt::AlignVCenter, layout.statusText ());
  myProgressBarStyle->rect = layout.barRect;
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
