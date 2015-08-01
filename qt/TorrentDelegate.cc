/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QStyleOptionProgressBar>

#include "Formatter.h"
#include "Torrent.h"
#include "TorrentDelegate.h"
#include "TorrentModel.h"
#include "Utils.h"

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

namespace
{
  class ItemLayout
  {
    private:
      QString myNameText;
      QString myStatusText;
      QString myProgressText;

    public:
      QFont nameFont;
      QFont statusFont;
      QFont progressFont;

      QRect iconRect;
      QRect emblemRect;
      QRect nameRect;
      QRect statusRect;
      QRect barRect;
      QRect progressRect;

    public:
      ItemLayout(const QString& nameText, const QString& statusText, const QString& progressText,
                 const QIcon& emblemIcon, const QFont& baseFont, Qt::LayoutDirection direction,
                 const QPoint& topLeft, int width);

      QSize size () const
      {
        return (iconRect | nameRect | statusRect | barRect | progressRect).size ();
      }

      QString nameText () const { return elidedText (nameFont, myNameText, nameRect.width ()); }
      QString statusText () const { return elidedText (statusFont, myStatusText, statusRect.width ()); }
      QString progressText () const  { return elidedText (progressFont, myProgressText, progressRect.width ()); }

    private:
      QString elidedText (const QFont& font, const QString& text, int width) const
      {
        return QFontMetrics (font).elidedText (text, Qt::ElideRight, width);
      }
  };

  ItemLayout::ItemLayout(const QString& nameText, const QString& statusText, const QString& progressText,
                         const QIcon& emblemIcon, const QFont& baseFont, Qt::LayoutDirection direction,
                         const QPoint& topLeft, int width):
    myNameText (nameText),
    myStatusText (statusText),
    myProgressText (progressText),
    nameFont (baseFont),
    statusFont (baseFont),
    progressFont (baseFont)
  {
    const QStyle * style (qApp->style ());
    const int iconSize (style->pixelMetric (QStyle::PM_LargeIconSize));

    nameFont.setWeight (QFont::Bold);
    const QFontMetrics nameFM (nameFont);
    const QSize nameSize (nameFM.size (0, myNameText));

    statusFont.setPointSize (static_cast<int> (statusFont.pointSize () * 0.9));
    const QFontMetrics statusFM (statusFont);
    const QSize statusSize (statusFM.size (0, myStatusText));

    progressFont.setPointSize (static_cast<int> (progressFont.pointSize () * 0.9));
    const QFontMetrics progressFM (progressFont);
    const QSize progressSize (progressFM.size (0, myProgressText));

    QRect baseRect (topLeft, QSize (width, 0));
    Utils::narrowRect (baseRect, iconSize + GUI_PAD, 0, direction);

    nameRect = baseRect.adjusted(0, 0, 0, nameSize.height ());
    statusRect = nameRect.adjusted(0, nameRect.height () + 1, 0, statusSize.height () + 1);
    barRect = statusRect.adjusted(0, statusRect.height () + 1, 0, BAR_HEIGHT + 1);
    progressRect = barRect.adjusted (0, barRect.height () + 1, 0, progressSize.height () + 1);
    iconRect = style->alignedRect (direction, Qt::AlignLeft | Qt::AlignVCenter,
                                   QSize (iconSize, iconSize),
                                   QRect (topLeft, QSize (width, progressRect.bottom () - nameRect.top ())));
    emblemRect = style->alignedRect (direction, Qt::AlignRight | Qt::AlignBottom,
                                     emblemIcon.actualSize (iconRect.size () / 2, QIcon::Normal, QIcon::On),
                                     iconRect);
  }
}

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
TorrentDelegate::progressString (const Torrent& tor)
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
      //: First part of torrent progress string;
      //: %1 is the percentage of torrent metadata downloaded
      str = tr ("Magnetized transfer - retrieving metadata (%1%)")
            .arg (Formatter::percentToString (tor.metadataPercentDone() * 100.0));
    }
  else if (!isDone) // downloading
    {
      //: First part of torrent progress string;
      //: %1 is how much we've got,
      //: %2 is how much we'll have when done,
      //: %3 is a percentage of the two
      str = tr ("%1 of %2 (%3%)")
            .arg (Formatter::sizeToString (haveTotal))
            .arg (Formatter::sizeToString (tor.sizeWhenDone()))
            .arg (Formatter::percentToString (tor.percentDone() * 100.0));
    }
  else if (!isSeed) // partial seed
    {
      if (hasSeedRatio)
        {
          //: First part of torrent progress string;
          //: %1 is how much we've got,
          //: %2 is the torrent's total size,
          //: %3 is a percentage of the two,
          //: %4 is how much we've uploaded,
          //: %5 is our upload-to-download ratio,
          //: %6 is the ratio we want to reach before we stop uploading
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
            //: First part of torrent progress string;
            //: %1 is how much we've got,
            //: %2 is the torrent's total size,
            //: %3 is a percentage of the two,
            //: %4 is how much we've uploaded,
            //: %5 is our upload-to-download ratio
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
          //: First part of torrent progress string;
          //: %1 is the torrent's total size,
          //: %2 is how much we've uploaded,
          //: %3 is our upload-to-download ratio,
          //: %4 is the ratio we want to reach before we stop uploading
          str = tr ("%1, uploaded %2 (Ratio: %3 Goal: %4)")
                .arg (Formatter::sizeToString (haveTotal))
                .arg (Formatter::sizeToString (tor.uploadedEver()))
                .arg (Formatter::ratioToString (tor.ratio()))
                .arg (Formatter::ratioToString (seedRatio));
        }
      else // seeding w/o a ratio
        {
          //: First part of torrent progress string;
          //: %1 is the torrent's total size,
          //: %2 is how much we've uploaded,
          //: %3 is our upload-to-download ratio
          str = tr ("%1, uploaded %2 (Ratio: %3)")
                .arg (Formatter::sizeToString (haveTotal))
                .arg (Formatter::sizeToString (tor.uploadedEver()))
                .arg (Formatter::ratioToString (tor.ratio()));
        }
    }

  // add time when downloading
  if ((hasSeedRatio && tor.isSeeding()) || tor.isDownloading())
    {
      if (tor.hasETA ())
        //: Second (optional) part of torrent progress string;
        //: %1 is duration;
        //: notice that leading space (before the dash) is included here
        str += tr (" - %1 left").arg (Formatter::timeToString (tor.getETA ()));
      else
        //: Second (optional) part of torrent progress string;
        //: notice that leading space (before the dash) is included here
        str += tr (" - Remaining time unknown");
    }

    return str.trimmed ();
}

QString
TorrentDelegate::shortTransferString (const Torrent& tor)
{
  QString str;
  const bool haveMeta (tor.hasMetadata());
  const bool haveDown (haveMeta && ((tor.webseedsWeAreDownloadingFrom()>0) || (tor.peersWeAreDownloadingFrom()>0)));
  const bool haveUp (haveMeta && tor.peersWeAreUploadingTo()>0);

  if (haveDown)
    str = Formatter::downloadSpeedToString(tor.downloadSpeed()) +
          QLatin1String ("   ") +
          Formatter::uploadSpeedToString(tor.uploadSpeed());

  else if (haveUp)
    str = Formatter::uploadSpeedToString(tor.uploadSpeed());

  return str.trimmed ();
}

QString
TorrentDelegate::shortStatusString (const Torrent& tor)
{
  QString str;

  switch (tor.getActivity ())
    {
      case TR_STATUS_CHECK:
        str = tr ("Verifying local data (%1% tested)").arg (Formatter::percentToString (tor.getVerifyProgress()*100.0));
        break;

      case TR_STATUS_DOWNLOAD:
      case TR_STATUS_SEED:
        str = shortTransferString(tor) +
              QLatin1String ("    ") +
              tr("Ratio: %1").arg(Formatter::ratioToString(tor.ratio()));
        break;

      default:
        str = tor.activityString ();
        break;
    }

  return str.trimmed ();
}

QString
TorrentDelegate::statusString (const Torrent& tor)
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
            str = tr ("Downloading metadata from %Ln peer(s) (%1% done)", 0, tor.peersWeAreDownloadingFrom ())
                  .arg (Formatter::percentToString (100.0 * tor.metadataPercentDone ()));
          }
        else
          {
            /* it would be nicer for translation if this was all one string, but I don't see how to do multiple %n's in tr() */
            if (tor.connectedPeersAndWebseeds () == 0)
              //: First part of phrase "Downloading from ... peer(s) and ... web seed(s)"
              str = tr ("Downloading from %Ln peer(s)", 0, tor.peersWeAreDownloadingFrom ());
            else
              //: First part of phrase "Downloading from ... of ... connected peer(s) and ... web seed(s)"
              str = tr ("Downloading from %1 of %Ln connected peer(s)", 0, tor.connectedPeersAndWebseeds ())
                    .arg (tor.peersWeAreDownloadingFrom ());

            if (tor.webseedsWeAreDownloadingFrom())
              //: Second (optional) part of phrase "Downloading from ... of ... connected peer(s) and ... web seed(s)";
              //: notice that leading space (before "and") is included here
              str += tr(" and %Ln web seed(s)", 0, tor.webseedsWeAreDownloadingFrom());
          }
        break;

      case TR_STATUS_SEED:
        if (tor.connectedPeers () == 0)
          str = tr ("Seeding to %Ln peer(s)", 0, tor.peersWeAreUploadingTo ());
        else
          str = tr ("Seeding to %1 of %Ln connected peer(s)", 0, tor.connectedPeers ())
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

  return str.trimmed ();
}

/***
****
***/

QSize
TorrentDelegate::sizeHint (const QStyleOptionViewItem& option, const Torrent& tor) const
{
  const QSize m (margin (*qApp->style ()));
  const ItemLayout layout (tor.name (), progressString (tor), statusString (tor), QIcon (),
                           option.font, option.direction, QPoint (0, 0), option.rect.width () - m.width () * 2);
  return layout.size () + m * 2;
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
      myProgressBarStyle->progress = static_cast<int> (myProgressBarStyle->minimum + (((isMagnet ? tor.metadataPercentDone() : tor.percentDone()) * (myProgressBarStyle->maximum - myProgressBarStyle->minimum))));
    }
}

void
TorrentDelegate::drawTorrent (QPainter                   * painter,
                              const QStyleOptionViewItem & option,
                              const Torrent              & tor) const
{
  const QStyle * style (qApp->style ());

  const bool isPaused (tor.isPaused ());

  const bool isItemSelected ((option.state & QStyle::State_Selected) != 0);
  const bool isItemEnabled ((option.state & QStyle::State_Enabled) != 0);
  const bool isItemActive ((option.state & QStyle::State_Active) != 0);

  painter->save ();

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
  const ItemLayout layout (tor.name (), progressString (tor), statusString (tor), emblemIcon,
                           option.font, option.direction, contentRect.topLeft (), contentRect.width ());

  // render
  if (tor.hasError () && !isItemSelected)
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
  painter->setFont (layout.progressFont);
  painter->drawText (layout.progressRect, Qt::AlignLeft | Qt::AlignVCenter, layout.progressText ());
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
  setProgressBarPercentDone (option, tor);

  style->drawControl (QStyle::CE_ProgressBar, myProgressBarStyle, painter);

  painter->restore ();
}
