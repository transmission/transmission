/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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
#include <QStyleOptionProgressBarV2>

#include "torrent.h"
#include "torrent-delegate-min.h"
#include "torrent-model.h"
#include "utils.h"
#include "qticonloader.h"

enum
{
   GUI_PAD = 6,
   BAR_HEIGHT = 12,
   LINE_SPACING = 4
};

/***
****
****   +---------+-----------------------------------------------+
****   |  Icon   |   Title                   shortStatusString   |
****   |  Icon   |   [ Progressbar.......................... ]   |
****   +-------- +-----------------------------------------------+
****
***/

QSize
TorrentDelegateMin :: sizeHint( const QStyleOptionViewItem& option, const Torrent& tor ) const
{
    const QStyle* style( QApplication::style( ) );
    static const int iconSize( style->pixelMetric( QStyle :: PM_SmallIconSize ) );

    QFont nameFont( option.font );
    const QFontMetrics nameFM( nameFont );
    const QString nameStr( tor.name( ) );
    const QSize nameSize( nameFM.size( 0, nameStr ) );

    QFont statusFont( option.font );
    statusFont.setPointSize( int( option.font.pointSize( ) * 0.85 ) );
    const QFontMetrics statusFM( statusFont );
    const QString statusStr( shortStatusString( tor ) );
    const QSize statusSize( statusFM.size( 0, statusStr ) );

    const QSize m( margin( *style ) );

    return QSize( m.width() + iconSize + GUI_PAD + nameSize.width() + GUI_PAD + statusSize.width() + m.width(),
                  m.height() + nameSize.height() + LINE_SPACING + BAR_HEIGHT  + m.height() );
}

void
TorrentDelegateMin :: drawTorrent( QPainter * painter, const QStyleOptionViewItem& option, const Torrent& tor ) const
{
    const bool isPaused( tor.isPaused( ) );
    const QStyle * style( QApplication::style( ) );
    static const int iconSize( style->pixelMetric( QStyle :: PM_SmallIconSize ) );

    QFont nameFont( option.font );
    const QFontMetrics nameFM( nameFont );
    const QString nameStr( tor.name( ) );
    const QSize nameSize( nameFM.size( 0, nameStr ) );

    QFont statusFont( option.font );
    statusFont.setPointSize( int( option.font.pointSize( ) * 0.85 ) );
    const QFontMetrics statusFM( statusFont );
    const QString statusStr( shortStatusString( tor ) );
    const QSize statusSize( statusFM.size( 0, statusStr ) );

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
    const QRect iconArea( fillArea.x( ),
                          fillArea.y( ) + ( fillArea.height( ) - iconSize ) / 2,
                          iconSize,
                          iconSize );
    const QRect statusArea( fillArea.width( ) - statusSize.width( ),
                            fillArea.top( ) + ((nameSize.height()-statusSize.height())/2),
                            statusSize.width( ),
                            statusSize.height( ) );
    const QRect nameArea( iconArea.x( ) + iconArea.width( ) + GUI_PAD,
                          fillArea.y( ),
                          fillArea.width( ) - statusArea.width( ) - (GUI_PAD*2) - iconArea.width( ),
                          nameSize.height( ) );
    const QRect barArea( nameArea.left( ),
                         nameArea.bottom( ),
                         statusArea.right( ) - nameArea.left( ),
                         BAR_HEIGHT );               
                           
    // render
    if( tor.hasError( ) )
        painter->setPen( QColor( "red" ) );
    else
        painter->setPen( option.palette.color( cg, cr ) );
    tor.getMimeTypeIcon().paint( painter, iconArea, Qt::AlignCenter, im, qs );
    painter->setFont( nameFont );
    painter->drawText( nameArea, 0, nameFM.elidedText( nameStr, Qt::ElideRight, nameArea.width( ) ) );
    painter->setFont( statusFont );
    painter->drawText( statusArea, 0, statusStr );
    myProgressBarStyle->rect = barArea;
    myProgressBarStyle->direction = option.direction;
    myProgressBarStyle->palette = option.palette;
    myProgressBarStyle->palette.setCurrentColorGroup( cg );
    myProgressBarStyle->state = progressBarState;
    myProgressBarStyle->progress = int(myProgressBarStyle->minimum + ((tor.percentDone() * (myProgressBarStyle->maximum - myProgressBarStyle->minimum))));
    style->drawControl( QStyle::CE_ProgressBar, myProgressBarStyle, painter );

    painter->restore( );
}
