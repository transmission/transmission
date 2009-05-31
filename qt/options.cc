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

#include <cstdio>
#include <iostream>

#include <QEvent>
#include <QResizeEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QCheckBox>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QSet>
#include <QWidget>
#include <QVBoxLayout>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/utils.h> /* mime64 */

#include "file-tree.h"
#include "hig.h"
#include "options.h"
#include "prefs.h"
#include "qticonloader.h"
#include "session.h"
#include "torrent.h"

/***
****
***/

Options :: Options( Session& session, const Prefs& prefs, const QString& filename, QWidget * parent ):
    QDialog( parent, Qt::Dialog ),
    mySession( session ),
    myFile( filename ),
    myHaveInfo( false ),
    myDestinationButton( 0 ),
    myVerifyButton( 0 ),
    myVerifyFile( 0 ),
    myVerifyHash( QCryptographicHash::Sha1 )

{
    setWindowTitle( tr( "Add Torrent" ) );
    QFontMetrics fontMetrics( font( ) );
    QGridLayout * layout = new QGridLayout( this );
    int row = 0;

    const int iconSize( style( )->pixelMetric( QStyle :: PM_SmallIconSize ) );
    QIcon fileIcon = style( )->standardIcon( QStyle::SP_FileIcon );
    const QPixmap filePixmap = fileIcon.pixmap( iconSize );

    QPushButton * p;
    int width = fontMetrics.size( 0, "This is a pretty long torrent filename indeed.torrent" ).width( );
    QLabel * l = new QLabel( tr( "&Torrent file:" ) );
    layout->addWidget( l, row, 0, Qt::AlignLeft );
    p = myFileButton =  new QPushButton;
    p->setIcon( filePixmap );
    p->setMinimumWidth( width );
    p->setStyleSheet( "text-align: left; padding-left: 5; padding-right: 5" );
    p->installEventFilter( this );

    layout->addWidget( p, row, 1 );
    l->setBuddy( p );
    connect( p, SIGNAL(clicked(bool)), this, SLOT(onFilenameClicked()));
  
    if( session.isLocal( ) ) 
    {
        const QIcon folderIcon = QtIconLoader :: icon( "folder", style()->standardIcon( QStyle::SP_DirIcon ) );
        const QPixmap folderPixmap = folderIcon.pixmap( iconSize );

        l = new QLabel( tr( "&Destination folder:" ) );
        layout->addWidget( l, ++row, 0, Qt::AlignLeft );
        myDestination.setPath( prefs.getString( Prefs :: DOWNLOAD_DIR ) );
        p = myDestinationButton = new QPushButton;
        p->setIcon( folderPixmap );
        p->setStyleSheet( "text-align: left; padding-left: 5; padding-right: 5" );
        p->installEventFilter( this );
        layout->addWidget( p, row, 1 );
        l->setBuddy( p );
        connect( p, SIGNAL(clicked(bool)), this, SLOT(onDestinationClicked()));
    }
 
    myTree = new FileTreeView;
    layout->addWidget( myTree, ++row, 0, 1, 2 );
    if( !session.isLocal( ) ) 
        myTree->hideColumn( 1 ); // hide the % done, since we've no way of knowing

    if( session.isLocal( ) )
    {
        p = myVerifyButton = new QPushButton( tr( "&Verify Local Data" ) );
        layout->addWidget( p, ++row, 0, Qt::AlignLeft );
    }

    QCheckBox * c;
    c = myStartCheck = new QCheckBox( tr( "&Start when added" ) );
    c->setChecked( prefs.getBool( Prefs :: START ) );
    layout->addWidget( c, ++row, 0, 1, 2, Qt::AlignLeft );

    c = myTrashCheck = new QCheckBox( tr( "&Delete source file" ) );
    c->setChecked( prefs.getBool( Prefs :: TRASH_ORIGINAL ) );
    layout->addWidget( c, ++row, 0, 1, 2, Qt::AlignLeft );

    QDialogButtonBox * b = new QDialogButtonBox( QDialogButtonBox::Ok|QDialogButtonBox::Cancel, Qt::Horizontal, this );
    connect( b, SIGNAL(rejected()), this, SLOT(deleteLater()) );
    connect( b, SIGNAL(accepted()), this, SLOT(onAccepted()) );
    layout->addWidget( b, ++row, 0, 1, 2 );

    layout->setRowStretch( 2, 2 );
    layout->setColumnStretch( 1, 2 );
    layout->setSpacing( HIG :: PAD );

    connect( myTree, SIGNAL(priorityChanged(const QSet<int>&,int)), this, SLOT(onPriorityChanged(const QSet<int>&,int)));
    connect( myTree, SIGNAL(wantedChanged(const QSet<int>&,bool)), this, SLOT(onWantedChanged(const QSet<int>&,bool)));
    if( session.isLocal( ) )
        connect( myVerifyButton, SIGNAL(clicked(bool)), this, SLOT(onVerify()));

    connect( &myVerifyTimer, SIGNAL(timeout()), this, SLOT(onTimeout()));

    reload( );
}
    
Options :: ~Options( )
{
    clearInfo( );
}

/***
****
***/

void
Options :: refreshButton( QPushButton * p, const QString& text, int width )
{
    if( width <= 0 ) width = p->width( );
    width -= 15;
    QFontMetrics fontMetrics( font( ) );
    QString str = fontMetrics.elidedText( text, Qt::ElideRight, width );
    p->setText( str );
}

void
Options :: refreshFileButton( int width )
{
    refreshButton( myFileButton, QFileInfo(myFile).baseName(), width );
}

void
Options :: refreshDestinationButton( int width )
{
    if( myDestinationButton != 0 )
        refreshButton( myDestinationButton, myDestination.absolutePath(), width );
}


bool
Options :: eventFilter( QObject * o, QEvent * event )
{
    if( o==myFileButton && event->type() == QEvent::Resize )
    {
        refreshFileButton( dynamic_cast<QResizeEvent*>(event)->size().width() );
    }

    if( o==myDestinationButton && event->type() == QEvent::Resize )
    {
        refreshDestinationButton( dynamic_cast<QResizeEvent*>(event)->size().width() );
    }

    return false;
}

/***
****
***/

void
Options :: clearInfo( )
{
    if( myHaveInfo )
        tr_metainfoFree( &myInfo );
    myHaveInfo = false;
    myFiles.clear( );
}

void
Options :: reload( )
{
    clearInfo( );
    clearVerify( );

    tr_ctor * ctor = tr_ctorNew( 0 );
    tr_ctorSetMetainfoFromFile( ctor, myFile.toUtf8().constData() );
    const int err = tr_torrentParse( ctor, &myInfo );
    myHaveInfo = !err;
    tr_ctorFree( ctor );

    myTree->clear( );
    myFiles.clear( );
    myPriorities.clear( );
    myWanted.clear( );

    if( myHaveInfo )
    {
        myPriorities.insert( 0, myInfo.fileCount, TR_PRI_NORMAL );
        myWanted.insert( 0, myInfo.fileCount, true );

        for( tr_file_index_t i=0; i<myInfo.fileCount; ++i ) {
            TrFile file;
            file.index = i;
            file.priority = myPriorities[i];
            file.wanted = myWanted[i];
            file.size = myInfo.files[i].length;
            file.have = 0;
            file.filename = QString::fromUtf8( myInfo.files[i].name );
            myFiles.append( file );
        }
    }

    myTree->update( myFiles );
}

void
Options :: onPriorityChanged( const QSet<int>& fileIndices, int priority )
{
    foreach( int i, fileIndices )
        myPriorities[i] = priority;
}

void
Options :: onWantedChanged( const QSet<int>& fileIndices, bool isWanted )
{
    foreach( int i, fileIndices )
        myWanted[i] = isWanted;
}

void
Options :: onAccepted( )
{
    // rpc spec section 3.4 "adding a torrent"

    const int64_t tag = mySession.getUniqueTag( );
    tr_benc top;
    tr_bencInitDict( &top, 3 );
    tr_bencDictAddStr( &top, "method", "torrent-add" );
    tr_bencDictAddInt( &top, "tag", tag );
    tr_benc * args( tr_bencDictAddDict( &top, "arguments", 10 ) );

    // "download-dir"
    if( myDestinationButton )
        tr_bencDictAddStr( args, "download-dir", myDestination.absolutePath().toUtf8().constData() );

    // "metainfo"
    QFile file( myFile );
    file.open( QIODevice::ReadOnly );
    const QByteArray metainfo( file.readAll( ) );
    file.close( );
    int base64Size = 0;
    char * base64 = tr_base64_encode( metainfo.constData(), metainfo.size(), &base64Size );
    tr_bencDictAddRaw( args, "metainfo", base64, base64Size );
    tr_free( base64 );

    // paused
    tr_bencDictAddBool( args, "paused", !myStartCheck->isChecked( ) );

    // files-unwanted
    int count = myWanted.count( false );
    if( count > 0 ) {
        tr_benc * l = tr_bencDictAddList( args, "files-unwanted", count );
        for( int i=0, n=myWanted.size(); i<n; ++i )
            if( myWanted.at(i) == false )
                tr_bencListAddInt( l, i );
    }

    // priority-low
    count = myPriorities.count( TR_PRI_LOW );
    if( count > 0 ) {
        tr_benc * l = tr_bencDictAddList( args, "priority-low", count );
        for( int i=0, n=myPriorities.size(); i<n; ++i )
            if( myPriorities.at(i) == TR_PRI_LOW )
                tr_bencListAddInt( l, i );
    }

    // priority-high
    count = myPriorities.count( TR_PRI_HIGH );
    if( count > 0 ) {
        tr_benc * l = tr_bencDictAddList( args, "priority-high", count );
        for( int i=0, n=myPriorities.size(); i<n; ++i )
            if( myPriorities.at(i) == TR_PRI_HIGH )
                tr_bencListAddInt( l, i );
    }

    // maybe delete the source .torrent
    if( myTrashCheck->isChecked( ) ) {
        FileAdded * fileAdded = new FileAdded( tag, myFile );
        connect( &mySession, SIGNAL(executed(int64_t,const QString&, struct tr_benc*)),
                 fileAdded, SLOT(executed(int64_t,const QString&, struct tr_benc*)));
    }

    mySession.exec( &top );

    tr_bencFree( &top );
    deleteLater( );
}

void
Options :: onFilenameClicked( )
{
    QFileDialog * d = new QFileDialog( this,
                                       tr( "Add Torrent" ),
                                       QFileInfo(myFile).absolutePath(),
                                       tr( "Torrent Files (*.torrent);;All Files (*.*)" ) );
    d->setFileMode( QFileDialog::ExistingFile );
    connect( d, SIGNAL(filesSelected(const QStringList&)), this, SLOT(onFilesSelected(const QStringList&)) );
    d->show( );
}

void
Options :: onFilesSelected( const QStringList& files )
{
    if( files.size() == 1 )
    {
        myFile = files.at( 0 );
        refreshFileButton( );
        reload( );
    }
}

void
Options :: onDestinationClicked( )
{
    QFileDialog * d = new QFileDialog( this,
                                       tr( "Select Destination" ),
                                       myDestination.absolutePath( ) );
    d->setFileMode( QFileDialog::Directory );
    connect( d, SIGNAL(filesSelected(const QStringList&)), this, SLOT(onDestinationsSelected(const QStringList&)) );
    d->show( );
}

void
Options :: onDestinationsSelected( const QStringList& destinations )
{
    if( destinations.size() == 1 )
    {
        const QString& destination( destinations.first( ) );
        myDestination.setPath( destination );
        refreshDestinationButton( );
    }
}

/***
****
****  VERIFY
****
***/

void
Options :: clearVerify( )
{
    myVerifyHash.reset( );
    myVerifyFile.close( );
    myVerifyFilePos = 0;
    myVerifyFlags.clear( );
    myVerifyFileIndex = 0;
    myVerifyPieceIndex = 0;
    myVerifyPiecePos = 0;
    myVerifyTimer.stop( );

    for( int i=0, n=myFiles.size(); i<n; ++i )
        myFiles[i].have = 0;
    myTree->update( myFiles );
}

void
Options :: onVerify( )
{
    //std::cerr << "starting to verify..." << std::endl;
    clearVerify( );
    myVerifyFlags.insert( 0, myInfo.pieceCount, false );
    myVerifyTimer.setSingleShot( false );
    myVerifyTimer.start( 0 );
}

namespace
{
    uint64_t getPieceSize( const tr_info * info, tr_piece_index_t pieceIndex )
    {
        if( pieceIndex != info->pieceCount - 1 )
            return info->pieceSize;
        return info->totalSize % info->pieceSize;
    }
}

void
Options :: onTimeout( )
{
    const tr_file * file = &myInfo.files[myVerifyFileIndex];

    if( !myVerifyFilePos && !myVerifyFile.isOpen( ) )
    {
        const QFileInfo fileInfo( myDestination, QString::fromUtf8( file->name ) );
        myVerifyFile.setFileName( fileInfo.absoluteFilePath( ) );
        //std::cerr << "opening file" << qPrintable(fileInfo.absoluteFilePath()) << std::endl;
        myVerifyFile.open( QIODevice::ReadOnly );
    }

    int64_t leftInPiece = getPieceSize( &myInfo, myVerifyPieceIndex ) - myVerifyPiecePos;
    int64_t leftInFile = file->length - myVerifyFilePos;
    int64_t bytesThisPass = std::min( leftInFile, leftInPiece );
    bytesThisPass = std::min( bytesThisPass, (int64_t)sizeof( myVerifyBuf ) );

    if( myVerifyFile.isOpen() && myVerifyFile.seek( myVerifyFilePos ) ) {
        int64_t numRead = myVerifyFile.read( myVerifyBuf, bytesThisPass );
        if( numRead == bytesThisPass )
            myVerifyHash.addData( myVerifyBuf, numRead );
    }

    leftInPiece -= bytesThisPass;
    leftInFile -= bytesThisPass;
    myVerifyPiecePos += bytesThisPass;
    myVerifyFilePos += bytesThisPass;

    myVerifyBins[myVerifyFileIndex] += bytesThisPass;

    if( leftInPiece == 0 )
    {
        const QByteArray result( myVerifyHash.result( ) );
        const bool matches = !memcmp( result.constData(),
                                      myInfo.pieces[myVerifyPieceIndex].hash,
                                      SHA_DIGEST_LENGTH );
        myVerifyFlags[myVerifyPieceIndex] = matches;
        myVerifyPiecePos = 0;
        ++myVerifyPieceIndex;
        myVerifyHash.reset( );

        FileList changedFiles;
        if( matches ) {
            mybins_t::const_iterator i;
            for( i=myVerifyBins.begin(); i!=myVerifyBins.end(); ++i ) {
                TrFile& f( myFiles[i.key( )] );
                f.have += i.value( );
                changedFiles.append( f );
            }
        }
        myTree->update( changedFiles );
        myVerifyBins.clear( );
    }

    if( leftInFile == 0 )
    {
        //std::cerr << "closing file" << std::endl;
        myVerifyFile.close( );
        ++myVerifyFileIndex;
        myVerifyFilePos = 0;
    }

    const bool done = myVerifyPieceIndex >= myInfo.pieceCount;

    if( done )
        myVerifyTimer.stop( );
}
