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

#ifndef QTR_HIG_H
#define QTR_HIG_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QString;
class QGridLayout;
class QLayout;

class HIG: public QWidget
{
        Q_OBJECT

    public:
        enum {
            PAD_SMALL = 3,
            PAD = 6,
            PAD_BIG = 12,
            PAD_LARGE = PAD_BIG
        };

    public:
        HIG( QWidget * parent = 0 );
        virtual ~HIG( );

    public:
        void addSectionDivider( );
        void addSectionTitle( const QString& );
        void addSectionTitle( QWidget* );
        void addSectionTitle( QLayout* );
        void addWideControl( QLayout * );
        void addWideControl( QWidget * );
        QCheckBox* addWideCheckBox( const QString&, bool isChecked );
        QLabel* addLabel( const QString& );
        void addLabel( QWidget * );
        void addControl( QWidget * );
        void addControl( QLayout * );
        QLabel* addRow( const QString & label, QWidget * control, QWidget * buddy=0 );
        QLabel* addRow( const QString & label, QLayout * control, QWidget * buddy );
        void addRow( QWidget * label, QWidget * control, QWidget * buddy=0 );
        void addRow( QWidget * label, QLayout * control, QWidget * buddy );
        void finish( );

    private:
        QLayout* addRow( QWidget* w );

    private:
        int myRow;
        QGridLayout * myGrid;
};

#endif // QTR_HIG_H
