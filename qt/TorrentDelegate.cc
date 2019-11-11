/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
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
    ItemLayout(QString const& nameText, QString const& statusText, QString const& progressText, QIcon const& emblemIcon,
        QFont const& baseFont, Qt::LayoutDirection direction, QPoint const& topLeft, int width);

    QSize size() const
    {
        return (iconRect | nameRect | statusRect | barRect | progressRect).size();
    }

    QString nameText() const
    {
        return elidedText(nameFont, myNameText, nameRect.width());
    }

    QString statusText() const
    {
        return elidedText(statusFont, myStatusText, statusRect.width());
    }

    QString progressText() const
    {
        return elidedText(progressFont, myProgressText, progressRect.width());
    }

private:
    QString elidedText(QFont const& font, QString const& text, int width) const
    {
        return QFontMetrics(font).elidedText(text, Qt::ElideRight, width);
    }
};

ItemLayout::ItemLayout(QString const& nameText, QString const& statusText, QString const& progressText, QIcon const& emblemIcon,
    QFont const& baseFont, Qt::LayoutDirection direction, QPoint const& topLeft, int width) :
    myNameText(nameText),
    myStatusText(statusText),
    myProgressText(progressText),
    nameFont(baseFont),
    statusFont(baseFont),
    progressFont(baseFont)
{
    QStyle const* style(qApp->style());
    int const iconSize(style->pixelMetric(QStyle::PM_LargeIconSize));

    nameFont.setWeight(QFont::Bold);
    QFontMetrics const nameFM(nameFont);
    QSize const nameSize(nameFM.size(0, myNameText));

    statusFont.setPointSize(static_cast<int>(statusFont.pointSize() * 0.9));
    QFontMetrics const statusFM(statusFont);
    QSize const statusSize(statusFM.size(0, myStatusText));

    progressFont.setPointSize(static_cast<int>(progressFont.pointSize() * 0.9));
    QFontMetrics const progressFM(progressFont);
    QSize const progressSize(progressFM.size(0, myProgressText));

    QRect baseRect(topLeft, QSize(width, 0));
    Utils::narrowRect(baseRect, iconSize + GUI_PAD, 0, direction);

    nameRect = baseRect.adjusted(0, 0, 0, nameSize.height());
    statusRect = nameRect.adjusted(0, nameRect.height() + 1, 0, statusSize.height() + 1);
    barRect = statusRect.adjusted(0, statusRect.height() + 1, 0, BAR_HEIGHT + 1);
    progressRect = barRect.adjusted(0, barRect.height() + 1, 0, progressSize.height() + 1);
    iconRect = style->alignedRect(direction, Qt::AlignLeft | Qt::AlignVCenter, QSize(iconSize, iconSize),
        QRect(topLeft, QSize(width, progressRect.bottom() - nameRect.top())));
    emblemRect = style->alignedRect(direction, Qt::AlignRight | Qt::AlignBottom,
        emblemIcon.actualSize(iconRect.size() / 2, QIcon::Normal, QIcon::On), iconRect);
}

} // namespace

TorrentDelegate::TorrentDelegate(QObject* parent) :
    QStyledItemDelegate(parent),
    myProgressBarStyle(new QStyleOptionProgressBar)
{
    myProgressBarStyle->minimum = 0;
    myProgressBarStyle->maximum = 1000;

    greenBrush = QColor("forestgreen");
    greenBack = QColor("darkseagreen");

    blueBrush = QColor("steelblue");
    blueBack = QColor("lightgrey");

    silverBrush = QColor("silver");
    silverBack = QColor("grey");
}

TorrentDelegate::~TorrentDelegate()
{
    delete myProgressBarStyle;
}

/***
****
***/

QSize TorrentDelegate::margin(QStyle const& style) const
{
    Q_UNUSED(style)

    return QSize(4, 4);
}

QString TorrentDelegate::progressString(Torrent const& tor)
{
    bool const isMagnet(!tor.hasMetadata());
    bool const isDone(tor.isDone());
    bool const isSeed(tor.isSeed());
    uint64_t const haveTotal(tor.haveTotal());
    QString str;
    double seedRatio;
    bool const hasSeedRatio(tor.getSeedRatio(seedRatio));

    if (isMagnet) // magnet link with no metadata
    {
        //: First part of torrent progress string;
        //: %1 is the percentage of torrent metadata downloaded
        str = tr("Magnetized transfer - retrieving metadata (%1%)").
            arg(Formatter::percentToString(tor.metadataPercentDone() * 100.0));
    }
    else if (!isDone) // downloading
    {
        //: First part of torrent progress string;
        //: %1 is how much we've got,
        //: %2 is how much we'll have when done,
        //: %3 is a percentage of the two
        str = tr("%1 of %2 (%3%)").arg(Formatter::sizeToString(haveTotal)).arg(Formatter::sizeToString(tor.sizeWhenDone())).
            arg(Formatter::percentToString(tor.percentDone() * 100.0));
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
            str = tr("%1 of %2 (%3%), uploaded %4 (Ratio: %5 Goal: %6)").arg(Formatter::sizeToString(haveTotal)).
                arg(Formatter::sizeToString(tor.totalSize())).
                arg(Formatter::percentToString(tor.percentComplete() * 100.0)).
                arg(Formatter::sizeToString(tor.uploadedEver())).arg(Formatter::ratioToString(tor.ratio())).
                arg(Formatter::ratioToString(seedRatio));
        }
        else
        {
            //: First part of torrent progress string;
            //: %1 is how much we've got,
            //: %2 is the torrent's total size,
            //: %3 is a percentage of the two,
            //: %4 is how much we've uploaded,
            //: %5 is our upload-to-download ratio
            str = tr("%1 of %2 (%3%), uploaded %4 (Ratio: %5)").arg(Formatter::sizeToString(haveTotal)).
                arg(Formatter::sizeToString(tor.totalSize())).
                arg(Formatter::percentToString(tor.percentComplete() * 100.0)).
                arg(Formatter::sizeToString(tor.uploadedEver())).arg(Formatter::ratioToString(tor.ratio()));
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
            str = tr("%1, uploaded %2 (Ratio: %3 Goal: %4)").arg(Formatter::sizeToString(haveTotal)).
                arg(Formatter::sizeToString(tor.uploadedEver())).arg(Formatter::ratioToString(tor.ratio())).
                arg(Formatter::ratioToString(seedRatio));
        }
        else // seeding w/o a ratio
        {
            //: First part of torrent progress string;
            //: %1 is the torrent's total size,
            //: %2 is how much we've uploaded,
            //: %3 is our upload-to-download ratio
            str = tr("%1, uploaded %2 (Ratio: %3)").arg(Formatter::sizeToString(haveTotal)).
                arg(Formatter::sizeToString(tor.uploadedEver())).arg(Formatter::ratioToString(tor.ratio()));
        }
    }

    // add time when downloading
    if ((hasSeedRatio && tor.isSeeding()) || tor.isDownloading())
    {
        if (tor.hasETA())
        {
            //: Second (optional) part of torrent progress string;
            //: %1 is duration;
            //: notice that leading space (before the dash) is included here
            str += tr(" - %1 left").arg(Formatter::timeToString(tor.getETA()));
        }
        else
        {
            //: Second (optional) part of torrent progress string;
            //: notice that leading space (before the dash) is included here
            str += tr(" - Remaining time unknown");
        }
    }

    return str.trimmed();
}

QString TorrentDelegate::shortTransferString(Torrent const& tor)
{
    QString str;
    bool const haveMeta(tor.hasMetadata());
    bool const haveDown(haveMeta && ((tor.webseedsWeAreDownloadingFrom() > 0) || (tor.peersWeAreDownloadingFrom() > 0)));
    bool const haveUp(haveMeta && tor.peersWeAreUploadingTo() > 0);

    if (haveDown)
    {
        str = Formatter::downloadSpeedToString(tor.downloadSpeed()) + QLatin1String("   ") +
            Formatter::uploadSpeedToString(tor.uploadSpeed());
    }
    else if (haveUp)
    {
        str = Formatter::uploadSpeedToString(tor.uploadSpeed());
    }

    return str.trimmed();
}

QString TorrentDelegate::shortStatusString(Torrent const& tor)
{
    QString str;

    switch (tor.getActivity())
    {
    case TR_STATUS_CHECK:
        str = tr("Verifying local data (%1% tested)").arg(Formatter::percentToString(tor.getVerifyProgress() * 100.0));
        break;

    case TR_STATUS_DOWNLOAD:
    case TR_STATUS_SEED:
        str = shortTransferString(tor) + QLatin1String("    ") + tr("Ratio: %1").arg(Formatter::ratioToString(tor.ratio()));
        break;

    default:
        str = tor.activityString();
        break;
    }

    return str.trimmed();
}

QString TorrentDelegate::statusString(Torrent const& tor)
{
    QString str;

    if (tor.hasError())
    {
        str = tor.getError();
    }
    else
    {
        switch (tor.getActivity())
        {
        case TR_STATUS_STOPPED:
        case TR_STATUS_CHECK_WAIT:
        case TR_STATUS_CHECK:
        case TR_STATUS_DOWNLOAD_WAIT:
        case TR_STATUS_SEED_WAIT:
            str = shortStatusString(tor);
            break;

        case TR_STATUS_DOWNLOAD:
            if (!tor.hasMetadata())
            {
                str = tr("Downloading metadata from %Ln peer(s) (%1% done)", nullptr, tor.peersWeAreDownloadingFrom()).
                    arg(Formatter::percentToString(100.0 * tor.metadataPercentDone()));
            }
            else
            {
                /* it would be nicer for translation if this was all one string, but I don't see how to do multiple %n's in
                 * tr() */
                if (tor.connectedPeersAndWebseeds() == 0)
                {
                    //: First part of phrase "Downloading from ... peer(s) and ... web seed(s)"
                    str = tr("Downloading from %Ln peer(s)", nullptr, tor.peersWeAreDownloadingFrom());
                }
                else
                {
                    //: First part of phrase "Downloading from ... of ... connected peer(s) and ... web seed(s)"
                    str = tr("Downloading from %1 of %Ln connected peer(s)", nullptr, tor.connectedPeersAndWebseeds()).
                        arg(tor.peersWeAreDownloadingFrom());
                }

                if (tor.webseedsWeAreDownloadingFrom())
                {
                    //: Second (optional) part of phrase "Downloading from ... of ... connected peer(s) and ... web seed(s)";
                    //: notice that leading space (before "and") is included here
                    str += tr(" and %Ln web seed(s)", nullptr, tor.webseedsWeAreDownloadingFrom());
                }
            }

            break;

        case TR_STATUS_SEED:
            if (tor.connectedPeers() == 0)
            {
                str = tr("Seeding to %Ln peer(s)", nullptr, tor.peersWeAreUploadingTo());
            }
            else
            {
                str = tr("Seeding to %1 of %Ln connected peer(s)", nullptr, tor.connectedPeers()).
                    arg(tor.peersWeAreUploadingTo());
            }

            break;

        default:
            str = tr("Error");
            break;
        }
    }

    if (tor.isReadyToTransfer())
    {
        QString s = shortTransferString(tor);

        if (!s.isEmpty())
        {
            str += tr(" - ") + s;
        }
    }

    return str.trimmed();
}

/***
****
***/

QSize TorrentDelegate::sizeHint(QStyleOptionViewItem const& option, Torrent const& tor) const
{
    QSize const m(margin(*qApp->style()));
    ItemLayout const layout(tor.name(), progressString(tor), statusString(tor), QIcon(), option.font, option.direction,
        QPoint(0, 0), option.rect.width() - m.width() * 2);
    return layout.size() + m * 2;
}

QSize TorrentDelegate::sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    // if the font changed, invalidate the height cache
    if (myHeightFont != option.font)
    {
        myHeightFont = option.font;
        myHeightHint.reset();
    }

    // ensure the height is cached
    if (!myHeightHint)
    {
        auto const* tor = index.data(TorrentModel::TorrentRole).value<Torrent const*>();
        myHeightHint = sizeHint(option, *tor).height();
    }

    return QSize(option.rect.width(), *myHeightHint);
}

QIcon& TorrentDelegate::getWarningEmblem() const
{
    auto& icon = myWarningEmblem;

    if (icon.isNull())
    {
        icon = QIcon::fromTheme(QLatin1String("emblem-important"));
    }

    if (icon.isNull())
    {
        icon = qApp->style()->standardIcon(QStyle::SP_MessageBoxWarning);
    }

    return icon;
}

void TorrentDelegate::paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    Torrent const* tor(index.data(TorrentModel::TorrentRole).value<Torrent const*>());
    painter->save();
    painter->setClipRect(option.rect);
    drawTorrent(painter, option, *tor);
    painter->restore();
}

void TorrentDelegate::setProgressBarPercentDone(QStyleOptionViewItem const& option, Torrent const& tor) const
{
    double seedRatioLimit;

    if (tor.isSeeding() && tor.getSeedRatio(seedRatioLimit))
    {
        double const seedRateRatio = tor.ratio() / seedRatioLimit;
        int const scaledProgress = seedRateRatio * (myProgressBarStyle->maximum - myProgressBarStyle->minimum);
        myProgressBarStyle->progress = myProgressBarStyle->minimum + scaledProgress;
    }
    else
    {
        bool const isMagnet(!tor.hasMetadata());
        myProgressBarStyle->direction = option.direction;
        myProgressBarStyle->progress = static_cast<int>(myProgressBarStyle->minimum + (isMagnet ? tor.metadataPercentDone() :
            tor.percentDone()) * (myProgressBarStyle->maximum - myProgressBarStyle->minimum));
    }
}

void TorrentDelegate::drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor) const
{
    QStyle const* style(qApp->style());

    bool const isPaused(tor.isPaused());

    bool const isItemSelected((option.state & QStyle::State_Selected) != 0);
    bool const isItemEnabled((option.state & QStyle::State_Enabled) != 0);
    bool const isItemActive((option.state & QStyle::State_Active) != 0);

    painter->save();

    if (isItemSelected)
    {
        QPalette::ColorGroup cg = isItemEnabled ? QPalette::Normal : QPalette::Disabled;

        if (cg == QPalette::Normal && !isItemActive)
        {
            cg = QPalette::Inactive;
        }

        painter->fillRect(option.rect, option.palette.brush(cg, QPalette::Highlight));
    }

    QIcon::Mode im;

    if (isPaused || !isItemEnabled)
    {
        im = QIcon::Disabled;
    }
    else if (isItemSelected)
    {
        im = QIcon::Selected;
    }
    else
    {
        im = QIcon::Normal;
    }

    QIcon::State qs;

    if (isPaused)
    {
        qs = QIcon::Off;
    }
    else
    {
        qs = QIcon::On;
    }

    QPalette::ColorGroup cg = QPalette::Normal;

    if (isPaused || !isItemEnabled)
    {
        cg = QPalette::Disabled;
    }

    if (cg == QPalette::Normal && !isItemActive)
    {
        cg = QPalette::Inactive;
    }

    QPalette::ColorRole cr;

    if (isItemSelected)
    {
        cr = QPalette::HighlightedText;
    }
    else
    {
        cr = QPalette::Text;
    }

    QStyle::State progressBarState(option.state);

    if (isPaused)
    {
        progressBarState = QStyle::State_None;
    }

    progressBarState |= QStyle::State_Small;

    QIcon::Mode const emblemIm = isItemSelected ? QIcon::Selected : QIcon::Normal;
    QIcon const emblemIcon = tor.hasError() ? getWarningEmblem() : QIcon();

    // layout
    QSize const m(margin(*style));
    QRect const contentRect(option.rect.adjusted(m.width(), m.height(), -m.width(), -m.height()));
    ItemLayout const layout(tor.name(), progressString(tor), statusString(tor), emblemIcon, option.font, option.direction,
        contentRect.topLeft(), contentRect.width());

    // render
    if (tor.hasError() && !isItemSelected)
    {
        painter->setPen(QColor("red"));
    }
    else
    {
        painter->setPen(option.palette.color(cg, cr));
    }

    tor.getMimeTypeIcon().paint(painter, layout.iconRect, Qt::AlignCenter, im, qs);

    if (!emblemIcon.isNull())
    {
        emblemIcon.paint(painter, layout.emblemRect, Qt::AlignCenter, emblemIm, qs);
    }

    painter->setFont(layout.nameFont);
    painter->drawText(layout.nameRect, Qt::AlignLeft | Qt::AlignVCenter, layout.nameText());
    painter->setFont(layout.statusFont);
    painter->drawText(layout.statusRect, Qt::AlignLeft | Qt::AlignVCenter, layout.statusText());
    painter->setFont(layout.progressFont);
    painter->drawText(layout.progressRect, Qt::AlignLeft | Qt::AlignVCenter, layout.progressText());
    myProgressBarStyle->rect = layout.barRect;

    if (tor.isDownloading())
    {
        myProgressBarStyle->palette.setBrush(QPalette::Highlight, blueBrush);
        myProgressBarStyle->palette.setColor(QPalette::Base, blueBack);
        myProgressBarStyle->palette.setColor(QPalette::Window, blueBack);
    }
    else if (tor.isSeeding())
    {
        myProgressBarStyle->palette.setBrush(QPalette::Highlight, greenBrush);
        myProgressBarStyle->palette.setColor(QPalette::Base, greenBack);
        myProgressBarStyle->palette.setColor(QPalette::Window, greenBack);
    }
    else
    {
        myProgressBarStyle->palette.setBrush(QPalette::Highlight, silverBrush);
        myProgressBarStyle->palette.setColor(QPalette::Base, silverBack);
        myProgressBarStyle->palette.setColor(QPalette::Window, silverBack);
    }

    myProgressBarStyle->state = progressBarState;
    setProgressBarPercentDone(option, tor);

    style->drawControl(QStyle::CE_ProgressBar, myProgressBarStyle, painter);

    painter->restore();
}
