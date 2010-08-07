/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <QDialogButtonBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include "hig.h"
#include "session.h"
#include "stats-dialog.h"
#include "utils.h"

enum
{
    REFRESH_INTERVAL_MSEC = (15*1000)
};

StatsDialog :: StatsDialog( Session & session, QWidget * parent ):
    QDialog( parent, Qt::Dialog ),
    mySession( session ),
    myTimer( new QTimer( this ) )
{
    myTimer->setSingleShot( false );
    connect( myTimer, SIGNAL(timeout()), this, SLOT(onTimer()) );
    setWindowTitle( tr( "Statistics" ) );

    HIG * hig = new HIG( );
    hig->addSectionTitle( tr( "Current Session" ) );
    hig->addRow( tr( "Uploaded:" ), myCurrentUp = new QLabel(  ) );
    hig->addRow( tr( "Downloaded:" ), myCurrentDown = new QLabel( ) );
    hig->addRow( tr( "Ratio:" ), myCurrentRatio = new QLabel( ) );
    hig->addRow( tr( "Duration:" ), myCurrentDuration = new QLabel( ) );
    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Total" ) );
    hig->addRow( myStartCount = new QLabel( tr( "Started %n time(s)", 0, 1 ) ), 0 );
    hig->addRow( tr( "Uploaded:" ), myTotalUp = new QLabel( ) );
    hig->addRow( tr( "Downloaded:" ), myTotalDown = new QLabel( ) );
    hig->addRow( tr( "Ratio:" ), myTotalRatio = new QLabel( ) );
    hig->addRow( tr( "Duration:" ), myTotalDuration = new QLabel( ) );
    hig->finish( );

    QLayout * layout = new QVBoxLayout( this );
    layout->addWidget( hig );
    QDialogButtonBox * buttons = new QDialogButtonBox( QDialogButtonBox::Close, Qt::Horizontal, this );
    connect( buttons, SIGNAL(rejected()), this, SLOT(hide()) ); // "close" triggers rejected
    layout->addWidget( buttons );

    connect( &mySession, SIGNAL(statsUpdated()), this, SLOT(updateStats()) );
    updateStats( );
    mySession.refreshSessionStats( );
}

StatsDialog :: ~StatsDialog( )
{
}

void
StatsDialog :: setVisible( bool visible )
{
    myTimer->stop( );
    if( visible )
        myTimer->start( REFRESH_INTERVAL_MSEC );
    QDialog::setVisible( visible );
}

void
StatsDialog :: onTimer( )
{
    mySession.refreshSessionStats( );
}

void
StatsDialog :: updateStats( )
{
    const struct tr_session_stats& current( mySession.getStats( ) );
    const struct tr_session_stats& total( mySession.getCumulativeStats( ) );

    myCurrentUp->setText( Utils :: sizeToString( current.uploadedBytes ) );
    myCurrentDown->setText( Utils :: sizeToString( current.downloadedBytes ) );
    myCurrentRatio->setText( Utils :: ratioToString( current.ratio ) );
    myCurrentDuration->setText( Utils :: timeToString( current.secondsActive ) );

    myTotalUp->setText( Utils :: sizeToString( total.uploadedBytes ) );
    myTotalDown->setText( Utils :: sizeToString( total.downloadedBytes ) );
    myTotalRatio->setText( Utils :: ratioToString( total.ratio ) );
    myTotalDuration->setText( Utils :: timeToString( total.secondsActive ) );

    myStartCount->setText( tr( "Started %n time(s)", 0, total.sessionCount ) );
}
