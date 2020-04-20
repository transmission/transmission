/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm> // std::any_of
#include <cassert>
#include <climits> /* INT_MAX */
#include <ctime>

#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHostAddress>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QResizeEvent>
#include <QStringList>
#include <QStyle>
#include <QTreeWidgetItem>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_getRatio()

#include "ColumnResizer.h"
#include "DetailsDialog.h"
#include "Formatter.h"
#include "Prefs.h"
#include "Session.h"
#include "SqueezeLabel.h"
#include "Torrent.h"
#include "TorrentModel.h"
#include "TrackerDelegate.h"
#include "TrackerModel.h"
#include "TrackerModelFilter.h"
#include "Utils.h"

class Prefs;
class Session;

/****
*****
****/

namespace
{

int const REFRESH_INTERVAL_MSEC = 4000;

char const* PREF_KEY("pref-key");

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

int measureViewItem(QTreeWidget* view, int column, QString const& text)
{
    QTreeWidgetItem const* headerItem = view->headerItem();

    int const itemWidth = Utils::measureViewItem(view, text);
    int const headerWidth = Utils::measureHeaderItem(view->header(), headerItem->text(column));

    return std::max(itemWidth, headerWidth);
}

} // namespace

/***
****
***/

class PeerItem : public QTreeWidgetItem
{
    Peer peer;
    QString mutable collatedAddress;
    QString status;

public:
    PeerItem(Peer const& p) :
        peer(p)
    {
    }

    virtual ~PeerItem()
    {
    }

public:
    void refresh(Peer const& p)
    {
        if (p.address != peer.address)
        {
            collatedAddress.clear();
        }

        peer = p;
    }

    void setStatus(QString const& s)
    {
        status = s;
    }

    virtual bool operator <(QTreeWidgetItem const& other) const
    {
        PeerItem const* i = dynamic_cast<PeerItem const*>(&other);
        QTreeWidget* tw(treeWidget());
        int const column = tw != nullptr ? tw->sortColumn() : 0;

        assert(i != nullptr);

        switch (column)
        {
        case COL_UP:
            return peer.rateToPeer < i->peer.rateToPeer;

        case COL_DOWN:
            return peer.rateToClient < i->peer.rateToClient;

        case COL_PERCENT:
            return peer.progress < i->peer.progress;

        case COL_STATUS:
            return status < i->status;

        case COL_CLIENT:
            return peer.clientName < i->peer.clientName;

        case COL_LOCK:
            return peer.isEncrypted && !i->peer.isEncrypted;

        default:
            return address() < i->address();
        }
    }

private:
    QString const& address() const
    {
        if (collatedAddress.isEmpty())
        {
            QHostAddress ipAddress;

            if (ipAddress.setAddress(peer.address))
            {
                if (ipAddress.protocol() == QAbstractSocket::IPv4Protocol)
                {
                    quint32 const ipv4Address = ipAddress.toIPv4Address();
                    collatedAddress = QLatin1String("1-") + QString::fromLatin1(QByteArray::number(ipv4Address, 16).
                        rightJustified(8, '0'));
                }
                else if (ipAddress.protocol() == QAbstractSocket::IPv6Protocol)
                {
                    Q_IPV6ADDR const ipv6Address = ipAddress.toIPv6Address();
                    QByteArray tmp(16, '\0');

                    for (int i = 0; i < 16; ++i)
                    {
                        tmp[i] = ipv6Address[i];
                    }

                    collatedAddress = QLatin1String("2-") + QString::fromLatin1(tmp.toHex());
                }
            }

            if (collatedAddress.isEmpty())
            {
                collatedAddress = QLatin1String("3-") + peer.address.toLower();
            }
        }

        return collatedAddress;
    }
};

/***
****
***/

QIcon DetailsDialog::getStockIcon(QString const& freedesktop_name, int fallback)
{
    QIcon icon = QIcon::fromTheme(freedesktop_name);

    if (icon.isNull())
    {
        icon = style()->standardIcon(QStyle::StandardPixmap(fallback), nullptr, this);
    }

    return icon;
}

DetailsDialog::DetailsDialog(Session& session, Prefs& prefs, TorrentModel const& model, QWidget* parent) :
    BaseDialog(parent),
    mySession(session),
    myPrefs(prefs),
    myModel(model),
    myChangedTorrents(false),
    myHavePendingRefresh(false)
{
    ui.setupUi(this);

    initInfoTab();
    initPeersTab();
    initTrackerTab();
    initFilesTab();
    initOptionsTab();

    adjustSize();
    ui.commentBrowser->setMaximumHeight(QWIDGETSIZE_MAX);

    QList<int> initKeys;
    initKeys << Prefs::SHOW_TRACKER_SCRAPES << Prefs::SHOW_BACKUP_TRACKERS;

    for (int const key : initKeys)
    {
        refreshPref(key);
    }

    connect(&myModel, &TorrentModel::torrentsChanged, this, &DetailsDialog::onTorrentsChanged);
    connect(&myPrefs, &Prefs::changed, this, &DetailsDialog::refreshPref);
    connect(&myTimer, &QTimer::timeout, this, &DetailsDialog::onTimer);

    onTimer();
    myTimer.setSingleShot(false);
    myTimer.start(REFRESH_INTERVAL_MSEC);
}

DetailsDialog::~DetailsDialog()
{
    myTrackerDelegate->deleteLater();
    myTrackerFilter->deleteLater();
    myTrackerModel->deleteLater();
}

void DetailsDialog::setIds(torrent_ids_t const& ids)
{
    if (ids != myIds)
    {
        setEnabled(false);
        ui.filesView->clear();

        myIds = ids;
        mySession.refreshDetailInfo(myIds);
        myChangedTorrents = true;
        myTrackerModel->refresh(myModel, myIds);
        onTimer();
    }
}

void DetailsDialog::refreshPref(int key)
{
    QString str;

    switch (key)
    {
    case Prefs::SHOW_TRACKER_SCRAPES:
        {
            QItemSelectionModel* selectionModel(ui.trackersView->selectionModel());
            QItemSelection const selection(selectionModel->selection());
            QModelIndex const currentIndex(selectionModel->currentIndex());
            myTrackerDelegate->setShowMore(myPrefs.getBool(key));
            selectionModel->clear();
            ui.trackersView->reset();
            selectionModel->select(selection, QItemSelectionModel::Select);
            selectionModel->setCurrentIndex(currentIndex, QItemSelectionModel::NoUpdate);
            break;
        }

    case Prefs::SHOW_BACKUP_TRACKERS:
        myTrackerFilter->setShowBackupTrackers(myPrefs.getBool(key));
        break;

    default:
        break;
    }
}

/***
****
***/

void DetailsDialog::onTimer()
{
    getNewData();
}

void DetailsDialog::getNewData()
{
    if (!myIds.empty())
    {
        mySession.refreshExtraStats(myIds);
    }
}

void DetailsDialog::onTorrentEdited(torrent_ids_t const& /*ids*/)
{
    // FIXME
    // refreshDetailInfo({ tor.id() });
}

void DetailsDialog::onTorrentsChanged(torrent_ids_t const& ids)
{
    if (myHavePendingRefresh)
    {
        return;
    }

    if (!std::any_of(ids.begin(), ids.end(), [this](auto const& id) { return myIds.count(id) != 0; }))
    {
        return;
    }

    myHavePendingRefresh = true;
    QTimer::singleShot(100, this, SLOT(refresh()));
}

namespace
{

void setIfIdle(QComboBox* box, int i)
{
    if (!box->hasFocus())
    {
        box->blockSignals(true);
        box->setCurrentIndex(i);
        box->blockSignals(false);
    }
}

void setIfIdle(QDoubleSpinBox* spin, double value)
{
    if (!spin->hasFocus())
    {
        spin->blockSignals(true);
        spin->setValue(value);
        spin->blockSignals(false);
    }
}

void setIfIdle(QSpinBox* spin, int value)
{
    if (!spin->hasFocus())
    {
        spin->blockSignals(true);
        spin->setValue(value);
        spin->blockSignals(false);
    }
}

} // namespace

void DetailsDialog::refresh()
{
    int const n = myIds.size();
    bool const single = n == 1;
    QString const blank;
    QFontMetrics const fm(fontMetrics());
    QList<Torrent const*> torrents;
    QString string;
    QString const none = tr("None");
    QString const mixed = tr("Mixed");
    QString const unknown = tr("Unknown");

    // build a list of torrents
    for (int const id : myIds)
    {
        Torrent const* tor = myModel.getTorrentFromId(id);

        if (tor != nullptr)
        {
            torrents << tor;
        }
    }

    ///
    ///  activity tab
    ///

    // myStateLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        bool isMixed = false;
        bool allPaused = true;
        bool allFinished = true;
        tr_torrent_activity const baseline = torrents[0]->getActivity();

        for (Torrent const* const t : torrents)
        {
            tr_torrent_activity const activity = t->getActivity();

            if (activity != baseline)
            {
                isMixed = true;
            }

            if (activity != TR_STATUS_STOPPED)
            {
                allPaused = allFinished = false;
            }

            if (!t->isFinished())
            {
                allFinished = false;
            }
        }

        if (isMixed)
        {
            string = mixed;
        }
        else if (allFinished)
        {
            string = tr("Finished");
        }
        else if (allPaused)
        {
            string = tr("Paused");
        }
        else
        {
            string = torrents[0]->activityString();
        }
    }

    ui.stateValueLabel->setText(string);
    QString const stateString = string;

    // myHaveLabel
    uint64_t sizeWhenDone = 0;
    uint64_t available = 0;

    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        uint64_t leftUntilDone = 0;
        int64_t haveTotal = 0;
        int64_t haveVerified = 0;
        int64_t haveUnverified = 0;
        int64_t verifiedPieces = 0;

        for (Torrent const* const t : torrents)
        {
            if (t->hasMetadata())
            {
                haveTotal += t->haveTotal();
                haveUnverified += t->haveUnverified();
                uint64_t const v = t->haveVerified();
                haveVerified += v;

                if (t->pieceSize())
                {
                    verifiedPieces += v / t->pieceSize();
                }

                sizeWhenDone += t->sizeWhenDone();
                leftUntilDone += t->leftUntilDone();
                available += t->sizeWhenDone() - t->leftUntilDone() + t->desiredAvailable();
            }
        }

        double const d = sizeWhenDone != 0 ? 100.0 * (sizeWhenDone - leftUntilDone) / sizeWhenDone : 100.0;
        QString pct = Formatter::percentToString(d);

        if (haveUnverified == 0 && leftUntilDone == 0)
        {
            //: Text following the "Have:" label in torrent properties dialog;
            //: %1 is amount of downloaded and verified data
            string = tr("%1 (100%)").arg(Formatter::sizeToString(haveVerified));
        }
        else if (haveUnverified == 0)
        {
            //: Text following the "Have:" label in torrent properties dialog;
            //: %1 is amount of downloaded and verified data,
            //: %2 is overall size of torrent data,
            //: %3 is percentage (%1/%2*100)
            string = tr("%1 of %2 (%3%)").arg(Formatter::sizeToString(haveVerified)).arg(Formatter::sizeToString(sizeWhenDone)).
                arg(pct);
        }
        else
        {
            //: Text following the "Have:" label in torrent properties dialog;
            //: %1 is amount of downloaded data (both verified and unverified),
            //: %2 is overall size of torrent data,
            //: %3 is percentage (%1/%2*100),
            //: %4 is amount of downloaded but not yet verified data
            string = tr("%1 of %2 (%3%), %4 Unverified").arg(Formatter::sizeToString(haveVerified + haveUnverified)).
                arg(Formatter::sizeToString(sizeWhenDone)).arg(pct).arg(Formatter::sizeToString(haveUnverified));
        }
    }

    ui.haveValueLabel->setText(string);

    // myAvailabilityLabel
    if (torrents.empty())
    {
        string = none;
    }
    else if (sizeWhenDone == 0)
    {
        string = none;
    }
    else
    {
        string = QString::fromLatin1("%1%").arg(Formatter::percentToString((100.0 * available) / sizeWhenDone));
    }

    ui.availabilityValueLabel->setText(string);

    // myDownloadedLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        uint64_t d = 0;
        uint64_t f = 0;

        for (Torrent const* const t : torrents)
        {
            d += t->downloadedEver();
            f += t->failedEver();
        }

        QString const dstr = Formatter::sizeToString(d);
        QString const fstr = Formatter::sizeToString(f);

        if (f != 0)
        {
            string = tr("%1 (%2 corrupt)").arg(dstr).arg(fstr);
        }
        else
        {
            string = dstr;
        }
    }

    ui.downloadedValueLabel->setText(string);

    //  myUploadedLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        uint64_t u = 0;
        uint64_t d = 0;

        for (Torrent const* const t : torrents)
        {
            u += t->uploadedEver();
            d += t->downloadedEver();
        }

        string = tr("%1 (Ratio: %2)").arg(Formatter::sizeToString(u)).arg(Formatter::ratioToString(tr_getRatio(u, d)));
    }

    ui.uploadedValueLabel->setText(string);

    // myRunTimeLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        bool allPaused = true;
        auto baseline = torrents[0]->lastStarted();

        for (Torrent const* const t : torrents)
        {
            if (baseline != t->lastStarted())
            {
                baseline = 0;
            }

            if (!t->isPaused())
            {
                allPaused = false;
            }
        }

        if (allPaused)
        {
            string = stateString; // paused || finished
        }
        else if (baseline == 0)
        {
            string = mixed;
        }
        else
        {
            auto const now = time(nullptr);
            auto const seconds = int(std::difftime(now, baseline));
            string = Formatter::timeToString(seconds);
        }
    }

    ui.runningTimeValueLabel->setText(string);

    // myETALabel
    string.clear();

    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        int baseline = torrents[0]->getETA();

        for (Torrent const* const t : torrents)
        {
            if (baseline != t->getETA())
            {
                string = mixed;
                break;
            }
        }

        if (string.isEmpty())
        {
            if (baseline < 0)
            {
                string = tr("Unknown");
            }
            else
            {
                string = Formatter::timeToString(baseline);
            }
        }
    }

    ui.remainingTimeValueLabel->setText(string);

    // myLastActivityLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        auto latest = torrents[0]->lastActivity();

        for (Torrent const* const t : torrents)
        {
            auto const dt = t->lastActivity();

            if (latest < dt)
            {
                latest = dt;
            }
        }

        auto const now = time(nullptr);
        auto const seconds = int(std::difftime(now, latest));

        if (seconds < 0)
        {
            string = none;
        }
        else if (seconds < 5)
        {
            string = tr("Active now");
        }
        else
        {
            string = tr("%1 ago").arg(Formatter::timeToString(seconds));
        }
    }

    ui.lastActivityValueLabel->setText(string);

    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        string = torrents[0]->getError();

        for (Torrent const* const t : torrents)
        {
            if (string != t->getError())
            {
                string = mixed;
                break;
            }
        }
    }

    if (string.isEmpty())
    {
        string = none;
    }

    ui.errorValueLabel->setText(string);

    ///
    /// information tab
    ///

    // mySizeLabel
    if (torrents.empty())
    {
        string = none;
    }
    else
    {
        int pieces = 0;
        uint64_t size = 0;
        uint32_t pieceSize = torrents[0]->pieceSize();

        for (Torrent const* const t : torrents)
        {
            pieces += t->pieceCount();
            size += t->totalSize();

            if (pieceSize != t->pieceSize())
            {
                pieceSize = 0;
            }
        }

        if (size == 0)
        {
            string = none;
        }
        else if (pieceSize > 0)
        {
            string = tr("%1 (%Ln pieces @ %2)", "", pieces).arg(Formatter::sizeToString(size)).
                arg(Formatter::memToString(pieceSize));
        }
        else
        {
            string = tr("%1 (%Ln pieces)", "", pieces).arg(Formatter::sizeToString(size));
        }
    }

    ui.sizeValueLabel->setText(string);

    // myHashLabel
    string = none;

    if (!torrents.empty())
    {
        string = torrents[0]->hashString();

        for (Torrent const* const t : torrents)
        {
            if (string != t->hashString())
            {
                string = mixed;
                break;
            }
        }
    }

    ui.hashValueLabel->setText(string);

    // myPrivacyLabel
    string = none;

    if (!torrents.empty())
    {
        bool b = torrents[0]->isPrivate();
        string = b ? tr("Private to this tracker -- DHT and PEX disabled") : tr("Public torrent");

        for (Torrent const* const t : torrents)
        {
            if (b != t->isPrivate())
            {
                string = mixed;
                break;
            }
        }
    }

    ui.privacyValueLabel->setText(string);

    // myCommentBrowser
    string = none;
    bool isCommentMixed = false;

    if (!torrents.empty())
    {
        string = torrents[0]->comment();

        for (Torrent const* const t : torrents)
        {
            if (string != t->comment())
            {
                string = mixed;
                isCommentMixed = true;
                break;
            }
        }
    }

    if (ui.commentBrowser->toPlainText() != string)
    {
        ui.commentBrowser->setText(string);
    }

    ui.commentBrowser->setEnabled(!isCommentMixed && !string.isEmpty());

    // myOriginLabel
    string = none;

    if (!torrents.empty())
    {
        bool mixed_creator = false;
        bool mixed_date = false;
        QString const creator = torrents[0]->creator();
        auto const date = torrents[0]->dateCreated();

        for (Torrent const* const t : torrents)
        {
            mixed_creator |= (creator != t->creator());
            mixed_date |= (date != t->dateCreated());
        }

        bool const empty_creator = creator.isEmpty();
        bool const empty_date = date <= 0;

        if (mixed_creator || mixed_date)
        {
            string = mixed;
        }
        else if (empty_creator && empty_date)
        {
            string = tr("N/A");
        }
        else if (empty_date && !empty_creator)
        {
            string = tr("Created by %1").arg(creator);
        }
        else if (empty_creator && !empty_date)
        {
            auto const dateStr = QDateTime::fromSecsSinceEpoch(date).toString();
            string = tr("Created on %1").arg(dateStr);
        }
        else
        {
            auto const dateStr = QDateTime::fromSecsSinceEpoch(date).toString();
            string = tr("Created by %1 on %2").arg(creator).arg(dateStr);
        }
    }

    ui.originValueLabel->setText(string);

    // myLocationLabel
    string = none;

    if (!torrents.empty())
    {
        string = torrents[0]->getPath();

        for (Torrent const* const t : torrents)
        {
            if (string != t->getPath())
            {
                string = mixed;
                break;
            }
        }
    }

    ui.locationValueLabel->setText(string);

    ///
    ///  Options Tab
    ///

    if (myChangedTorrents && !torrents.empty())
    {
        int i;
        bool uniform;
        bool baselineFlag;
        int baselineInt;
        Torrent const& baseline = *torrents.front();

        // mySessionLimitCheck
        uniform = true;
        baselineFlag = baseline.honorsSessionLimits();

        for (Torrent const* const tor : torrents)
        {
            if (baselineFlag != tor->honorsSessionLimits())
            {
                uniform = false;
                break;
            }
        }

        ui.sessionLimitCheck->setChecked(uniform && baselineFlag);

        // mySingleDownCheck
        uniform = true;
        baselineFlag = baseline.downloadIsLimited();

        for (Torrent const* const tor : torrents)
        {
            if (baselineFlag != tor->downloadIsLimited())
            {
                uniform = false;
                break;
            }
        }

        ui.singleDownCheck->setChecked(uniform && baselineFlag);

        // mySingleUpCheck
        uniform = true;
        baselineFlag = baseline.uploadIsLimited();

        for (Torrent const* const tor : torrents)
        {
            if (baselineFlag != tor->uploadIsLimited())
            {
                uniform = false;
                break;
            }
        }

        ui.singleUpCheck->setChecked(uniform && baselineFlag);

        // myBandwidthPriorityCombo
        uniform = true;
        baselineInt = baseline.getBandwidthPriority();

        for (Torrent const* const tor : torrents)
        {
            if (baselineInt != tor->getBandwidthPriority())
            {
                uniform = false;
                break;
            }
        }

        if (uniform)
        {
            i = ui.bandwidthPriorityCombo->findData(baselineInt);
        }
        else
        {
            i = -1;
        }

        setIfIdle(ui.bandwidthPriorityCombo, i);

        setIfIdle(ui.singleDownSpin, int(baseline.downloadLimit().KBps()));
        setIfIdle(ui.singleUpSpin, int(baseline.uploadLimit().KBps()));
        setIfIdle(ui.peerLimitSpin, baseline.peerLimit());
    }

    if (!torrents.empty())
    {
        Torrent const& baseline = *torrents.front();

        // ratio
        bool uniform = true;
        int baselineInt = baseline.seedRatioMode();

        for (Torrent const* const tor : torrents)
        {
            if (baselineInt != tor->seedRatioMode())
            {
                uniform = false;
                break;
            }
        }

        setIfIdle(ui.ratioCombo, uniform ? ui.ratioCombo->findData(baselineInt) : -1);
        ui.ratioSpin->setVisible(uniform && baselineInt == TR_RATIOLIMIT_SINGLE);

        setIfIdle(ui.ratioSpin, baseline.seedRatioLimit());

        // idle
        uniform = true;
        baselineInt = baseline.seedIdleMode();

        for (Torrent const* const tor : torrents)
        {
            if (baselineInt != tor->seedIdleMode())
            {
                uniform = false;
                break;
            }
        }

        setIfIdle(ui.idleCombo, uniform ? ui.idleCombo->findData(baselineInt) : -1);
        ui.idleSpin->setVisible(uniform && baselineInt == TR_RATIOLIMIT_SINGLE);

        setIfIdle(ui.idleSpin, baseline.seedIdleLimit());
        onIdleLimitChanged();
    }

    ///
    ///  Tracker tab
    ///

    myTrackerModel->refresh(myModel, myIds);

    ///
    ///  Peers tab
    ///

    QMap<QString, QTreeWidgetItem*> peers2;
    QList<QTreeWidgetItem*> newItems;

    for (Torrent const* const t : torrents)
    {
        QString const idStr(QString::number(t->id()));
        PeerList peers = t->peers();

        for (Peer const& peer : peers)
        {
            QString const key = idStr + QLatin1Char(':') + peer.address;
            PeerItem* item = static_cast<PeerItem*>(myPeers.value(key, nullptr));

            if (item == nullptr) // new peer has connected
            {
                static QIcon const myEncryptionIcon(QLatin1String(":/icons/encrypted.png"));
                static QIcon const myEmptyIcon;
                item = new PeerItem(peer);
                item->setTextAlignment(COL_UP, Qt::AlignRight | Qt::AlignVCenter);
                item->setTextAlignment(COL_DOWN, Qt::AlignRight | Qt::AlignVCenter);
                item->setTextAlignment(COL_PERCENT, Qt::AlignRight | Qt::AlignVCenter);
                item->setIcon(COL_LOCK, peer.isEncrypted ? myEncryptionIcon : myEmptyIcon);
                item->setToolTip(COL_LOCK, peer.isEncrypted ? tr("Encrypted connection") : QString());
                item->setText(COL_ADDRESS, peer.address);
                item->setText(COL_CLIENT, peer.clientName);
                newItems << item;
            }

            QString const code = peer.flagStr;
            item->setStatus(code);
            item->refresh(peer);

            QString codeTip;

            for (QChar const ch : code)
            {
                QString txt;

                switch (ch.unicode())
                {
                case 'O':
                    txt = tr("Optimistic unchoke");
                    break;

                case 'D':
                    txt = tr("Downloading from this peer");
                    break;

                case 'd':
                    txt = tr("We would download from this peer if they would let us");
                    break;

                case 'U':
                    txt = tr("Uploading to peer");
                    break;

                case 'u':
                    txt = tr("We would upload to this peer if they asked");
                    break;

                case 'K':
                    txt = tr("Peer has unchoked us, but we're not interested");
                    break;

                case '?':
                    txt = tr("We unchoked this peer, but they're not interested");
                    break;

                case 'E':
                    txt = tr("Encrypted connection");
                    break;

                case 'H':
                    txt = tr("Peer was discovered through DHT");
                    break;

                case 'X':
                    txt = tr("Peer was discovered through Peer Exchange (PEX)");
                    break;

                case 'I':
                    txt = tr("Peer is an incoming connection");
                    break;

                case 'T':
                    txt = tr("Peer is connected over uTP");
                    break;
                }

                if (!txt.isEmpty())
                {
                    codeTip += QString::fromLatin1("%1: %2\n").arg(ch).arg(txt);
                }
            }

            if (!codeTip.isEmpty())
            {
                codeTip.resize(codeTip.size() - 1); // eat the trailing linefeed
            }

            item->setText(COL_UP, peer.rateToPeer.isZero() ? QString() : Formatter::speedToString(peer.rateToPeer));
            item->setText(COL_DOWN, peer.rateToClient.isZero() ? QString() : Formatter::speedToString(peer.rateToClient));
            item->setText(COL_PERCENT, peer.progress > 0 ? QString::fromLatin1("%1%").arg(int(peer.progress * 100.0)) :
                QString());
            item->setText(COL_STATUS, code);
            item->setToolTip(COL_STATUS, codeTip);

            peers2.insert(key, item);
        }
    }

    ui.peersView->addTopLevelItems(newItems);

    for (QString const& key : myPeers.keys())
    {
        if (!peers2.contains(key)) // old peer has disconnected
        {
            QTreeWidgetItem* item = myPeers.value(key, nullptr);
            ui.peersView->takeTopLevelItem(ui.peersView->indexOfTopLevelItem(item));
            delete item;
        }
    }

    myPeers = peers2;

    if (!single)
    {
        ui.filesView->clear();
    }

    if (single)
    {
        ui.filesView->update(torrents[0]->files(), myChangedTorrents);
    }

    myChangedTorrents = false;
    myHavePendingRefresh = false;
    setEnabled(true);
}

void DetailsDialog::setEnabled(bool enabled)
{
    for (int i = 0; i < ui.tabs->count(); ++i)
    {
        ui.tabs->widget(i)->setEnabled(enabled);
    }
}

/***
****
***/

void DetailsDialog::initInfoTab()
{
    int const h = QFontMetrics(ui.commentBrowser->font()).lineSpacing() * 4;
    ui.commentBrowser->setFixedHeight(h);

    ColumnResizer* cr(new ColumnResizer(this));
    cr->addLayout(ui.activitySectionLayout);
    cr->addLayout(ui.detailsSectionLayout);
    cr->update();
}

/***
****
***/

void DetailsDialog::onShowTrackerScrapesToggled(bool val)
{
    myPrefs.set(Prefs::SHOW_TRACKER_SCRAPES, val);
}

void DetailsDialog::onShowBackupTrackersToggled(bool val)
{
    myPrefs.set(Prefs::SHOW_BACKUP_TRACKERS, val);
}

void DetailsDialog::onHonorsSessionLimitsToggled(bool val)
{
    mySession.torrentSet(myIds, TR_KEY_honorsSessionLimits, val);
    getNewData();
}

void DetailsDialog::onDownloadLimitedToggled(bool val)
{
    mySession.torrentSet(myIds, TR_KEY_downloadLimited, val);
    getNewData();
}

void DetailsDialog::onSpinBoxEditingFinished()
{
    QObject const* spin = sender();
    tr_quark const key = spin->property(PREF_KEY).toInt();
    QDoubleSpinBox const* d = qobject_cast<QDoubleSpinBox const*>(spin);

    if (d != nullptr)
    {
        mySession.torrentSet(myIds, key, d->value());
    }
    else
    {
        mySession.torrentSet(myIds, key, qobject_cast<QSpinBox const*>(spin)->value());
    }

    getNewData();
}

void DetailsDialog::onUploadLimitedToggled(bool val)
{
    mySession.torrentSet(myIds, TR_KEY_uploadLimited, val);
    getNewData();
}

void DetailsDialog::onIdleModeChanged(int index)
{
    int const val = ui.idleCombo->itemData(index).toInt();
    mySession.torrentSet(myIds, TR_KEY_seedIdleMode, val);
    getNewData();
}

void DetailsDialog::onIdleLimitChanged()
{
    //: Spin box suffix, "Stop seeding if idle for: [ 5 minutes ]" (includes leading space after the number, if needed)
    QString const unitsSuffix = tr(" minute(s)", nullptr, ui.idleSpin->value());

    if (ui.idleSpin->suffix() != unitsSuffix)
    {
        ui.idleSpin->setSuffix(unitsSuffix);
    }
}

void DetailsDialog::onRatioModeChanged(int index)
{
    int const val = ui.ratioCombo->itemData(index).toInt();
    mySession.torrentSet(myIds, TR_KEY_seedRatioMode, val);
}

void DetailsDialog::onBandwidthPriorityChanged(int index)
{
    if (index != -1)
    {
        int const priority = ui.bandwidthPriorityCombo->itemData(index).toInt();
        mySession.torrentSet(myIds, TR_KEY_bandwidthPriority, priority);
        getNewData();
    }
}

void DetailsDialog::onTrackerSelectionChanged()
{
    int const selectionCount = ui.trackersView->selectionModel()->selectedRows().size();
    ui.editTrackerButton->setEnabled(selectionCount == 1);
    ui.removeTrackerButton->setEnabled(selectionCount > 0);
}

void DetailsDialog::onAddTrackerClicked()
{
    bool ok = false;
    QString const url = QInputDialog::getText(this, tr("Add URL "), tr("Add tracker announce URL:"), QLineEdit::Normal,
        QString(), &ok);

    if (!ok)
    {
        // user pressed "cancel" -- noop
    }
    else if (!QUrl(url).isValid())
    {
        QMessageBox::warning(this, tr("Error"), tr("Invalid URL \"%1\"").arg(url));
    }
    else
    {
        torrent_ids_t ids;

        for (int const id : myIds)
        {
            if (myTrackerModel->find(id, url) == -1)
            {
                ids.insert(id);
            }
        }

        if (ids.empty()) // all the torrents already have this tracker
        {
            QMessageBox::warning(this, tr("Error"), tr("Tracker already exists."));
        }
        else
        {
            QStringList urls;
            urls << url;
            mySession.torrentSet(ids, TR_KEY_trackerAdd, urls);
            getNewData();
        }
    }
}

void DetailsDialog::onEditTrackerClicked()
{
    QItemSelectionModel* selectionModel = ui.trackersView->selectionModel();
    QModelIndexList selectedRows = selectionModel->selectedRows();
    assert(selectedRows.size() == 1);
    QModelIndex i = selectionModel->currentIndex();
    TrackerInfo const trackerInfo = ui.trackersView->model()->data(i, TrackerModel::TrackerRole).value<TrackerInfo>();

    bool ok = false;
    QString const newval = QInputDialog::getText(this, tr("Edit URL "), tr("Edit tracker announce URL:"), QLineEdit::Normal,
        trackerInfo.st.announce, &ok);

    if (!ok)
    {
        // user pressed "cancel" -- noop
    }
    else if (!QUrl(newval).isValid())
    {
        QMessageBox::warning(this, tr("Error"), tr("Invalid URL \"%1\"").arg(newval));
    }
    else
    {
        torrent_ids_t ids{ trackerInfo.torrentId };

        QPair<int, QString> const idUrl = qMakePair(trackerInfo.st.id, newval);

        mySession.torrentSet(ids, TR_KEY_trackerReplace, idUrl);
        getNewData();
    }
}

void DetailsDialog::onRemoveTrackerClicked()
{
    // make a map of torrentIds to announce URLs to remove
    QItemSelectionModel* selectionModel = ui.trackersView->selectionModel();
    QModelIndexList selectedRows = selectionModel->selectedRows();
    QMap<int, int> torrentId_to_trackerIds;

    for (QModelIndex const& i : selectedRows)
    {
        TrackerInfo const inf = ui.trackersView->model()->data(i, TrackerModel::TrackerRole).value<TrackerInfo>();
        torrentId_to_trackerIds.insertMulti(inf.torrentId, inf.st.id);
    }

    // batch all of a tracker's torrents into one command
    for (int const id : torrentId_to_trackerIds.uniqueKeys())
    {
        torrent_ids_t const ids{ id };
        mySession.torrentSet(ids, TR_KEY_trackerRemove, torrentId_to_trackerIds.values(id));
    }

    selectionModel->clearSelection();
    getNewData();
}

void DetailsDialog::initOptionsTab()
{
    QString const speed_K_str = Formatter::unitStr(Formatter::SPEED, Formatter::KB);

    ui.singleDownSpin->setSuffix(QString::fromLatin1(" %1").arg(speed_K_str));
    ui.singleUpSpin->setSuffix(QString::fromLatin1(" %1").arg(speed_K_str));

    ui.singleDownSpin->setProperty(PREF_KEY, TR_KEY_downloadLimit);
    ui.singleUpSpin->setProperty(PREF_KEY, TR_KEY_uploadLimit);
    ui.ratioSpin->setProperty(PREF_KEY, TR_KEY_seedRatioLimit);
    ui.idleSpin->setProperty(PREF_KEY, TR_KEY_seedIdleLimit);
    ui.peerLimitSpin->setProperty(PREF_KEY, TR_KEY_peer_limit);

    ui.bandwidthPriorityCombo->addItem(tr("High"), TR_PRI_HIGH);
    ui.bandwidthPriorityCombo->addItem(tr("Normal"), TR_PRI_NORMAL);
    ui.bandwidthPriorityCombo->addItem(tr("Low"), TR_PRI_LOW);

    ui.ratioCombo->addItem(tr("Use Global Settings"), TR_RATIOLIMIT_GLOBAL);
    ui.ratioCombo->addItem(tr("Seed regardless of ratio"), TR_RATIOLIMIT_UNLIMITED);
    ui.ratioCombo->addItem(tr("Stop seeding at ratio:"), TR_RATIOLIMIT_SINGLE);

    ui.idleCombo->addItem(tr("Use Global Settings"), TR_IDLELIMIT_GLOBAL);
    ui.idleCombo->addItem(tr("Seed regardless of activity"), TR_IDLELIMIT_UNLIMITED);
    ui.idleCombo->addItem(tr("Stop seeding if idle for:"), TR_IDLELIMIT_SINGLE);

    ColumnResizer* cr(new ColumnResizer(this));
    cr->addLayout(ui.speedSectionLayout);
    cr->addLayout(ui.seedingLimitsSectionRatioLayout);
    cr->addLayout(ui.seedingLimitsSectionIdleLayout);
    cr->addLayout(ui.peerConnectionsSectionLayout);
    cr->update();

    void (QComboBox::* comboIndexChanged)(int) = &QComboBox::currentIndexChanged;
    void (QSpinBox::* spinValueChanged)(int) = &QSpinBox::valueChanged;
    connect(ui.bandwidthPriorityCombo, comboIndexChanged, this, &DetailsDialog::onBandwidthPriorityChanged);
    connect(ui.idleCombo, comboIndexChanged, this, &DetailsDialog::onIdleModeChanged);
    connect(ui.idleSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
    connect(ui.idleSpin, spinValueChanged, this, &DetailsDialog::onIdleLimitChanged);
    connect(ui.peerLimitSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
    connect(ui.ratioCombo, comboIndexChanged, this, &DetailsDialog::onRatioModeChanged);
    connect(ui.ratioSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
    connect(ui.sessionLimitCheck, &QCheckBox::clicked, this, &DetailsDialog::onHonorsSessionLimitsToggled);
    connect(ui.singleDownCheck, &QCheckBox::clicked, this, &DetailsDialog::onDownloadLimitedToggled);
    connect(ui.singleDownSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
    connect(ui.singleUpCheck, &QCheckBox::clicked, this, &DetailsDialog::onUploadLimitedToggled);
    connect(ui.singleUpSpin, &QSpinBox::editingFinished, this, &DetailsDialog::onSpinBoxEditingFinished);
}

/***
****
***/

void DetailsDialog::initTrackerTab()
{
    myTrackerModel = new TrackerModel();
    myTrackerFilter = new TrackerModelFilter();
    myTrackerFilter->setSourceModel(myTrackerModel);
    myTrackerDelegate = new TrackerDelegate();

    ui.trackersView->setModel(myTrackerFilter);
    ui.trackersView->setItemDelegate(myTrackerDelegate);

    ui.addTrackerButton->setIcon(getStockIcon(QLatin1String("list-add"), QStyle::SP_DialogOpenButton));
    ui.editTrackerButton->setIcon(getStockIcon(QLatin1String("document-properties"), QStyle::SP_DesktopIcon));
    ui.removeTrackerButton->setIcon(getStockIcon(QLatin1String("list-remove"), QStyle::SP_TrashIcon));

    ui.showTrackerScrapesCheck->setChecked(myPrefs.getBool(Prefs::SHOW_TRACKER_SCRAPES));
    ui.showBackupTrackersCheck->setChecked(myPrefs.getBool(Prefs::SHOW_BACKUP_TRACKERS));

    connect(ui.addTrackerButton, &QAbstractButton::clicked, this, &DetailsDialog::onAddTrackerClicked);
    connect(ui.editTrackerButton, &QAbstractButton::clicked, this, &DetailsDialog::onEditTrackerClicked);
    connect(ui.removeTrackerButton, &QAbstractButton::clicked, this, &DetailsDialog::onRemoveTrackerClicked);
    connect(ui.showBackupTrackersCheck, &QAbstractButton::clicked, this, &DetailsDialog::onShowBackupTrackersToggled);
    connect(ui.showTrackerScrapesCheck, &QAbstractButton::clicked, this, &DetailsDialog::onShowTrackerScrapesToggled);
    connect(
        ui.trackersView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        &DetailsDialog::onTrackerSelectionChanged);

    onTrackerSelectionChanged();
}

/***
****
***/

void DetailsDialog::initPeersTab()
{
    ui.peersView->setHeaderLabels({ QString(), tr("Up"), tr("Down"), tr("%"), tr("Status"), tr("Address"), tr("Client") });
    ui.peersView->sortByColumn(COL_ADDRESS, Qt::AscendingOrder);

    ui.peersView->setColumnWidth(COL_LOCK, 20);
    ui.peersView->setColumnWidth(COL_UP, measureViewItem(ui.peersView, COL_UP, QLatin1String("1024 MiB/s")));
    ui.peersView->setColumnWidth(COL_DOWN, measureViewItem(ui.peersView, COL_DOWN, QLatin1String("1024 MiB/s")));
    ui.peersView->setColumnWidth(COL_PERCENT, measureViewItem(ui.peersView, COL_PERCENT, QLatin1String("100%")));
    ui.peersView->setColumnWidth(COL_STATUS, measureViewItem(ui.peersView, COL_STATUS, QLatin1String("ODUK?EXI")));
    ui.peersView->setColumnWidth(COL_ADDRESS, measureViewItem(ui.peersView, COL_ADDRESS, QLatin1String("888.888.888.888")));
}

/***
****
***/

void DetailsDialog::initFilesTab()
{
    connect(ui.filesView, &FileTreeView::openRequested, this, &DetailsDialog::onOpenRequested);
    connect(ui.filesView, &FileTreeView::pathEdited, this, &DetailsDialog::onPathEdited);
    connect(ui.filesView, &FileTreeView::priorityChanged, this, &DetailsDialog::onFilePriorityChanged);
    connect(ui.filesView, &FileTreeView::wantedChanged, this, &DetailsDialog::onFileWantedChanged);
}

void DetailsDialog::onFilePriorityChanged(QSet<int> const& indices, int priority)
{
    tr_quark key;

    switch (priority)
    {
    case TR_PRI_LOW:
        key = TR_KEY_priority_low;
        break;

    case TR_PRI_HIGH:
        key = TR_KEY_priority_high;
        break;

    default:
        key = TR_KEY_priority_normal;
        break;
    }

    mySession.torrentSet(myIds, key, indices.toList());
    getNewData();
}

void DetailsDialog::onFileWantedChanged(QSet<int> const& indices, bool wanted)
{
    tr_quark const key = wanted ? TR_KEY_files_wanted : TR_KEY_files_unwanted;
    mySession.torrentSet(myIds, key, indices.toList());
    getNewData();
}

void DetailsDialog::onPathEdited(QString const& oldpath, QString const& newname)
{
    mySession.torrentRenamePath(myIds, oldpath, newname);
}

void DetailsDialog::onOpenRequested(QString const& path)
{
    if (!mySession.isLocal())
    {
        return;
    }

    for (int const id : myIds)
    {
        Torrent const* const tor = myModel.getTorrentFromId(id);

        if (tor == nullptr)
        {
            continue;
        }

        QString const localFilePath = tor->getPath() + QLatin1Char('/') + path;

        if (!QFile::exists(localFilePath))
        {
            continue;
        }

        if (QDesktopServices::openUrl(QUrl::fromLocalFile(localFilePath)))
        {
            break;
        }
    }
}
