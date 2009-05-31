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

#include <cassert>
#include <iostream>

#include <QDialogButtonBox>
#include <QTimer>
#include <QFileDialog>
#include <QLabel>
#include <QStyle>
#include <QPushButton>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QList>
#include <QProgressBar>

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/utils.h>

#include "hig.h"
#include "make-dialog.h"
#include "squeezelabel.h"
#include "qticonloader.h"
#include "utils.h"

MakeDialog :: MakeDialog( Session & mySession, QWidget * parent ):
    QDialog( parent, Qt::Dialog ),
    myBuilder( 0 ),
    myIsBuilding( 0 )
{
    Q_UNUSED( mySession );
    connect( &myTimer, SIGNAL(timeout()), this, SLOT(onProgress()) );

    setWindowTitle( tr( "New Torrent" ) );
    QVBoxLayout * top = new QVBoxLayout( this );
    top->setSpacing( HIG :: PAD );

    HIG * hig = new HIG;
    hig->setContentsMargins( 0, 0, 0, 0 );
    hig->addSectionTitle( tr( "Source" ) );
    hig->addWideControl( mySourceEdit = new QLineEdit );
    connect( mySourceEdit, SIGNAL(textChanged(const QString&)), this, SLOT(refresh()));
    connect( mySourceEdit, SIGNAL(editingFinished()), this, SLOT(onSourceChanged()));
    QHBoxLayout * h = new QHBoxLayout;
    h->setContentsMargins( 0, 0, 0, 0 );
    h->addWidget( mySourceLabel = new QLabel( tr( "<i>No source selected</i>" ) ) );
    mySourceLabel->setMinimumWidth( fontMetrics().size( 0, "420 KB in 412 Files; 402 Pieces @ 42 KB each" ).width( ) );
    h->addStretch( 1 );
    h->setSpacing( HIG :: PAD );
    QPushButton * b = new QPushButton( style()->standardIcon( QStyle::SP_DirIcon ), tr( "F&older" ) );
    connect( b, SIGNAL(clicked(bool)), this, SLOT(onFolderButtonPressed()));
    h->addWidget( b );
    b = new QPushButton( style()->standardIcon( QStyle::SP_FileIcon ), tr( "&File" ) );
    connect( b, SIGNAL(clicked(bool)), this, SLOT(onFileButtonPressed()));
    h->addWidget( b );
    hig->addWideControl( h );
    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Trackers" ) );
    hig->addWideControl( myTrackerEdit = new QPlainTextEdit );
    connect( myTrackerEdit, SIGNAL(textChanged()), this, SLOT(refresh()) );
    const int height = fontMetrics().size( 0, "\n\n\n\n" ).height( );
    myTrackerEdit->setMinimumHeight( height );
    myTrackerEdit->setMaximumHeight( height );
    hig->addWideControl( new QLabel( tr( "Separate tiers with an empty line" ) ) );
    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Options" ) );
    hig->addRow( tr( "Commen&t:" ), myCommentEdit = new QLineEdit );
    hig->addWideControl( myPrivateCheck = new QCheckBox( "&Private torrent" ) );
    hig->addSectionDivider( );
    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Progress" ) );
    QProgressBar * p = myProgressBar = new QProgressBar;
    p->setTextVisible( false );
    p->setMinimum( 0 );
    p->setMaximum( 100 );
    p->setValue( 0 );
    hig->addWideControl( p );
    hig->addWideControl( myProgressLabel = new QLabel( "<i>No source selected</i>" ) );
    hig->finish( );
    top->addWidget( hig, 1 );

    //QFrame * f = new QFrame;
    //f->setFrameShape( QFrame :: HLine );
    //top->addWidget( f );


    QIcon icon;
    QDialogButtonBox * buttons = new QDialogButtonBox( this );

    icon = style()->standardIcon( QStyle::SP_FileDialogNewFolder );
    icon = QtIconLoader :: icon( "document-new", icon );
    b = myMakeButton = new QPushButton( icon, tr( "&New Torrent" ) );
    buttons->addButton( b, QDialogButtonBox::ActionRole );

    icon = style()->standardIcon( QStyle::SP_DialogCancelButton );
    icon = QtIconLoader :: icon( "process-stop", icon );
    b = myStopButton = new QPushButton( icon, tr( "&Stop" ) );
    buttons->addButton( b, QDialogButtonBox::RejectRole );

    icon = style()->standardIcon( QStyle::SP_DialogCloseButton );
    icon = QtIconLoader :: icon( "window-close", icon );
    b = myCloseButton = new QPushButton( icon, tr( "&Close" ) );
    buttons->addButton( b, QDialogButtonBox::AcceptRole );

    connect( buttons, SIGNAL(clicked(QAbstractButton*)),
             this, SLOT(onButtonBoxClicked(QAbstractButton*)) );

    top->addWidget( buttons );
    refresh( );
}

MakeDialog :: ~MakeDialog( )
{
    if( myBuilder )
        tr_metaInfoBuilderFree( myBuilder );
}

/***
****
***/

QString
MakeDialog :: getResult( ) const
{
    QString str;

    switch( myBuilder->result )
    {
        case TR_MAKEMETA_OK:
            str = tr( "%1.torrent created!" ).arg( myBuilder->top );
            break;

        case TR_MAKEMETA_URL:
            str = tr( "Error: Invalid URL" );
            break;

        case TR_MAKEMETA_CANCELLED:
            str = tr( "Torrent creation cancelled" );
            break;

        case TR_MAKEMETA_IO_READ:
            str = tr( "Error: Couldn't read \"%1\": %2" )
                     .arg( myBuilder->errfile )
                     .arg( tr_strerror( myBuilder->my_errno ) );
            break;

        case TR_MAKEMETA_IO_WRITE:
            str = tr( "Error: Couldn't create \"%1\": %2" )
                     .arg( myBuilder->errfile )
                     .arg( tr_strerror( myBuilder->my_errno ) );
            break;
    }

    return str;
}

void
MakeDialog :: refresh( )
{
    QString progressText;
    bool canBuild = true;

    if( myIsBuilding ) {
        progressText = tr( "Creating %1.torrent" ).arg( myBuilder->top );
        canBuild = false;
    } else if( mySourceEdit->text().trimmed().isEmpty() ) {
        progressText = tr( "<i>No source selected<i>" );
        canBuild = false;
    } else if( myTrackerEdit->toPlainText().isEmpty() ) {
        progressText = tr( "<i>No tracker announce URLs listed</i>" );
        canBuild = false;
    } else if( myBuilder && myBuilder->isDone ) {
        progressText = getResult( );
        canBuild = true;
    }

    myProgressLabel->setText( progressText );
    myMakeButton->setEnabled( canBuild && myBuilder );
    myCloseButton->setEnabled( !myIsBuilding );
    myStopButton->setEnabled( myIsBuilding );
}

void
MakeDialog :: setIsBuilding( bool isBuilding )
{
    myIsBuilding = isBuilding;

    if( myBuilder )
        myBuilder->result = TR_MAKEMETA_OK;

    if( isBuilding )
        myProgressBar->setValue( 0 );

    if( isBuilding )
        myTimer.start( 100 );
    else
        myTimer.stop( );

    refresh( );
}

void
MakeDialog :: onProgress( )
{
    const double denom = myBuilder->pieceCount ? myBuilder->pieceCount : 1;
    myProgressBar->setValue( (int) ((100.0 * myBuilder->pieceIndex) / denom ) );

    refresh( );

    if( myBuilder->isDone )
        setIsBuilding( false );

    //tr_metainfo_builder_err    result;
}

void
MakeDialog :: makeTorrent( )
{
    if( !myBuilder )
        return;

    int tier = 0;
    QList<tr_tracker_info> trackers;
    foreach( QString line, myTrackerEdit->toPlainText().split("\n") ) {
        line = line.trimmed( );
        if( line.isEmpty( ) )
            ++tier;
        else {
            tr_tracker_info tmp;
            tmp.announce = tr_strdup( line.toUtf8().constData( ) );
            tmp.tier = tier;
            std::cerr << "tier [" << tmp.tier << "] announce [" << tmp.announce << ']' << std::endl;
            trackers.append( tmp );
        }
    }

    tr_makeMetaInfo( myBuilder,
                     NULL,
                     &trackers.front(),
                     trackers.size(),
                     myCommentEdit->text().toUtf8().constData(),
                     myPrivateCheck->isChecked() );
    
    refresh( );
    setIsBuilding( true );
}

void
MakeDialog :: onButtonBoxClicked( QAbstractButton * button )
{
    if( button == myMakeButton )
        makeTorrent( );

    if( button == myStopButton )
        myBuilder->abortFlag = true;

    if( button == myCloseButton )
        deleteLater( );
}

/***
****
***/

void
MakeDialog :: onSourceChanged( )
{
    if( myBuilder ) {
        tr_metaInfoBuilderFree( myBuilder );
        myBuilder = 0;
    }

    const QString filename = mySourceEdit->text( );
    if( !filename.isEmpty( ) )
        myBuilder = tr_metaInfoBuilderCreate( filename.toUtf8().constData() );

    QString text;
    if( !myBuilder )
        text = tr( "<i>No source selected<i>" );
    else {
        QString files = tr( "%Ln File(s)", 0, myBuilder->fileCount );
        QString pieces = tr( "%Ln Piece(s)", 0, myBuilder->pieceCount );
        text = tr( "%1 in %2; %3 @ %4" )
                 .arg( Utils::sizeToString( myBuilder->totalSize ) )
                 .arg( files )
                 .arg( pieces )
                 .arg( Utils::sizeToString( myBuilder->pieceSize ) );
    }
    mySourceLabel->setText( text );

    refresh( );
}

void
MakeDialog :: onFileSelectedInDialog( const QString& path )
{
    mySourceEdit->setText( path );
    onSourceChanged( );
}

void
MakeDialog :: onFolderButtonPressed( )
{
    QFileDialog * f = new QFileDialog( this );
    f->setFileMode( QFileDialog :: Directory );
    connect( f, SIGNAL(fileSelected(const QString&)), this, SLOT(onFileSelectedInDialog(const QString&)));
    f->show( );
}

void
MakeDialog :: onFileButtonPressed( )
{
    QFileDialog * f = new QFileDialog( this );
    f->setFileMode( QFileDialog :: ExistingFile );
    connect( f, SIGNAL(fileSelected(const QString&)), this, SLOT(onFileSelectedInDialog(const QString&)));
    f->show( );
}
