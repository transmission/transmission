/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
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
