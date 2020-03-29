/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cassert>
#include <iostream>

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStyle>
#include <QTextStream>

#include <libtransmission/transmission.h>
#include <libtransmission/session-id.h>
#include <libtransmission/utils.h> // tr_free
#include <libtransmission/variant.h>

#include "AddData.h"
#include "Prefs.h"
#include "RpcQueue.h"
#include "Session.h"
#include "SessionDialog.h"
#include "Torrent.h"
#include "Utils.h"

/***
****
***/

namespace
{

typedef Torrent::KeyList KeyList;

void addList(tr_variant* list, KeyList const& keys)
{
    tr_variantListReserve(list, keys.size());

    for (tr_quark const key : keys)
    {
        tr_variantListAddQuark(list, key);
    }
}

// If this object is passed as "ids" (compared by address), then recently active torrents are queried.
auto const recentlyActiveIds = torrent_ids_t{ -1 };

// If this object is passed as "ids" (compared by being empty), then all torrents are queried.
auto const allIds = torrent_ids_t{};

} // namespace

void Session::sessionSet(tr_quark const key, QVariant const& value)
{
    tr_variant args;
    tr_variantInitDict(&args, 1);

    switch (value.type())
    {
    case QVariant::Bool:
        tr_variantDictAddBool(&args, key, value.toBool());
        break;

    case QVariant::Int:
        tr_variantDictAddInt(&args, key, value.toInt());
        break;

    case QVariant::Double:
        tr_variantDictAddReal(&args, key, value.toDouble());
        break;

    case QVariant::String:
        tr_variantDictAddStr(&args, key, value.toString().toUtf8().constData());
        break;

    default:
        assert(false);
    }

    exec("session-set", &args);
}

void Session::portTest()
{
    RpcQueue* q = new RpcQueue();

    q->add([this]()
        {
            return exec("port-test", nullptr);
        });

    q->add([this](RpcResponse const& r)
        {
            bool isOpen = false;

            if (r.success)
            {
                (void)tr_variantDictFindBool(r.args.get(), TR_KEY_port_is_open, &isOpen);
            }

            emit portTested(isOpen);
        });

    q->run();
}

void Session::copyMagnetLinkToClipboard(int torrentId)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    tr_variantListAddInt(tr_variantDictAddList(&args, TR_KEY_ids, 1), torrentId);
    tr_variantListAddStr(tr_variantDictAddList(&args, TR_KEY_fields, 1), "magnetLink");

    RpcQueue* q = new RpcQueue();

    q->add([this, &args]()
        {
            return exec(TR_KEY_torrent_get, &args);
        });

    q->add([](RpcResponse const& r)
        {
            tr_variant* torrents;

            if (!tr_variantDictFindList(r.args.get(), TR_KEY_torrents, &torrents))
            {
                return;
            }

            tr_variant* const child = tr_variantListChild(torrents, 0);
            char const* str;

            if (child != nullptr && tr_variantDictFindStr(child, TR_KEY_magnetLink, &str, nullptr))
            {
                qApp->clipboard()->setText(QString::fromUtf8(str));
            }
        });

    q->run();
}

void Session::updatePref(int key)
{
    if (myPrefs.isCore(key))
    {
        switch (key)
        {
        case Prefs::ALT_SPEED_LIMIT_DOWN:
        case Prefs::ALT_SPEED_LIMIT_ENABLED:
        case Prefs::ALT_SPEED_LIMIT_TIME_BEGIN:
        case Prefs::ALT_SPEED_LIMIT_TIME_DAY:
        case Prefs::ALT_SPEED_LIMIT_TIME_ENABLED:
        case Prefs::ALT_SPEED_LIMIT_TIME_END:
        case Prefs::ALT_SPEED_LIMIT_UP:
        case Prefs::BLOCKLIST_DATE:
        case Prefs::BLOCKLIST_ENABLED:
        case Prefs::BLOCKLIST_URL:
        case Prefs::DHT_ENABLED:
        case Prefs::DOWNLOAD_QUEUE_ENABLED:
        case Prefs::DOWNLOAD_QUEUE_SIZE:
        case Prefs::DSPEED:
        case Prefs::DSPEED_ENABLED:
        case Prefs::IDLE_LIMIT:
        case Prefs::IDLE_LIMIT_ENABLED:
        case Prefs::INCOMPLETE_DIR:
        case Prefs::INCOMPLETE_DIR_ENABLED:
        case Prefs::LPD_ENABLED:
        case Prefs::PEER_LIMIT_GLOBAL:
        case Prefs::PEER_LIMIT_TORRENT:
        case Prefs::PEER_PORT:
        case Prefs::PEER_PORT_RANDOM_ON_START:
        case Prefs::QUEUE_STALLED_MINUTES:
        case Prefs::PEX_ENABLED:
        case Prefs::PORT_FORWARDING:
        case Prefs::RENAME_PARTIAL_FILES:
        case Prefs::SCRIPT_TORRENT_DONE_ENABLED:
        case Prefs::SCRIPT_TORRENT_DONE_FILENAME:
        case Prefs::START:
        case Prefs::TRASH_ORIGINAL:
        case Prefs::USPEED:
        case Prefs::USPEED_ENABLED:
        case Prefs::UTP_ENABLED:
            sessionSet(myPrefs.getKey(key), myPrefs.variant(key));
            break;

        case Prefs::DOWNLOAD_DIR:
            sessionSet(myPrefs.getKey(key), myPrefs.variant(key));
            /* this will change the 'freespace' argument, so refresh */
            refreshSessionInfo();
            break;

        case Prefs::RATIO:
            sessionSet(TR_KEY_seedRatioLimit, myPrefs.variant(key));
            break;

        case Prefs::RATIO_ENABLED:
            sessionSet(TR_KEY_seedRatioLimited, myPrefs.variant(key));
            break;

        case Prefs::ENCRYPTION:
            {
                int const i = myPrefs.variant(key).toInt();

                switch (i)
                {
                case 0:
                    sessionSet(myPrefs.getKey(key), QLatin1String("tolerated"));
                    break;

                case 1:
                    sessionSet(myPrefs.getKey(key), QLatin1String("preferred"));
                    break;

                case 2:
                    sessionSet(myPrefs.getKey(key), QLatin1String("required"));
                    break;
                }

                break;
            }

        case Prefs::RPC_AUTH_REQUIRED:
            if (mySession != nullptr)
            {
                tr_sessionSetRPCPasswordEnabled(mySession, myPrefs.getBool(key));
            }

            break;

        case Prefs::RPC_ENABLED:
            if (mySession != nullptr)
            {
                tr_sessionSetRPCEnabled(mySession, myPrefs.getBool(key));
            }

            break;

        case Prefs::RPC_PASSWORD:
            if (mySession != nullptr)
            {
                tr_sessionSetRPCPassword(mySession, myPrefs.getString(key).toUtf8().constData());
            }

            break;

        case Prefs::RPC_PORT:
            if (mySession != nullptr)
            {
                tr_sessionSetRPCPort(mySession, myPrefs.getInt(key));
            }

            break;

        case Prefs::RPC_USERNAME:
            if (mySession != nullptr)
            {
                tr_sessionSetRPCUsername(mySession, myPrefs.getString(key).toUtf8().constData());
            }

            break;

        case Prefs::RPC_WHITELIST_ENABLED:
            if (mySession != nullptr)
            {
                tr_sessionSetRPCWhitelistEnabled(mySession, myPrefs.getBool(key));
            }

            break;

        case Prefs::RPC_WHITELIST:
            if (mySession != nullptr)
            {
                tr_sessionSetRPCWhitelist(mySession, myPrefs.getString(key).toUtf8().constData());
            }

            break;

        default:
            std::cerr << "unhandled pref: " << key << std::endl;
        }
    }
}

/***
****
***/

Session::Session(QString const& configDir, Prefs& prefs) :
    myConfigDir(configDir),
    myPrefs(prefs),
    myBlocklistSize(-1),
    mySession(nullptr),
    myIsDefinitelyLocalSession(true)
{
    myStats.ratio = TR_RATIO_NA;
    myStats.uploadedBytes = 0;
    myStats.downloadedBytes = 0;
    myStats.filesAdded = 0;
    myStats.sessionCount = 0;
    myStats.secondsActive = 0;
    myCumulativeStats = myStats;

    connect(&myPrefs, SIGNAL(changed(int)), this, SLOT(updatePref(int)));
    connect(&myRpc, SIGNAL(httpAuthenticationRequired()), this, SIGNAL(httpAuthenticationRequired()));
    connect(&myRpc, SIGNAL(dataReadProgress()), this, SIGNAL(dataReadProgress()));
    connect(&myRpc, SIGNAL(dataSendProgress()), this, SIGNAL(dataSendProgress()));
    connect(&myRpc, SIGNAL(networkResponse(QNetworkReply::NetworkError, QString)), this,
        SIGNAL(networkResponse(QNetworkReply::NetworkError, QString)));
}

Session::~Session()
{
    stop();
}

/***
****
***/

void Session::stop()
{
    myRpc.stop();

    if (mySession != nullptr)
    {
        tr_sessionClose(mySession);
        mySession = nullptr;
    }
}

void Session::restart()
{
    stop();
    start();
}

void Session::start()
{
    if (myPrefs.get<bool>(Prefs::SESSION_IS_REMOTE))
    {
        QUrl url;
        url.setScheme(QLatin1String("http"));
        url.setHost(myPrefs.get<QString>(Prefs::SESSION_REMOTE_HOST));
        url.setPort(myPrefs.get<int>(Prefs::SESSION_REMOTE_PORT));
        url.setPath(QLatin1String("/transmission/rpc"));

        if (myPrefs.get<bool>(Prefs::SESSION_REMOTE_AUTH))
        {
            url.setUserName(myPrefs.get<QString>(Prefs::SESSION_REMOTE_USERNAME));
            url.setPassword(myPrefs.get<QString>(Prefs::SESSION_REMOTE_PASSWORD));
        }

        myRpc.start(url);
    }
    else
    {
        tr_variant settings;
        tr_variantInitDict(&settings, 0);
        tr_sessionLoadSettings(&settings, myConfigDir.toUtf8().constData(), "qt");
        mySession = tr_sessionInit(myConfigDir.toUtf8().constData(), true, &settings);
        tr_variantFree(&settings);

        myRpc.start(mySession);

        tr_ctor* ctor = tr_ctorNew(mySession);
        int torrentCount;
        tr_torrent** torrents = tr_sessionLoadTorrents(mySession, ctor, &torrentCount);
        tr_free(torrents);
        tr_ctorFree(ctor);
    }

    emit sourceChanged();
}

bool Session::isServer() const
{
    return mySession != nullptr;
}

bool Session::isLocal() const
{
    if (!mySessionId.isEmpty())
    {
        return myIsDefinitelyLocalSession;
    }

    return myRpc.isLocal();
}

/***
****
***/

namespace
{

void addOptionalIds(tr_variant* args, torrent_ids_t const& ids)
{
    if (&ids == &recentlyActiveIds)
    {
        tr_variantDictAddStr(args, TR_KEY_ids, "recently-active");
    }
    else if (!ids.empty())
    {
        tr_variant* idList(tr_variantDictAddList(args, TR_KEY_ids, ids.size()));

        for (int const i : ids)
        {
            tr_variantListAddInt(idList, i);
        }
    }
}

} // namespace

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, double value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    tr_variantDictAddReal(&args, key, value);
    addOptionalIds(&args, ids);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, int value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    tr_variantDictAddInt(&args, key, value);
    addOptionalIds(&args, ids);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, bool value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    tr_variantDictAddBool(&args, key, value);
    addOptionalIds(&args, ids);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, QStringList const& value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    tr_variant* list(tr_variantDictAddList(&args, key, value.size()));

    for (QString const& str : value)
    {
        tr_variantListAddStr(list, str.toUtf8().constData());
    }

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, QList<int> const& value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    tr_variant* list(tr_variantDictAddList(&args, key, value.size()));

    for (int const i : value)
    {
        tr_variantListAddInt(list, i);
    }

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, QPair<int, QString> const& value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    tr_variant* list(tr_variantDictAddList(&args, key, 2));
    tr_variantListAddInt(list, value.first);
    tr_variantListAddStr(list, value.second.toUtf8().constData());

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSetLocation(torrent_ids_t const& ids, QString const& location, bool doMove)
{
    tr_variant args;
    tr_variantInitDict(&args, 3);
    addOptionalIds(&args, ids);
    tr_variantDictAddStr(&args, TR_KEY_location, location.toUtf8().constData());
    tr_variantDictAddBool(&args, TR_KEY_move, doMove);

    exec(TR_KEY_torrent_set_location, &args);
}

void Session::torrentRenamePath(torrent_ids_t const& ids, QString const& oldpath, QString const& newname)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    tr_variantDictAddStr(&args, TR_KEY_path, oldpath.toUtf8().constData());
    tr_variantDictAddStr(&args, TR_KEY_name, newname.toUtf8().constData());

    RpcQueue* q = new RpcQueue();

    q->add([this, &args]()
        {
            return exec("torrent-rename-path", &args);
        },
        [](RpcResponse const& r)
        {
            char const* path = "(unknown)";
            char const* name = "(unknown)";
            tr_variantDictFindStr(r.args.get(), TR_KEY_path, &path, nullptr);
            tr_variantDictFindStr(r.args.get(), TR_KEY_name, &name, nullptr);

            QMessageBox* d = new QMessageBox(QMessageBox::Information, tr("Error Renaming Path"),
                tr("<p><b>Unable to rename \"%1\" as \"%2\": %3.</b></p><p>Please correct the errors and try again.</p>").
                    arg(QString::fromUtf8(path)).arg(QString::fromUtf8(name)).arg(r.result), QMessageBox::Close,
                qApp->activeWindow());
            QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
            d->show();
        });

    q->add([this, ids](RpcResponse const& /*r*/)
        {
            refreshTorrents(ids, { TR_KEY_fileStats, TR_KEY_files, TR_KEY_id, TR_KEY_name });
        });

    q->run();
}

void Session::refreshTorrents(torrent_ids_t const& ids, KeyList const& keys)
{
    tr_variant args;
    tr_variantInitDict(&args, 3);
    tr_variantDictAddStr(&args, TR_KEY_format, "table");
    addList(tr_variantDictAddList(&args, TR_KEY_fields, 0), keys);
    addOptionalIds(&args, ids);

    RpcQueue* q = new RpcQueue();

    q->add([this, &args]()
        {
            return exec(TR_KEY_torrent_get, &args);
        });

    bool const allTorrents = ids.empty();

    q->add([this, allTorrents](RpcResponse const& r)
        {
            tr_variant* torrents;

            if (tr_variantDictFindList(r.args.get(), TR_KEY_torrents, &torrents))
            {
                emit torrentsUpdated(torrents, allTorrents);
            }

            if (tr_variantDictFindList(r.args.get(), TR_KEY_removed, &torrents))
            {
                emit torrentsRemoved(torrents);
            }
        });

    q->run();
}

void Session::refreshDetailInfo(torrent_ids_t const& ids)
{
    refreshTorrents(ids, Torrent::detailInfoKeys);
}

void Session::refreshExtraStats(torrent_ids_t const& ids)
{
    refreshTorrents(ids, Torrent::mainStatKeys + Torrent::detailStatKeys);
}

void Session::sendTorrentRequest(char const* request, torrent_ids_t const& ids)
{
    tr_variant args;
    tr_variantInitDict(&args, 1);
    addOptionalIds(&args, ids);

    RpcQueue* q = new RpcQueue();

    q->add([this, request, &args]()
        {
            return exec(request, &args);
        });

    q->add([this, ids]()
        {
            refreshTorrents(ids, Torrent::mainStatKeys);
        });

    q->run();
}

void Session::pauseTorrents(torrent_ids_t const& ids)
{
    sendTorrentRequest("torrent-stop", ids);
}

void Session::startTorrents(torrent_ids_t const& ids)
{
    sendTorrentRequest("torrent-start", ids);
}

void Session::startTorrentsNow(torrent_ids_t const& ids)
{
    sendTorrentRequest("torrent-start-now", ids);
}

void Session::queueMoveTop(torrent_ids_t const& ids)
{
    sendTorrentRequest("queue-move-top", ids);
}

void Session::queueMoveUp(torrent_ids_t const& ids)
{
    sendTorrentRequest("queue-move-up", ids);
}

void Session::queueMoveDown(torrent_ids_t const& ids)
{
    sendTorrentRequest("queue-move-down", ids);
}

void Session::queueMoveBottom(torrent_ids_t const& ids)
{
    sendTorrentRequest("queue-move-bottom", ids);
}

void Session::refreshActiveTorrents()
{
    refreshTorrents(recentlyActiveIds, Torrent::mainStatKeys);
}

void Session::refreshAllTorrents()
{
    refreshTorrents(allIds, Torrent::mainStatKeys);
}

void Session::initTorrents(torrent_ids_t const& ids)
{
    refreshTorrents(ids, Torrent::allMainKeys);
}

void Session::refreshSessionStats()
{
    RpcQueue* q = new RpcQueue();

    q->add([this]()
        {
            return exec("session-stats", nullptr);
        });

    q->add([this](RpcResponse const& r)
        {
            updateStats(r.args.get());
        });

    q->run();
}

void Session::refreshSessionInfo()
{
    RpcQueue* q = new RpcQueue();

    q->add([this]()
        {
            return exec("session-get", nullptr);
        });

    q->add([this](RpcResponse const& r)
        {
            updateInfo(r.args.get());
        });

    q->run();
}

void Session::updateBlocklist()
{
    RpcQueue* q = new RpcQueue();

    q->add([this]()
        {
            return exec("blocklist-update", nullptr);
        });

    q->add([this](RpcResponse const& r)
        {
            int64_t blocklistSize;

            if (tr_variantDictFindInt(r.args.get(), TR_KEY_blocklist_size, &blocklistSize))
            {
                setBlocklistSize(blocklistSize);
            }
        });

    q->run();
}

/***
****
***/

RpcResponseFuture Session::exec(tr_quark method, tr_variant* args)
{
    return myRpc.exec(method, args);
}

RpcResponseFuture Session::exec(char const* method, tr_variant* args)
{
    return myRpc.exec(method, args);
}

void Session::updateStats(tr_variant* d, tr_session_stats* stats)
{
    int64_t i;

    if (tr_variantDictFindInt(d, TR_KEY_uploadedBytes, &i))
    {
        stats->uploadedBytes = i;
    }

    if (tr_variantDictFindInt(d, TR_KEY_downloadedBytes, &i))
    {
        stats->downloadedBytes = i;
    }

    if (tr_variantDictFindInt(d, TR_KEY_filesAdded, &i))
    {
        stats->filesAdded = i;
    }

    if (tr_variantDictFindInt(d, TR_KEY_sessionCount, &i))
    {
        stats->sessionCount = i;
    }

    if (tr_variantDictFindInt(d, TR_KEY_secondsActive, &i))
    {
        stats->secondsActive = i;
    }

    stats->ratio = tr_getRatio(stats->uploadedBytes, stats->downloadedBytes);
}

void Session::updateStats(tr_variant* d)
{
    tr_variant* c;

    if (tr_variantDictFindDict(d, TR_KEY_current_stats, &c))
    {
        updateStats(c, &myStats);
    }

    if (tr_variantDictFindDict(d, TR_KEY_cumulative_stats, &c))
    {
        updateStats(c, &myCumulativeStats);
    }

    emit statsUpdated();
}

void Session::updateInfo(tr_variant* d)
{
    int64_t i;
    char const* str;

    disconnect(&myPrefs, SIGNAL(changed(int)), this, SLOT(updatePref(int)));

    for (int i = Prefs::FIRST_CORE_PREF; i <= Prefs::LAST_CORE_PREF; ++i)
    {
        tr_variant const* b(tr_variantDictFind(d, myPrefs.getKey(i)));

        if (b == nullptr)
        {
            continue;
        }

        if (i == Prefs::ENCRYPTION)
        {
            char const* val;

            if (tr_variantGetStr(b, &val, nullptr))
            {
                if (qstrcmp(val, "required") == 0)
                {
                    myPrefs.set(i, 2);
                }
                else if (qstrcmp(val, "preferred") == 0)
                {
                    myPrefs.set(i, 1);
                }
                else if (qstrcmp(val, "tolerated") == 0)
                {
                    myPrefs.set(i, 0);
                }
            }

            continue;
        }

        switch (myPrefs.type(i))
        {
        case QVariant::Int:
            {
                int64_t val;

                if (tr_variantGetInt(b, &val))
                {
                    myPrefs.set(i, static_cast<int>(val));
                }

                break;
            }

        case QVariant::Double:
            {
                double val;

                if (tr_variantGetReal(b, &val))
                {
                    myPrefs.set(i, val);
                }

                break;
            }

        case QVariant::Bool:
            {
                bool val;

                if (tr_variantGetBool(b, &val))
                {
                    myPrefs.set(i, val);
                }

                break;
            }

        case CustomVariantType::FilterModeType:
        case CustomVariantType::SortModeType:
        case QVariant::String:
            {
                char const* val;

                if (tr_variantGetStr(b, &val, nullptr))
                {
                    myPrefs.set(i, QString::fromUtf8(val));
                }

                break;
            }

        default:
            break;
        }
    }

    bool b;
    double x;

    if (tr_variantDictFindBool(d, TR_KEY_seedRatioLimited, &b))
    {
        myPrefs.set(Prefs::RATIO_ENABLED, b);
    }

    if (tr_variantDictFindReal(d, TR_KEY_seedRatioLimit, &x))
    {
        myPrefs.set(Prefs::RATIO, x);
    }

    /* Use the C API to get settings that, for security reasons, aren't supported by RPC */
    if (mySession != nullptr)
    {
        myPrefs.set(Prefs::RPC_ENABLED, tr_sessionIsRPCEnabled(mySession));
        myPrefs.set(Prefs::RPC_AUTH_REQUIRED, tr_sessionIsRPCPasswordEnabled(mySession));
        myPrefs.set(Prefs::RPC_PASSWORD, QString::fromUtf8(tr_sessionGetRPCPassword(mySession)));
        myPrefs.set(Prefs::RPC_PORT, tr_sessionGetRPCPort(mySession));
        myPrefs.set(Prefs::RPC_USERNAME, QString::fromUtf8(tr_sessionGetRPCUsername(mySession)));
        myPrefs.set(Prefs::RPC_WHITELIST_ENABLED, tr_sessionGetRPCWhitelistEnabled(mySession));
        myPrefs.set(Prefs::RPC_WHITELIST, QString::fromUtf8(tr_sessionGetRPCWhitelist(mySession)));
    }

    if (tr_variantDictFindInt(d, TR_KEY_blocklist_size, &i) && i != blocklistSize())
    {
        setBlocklistSize(i);
    }

    if (tr_variantDictFindStr(d, TR_KEY_version, &str, nullptr) && mySessionVersion != QString::fromUtf8(str))
    {
        mySessionVersion = QString::fromUtf8(str);
    }

    if (tr_variantDictFindStr(d, TR_KEY_session_id, &str, nullptr))
    {
        QString const sessionId = QString::fromUtf8(str);

        if (mySessionId != sessionId)
        {
            mySessionId = sessionId;
            myIsDefinitelyLocalSession = tr_session_id_is_local(str);
        }
    }
    else
    {
        mySessionId.clear();
    }

    // std::cerr << "Session::updateInfo end" << std::endl;
    connect(&myPrefs, SIGNAL(changed(int)), this, SLOT(updatePref(int)));

    emit sessionUpdated();
}

void Session::setBlocklistSize(int64_t i)
{
    myBlocklistSize = i;

    emit blocklistUpdated(i);
}

void Session::addTorrent(AddData const& addMe, tr_variant* args, bool trashOriginal)
{
    assert(tr_variantDictFind(args, TR_KEY_filename) == nullptr);
    assert(tr_variantDictFind(args, TR_KEY_metainfo) == nullptr);

    if (tr_variantDictFind(args, TR_KEY_paused) == nullptr)
    {
        tr_variantDictAddBool(args, TR_KEY_paused, !myPrefs.getBool(Prefs::START));
    }

    switch (addMe.type)
    {
    case AddData::MAGNET:
        tr_variantDictAddStr(args, TR_KEY_filename, addMe.magnet.toUtf8().constData());
        break;

    case AddData::URL:
        tr_variantDictAddStr(args, TR_KEY_filename, addMe.url.toString().toUtf8().constData());
        break;

    case AddData::FILENAME: /* fall-through */
    case AddData::METAINFO:
        {
            QByteArray const b64 = addMe.toBase64();
            tr_variantDictAddRaw(args, TR_KEY_metainfo, b64.constData(), b64.size());
            break;
        }

    default:
        qWarning() << "Unhandled AddData type: " << addMe.type;
        break;
    }

    RpcQueue* q = new RpcQueue();

    q->add([this, args]()
        {
            return exec("torrent-add", args);
        },
        [addMe](RpcResponse const& r)
        {
            QMessageBox* d = new QMessageBox(QMessageBox::Warning, tr("Error Adding Torrent"),
                QString::fromLatin1("<p><b>%1</b></p><p>%2</p>").arg(r.result).arg(addMe.readableName()), QMessageBox::Close,
                qApp->activeWindow());
            QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
            d->show();
        });

    q->add([addMe](RpcResponse const& r)
        {
            tr_variant* dup;

            if (!tr_variantDictFindDict(r.args.get(), TR_KEY_torrent_duplicate, &dup))
            {
                return;
            }

            char const* str;

            if (tr_variantDictFindStr(dup, TR_KEY_name, &str, nullptr))
            {
                QString const name = QString::fromUtf8(str);
                QMessageBox* d = new QMessageBox(QMessageBox::Warning, tr("Add Torrent"),
                    tr("<p><b>Unable to add \"%1\".</b></p><p>It is a duplicate of \"%2\" which is already added.</p>").
                        arg(addMe.readableShortName()).arg(name), QMessageBox::Close, qApp->activeWindow());
                QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
                d->show();
            }
        });

    if (trashOriginal && addMe.type == AddData::FILENAME)
    {
        q->add([addMe]()
            {
                QFile original(addMe.filename);
                original.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
                original.remove();
            });
    }

    q->run();
}

void Session::addTorrent(AddData const& addMe)
{
    tr_variant args;
    tr_variantInitDict(&args, 3);

    addTorrent(addMe, &args, myPrefs.getBool(Prefs::TRASH_ORIGINAL));
}

void Session::addNewlyCreatedTorrent(QString const& filename, QString const& localPath)
{
    QByteArray const b64 = AddData(filename).toBase64();

    tr_variant args;
    tr_variantInitDict(&args, 3);
    tr_variantDictAddStr(&args, TR_KEY_download_dir, localPath.toUtf8().constData());
    tr_variantDictAddBool(&args, TR_KEY_paused, !myPrefs.getBool(Prefs::START));
    tr_variantDictAddRaw(&args, TR_KEY_metainfo, b64.constData(), b64.size());

    exec("torrent-add", &args);
}

void Session::removeTorrents(torrent_ids_t const& ids, bool deleteFiles)
{
    if (!ids.empty())
    {
        tr_variant args;
        tr_variantInitDict(&args, 2);
        addOptionalIds(&args, ids);
        tr_variantDictAddInt(&args, TR_KEY_delete_local_data, deleteFiles);

        exec("torrent-remove", &args);
    }
}

void Session::verifyTorrents(torrent_ids_t const& ids)
{
    if (!ids.empty())
    {
        tr_variant args;
        tr_variantInitDict(&args, 1);
        addOptionalIds(&args, ids);

        exec("torrent-verify", &args);
    }
}

void Session::reannounceTorrents(torrent_ids_t const& ids)
{
    if (!ids.empty())
    {
        tr_variant args;
        tr_variantInitDict(&args, 1);
        addOptionalIds(&args, ids);

        exec("torrent-reannounce", &args);
    }
}

/***
****
***/

void Session::launchWebInterface()
{
    QUrl url;

    if (mySession == nullptr) // remote session
    {
        url = myRpc.url();
        url.setPath(QLatin1String("/transmission/web/"));
    }
    else // local session
    {
        url.setScheme(QLatin1String("http"));
        url.setHost(QLatin1String("localhost"));
        url.setPort(myPrefs.getInt(Prefs::RPC_PORT));
    }

    QDesktopServices::openUrl(url);
}
