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
#include <QComboBox>
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
#include "torrent-model.h"
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

Details :: Details( Session& session, TorrentModel& model, QWidget * parent ):
    QDialog( parent, Qt::Dialog ),
    mySession( session ),
    myModel( model ),
    myHavePendingRefresh( false )
{
    QVBoxLayout * layout = new QVBoxLayout( this );

    setWindowTitle( tr( "Torrent Properties" ) );

    QTabWidget * t = new QTabWidget( this );
    QWidget * w;
    t->addTab( w = createActivityTab( ),  tr( "Activity" ) );
    myWidgets << w;
    t->addTab( w = createPeersTab( ),     tr( "Peers" ) );
    myWidgets << w;
    t->addTab( w = createTrackerTab( ),   tr( "Tracker" ) );
    myWidgets << w;
    t->addTab( w = createInfoTab( ),      tr( "Information" ) );
    myWidgets << w;
    t->addTab( w = createFilesTab( ),     tr( "Files" ) );
    myWidgets << w;
    t->addTab( w = createOptionsTab( ),   tr( "Options" ) );
    myWidgets << w;
    layout->addWidget( t );

    QDialogButtonBox * buttons = new QDialogButtonBox( QDialogButtonBox::Close, Qt::Horizontal, this );
    connect( buttons, SIGNAL(rejected()), this, SLOT(deleteLater()) ); // "close" triggers rejected
    layout->addWidget( buttons );

    connect( &myTimer, SIGNAL(timeout()), this, SLOT(onTimer()) );

    onTimer( );
    myTimer.setSingleShot( false );
    myTimer.start( REFRESH_INTERVAL_MSEC );
}
    
Details :: ~Details( )
{
}

void
Details :: setIds( const QSet<int>& ids )
{
    if( ids == myIds )
        return;

    // stop listening to the old torrents
    foreach( int id, myIds ) {
        const Torrent * tor = myModel.getTorrentFromId( id );
        if( tor )
            disconnect( tor, SIGNAL(torrentChanged(int)), this, SLOT(onTorrentChanged()) );
    }

    myIds = ids;

    // listen to the new torrents
    foreach( int id, myIds ) {
        const Torrent * tor = myModel.getTorrentFromId( id );
        if( tor )
            connect( tor, SIGNAL(torrentChanged(int)), this, SLOT(onTorrentChanged()) );
    }

    foreach( QWidget * w, myWidgets )
        w->setEnabled( false );
    
    onTimer( );
}

/***
****
***/

void
Details :: onTimer( )
{
    if( !myIds.empty( ) )
        mySession.refreshExtraStats( myIds );
}

void
Details :: onTorrentChanged( )
{
    if( !myHavePendingRefresh ) {
        myHavePendingRefresh = true;
        QTimer::singleShot( 100, this, SLOT(refresh()));
    }
}


void
Details :: refresh( )
{
    int i;
    QLocale locale;
    const int n = myIds.size( );
    const bool single = n == 1;
    const QString blank;
    const QFontMetrics fm( fontMetrics( ) );
    QSet<const Torrent*> torrents;
    const Torrent * tor;
    QSet<QString> strings;
    QString string;

    // build a list of torrents
    foreach( int id, myIds ) {
        const Torrent * tor = myModel.getTorrentFromId( id );
        if( tor )
            torrents << tor;
    }

    ///
    ///  activity tab 
    ///

    // myStateLabel
    if( torrents.empty( ) )
        string = tr( "None" );
    else {
        strings.clear( );
        foreach( tor, torrents ) strings.insert( tor->activityString( ) );
        string = strings.size()==1 ? *strings.begin() : blank;
    }
    myStateLabel->setText( string );

    // myProgressLabel
    if( torrents.empty( ) )
        string = tr( "None" );
    else {
        double sizeWhenDone = 0;
        double leftUntilDone = 0;
        foreach( tor, torrents ) {
            sizeWhenDone += tor->sizeWhenDone( );
            leftUntilDone += tor->leftUntilDone( );
        }
        string = locale.toString( 100.0*((sizeWhenDone-leftUntilDone)/sizeWhenDone), 'f', 2 );
    }
    myProgressLabel->setText( string );

    // myHaveLabel
    int64_t haveTotal = 0;
    int64_t haveVerified = 0;
    int64_t verifiedPieces = 0;
    foreach( tor, torrents ) {
        haveTotal += tor->haveTotal( );
        haveVerified += tor->haveVerified( );
        verifiedPieces += tor->haveVerified( ) / tor->pieceSize( );
    }
    myHaveLabel->setText( tr( "%1 (%2 verified in %L3 pieces)" )
                            .arg( Utils::sizeToString( haveTotal ) )
                            .arg( Utils::sizeToString( haveVerified ) )
                            .arg( verifiedPieces ) );

    int64_t num = 0;
    foreach( tor, torrents ) num += tor->downloadedEver( );
    myDownloadedLabel->setText( Utils::sizeToString( num ) );

    num = 0;
    foreach( tor, torrents ) num += tor->uploadedEver( );
    myUploadedLabel->setText( Utils::sizeToString( num ) );

    num = 0;
    foreach( tor, torrents ) num += tor->failedEver( );
    myFailedLabel->setText( Utils::sizeToString( num ) );

    double d = 0;
    foreach( tor, torrents ) d += tor->ratio( );
    myRatioLabel->setText( Utils :: ratioToString( d / n ) );

    Speed speed;
    foreach( tor, torrents ) speed += tor->swarmSpeed( );
    mySwarmSpeedLabel->setText( Utils::speedToString( speed ) );

    strings.clear( );
    foreach( tor, torrents ) strings.insert( tor->dateAdded().toString() );
    string = strings.size()==1 ? *strings.begin() : blank;
    myAddedDateLabel->setText( string );

    strings.clear( );
    foreach( tor, torrents ) {
        QDateTime dt = tor->lastActivity( );
        strings.insert( dt.isNull() ? tr("Never") : dt.toString() );
    }
    string = strings.size()==1 ? *strings.begin() : blank;
    myActivityLabel->setText( string );

    if( torrents.empty( ) )
        string = tr( "None" );
    else {
        strings.clear( );
        foreach( tor, torrents ) strings.insert( tor->getError( ) );
        string = strings.size()==1 ? *strings.begin() : blank;
    }
    myErrorLabel->setText( string );

    ///
    /// information tab
    ///

    // myPiecesLabel
    int64_t pieceCount = 0;
    int64_t pieceSize = 0;
    foreach( tor, torrents ) {
        pieceCount += tor->pieceCount( );
        pieceSize += tor->pieceSize( );
    }
    myPiecesLabel->setText( tr( "%L1 Pieces @ %2" ).arg( pieceCount )
                                                   .arg( Utils::sizeToString( pieceSize ) ) );

    // myHashLabel
    strings.clear( );
    foreach( tor, torrents ) strings.insert( tor->hashString( ) );
    string = strings.size()==1 ? *strings.begin() : blank;
    myHashLabel->setText( string );

    // myPrivacyLabel
    strings.clear( );
    foreach( tor, torrents )
        strings.insert( tor->isPrivate( ) ? tr( "Private to this tracker -- PEX disabled" )
                                          : tr( "Public torrent" ) );
    string = strings.size()==1 ? *strings.begin() : blank;
    myPrivacyLabel->setText( string );

    // myCommentBrowser
    strings.clear( );
    foreach( tor, torrents ) strings.insert( tor->comment( ) );
    string = strings.size()==1 ? *strings.begin() : blank;
    myCommentBrowser->setText( string );

    // myCreatorLabel
    strings.clear( );
    foreach( tor, torrents ) strings.insert( tor->creator().isEmpty() ? tr( "Unknown" ) : tor->creator() );
    string = strings.size()==1 ? *strings.begin() : blank;
    myCreatorLabel->setText( string );

    // myDateCreatedLabel
    strings.clear( );
    foreach( tor, torrents ) strings.insert( tor->dateCreated().toString() );
    string = strings.size()==1 ? *strings.begin() : blank;
    myDateCreatedLabel->setText( string );

    // myDestinationLabel
    strings.clear( );
    foreach( tor, torrents ) strings.insert( tor->getPath( ) );
    string = strings.size()==1 ? *strings.begin() : blank;
    myDestinationLabel->setText( string );

    // myTorrentFileLabel
    strings.clear( );
    foreach( tor, torrents ) strings.insert( tor->torrentFile( ) );
    string = strings.size()==1 ? *strings.begin() : blank;
    myTorrentFileLabel->setText( string );

    ///
    ///  Options Tab
    ///

    if( !torrents.empty( ) )
    {
        int i;
        const Torrent * baseline = *torrents.begin();
        bool uniform;
        bool baselineFlag;
        int baselineInt;

        // mySessionLimitCheck
        uniform = true;
        baselineFlag = baseline->honorsSessionLimits( );
        foreach( tor, torrents ) if( baselineFlag != tor->honorsSessionLimits( ) ) { uniform = false; break; }
        mySessionLimitCheck->setChecked( uniform && baselineFlag );

        // mySingleDownCheck
        uniform = true;
        baselineFlag = baseline->downloadIsLimited( );
        foreach( tor, torrents ) if( baselineFlag != tor->downloadIsLimited( ) ) { uniform = false; break; }
        mySingleDownCheck->setChecked( uniform && baselineFlag );

        // mySingleUpCheck
        uniform = true;
        baselineFlag = baseline->uploadIsLimited( );
        foreach( tor, torrents ) if( baselineFlag != tor->uploadIsLimited( ) ) { uniform = false; break; }
        mySingleUpCheck->setChecked( uniform && baselineFlag );

        // myBandwidthPriorityCombo
        uniform = true;
        baselineInt = baseline->getBandwidthPriority( );
        foreach( tor, torrents ) if ( baselineInt != tor->getBandwidthPriority( ) ) { uniform = false; break; }
        if( uniform )
            i = myBandwidthPriorityCombo->findData( baselineInt );
        else
            i = -1;
        myBandwidthPriorityCombo->blockSignals( true );
        myBandwidthPriorityCombo->setCurrentIndex( i );
        myBandwidthPriorityCombo->blockSignals( false );

        mySingleDownSpin->blockSignals( true );
        mySingleDownSpin->setValue( tor->downloadLimit().kbps() );
        mySingleDownSpin->blockSignals( false );

        mySingleUpSpin->blockSignals( true );
        mySingleUpSpin->setValue( tor->uploadLimit().kbps() );
        mySingleUpSpin->blockSignals( false );

        myPeerLimitSpin->blockSignals( true );
        myPeerLimitSpin->setValue( tor->peerLimit() );
        myPeerLimitSpin->blockSignals( false );

        // ratio radios
        uniform = true;
        baselineInt = tor->seedRatioMode( );
        foreach( tor, torrents ) if( baselineInt != tor->seedRatioMode( ) ) { uniform = false; break; }
        if( !uniform ) {
            mySeedGlobalRadio->setChecked( false );
            mySeedCustomRadio->setChecked( false );
            mySeedForeverRadio->setChecked( false );
        } else {
            QRadioButton * rb;
            switch( baselineInt ) {
                case TR_RATIOLIMIT_GLOBAL:    rb = mySeedGlobalRadio; break;
                case TR_RATIOLIMIT_SINGLE:    rb = mySeedCustomRadio; break;
                case TR_RATIOLIMIT_UNLIMITED: rb = mySeedForeverRadio; break;
            }
            rb->setChecked( true );
        }

        mySeedCustomSpin->blockSignals( true );
        mySeedCustomSpin->setValue( tor->seedRatioLimit( ) );
        mySeedCustomSpin->blockSignals( false );
    }

    // tracker tab
    const time_t now( time( 0 ) );
    myScrapeTimePrevLabel->setText( tor ? tor->lastScrapeTime().toString() : blank );
    myScrapeResponseLabel->setText( tor ? tor->scrapeResponse() : blank );
    myScrapeTimeNextLabel->setText( Utils :: timeToString( tor ? tor->nextScrapeTime().toTime_t() - now : 0 ) );
    myAnnounceTimePrevLabel->setText( tor ? tor->lastScrapeTime().toString() : blank );
    myAnnounceTimeNextLabel->setText( Utils :: timeToString( tor ? tor->nextAnnounceTime().toTime_t() - now : 0  ) );
    myAnnounceManualLabel->setText( Utils :: timeToString( tor ? tor->manualAnnounceTime().toTime_t() - now : 0 ) );
    myAnnounceResponseLabel->setText( tor ? tor->announceResponse( ) : blank );

    // myTrackerLabel
    strings.clear( );
    foreach( tor, torrents ) strings.insert( QUrl(tor->announceUrl()).host() );
    string = strings.size()==1 ? *strings.begin() : blank;
    myTrackerLabel->setText( string );

    ///
    ///  Peers tab
    ///

    i = 0;
    foreach( tor, torrents ) i += tor->seeders( );
    mySeedersLabel->setText( locale.toString( i ) );

    i = 0;
    foreach( tor, torrents ) i += tor->leechers( );
    myLeechersLabel->setText( locale.toString( i ) );

    i = 0;
    foreach( tor, torrents ) i += tor->timesCompleted( );
    myTimesCompletedLabel->setText( locale.toString( i ) );

    PeerList peers;
    foreach( tor, torrents ) peers << tor->peers( );
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

    if( single ) {
        tor = *torrents.begin();
        myFileTreeView->update( tor->files( ) );
    } else { 
        myFileTreeView->clear( );
    }

    myHavePendingRefresh = false;
    foreach( QWidget * w, myWidgets )
        w->setEnabled( true );
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
std::cerr << " honorsSessionLimits clicked to " << val << std::endl;
    mySession.torrentSet( myIds, "honorsSessionLimits", val );
}
void
Details :: onDownloadLimitedToggled( bool val )
{
    mySession.torrentSet( myIds, "downloadLimited", val );
}
void
Details :: onDownloadLimitChanged( int val )
{
    mySession.torrentSet( myIds, "downloadLimit", val );
}
void
Details :: onUploadLimitedToggled( bool val )
{
    mySession.torrentSet( myIds, "uploadLimited", val );
}
void
Details :: onUploadLimitChanged( int val )
{
    mySession.torrentSet( myIds, "uploadLimit", val );
}

#define RATIO_KEY "seedRatioMode"

void
Details :: onSeedUntilChanged( bool b )
{
    if( b )
        mySession.torrentSet( myIds, RATIO_KEY, sender()->property(RATIO_KEY).toInt() );
}

void
Details :: onSeedRatioLimitChanged( double val )
{
    QSet<int> ids;

    foreach( int id, myIds ) {
        const Torrent * tor = myModel.getTorrentFromId( id );
        if( tor && tor->seedRatioLimit( ) )
            ids.insert( id );
    }

    if( !ids.empty( ) )
        mySession.torrentSet( ids, "seedRatioLimit", val );
}

void
Details :: onMaxPeersChanged( int val )
{
    mySession.torrentSet( myIds, "peer-limit", val );
}

void
Details :: onBandwidthPriorityChanged( int index )
{
    if( index != -1 )
    {
        const int priority = myBandwidthPriorityCombo->itemData(index).toInt( );
        mySession.torrentSet( myIds, "bandwidthPriority", priority );
    }
}

QWidget *
Details :: createOptionsTab( )
{
    //QWidget * l;
    QSpinBox * s;
    QCheckBox * c;
    QComboBox * m;
    QHBoxLayout * h;
    QRadioButton * r;
    QDoubleSpinBox * ds;

    HIG * hig = new HIG( this );
    hig->addSectionTitle( tr( "Speed" ) );

    c = new QCheckBox( tr( "Honor global &limits" ) );
    mySessionLimitCheck = c;
    hig->addWideControl( c );
    connect( c, SIGNAL(clicked(bool)), this, SLOT(onHonorsSessionLimitsToggled(bool)) );

    c = new QCheckBox( tr( "Limit &download speed (KB/s)" ) );
    mySingleDownCheck = c;
    s = new QSpinBox( );
    mySingleDownSpin = s;
    s->setRange( 0, INT_MAX );
    hig->addRow( c, s );
    enableWhenChecked( c, s );
    connect( c, SIGNAL(clicked(bool)), this, SLOT(onDownloadLimitedToggled(bool)) );
    connect( s, SIGNAL(valueChanged(int)), this, SLOT(onDownloadLimitChanged(int)));

    c = new QCheckBox( tr( "Limit &upload speed (KB/s)" ) );
    mySingleUpCheck = c;
    s = new QSpinBox( );
    mySingleUpSpin = s;
    s->setRange( 0, INT_MAX );
    hig->addRow( c, s );
    enableWhenChecked( c, s );
    connect( c, SIGNAL(clicked(bool)), this, SLOT(onUploadLimitedToggled(bool)) );
    connect( s, SIGNAL(valueChanged(int)), this, SLOT(onUploadLimitChanged(int)));

    m = new QComboBox;
    m->addItem( tr( "Low" ),    TR_PRI_LOW );
    m->addItem( tr( "Normal" ), TR_PRI_NORMAL );
    m->addItem( tr( "High" ),   TR_PRI_HIGH );
    connect( m, SIGNAL(currentIndexChanged(int)), this, SLOT(onBandwidthPriorityChanged(int)));
    hig->addRow( tr( "&Bandwidth priority:" ), m );
    myBandwidthPriorityCombo = m;
    

    hig->addSectionDivider( );
    hig->addSectionTitle( tr( "Seed-Until Ratio" ) );

    r = new QRadioButton( tr( "Use &global setting" ) );
    r->setProperty( RATIO_KEY, TR_RATIOLIMIT_GLOBAL );
    connect( r, SIGNAL(clicked(bool)), this, SLOT(onSeedUntilChanged(bool)));
    mySeedGlobalRadio = r;
    hig->addWideControl( r );

    r = new QRadioButton( tr( "Seed &regardless of ratio" ) );
    r->setProperty( RATIO_KEY, TR_RATIOLIMIT_UNLIMITED );
    connect( r, SIGNAL(clicked(bool)), this, SLOT(onSeedUntilChanged(bool)));
    mySeedForeverRadio = r;
    hig->addWideControl( r );

    h = new QHBoxLayout( );
    h->setSpacing( HIG :: PAD );
    r = new QRadioButton( tr( "&Stop seeding when a torrent's ratio reaches" ) );
    r->setProperty( RATIO_KEY, TR_RATIOLIMIT_SINGLE );
    connect( r, SIGNAL(clicked(bool)), this, SLOT(onSeedUntilChanged(bool)));
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
    mySession.torrentSet( myIds, key, indices.toList( ) );
}

void
Details :: onFileWantedChanged( const QSet<int>& indices, bool wanted )
{
    QString key( wanted ? "files-wanted" : "files-unwanted" );
    mySession.torrentSet( myIds, key, indices.toList( ) );
}
