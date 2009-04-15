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

#ifndef QTR_IconPushButton_H
#define QTR_IconPushButton_H

#include <QPushButton>

class QIcon;

class TrIconPushButton: public QPushButton
{
        Q_OBJECT

    public:
        TrIconPushButton( QWidget * parent = 0 );
        TrIconPushButton( const QIcon&, QWidget * parent = 0 );
        virtual ~TrIconPushButton( ) { }
        QSize sizeHint () const;

    protected:
        void paintEvent( QPaintEvent * event );
};

#endif // QTR_IconPushButton_H
