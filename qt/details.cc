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

#include <cassert>
#include <ctime>
#include <iostream>

#include <QCheckBox>
#include <QEvent>
#include <QHeaderView>
#include <QResizeEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QRadioButton>
#include <QStyle>
#include <QTabWidget>
#include <QTreeView>
#include <QTextBrowser>
#include <QDateTime>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <libtransmission/transmission.h>

#include "details.h"
#include "file-tree.h"
#include "hig.h"
#include "session.h"
#include "squeezelabel.h"
#include "torrent.h"
#include "utils.h"

class Prefs;
class Session;

/****
*****
****/

namespace
{
    const int REFRESH_INTERVAL_MSEC = 4000;

    enum // peer columns
    {
        COL_LOCK,
        COL_UP,
        COL_DOWN,
        COL_PERCENT,
        COL_STATUS,
        COL_ADDRESS,
        COL_CLIENT,
        N_COLUMNS
    };
}

/***
****
***/

class PeerItem: public QTreeWidgetItem
{
        Peer peer;
        QString collatedAddress;
        QString status;

    public:
        PeerItem( ) { }
        virtual ~PeerItem( ) { }

    public:
        void setStatus( const QString& s ) {
            status = s;
        }
        void setPeer( const Peer& p ) {
            peer = p;
            int quads[4];
            if( sscanf( p.address.toUtf8().constData(), "%d.%d.%d.%d", quads+0, quads+1, quads+2, quads+3 ) == 4 )
                collatedAddress.sprintf( "%03d.%03d.%03d.%03d", quads[0], quads[1], quads[2], quads[3] );
            else
                collatedAddress = p.address;
        }
        virtual bool operator< ( const QTreeWidgetItem & other ) const {
            const PeerItem * that = dynamic_cast<const PeerItem*>(&other);
            QTreeWidget * tw( treeWidget( ) );
            const int column = tw ? tw->sortColumn() : 0;
            switch( column ) {
                case COL_UP: return peer.rateToPeer < that->peer.rateToPeer;
                case COL_DOWN: return peer.rateToClient < that->peer.rateToClient;
                case COL_PERCENT: return peer.progress < that->peer.progress;
                case COL_STATUS: return status < that->status;
                case COL_ADDRESS: return collatedAddress < that->collatedAddress;
                case COL_CLIENT: return peer.clientName < that->peer.clientName;
                default: /*COL_LOCK*/ return peer.isEncrypted && !that->peer.isEncrypted;
            }
        }
};

/***
****
***/

Details :: Details( Session& session, Torrent& torrent, QWidget * parent ):
    QDialog( parent, Qt::Dialog ),
    mySession( session ),
    myTorrent( torrent )
{
    QVBoxLayout * layout = new QVBoxLayout( this );

    setWindowTitle( tr( "%1 Properties" ).arg( torrent.name( ) ) );

    QTabWidget * t = new QTabWidget( this );
    t->addTab( createActivityTab( ),  tr( "Activity" ) );
    t->addTab( createPeersTab( ),     tr( "Peers" ) );
    t->addTab( createTrackerTab( ),   tr( "Tracker" ) );
    t->addTab( createInfoTab( ),      tr( "Information" ) );
    t->addTab( createFilesTab( ),     tr( "Files" ) );
    t->addTab( createOptionsTab( ),   tr( "Options" ) );
    layout->addWidget( t );

    QDialogButtonBox * buttons = new QDialogButtonBox( QDialogButtonBox::Close, Qt::Horizontal, this );
    connect( buttons, SIGNAL(rejected()), this, SLOT(deleteLater()) ); // "close" triggers rejected
    layout->addWidget( buttons );

    connect( &myTorrent, SIGNAL(torrentChanged(int)), this, SLOT(onTorrentChanged()) );
    connect( &myTorrent, SIGNAL(destroyed(QObject*)), this, SLOT(deleteLater()) );
    connect( &myTimer, SIGNAL(timeout()), this, SLOT(onTimer()) );

    onTimer( );
    myTimer.setSingleShot( false );
    myTimer.start( REFRESH_INTERVAL_MSEC );
}
    
Details :: ~Details( )
{
}

/***
****
***/

void
Details :: onTimer( )
{
    mySession.refreshExtraStats( myTorrent.id( ) );
}

void
Details :: onTorrentChanged( )
{
    QLocale locale;
    const QFontMetrics fm( fontMetrics( ) );

    // activity tab 
    myStateLabel->setText( myTorrent.activityString( ) );
    myProgressLabel->setText( locale.toString( myTorrent.percentDone( )*100.0, 'f', 2 ) );
    myHaveLabel->setText( tr( "%1 (%2 verified in %L3 pieces)" )
                            .arg( Utils::sizeToString( myTorrent.haveTotal( ) ) )
                            .arg( Utils::sizeToString( myTorrent.haveVerified( ) ) )
                            .arg( myTorrent.haveVerified()/myTorrent.pieceSize() ) );
    myDownloadedLabel->setText( Utils::sizeToString( myTorrent.downloadedEver( ) ) );
    myUploadedLabel->setText( Utils::sizeToString( myTorrent.uploadedEver( ) ) );
    myFailedLabel->setText( Utils::sizeToString( myTorrent.failedEver( ) ) );
    myRatioLabel->setText( Utils :: ratioToString( myTorrent.ratio( ) ) );
    mySwarmSpeedLabel->setText( Utils::speedToString( myTorrent.swarmSpeed( ) ) );
    myAddedDateLabel->setText( myTorrent.dateAdded().toString() );

    QDateTime dt = myTorrent.lastActivity( );
    myActivityLabel->setText( dt.isNull() ? tr("Never") : dt.toString() );
    QString s = myTorrent.getError( );
    myErrorLabel->setText( s.isEmpty() ? tr("None") : s );

    // information tab
    myPiecesLabel->setText( tr( "%L1 Pieces @ %2" ).arg( myTorrent.pieceCount() )
                                                   .arg( Utils::sizeToString(myTorrent.pieceSize()) ) );
    myHashLabel->setText( myTorrent.hashString( ) );

    myPrivacyLabel->setText( myTorrent.isPrivate( ) ? tr( "Private to this tracker -- PEX disabled" )
                                                    : tr( "Public torrent" ) );
    myCommentBrowser->setText( myTorrent.comment( ) );
    QString str = myTorrent.creator( );
    if( str.isEmpty( ) )
        str = tr( "Unknown" );
    myCreatorLabel->setText( str );
    myDateCreatedLabel->setText( myTorrent.dateCreated( ).toString( ) );
    myDestinationLabel->setText( myTorrent.getPath( ) );
    myTorrentFileLabel->setText( myTorrent.torrentFile( ) );

    // options tab
    mySessionLimitCheck->setChecked( myTorrent.honorsSessionLimits( ) );
    mySingleDownCheck->setChecked( myTorrent.downloadIsLimited( ) );
    mySingleUpCheck->setChecked( myTorrent.uploadIsLimited( ) );
    mySingleDownSpin->setValue( (int)myTorrent.downloadLimit().kbps() );
    mySingleUpSpin->setValue( (int)myTorrent.uploadLimit().kbps() );
    myPeerLimitSpin->setValue( myTorrent.peerLimit( ) );

    QRadioButton * rb;
    switch( myTorrent.seedRatioMode( ) ) {
        case TR_RATIOLIMIT_GLOBAL:    rb = mySeedGlobalRadio; break;
        case TR_RATIOLIMIT_SINGLE:    rb = mySeedCustomRadio; break;
        case TR_RATIOLIMIT_UNLIMITED: rb = mySeedForeverRadio; break;
    }
    rb->setChecked( true );
    mySeedCustomSpin->setValue( myTorrent.seedRatioLimit( ) );


    // tracker tab
    const time_t now( time( 0 ) );
    myScrapeTimePrevLabel->setText( myTorrent.lastScrapeTime().toString() );
    myScrapeResponseLabel->setText( myTorrent.scrapeResponse() );
    myScrapeTimeNextLabel->setText( Utils :: timeToString( myTorrent.nextScrapeTime().toTime_t() - now ) );
    myAnnounceTimePrevLabel->setText( myTorrent.lastScrapeTime().toString() );
    myAnnounceTimeNextLabel->setText( Utils :: timeToString( myTorrent.nextAnnounceTime().toTime_t() - now ) );
    myAnnounceManualLabel->setText( Utils :: timeToString( myTorrent.manualAnnounceTime().toTime_t() - now ) );
    myAnnounceResponseLabel->setText( myTorrent.announceResponse( ) );
    const QUrl url( myTorrent.announceUrl( ) );
    myTrackerLabel->setText( url.host( ) );

    // peers tab
    mySeedersLabel->setText( locale.toString( myTorrent.seeders( ) ) );
    myLeechersLabel->setText( locale.toString( myTorrent.leechers( ) ) );
    myTimesCompletedLabel->setText( locale.toString( myTorrent.timesCompleted( ) ) );
    const PeerList peers( myTorrent.peers( ) );
    QMap<QString,QTreeWidgetItem*> peers2;
    QList<QTreeWidgetItem*> newItems;
    static const QIcon myEncryptionIcon( ":/icons/encrypted.png" );
    static const QIcon myEmptyIcon;
    foreach( const Peer& peer, peers )
    {
        PeerItem * item = (PeerItem*) myPeers.value( peer.address, 0 );
        if( item == 0 ) { // new peer has connected
            item = new PeerItem;
            item->setTextAlignment( COL_UP, Qt::AlignRight );
            item->setTextAlignment( COL_DOWN, Qt::AlignRight );
            item->setTextAlignment( COL_PERCENT, Qt::AlignRight );
            newItems << item;
        }

        QString code;
        if( peer.isDownloadingFrom )                           { code += 'D'; }
        else if( peer.clientIsInterested )                     { code += 'd'; }
        if( peer.isUploadingTo )                               { code += 'U'; }
        else if( peer.peerIsInterested )                       { code += 'u'; }
        if( !peer.clientIsChoked && !peer.clientIsInterested ) { code += 'K'; }
        if( !peer.peerIsChoked && !peer.peerIsInterested )     { code += '?'; }
        if( peer.isEncrypted )                                 { code += 'E'; }
        if( peer.isIncoming )                                  { code += 'I'; }

        item->setPeer( peer );
        item->setStatus( code );

        QString codeTip;
        foreach( QChar ch, code ) {
            QString txt;
            switch( ch.toAscii() ) {
                case 'O': txt = tr( "Optimistic unchoke" ); break;
                case 'D': txt = tr( "Downloading from this peer" ); break;
                case 'd': txt = tr( "We would download from this peer if they would let us" ); break;
                case 'U': txt = tr( "Uploading to peer" ); break;
                case 'u': txt = tr( "We would upload to this peer if they asked" ); break;
                case 'K': txt = tr( "Peer has unchoked us, but we're not interested" ); break;
                case '?': txt = tr( "We unchoked this peer, but they're not interested" ); break;
                case 'E': txt = tr( "Encrypted connection" ); break;
                case 'X': txt = tr( "Peer was discovered through Peer Exchange (PEX)" ); break;
                case 'I': txt = tr( "Peer is an incoming connection" ); break;
            }
            if( !txt.isEmpty( ) )
                codeTip += QString("%1: %2\n").arg(ch).arg(txt);
        }
        if( !codeTip.isEmpty() )
            codeTip.resize( codeTip.size()-1 ); // eat the trailing linefeed

        item->setIcon( COL_LOCK, peer.isEncrypted ? myEncryptionIcon : myEmptyIcon );
        item->setToolTip( COL_LOCK, peer.isEncrypted ? tr( "Encrypted connection" ) : "" );
        item->setText( COL_UP, peer.rateToPeer.isZero() ? "" : Utils::speedToString( peer.rateToPeer ) );
        item->setText( COL_DOWN, peer.rateToClient.isZero() ? "" : Utils::speedToString( peer.rateToClient ) );
        item->setText( COL_PERCENT, peer.progress > 0 ? QString( "%1%" ).arg( locale.toString((int)(peer.progress*100.0))) : "" );
        item->setText( COL_STATUS, code );
        item->setToolTip( COL_STATUS, codeTip );
        item->setText( COL_ADDRESS, peer.address );
        item->setText( COL_CLIENT, peer.clientName );
        peers2.insert( peer.address, item );
    }
    myPeerTree->addTopLevelItems( newItems );
    foreach( QString key, myPeers.keys() ) {
        if( !peers2.contains( key ) ) { // old peer has disconnected
            QTreeWidgetItem * item = myPeers.value( key, 0 );
            myPeerTree->takeTopLevelItem( myPeerTree->indexOfTopLevelItem( item ) );
            delete item;
        }
    }
    myPeers = peers2;

    myFileTreeView->update( myTorrent.files( ) );
}

void
Details :: enableWhenChecked( QCheckBox * box, QWidget * w )
{
    connect( box, SIGNAL(toggled(bool)), w, SLOT(setEnabled(bool)) );
    w->setEnabled( box->isChecked( ) );
}


/***
****
***/

QWidget *
Details :: createActivityTab( )
{
    HIG * hig = new HIG( this );

    hig->addSectionTitle( tr( "Transfer" ) );
    hig->addRow( tr( "State:" ), myStateLabel = new SqueezeLabel );
    hig->addRow( tr( "Progress:" ), myProgressLabel = new SqueezeLabel );
    hig->addRow( tr( "Have:" ), myHaveLabel = new SqueezeLabel );
    hig->addRow( tr( "Downloaded:" ), myDownloadedLabel = new SqueezeLabel );
    hig->addRow( tr( "Uploaded:" ), myUploadedLabel = new SqueezeLabel );
    hig->addRow( tr( "Failed DL:" ), myFailedLabel = new SqueezeLabel );
    hig->addRow( tr( "Ratio:" ), myRatioLabel = new SqueezeLabel );
    hig->addRow( tr( "Swarm Rate:" ), mySwarmSpeedLabel = new SqueezeLabel );
    hig->addRow( tr( "Error:" ), myErrorLabel = new SqueezeLabel );
    hig->addSectionDivider( );

    hig->addSectionTitle( tr( "Dates" ) );
    hig->addRow( tr( "Added on:" ), myAddedDateLabel = new SqueezeLabel );
    hig->addRow( tr( "Last activity on:" ), myActivityLabel = new SqueezeLabel );
    hig->finish( );

    return hig;
}

/***
****
***/

void
Details :: onHonorsSessionLimitsToggled( bool val )
{
    mySession.torrentSet( myTorrent.id(), "honorsSessionLimits", val );
}
void
Details :: onDownloadLimitedToggled( bool val )
{
    mySession.torrentSet( myTorrent.id(), "downloadLimited", val );
}
void
Details :: onDownloadLimitChanged( int val )
{
    mySession.torrentSet( myTorrent.id(), "downloadLimit", val );
}
void
Details :: onUploadLimitedToggled( bool val )
{
    mySession.torrentSet( myTorrent.id(), "uploadLimited", val );
}
void
Details :: onUploadLimitChanged( int val )
{
    mySession.torrentSet( myTorrent.id(), "uploadLimit", val );
}

#define RATIO_KEY "seedRatioMode"

void
Details :: onSeedUntilChanged( bool b )
{
    if( b )
        mySession.torrentSet( myTorrent.id(), RATIO_KEY, sender()->property(RATIO_KEY).toInt() );
}

void
Details :: onSeedRatioLimitChanged( double val )
{
    mySession.torrentSet( myTorrent.id(), "seedRatioLimit", val );
}

void
Details :: onMaxPeersChanged( int val )
{
    mySession.torrentSet( myTorrent.id(), "peer-limit", val );
}

QWidget *
Details :: createOptionsTab( )
{
    //QWidget * l;
    QSpinBox * s;
    QCheckBox * c;
    QHBoxLayout * h;
    QRadioButton * r;
    QDoubleSpinBox * ds;

    HIG * hig = new HIG( this );
    hig->addSectionTitle( tr( "Speed Limits" ) );

    c = new QCheckBox( tr( "Honor global &limits" ) );
    mySessionLimitCheck = c;
    hig->addWideControl( c );
    connect( c, SIGNAL(toggled(bool)), this, SLOT(onHonorsSessionLimitsToggled(bool)) );

    c = new QCheckBox( tr( "Limit &download speed (KB/s)" ) );
    mySingleDownCheck = c;
    s = new QSpinBox( );
    mySingleDownSpin = s;
    s->setRange( 0, INT_MAX );
    hig->addRow( c, s );
    enableWhenChecked( c, s );
    connect( c, SIGNAL(toggled(bool)), this, SLOT(onDownloadLimitedToggled(bool)) );
    connect( s, SIGNAL(valueChanged(int)), this, SLOT(onDownloadLimitChanged(int)));

    c = new QCheckBox( tr( "Limit &upload speed (KB/s)" ) );
    mySingleUpCheck = c;
    s = new QSpinBox( );
    mySingleUpSpin = s;
    s->setRange( 0, INT_MAX );
    hig->addRow( c, s );
    enableWhenChecked( c, s );
    connect( c, SIGNAL(toggled(bool)), this, SLOT(onUploadLimitedToggled(bool)) );
    connect( s, SIGNAL(valueChanged(int)), this, SLOT(onUploadLimitChanged(int)));

    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Seed-Until Ratio" ) );

    r = new QRadioButton( tr( "Use &global setting" ) );
    r->setProperty( RATIO_KEY, TR_RATIOLIMIT_GLOBAL );
    connect( r, SIGNAL(toggled(bool)), this, SLOT(onSeedUntilChanged(bool)));
    mySeedGlobalRadio = r;
    hig->addWideControl( r );

    r = new QRadioButton( tr( "Seed &regardless of ratio" ) );
    r->setProperty( RATIO_KEY, TR_RATIOLIMIT_UNLIMITED );
    connect( r, SIGNAL(toggled(bool)), this, SLOT(onSeedUntilChanged(bool)));
    mySeedForeverRadio = r;
    hig->addWideControl( r );

    h = new QHBoxLayout( );
    h->setSpacing( HIG :: PAD );
    r = new QRadioButton( tr( "&Stop seeding when a torrent's ratio reaches" ) );
    r->setProperty( RATIO_KEY, TR_RATIOLIMIT_SINGLE );
    connect( r, SIGNAL(toggled(bool)), this, SLOT(onSeedUntilChanged(bool)));
    mySeedCustomRadio = r;
    h->addWidget( r );
    ds = new QDoubleSpinBox( );
    ds->setRange( 0.5, INT_MAX );
    connect( ds, SIGNAL(valueChanged(double)), this, SLOT(onSeedRatioLimitChanged(double)));
    mySeedCustomSpin = ds;
    h->addWidget( ds );
    hig->addWideControl( h );

    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Peer Connections" ) );

    s = new QSpinBox( );
    s->setRange( 1, 300 );
    connect( s, SIGNAL(valueChanged(int)), this, SLOT(onMaxPeersChanged(int)));
    myPeerLimitSpin = s;
    hig->addRow( tr( "&Maximum Peers" ), s );

    hig->finish( );

    return hig;
}

/***
****
***/

QWidget *
Details :: createInfoTab( )
{
    HIG * hig = new HIG( );
    hig->addSectionTitle( tr( "Details" ) );
    hig->addRow( tr( "Pieces:" ), myPiecesLabel = new SqueezeLabel );
    hig->addRow( tr( "Hash:" ), myHashLabel = new SqueezeLabel );
    hig->addRow( tr( "Privacy:" ), myPrivacyLabel = new SqueezeLabel );
    hig->addRow( tr( "Comment:" ), myCommentBrowser = new QTextBrowser );
    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Origins" ) );
    hig->addRow( tr( "Creator:" ), myCreatorLabel = new SqueezeLabel );
    hig->addRow( tr( "Date:" ), myDateCreatedLabel = new SqueezeLabel );
    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Origins" ) );
    hig->addRow( tr( "Destination folder:" ), myDestinationLabel = new SqueezeLabel );
    hig->addRow( tr( "Torrent file:" ), myTorrentFileLabel = new SqueezeLabel );
    const int h = QFontMetrics(myCommentBrowser->font()).lineSpacing() * 4;
    myTorrentFileLabel->setMinimumWidth( 300 );
    myTorrentFileLabel->setSizePolicy ( QSizePolicy::Expanding, QSizePolicy::Preferred );

    myCommentBrowser->setMinimumHeight( h );
    myCommentBrowser->setMaximumHeight( h );

    hig->finish( );
    return hig;
}

/***
****
***/

QWidget *
Details :: createTrackerTab( )
{
    HIG * hig = new HIG( );

    hig->addSectionTitle( tr( "Scrape" ) );
    hig->addRow( tr( "Last scrape at:" ), myScrapeTimePrevLabel = new SqueezeLabel );
    hig->addRow( tr( "Tracker responded:" ), myScrapeResponseLabel = new SqueezeLabel );
    hig->addRow( tr( "Next scrape in:" ), myScrapeTimeNextLabel = new SqueezeLabel );
    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Announce" ) );
    hig->addRow( tr( "Tracker:" ), myTrackerLabel = new SqueezeLabel );
    hig->addRow( tr( "Last announce at:" ), myAnnounceTimePrevLabel = new SqueezeLabel );
    hig->addRow( tr( "Tracker responded:" ), myAnnounceResponseLabel = new SqueezeLabel );
    hig->addRow( tr( "Next announce in:" ), myAnnounceTimeNextLabel = new SqueezeLabel );
    hig->addRow( tr( "Manual announce allowed in:" ), myAnnounceManualLabel = new SqueezeLabel );
    hig->finish( );

    myTrackerLabel->setScaledContents( true );

    return hig;
}

/***
****
***/

QWidget *
Details :: createPeersTab( )
{
    QWidget * top = new QWidget;
    QVBoxLayout * v = new QVBoxLayout( top );
    v->setSpacing( HIG :: PAD_BIG );
    v->setContentsMargins( HIG::PAD_BIG, HIG::PAD_BIG, HIG::PAD_BIG, HIG::PAD_BIG );

    QStringList headers;
    headers << QString() << tr("Up") << tr("Down") << tr("%") << tr("Status") << tr("Address") << tr("Client");
    myPeerTree = new QTreeWidget;
    myPeerTree->setUniformRowHeights( true );
    myPeerTree->setHeaderLabels( headers );
    myPeerTree->setColumnWidth( 0, 20 );
    myPeerTree->setSortingEnabled( true );
    myPeerTree->setRootIsDecorated( false );
    myPeerTree->setTextElideMode( Qt::ElideRight );
    v->addWidget( myPeerTree, 1 );

    const QFontMetrics m( font( ) );
    QSize size = m.size( 0, "1024 MB/s" );
    myPeerTree->setColumnWidth( COL_UP, size.width( ) );
    myPeerTree->setColumnWidth( COL_DOWN, size.width( ) );
    size = m.size( 0, " 100% " );
    myPeerTree->setColumnWidth( COL_PERCENT, size.width( ) );
    size = m.size( 0, "ODUK?EXI" );
    myPeerTree->setColumnWidth( COL_STATUS, size.width( ) );
    size = m.size( 0, "888.888.888.888" );
    myPeerTree->setColumnWidth( COL_ADDRESS, size.width( ) );
    size = m.size( 0, "Some BitTorrent Client" );
    myPeerTree->setColumnWidth( COL_CLIENT, size.width( ) );
    //myPeerTree->sortItems( myTorrent.isDone() ? COL_UP : COL_DOWN, Qt::DescendingOrder );
    myPeerTree->setAlternatingRowColors( true );

    QHBoxLayout * h = new QHBoxLayout;
    h->setSpacing( HIG :: PAD );
    v->addLayout( h );

    QLabel * l = new QLabel( "Seeders:" );
    l->setStyleSheet( "font: bold" );
    h->addWidget( l );
    l = mySeedersLabel = new QLabel( "a" );
    h->addWidget( l );
    h->addStretch( 1 );
    
    l = new QLabel( "Leechers:" );
    l->setStyleSheet( "font: bold" );
    h->addWidget( l );
    l = myLeechersLabel = new QLabel( "b" );
    h->addWidget( l );
    h->addStretch( 1 );
    
    l = new QLabel( "Times Completed:" );
    l->setStyleSheet( "font: bold" );
    h->addWidget( l );
    l = myTimesCompletedLabel = new QLabel( "c" );
    h->addWidget( l );

    return top;
}

/***
****
***/

QWidget *
Details :: createFilesTab( )
{
    myFileTreeView = new FileTreeView( );

    connect( myFileTreeView, SIGNAL(      priorityChanged(const QSet<int>&, int)),
             this,           SLOT(  onFilePriorityChanged(const QSet<int>&, int)));

    connect( myFileTreeView, SIGNAL(      wantedChanged(const QSet<int>&, bool)),
             this,           SLOT(  onFileWantedChanged(const QSet<int>&, bool)));

    return myFileTreeView;
}

void
Details :: onFilePriorityChanged( const QSet<int>& indices, int priority )
{
    QString key;
    switch( priority ) {
        case TR_PRI_LOW:   key = "priority-low"; break;
        case TR_PRI_HIGH:  key = "priority-high"; break;
        default:           key = "priority-normal"; break;
    }
    mySession.torrentSet( myTorrent.id( ), key, indices.toList( ) );
}

void
Details :: onFileWantedChanged( const QSet<int>& indices, bool wanted )
{
    QString key( wanted ? "files-wanted" : "files-unwanted" );
    mySession.torrentSet( myTorrent.id( ), key, indices.toList( ) );
}
