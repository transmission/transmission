/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */


#include <iostream>

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QStyleOptionProgressBar>

#include "formatter.h"
#include "torrent.h"
#include "torrent-delegate.h"
#include "torrent-model.h"

enum
{
  GUI_PAD = 6,
  BAR_HEIGHT = 12
};

QColor TorrentDelegate::greenBrush;
QColor TorrentDelegate::blueBrush;
QColor TorrentDelegate::silverBrush;
QColor TorrentDelegate::greenBack;
QColor TorrentDelegate::blueBack;
QColor TorrentDelegate::silverBack;

TorrentDelegate::TorrentDelegate (QObject * parent):
  QStyledItemDelegate (parent),
  myProgressBarStyle (new QStyleOptionProgressBar)
{
  myProgressBarStyle->minimum = 0;
  myProgressBarStyle->maximum = 1000;

  greenBrush = QColor ("forestgreen");
  greenBack = QColor ("darkseagreen");

  blueBrush = QColor ("steelblue");
  blueBack = QColor ("lightgrey");

  silverBrush = QColor ("silver");
  silverBack = QColor ("grey");
}

TorrentDelegate::~TorrentDelegate ()
{
  delete myProgressBarStyle;
}

/***
****
***/

QSize
TorrentDelegate::margin (const QStyle& style) const
{
  Q_UNUSED (style);

  return QSize (4, 4);
}

QString
TorrentDelegate::progressString (const Torrent& tor) const
{
  const bool isMagnet (!tor.hasMetadata());
  const bool isDone (tor.isDone ());
  const bool isSeed (tor.isSeed ());
  const uint64_t haveTotal (tor.haveTotal());
  QString str;
  double seedRatio;
  const bool hasSeedRatio (tor.getSeedRatio (seedRatio));

  if (isMagnet) // magnet link with no metadata
    {
      // %1 is the percentage of torrent metadata downloaded
      str = tr ("Magnetized transfer - retrieving metadata (%1%)")
            .arg (Formatter::percentToString (tor.metadataPercentDone() * 100.0));
    }
  else if (!isDone) // downloading
    {
      /* %1 is how much we've got,
         %2 is how much we'll have when done,
         %3 is a percentage of the two */
      str = tr ("%1 of %2 (%3%)")
            .arg (Formatter::sizeToString (haveTotal))
            .arg (Formatter::sizeToString (tor.sizeWhenDone()))
            .arg (Formatter::percentToString (tor.percentDone() * 100.0));
    }
  else if (!isSeed) // partial seed
    {
      if (hasSeedRatio)
        {
          /* %1 is how much we've got,
             %2 is the torrent's total size,
             %3 is a percentage of the two,
             %4 is how much we've uploaded,
             %5 is our upload-to-download ratio
             %6 is the ratio we want to reach before we stop uploading */
          str = tr ("%1 of %2 (%3%), uploaded %4 (Ratio: %5 Goal: %6)")
                .arg (Formatter::sizeToString (haveTotal))
                .arg (Formatter::sizeToString (tor.totalSize()))
                .arg (Formatter::percentToString (tor.percentComplete() * 100.0))
                .arg (Formatter::sizeToString (tor.uploadedEver()))
                .arg (Formatter::ratioToString (tor.ratio()))
                .arg (Formatter::ratioToString (seedRatio));
        }
        else
        {
            /* %1 is how much we've got,
               %2 is the torrent's total size,
               %3 is a percentage of the two,
               %4 is how much we've uploaded,
               %5 is our upload-to-download ratio */
            str = tr ("%1 of %2 (%3%), uploaded %4 (Ratio: %5)")
                  .arg (Formatter::sizeToString (haveTotal))
                  .arg (Formatter::sizeToString (tor.totalSize()))
                  .arg (Formatter::percentToString (tor.percentComplete() * 100.0))
                  .arg (Formatter::sizeToString (tor.uploadedEver()))
                  .arg (Formatter::ratioToString (tor.ratio()));
        }
    }
  else // seeding
    {
      if (hasSeedRatio)
        {
          /* %1 is the torrent's total size,
             %2 is how much we've uploaded,
             %3 is our upload-to-download ratio,
             %4 is the ratio we want to reach before we stop uploading */
          str = tr ("%1, uploaded %2 (Ratio: %3 Goal: %4)")
                .arg (Formatter::sizeToString (haveTotal))
                .arg (Formatter::sizeToString (tor.uploadedEver()))
                .arg (Formatter::ratioToString (tor.ratio()))
                .arg (Formatter::ratioToString (seedRatio));
        }
      else // seeding w/o a ratio
        {
          /* %1 is the torrent's total size,
             %2 is how much we've uploaded,
             %3 is our upload-to-download ratio */
          str = tr ("%1, uploaded %2 (Ratio: %3)")
                .arg (Formatter::sizeToString (haveTotal))
                .arg (Formatter::sizeToString (tor.uploadedEver()))
                .arg (Formatter::ratioToString (tor.ratio()));
        }
    }

  // add time when downloading
  if ((hasSeedRatio && tor.isSeeding()) || tor.isDownloading())
    {
      str += tr (" - ");
      if (tor.hasETA ())
        str += tr ("%1 left").arg (Formatter::timeToString (tor.getETA ()));
      else
        str += tr ("Remaining time unknown");
    }

    return str;
}

QString
TorrentDelegate::shortTransferString (const Torrent& tor) const
{
  QString str;
  const bool haveMeta (tor.hasMetadata());
  const bool haveDown (haveMeta && ((tor.webseedsWeAreDownloadingFrom()>0) || (tor.peersWeAreDownloadingFrom()>0)));
  const bool haveUp (haveMeta && tor.peersWeAreUploadingTo()>0);

  if (haveDown)
    str = tr ("%1   %2")
          .arg(Formatter::downloadSpeedToString(tor.downloadSpeed()))
          .arg(Formatter::uploadSpeedToString(tor.uploadSpeed()));

  else if (haveUp)
    str = Formatter::uploadSpeedToString(tor.uploadSpeed());

  return str;
}

QString
TorrentDelegate::shortStatusString (const Torrent& tor) const
{
  QString str;
  static const QChar ratioSymbol (0x262F);

  switch (tor.getActivity ())
    {
      case TR_STATUS_CHECK:
        str = tr ("Verifying local data (%1% tested)").arg (Formatter::percentToString (tor.getVerifyProgress()*100.0));
        break;

      case TR_STATUS_DOWNLOAD:
      case TR_STATUS_SEED:
        str = tr("%1    %2 %3")
              .arg(shortTransferString(tor))
              .arg(tr("Ratio:"))
              .arg(Formatter::ratioToString(tor.ratio()));
        break;

      default:
        str = tor.activityString ();
        break;
    }

  return str;
}

QString
TorrentDelegate::statusString (const Torrent& tor) const
{
  QString str;

  if (tor.hasError ())
    {
      str = tor.getError ();
    }
  else switch (tor.getActivity ())
    {
      case TR_STATUS_STOPPED:
      case TR_STATUS_CHECK_WAIT:
      case TR_STATUS_CHECK:
      case TR_STATUS_DOWNLOAD_WAIT:
      case TR_STATUS_SEED_WAIT:
        str = shortStatusString (tor);
        break;

      case TR_STATUS_DOWNLOAD:
        if (!tor.hasMetadata())
          {
            str = tr ("Downloading metadata from %n peer(s) (%1% done)", 0, tor.peersWeAreDownloadingFrom ())
                  .arg (Formatter::percentToString (100.0 * tor.metadataPercentDone ()));
          }
        else
          {
            /* it would be nicer for translation if this was all one string, but I don't see how to do multiple %n's in tr() */
            str = tr ("Downloading from %1 of %n connected peer(s)", 0, tor.connectedPeersAndWebseeds ())
                  .arg (tor.peersWeAreDownloadingFrom ());

            if (tor.webseedsWeAreDownloadingFrom())
              str += tr(" and %n web seed(s)", "", tor.webseedsWeAreDownloadingFrom());
          }
        break;

      case TR_STATUS_SEED:
        str = tr ("Seeding to %1 of %n connected peer(s)", 0, tor.connectedPeers ())
              .arg (tor.peersWeAreUploadingTo ());
        break;

      default:
        str = tr ("Error");
        break;
    }

  if (tor.isReadyToTransfer ())
    {
      QString s = shortTransferString (tor);
      if (!s.isEmpty ())
        str += tr (" - ") + s;
    }

  return str;
}

/***
****
***/

namespace
{
  int MAX3 (int a, int b, int c)
    {
      const int ab (a > b ? a : b);
      return ab > c ? ab : c;
    }
}

QSize
TorrentDelegate::sizeHint (const QStyleOptionViewItem& option, const Torrent& tor) const
{
  const QStyle* style (QApplication::style ());
  static const int iconSize (style->pixelMetric (QStyle::PM_MessageBoxIconSize));

  QFont nameFont (option.font);
  nameFont.setWeight (QFont::Bold);
  const QFontMetrics nameFM (nameFont);
  const QString nameStr (tor.name ());
  const int nameWidth = nameFM.width (nameStr);
  QFont statusFont (option.font);
  statusFont.setPointSize (int (option.font.pointSize () * 0.9));
  const QFontMetrics statusFM (statusFont);
  const QString statusStr (statusString (tor));
  const int statusWidth = statusFM.width (statusStr);
  QFont progressFont (statusFont);
  const QFontMetrics progressFM (progressFont);
  const QString progressStr (progressString (tor));
  const int progressWidth = progressFM.width (progressStr);
  const QSize m (margin (*style));
  return QSize (m.width()*2 + iconSize + GUI_PAD + MAX3 (nameWidth, statusWidth, progressWidth),
                //m.height()*3 + nameFM.lineSpacing() + statusFM.lineSpacing()*2 + progressFM.lineSpacing());
                m.height()*3 + nameFM.lineSpacing() + statusFM.lineSpacing() + BAR_HEIGHT + progressFM.lineSpacing());
}

QSize
TorrentDelegate::sizeHint (const QStyleOptionViewItem  & option,
                           const QModelIndex           & index) const
{
  const Torrent * tor (index.data (TorrentModel::TorrentRole).value<const Torrent*>());
  return sizeHint (option, *tor);
}

void
TorrentDelegate::paint (QPainter                    * painter,
                        const QStyleOptionViewItem  & option,
                        const QModelIndex           & index) const
{
  const Torrent * tor (index.data (TorrentModel::TorrentRole).value<const Torrent*>());
  painter->save ();
  painter->setClipRect (option.rect);
  drawTorrent (painter, option, *tor);
  painter->restore ();
}

void
TorrentDelegate::setProgressBarPercentDone (const QStyleOptionViewItem & option,
                                            const Torrent              & tor) const
{
  double seedRatioLimit;
  if (tor.isSeeding() && tor.getSeedRatio(seedRatioLimit))
    {
      const double seedRateRatio = tor.ratio() / seedRatioLimit;
      const int scaledProgress = seedRateRatio * (myProgressBarStyle->maximum - myProgressBarStyle->minimum);
      myProgressBarStyle->progress = myProgressBarStyle->minimum + scaledProgress;
    }
  else
    {
      const bool isMagnet (!tor.hasMetadata ());
      myProgressBarStyle->direction = option.direction;
      myProgressBarStyle->progress = int(myProgressBarStyle->minimum + (((isMagnet ? tor.metadataPercentDone() : tor.percentDone()) * (myProgressBarStyle->maximum - myProgressBarStyle->minimum))));
    }
}

void
TorrentDelegate::drawTorrent (QPainter                   * painter,
                              const QStyleOptionViewItem & option,
                              const Torrent              & tor) const
{
  const QStyle * style (QApplication::style ());
  static const int iconSize (style->pixelMetric (QStyle::PM_LargeIconSize));
  QFont nameFont (option.font);
  nameFont.setWeight (QFont::Bold);
  const QFontMetrics nameFM (nameFont);
  const QString nameStr (tor.name ());
  const QSize nameSize (nameFM.size (0, nameStr));
  QFont statusFont (option.font);
  statusFont.setPointSize (int (option.font.pointSize () * 0.9));
  const QFontMetrics statusFM (statusFont);
  const QString statusStr (progressString (tor));
  QFont progressFont (statusFont);
  const QFontMetrics progressFM (progressFont);
  const QString progressStr (statusString (tor));
  const bool isPaused (tor.isPaused ());

  painter->save ();

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
  QRect iconArea (fillArea.x (), fillArea.y () +  (fillArea.height () - iconSize) / 2, iconSize, iconSize);
  QRect nameArea (iconArea.x () + iconArea.width () + GUI_PAD, fillArea.y (),
                  fillArea.width () - GUI_PAD - iconArea.width (), nameSize.height ());
  QRect statusArea (nameArea);
  statusArea.moveTop (nameArea.y () + nameFM.lineSpacing ());
  statusArea.setHeight (nameSize.height ());
  QRect barArea (statusArea);
  barArea.setHeight (BAR_HEIGHT);
  barArea.moveTop (statusArea.y () + statusFM.lineSpacing ());
  QRect progArea (statusArea);
  progArea.moveTop (barArea.y () + barArea.height ());

  // render
  if (tor.hasError ())
    painter->setPen (QColor ("red"));
  else
    painter->setPen (option.palette.color (cg, cr));
  tor.getMimeTypeIcon().paint (painter, iconArea, Qt::AlignCenter, im, qs);
  painter->setFont (nameFont);
  painter->drawText (nameArea, 0, nameFM.elidedText (nameStr, Qt::ElideRight, nameArea.width ()));
  painter->setFont (statusFont);
  painter->drawText (statusArea, 0, statusFM.elidedText (statusStr, Qt::ElideRight, statusArea.width ()));
  painter->setFont (progressFont);
  painter->drawText (progArea, 0, progressFM.elidedText (progressStr, Qt::ElideRight, progArea.width ()));
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
  setProgressBarPercentDone (option, tor);

  style->drawControl (QStyle::CE_ProgressBar, myProgressBarStyle, painter);

  painter->restore ();
}
