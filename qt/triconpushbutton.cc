/*
 * This file Copyright (C) 2009 Mnemosyne LLC
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
#include <QIcon>
#include <QStyleOption>
#include <QStyleOptionButton>
#include <QStylePainter>

#include "hig.h"
#include "triconpushbutton.h"

TrIconPushButton :: TrIconPushButton( QWidget * parent ):
    QPushButton( parent )
{
}

TrIconPushButton :: TrIconPushButton( const QIcon& icon, QWidget * parent ):
    QPushButton( parent )
{
    setIcon( icon );
}

QSize
TrIconPushButton :: sizeHint () const
{
    QSize s = iconSize( );
    s.rwidth() += HIG::PAD_SMALL*2;
    return s;
}

void
TrIconPushButton :: paintEvent( QPaintEvent * )
{
    QStylePainter p( this );
    QStyleOptionButton opt;
    initStyleOption( &opt );

    QIcon::Mode mode = opt.state & QStyle::State_Enabled ? QIcon::Normal : QIcon::Disabled;
    if( ( mode == QIcon::Normal ) && ( opt.state & QStyle::State_HasFocus ) )
        mode = QIcon::Active;
    QIcon::State state = QIcon::Off;
    if( opt.state & QStyle::State_On )
        state = QIcon::On;
    QPixmap pixmap = opt.icon.pixmap( opt.iconSize, QIcon::Active, QIcon::On );
    QRect iconRect( opt.rect.x() + HIG::PAD_SMALL,
                    opt.rect.y() + (opt.rect.height() - pixmap.height())/2,
                    pixmap.width(),
                    pixmap.height());
    if( opt.state & ( QStyle::State_On | QStyle::State_Sunken ) )
        iconRect.translate( style()->pixelMetric( QStyle::PM_ButtonShiftHorizontal, &opt, this ),
                            style()->pixelMetric( QStyle::PM_ButtonShiftVertical, &opt, this ) );

    p.drawPixmap(iconRect, pixmap);

    if( opt.state & QStyle::State_HasFocus )
        p.drawPrimitive( QStyle::PE_FrameFocusRect, opt );
}
