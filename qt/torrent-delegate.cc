/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
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
#include <QStyleOptionProgressBarV2>

#include "torrent.h"
#include "torrent-delegate.h"
#include "torrent-model.h"
#include "utils.h"
#include "qticonloader.h"

enum
{
   GUI_PAD = 6,
   BAR_HEIGHT = 12
};

TorrentDelegate :: TorrentDelegate( QObject * parent ):
    QItemDelegate( parent ),
    myProgressBarStyle( new QStyleOptionProgressBarV2 )
{
    myProgressBarStyle->minimum = 0;
    myProgressBarStyle->maximum = 1000;
}

TorrentDelegate :: ~TorrentDelegate( )
{
    delete myProgressBarStyle;
}

/***
****
***/

QSize
TorrentDelegate :: margin( const QStyle& style ) const
{
    Q_UNUSED( style );

    return QSize( 4, 4 );
}

QString
TorrentDelegate :: progressString( const Torrent& tor ) const
{
    const bool isDone( tor.isDone( ) );
    const bool isSeed( tor.isSeed( ) );
    const uint64_t haveTotal( tor.haveTotal( ) );
    QString str;
    double seedRatio; 
    bool hasSeedRatio;

    if( !isDone )
    {
        /* %1 is how much we've got,
           %2 is how much we'll have when done,
           %3 is a percentage of the two */
        str = tr( "%1 of %2 (%3%)" ).arg( Utils::sizeToString( haveTotal ) )
                                    .arg( Utils::sizeToString( tor.sizeWhenDone( ) ) )
                                    .arg( tor.percentDone( ) * 100.0, 0, 'f', 2 );
    }
    else if( !isSeed )
    {
        /* %1 is how much we've got,
           %2 is the torrent's total size,
           %3 is a percentage of the two,
           %4 is how much we've uploaded,
           %5 is our upload-to-download ratio */
        str = tr( "%1 of %2 (%3%), uploaded %4 (Ratio: %5)" )
              .arg( Utils::sizeToString( haveTotal ) )
              .arg( Utils::sizeToString( tor.sizeWhenDone( ) ) )
              .arg( tor.percentDone( ) * 100.0, 0, 'f', 2 )
              .arg( Utils::sizeToString( tor.uploadedEver( ) ) )
              .arg( Utils::ratioToString( tor.ratio( ) ) );
    }
    else if(( hasSeedRatio = tor.getSeedRatio( seedRatio )))
    {
        /* %1 is the torrent's total size,
           %2 is how much we've uploaded,
           %3 is our upload-to-download ratio,
           $4 is the ratio we want to reach before we stop uploading */
        str = tr( "%1, uploaded %2 (Ratio: %3 Goal %4)" )
              .arg( Utils::sizeToString( haveTotal ) )
              .arg( Utils::sizeToString( tor.uploadedEver( ) ) )
              .arg( Utils::ratioToString( tor.ratio( ) ) )
              .arg( Utils::ratioToString( seedRatio ) );
    }
    else /* seeding w/o a ratio */
    {
        /* %1 is the torrent's total size,
           %2 is how much we've uploaded,
           %3 is our upload-to-download ratio */
        str = tr( "%1, uploaded %2 (Ratio: %3)" )
              .arg( Utils::sizeToString( haveTotal ) )
              .arg( Utils::sizeToString( tor.uploadedEver( ) ) )
              .arg( Utils::ratioToString( tor.ratio( ) ) );
    }

    /* add time when downloading */
    if( hasSeedRatio || tor.isDownloading( ) )
    {
        str += tr( " - " );
        if( tor.hasETA( ) )
            str += tr( "%1 left" ).arg( Utils::timeToString( tor.getETA( ) ) );
        else
            str += tr( "Remaining time unknown" );
    }

    return str;
}

QString
TorrentDelegate :: shortTransferString( const Torrent& tor ) const
{
    const bool haveDown( tor.peersWeAreDownloadingFrom( ) > 0 );
    const bool haveUp( tor.peersWeAreUploadingTo( ) > 0 );
    QString downStr, upStr, str;

    if( haveDown )
        downStr = Utils :: speedToString( tor.downloadSpeed( ) );
    if( haveUp )
        upStr = Utils :: speedToString( tor.uploadSpeed( ) );

    if( haveDown && haveUp )
        str = tr( "Down: %1, Up: %2" ).arg(downStr).arg(upStr);
    else if( haveDown )
        str = tr( "Down: %1" ).arg( downStr );
    else if( haveUp )
        str = tr( "Up: %1" ).arg( upStr );
    else
        str = tr( "Idle" );

    return str;
}

QString
TorrentDelegate :: shortStatusString( const Torrent& tor ) const
{
    QString str;

    switch( tor.getActivity( ) )
    {
        case TR_STATUS_STOPPED:
            str = tr( "Paused" );
            break;

        case TR_STATUS_CHECK_WAIT:
            str = tr( "Waiting to verify local data" );
            break;

        case TR_STATUS_CHECK:
            str = tr( "Verifying local data (%1% tested)" ).arg( tor.getVerifyProgress()*100.0, 0, 'f', 1 );
            break;

        case TR_STATUS_DOWNLOAD:
        case TR_STATUS_SEED:
            if( !tor.isDownloading( ) )
                str = tr( "Ratio: %1, " ).arg( Utils::ratioToString( tor.ratio( ) ) );
            str += shortTransferString( tor );
            break;

        default:
            break;
    }

    return str;
}

QString
TorrentDelegate :: statusString( const Torrent& tor ) const
{
    QString str;

    if( tor.hasError( ) )
    {
        str = tor.getError( );
    }
    else switch( tor.getActivity( ) )
    {
        case TR_STATUS_STOPPED:
        case TR_STATUS_CHECK_WAIT:
        case TR_STATUS_CHECK:
            str = shortStatusString( tor );
            break;

        case TR_STATUS_DOWNLOAD:
            str = tr( "Downloading from %1 of %n connected peer(s)", 0, tor.connectedPeersAndWebseeds( ) )
                  .arg( tor.peersWeAreDownloadingFrom( ) );
            break;

        case TR_STATUS_SEED:
            str = tr( "Seeding to %1 of %n connected peer(s)", 0, tor.connectedPeers( ) )
                  .arg( tor.peersWeAreUploadingTo( ) );
            break;

        default:
            str = "Error";
            break;
    }

    if( tor.isReadyToTransfer( ) )
        str += tr( " - " ) + shortTransferString( tor );

    return str;
}

/***
****
***/

namespace
{
    int MAX3( int a, int b, int c )
    {
        const int ab( a > b ? a : b );
        return ab > c ? ab : c;
    }
}

QSize
TorrentDelegate :: sizeHint( const QStyleOptionViewItem& option, const Torrent& tor ) const
{
    const QStyle* style( QApplication::style( ) );
    static const int iconSize( style->pixelMetric( QStyle::PM_MessageBoxIconSize ) );

    QFont nameFont( option.font );
    nameFont.setWeight( QFont::Bold );
    const QFontMetrics nameFM( nameFont );
    const QString nameStr( tor.name( ) );
    const QSize nameSize( nameFM.size( 0, nameStr ) );
    QFont statusFont( option.font );
    statusFont.setPointSize( int( option.font.pointSize( ) * 0.9 ) );
    const QFontMetrics statusFM( statusFont );
    const QString statusStr( statusString( tor ) );
    const QSize statusSize( statusFM.size( 0, statusStr ) );
    QFont progressFont( statusFont );
    const QFontMetrics progressFM( progressFont );
    const QString progressStr( progressString( tor ) );
    const QSize progressSize( progressFM.size( 0, progressStr ) );
    const QSize m( margin( *style ) );
    return QSize( m.width()*2 + iconSize + GUI_PAD + MAX3( nameSize.width(), statusSize.width(), progressSize.width() ),
                  //m.height()*3 + nameFM.lineSpacing() + statusFM.lineSpacing()*2 + progressFM.lineSpacing() );
                  m.height()*3 + nameFM.lineSpacing() + statusFM.lineSpacing() + BAR_HEIGHT + progressFM.lineSpacing() );
}

QSize
TorrentDelegate :: sizeHint( const QStyleOptionViewItem  & option,
                             const QModelIndex           & index ) const
{
    const Torrent * tor( index.model()->data( index, TorrentModel::TorrentRole ).value<const Torrent*>() );
    return sizeHint( option, *tor );
}

void
TorrentDelegate :: paint( QPainter                    * painter,
                          const QStyleOptionViewItem  & option,
                          const QModelIndex           & index) const
{
    const Torrent * tor( index.model()->data( index, TorrentModel::TorrentRole ).value<const Torrent*>() );
    painter->save( );
    painter->setClipRect( option.rect );
    drawBackground( painter, option, index );
    drawTorrent( painter, option, *tor );
    drawFocus(painter, option, option.rect );
    painter->restore( );
}

void
TorrentDelegate :: drawTorrent( QPainter * painter, const QStyleOptionViewItem& option, const Torrent& tor ) const
{
    const QStyle * style( QApplication::style( ) );
    static const int iconSize( style->pixelMetric( QStyle::PM_LargeIconSize ) );
    QFont nameFont( option.font );
    nameFont.setWeight( QFont::Bold );
    const QFontMetrics nameFM( nameFont );
    const QString nameStr( tor.name( ) );
    const QSize nameSize( nameFM.size( 0, nameStr ) );
    QFont statusFont( option.font );
    statusFont.setPointSize( int( option.font.pointSize( ) * 0.9 ) );
    const QFontMetrics statusFM( statusFont );
    const QString statusStr( progressString( tor ) );
    QFont progressFont( statusFont );
    const QFontMetrics progressFM( progressFont );
    const QString progressStr( statusString( tor ) );
    const bool isPaused( tor.isPaused( ) );

    painter->save( );

    if (option.state & QStyle::State_Selected) {
        QPalette::ColorGroup cg = option.state & QStyle::State_Enabled
                                  ? QPalette::Normal : QPalette::Disabled;
        if (cg == QPalette::Normal && !(option.state & QStyle::State_Active))
            cg = QPalette::Inactive;

        painter->fillRect(option.rect, option.palette.brush(cg, QPalette::Highlight));
    }

    QIcon::Mode im;
    if( isPaused || !(option.state & QStyle::State_Enabled ) ) im = QIcon::Disabled;
    else if( option.state & QStyle::State_Selected ) im = QIcon::Selected;
    else im = QIcon::Normal;

    QIcon::State qs;
    if( isPaused ) qs = QIcon::Off;
    else qs = QIcon::On;

    QPalette::ColorGroup cg = QPalette::Normal;
    if( isPaused || !(option.state & QStyle::State_Enabled ) ) cg = QPalette::Disabled;
    if( cg == QPalette::Normal && !(option.state & QStyle::State_Active ) ) cg = QPalette::Inactive;

    QPalette::ColorRole cr;
    if( option.state & QStyle::State_Selected ) cr = QPalette::HighlightedText;
    else cr = QPalette::Text;

    QStyle::State progressBarState( option.state );
    if( isPaused ) progressBarState = QStyle::State_None;
    progressBarState |= QStyle::State_Small;

    // layout
    const QSize m( margin( *style ) );
    QRect fillArea( option.rect );
    fillArea.adjust( m.width(), m.height(), -m.width(), -m.height() );
    QRect iconArea( fillArea.x( ), fillArea.y( ) + ( fillArea.height( ) - iconSize ) / 2, iconSize, iconSize );
    QRect nameArea( iconArea.x( ) + iconArea.width( ) + GUI_PAD, fillArea.y( ),
                    fillArea.width( ) - GUI_PAD - iconArea.width( ), nameSize.height( ) );
    QRect statusArea( nameArea );
    statusArea.moveTop( nameArea.y( ) + nameFM.lineSpacing( ) );
    statusArea.setHeight( nameSize.height( ) );
    QRect barArea( statusArea );
    barArea.setHeight( BAR_HEIGHT );
    barArea.moveTop( statusArea.y( ) + statusFM.lineSpacing( ) );
    QRect progArea( statusArea );
    progArea.moveTop( barArea.y( ) + barArea.height( ) );

    // render
    if( tor.hasError( ) )
        painter->setPen( QColor( "red" ) );
    else
        painter->setPen( option.palette.color( cg, cr ) );
    tor.getMimeTypeIcon().paint( painter, iconArea, Qt::AlignCenter, im, qs );
    painter->setFont( nameFont );
    painter->drawText( nameArea, 0, nameFM.elidedText( nameStr, Qt::ElideRight, nameArea.width( ) ) );
    painter->setFont( statusFont );
    painter->drawText( statusArea, 0, statusFM.elidedText( statusStr, Qt::ElideRight, statusArea.width( ) ) );
    painter->setFont( progressFont );
    painter->drawText( progArea, 0, progressFM.elidedText( progressStr, Qt::ElideRight, progArea.width( ) ) );
    myProgressBarStyle->rect = barArea;
    myProgressBarStyle->direction = option.direction;
    myProgressBarStyle->palette = option.palette;
    myProgressBarStyle->palette.setCurrentColorGroup( cg );
    myProgressBarStyle->state = progressBarState;
    myProgressBarStyle->progress = int(myProgressBarStyle->minimum + ((tor.percentDone() * (myProgressBarStyle->maximum - myProgressBarStyle->minimum))));
    style->drawControl( QStyle::CE_ProgressBar, myProgressBarStyle, painter );

    painter->restore( );
}
