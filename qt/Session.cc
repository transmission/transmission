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
#include "CustomVariantType.h"
#include "Prefs.h"
#include "RpcQueue.h"
#include "Session.h"
#include "SessionDialog.h"
#include "Torrent.h"
#include "Utils.h"
#include "VariantHelpers.h"

using ::trqt::variant_helpers::dictAdd;
using ::trqt::variant_helpers::dictFind;
using ::trqt::variant_helpers::getValue;
using ::trqt::variant_helpers::listAdd;

/***
****
***/

namespace
{

using KeyList = Torrent::KeyList;

// If this object is passed as "ids" (compared by address), then recently active torrents are queried.
auto const RecentlyActiveIDs = torrent_ids_t{ -1 };

// If this object is passed as "ids" (compared by being empty), then all torrents are queried.
auto const AllIDs = torrent_ids_t{};

} // namespace

void Session::sessionSet(tr_quark const key, QVariant const& value)
{
    tr_variant args;
    tr_variantInitDict(&args, 1);

    switch (value.type())
    {
    case QVariant::Bool:
        dictAdd(&args, key, value.toBool());
        break;

    case QVariant::Int:
        dictAdd(&args, key, value.toInt());
        break;

    case QVariant::Double:
        dictAdd(&args, key, value.toDouble());
        break;

    case QVariant::String:
        dictAdd(&args, key, value.toString());
        break;

    default:
        assert(false);
    }

    exec("session-set", &args);
}

void Session::portTest()
{
    auto* q = new RpcQueue();

    q->add([this]()
        {
            return exec("port-test", nullptr);
        });

    q->add([this](RpcResponse const& r)
        {
            bool is_open = false;

            if (r.success)
            {
                auto const value = dictFind<bool>(r.args.get(), TR_KEY_port_is_open);
                if (value)
                {
                    is_open = *value;
                }
            }

            emit portTested(is_open);
        });

    q->run();
}

void Session::copyMagnetLinkToClipboard(int torrent_id)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    dictAdd(&args, TR_KEY_ids, std::array<int, 1>{ torrent_id });
    dictAdd(&args, TR_KEY_fields, std::array<std::string_view, 1>{ "magnetLink" });

    auto* q = new RpcQueue();

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
            if (child != nullptr)
            {
                auto const link = dictFind<QString>(child, TR_KEY_magnetLink);
                if (link)
                {
                    qApp->clipboard()->setText(*link);
                }
            }
        });

    q->run();
}

void Session::updatePref(int key)
{
    if (prefs_.isCore(key))
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
            sessionSet(prefs_.getKey(key), prefs_.variant(key));
            break;

        case Prefs::DOWNLOAD_DIR:
            sessionSet(prefs_.getKey(key), prefs_.variant(key));
            /* this will change the 'freespace' argument, so refresh */
            refreshSessionInfo();
            break;

        case Prefs::RATIO:
            sessionSet(TR_KEY_seedRatioLimit, prefs_.variant(key));
            break;

        case Prefs::RATIO_ENABLED:
            sessionSet(TR_KEY_seedRatioLimited, prefs_.variant(key));
            break;

        case Prefs::ENCRYPTION:
            {
                int const i = prefs_.variant(key).toInt();

                switch (i)
                {
                case 0:
                    sessionSet(prefs_.getKey(key), QStringLiteral("tolerated"));
                    break;

                case 1:
                    sessionSet(prefs_.getKey(key), QStringLiteral("preferred"));
                    break;

                case 2:
                    sessionSet(prefs_.getKey(key), QStringLiteral("required"));
                    break;
                }

                break;
            }

        case Prefs::RPC_AUTH_REQUIRED:
            if (session_ != nullptr)
            {
                tr_sessionSetRPCPasswordEnabled(session_, prefs_.getBool(key));
            }

            break;

        case Prefs::RPC_ENABLED:
            if (session_ != nullptr)
            {
                tr_sessionSetRPCEnabled(session_, prefs_.getBool(key));
            }

            break;

        case Prefs::RPC_PASSWORD:
            if (session_ != nullptr)
            {
                tr_sessionSetRPCPassword(session_, prefs_.getString(key).toUtf8().constData());
            }

            break;

        case Prefs::RPC_PORT:
            if (session_ != nullptr)
            {
                tr_sessionSetRPCPort(session_, prefs_.getInt(key));
            }

            break;

        case Prefs::RPC_USERNAME:
            if (session_ != nullptr)
            {
                tr_sessionSetRPCUsername(session_, prefs_.getString(key).toUtf8().constData());
            }

            break;

        case Prefs::RPC_WHITELIST_ENABLED:
            if (session_ != nullptr)
            {
                tr_sessionSetRPCWhitelistEnabled(session_, prefs_.getBool(key));
            }

            break;

        case Prefs::RPC_WHITELIST:
            if (session_ != nullptr)
            {
                tr_sessionSetRPCWhitelist(session_, prefs_.getString(key).toUtf8().constData());
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

Session::Session(QString config_dir, Prefs& prefs) :
    config_dir_(std::move(config_dir)),
    prefs_(prefs)
{
    stats_.ratio = TR_RATIO_NA;
    stats_.uploadedBytes = 0;
    stats_.downloadedBytes = 0;
    stats_.filesAdded = 0;
    stats_.sessionCount = 0;
    stats_.secondsActive = 0;
    cumulative_stats_ = stats_;

    connect(&prefs_, SIGNAL(changed(int)), this, SLOT(updatePref(int)));
    connect(&rpc_, SIGNAL(httpAuthenticationRequired()), this, SIGNAL(httpAuthenticationRequired()));
    connect(&rpc_, SIGNAL(dataReadProgress()), this, SIGNAL(dataReadProgress()));
    connect(&rpc_, SIGNAL(dataSendProgress()), this, SIGNAL(dataSendProgress()));
    connect(&rpc_, SIGNAL(networkResponse(QNetworkReply::NetworkError, QString)), this,
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
    rpc_.stop();

    if (session_ != nullptr)
    {
        tr_sessionClose(session_);
        session_ = nullptr;
    }
}

void Session::restart()
{
    stop();
    start();
}

void Session::start()
{
    if (prefs_.get<bool>(Prefs::SESSION_IS_REMOTE))
    {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(prefs_.get<QString>(Prefs::SESSION_REMOTE_HOST));
        url.setPort(prefs_.get<int>(Prefs::SESSION_REMOTE_PORT));
        url.setPath(QStringLiteral("/transmission/rpc"));

        if (prefs_.get<bool>(Prefs::SESSION_REMOTE_AUTH))
        {
            url.setUserName(prefs_.get<QString>(Prefs::SESSION_REMOTE_USERNAME));
            url.setPassword(prefs_.get<QString>(Prefs::SESSION_REMOTE_PASSWORD));
        }

        rpc_.start(url);
    }
    else
    {
        tr_variant settings;
        tr_variantInitDict(&settings, 0);
        tr_sessionLoadSettings(&settings, config_dir_.toUtf8().constData(), "qt");
        session_ = tr_sessionInit(config_dir_.toUtf8().constData(), true, &settings);
        tr_variantFree(&settings);

        rpc_.start(session_);

        tr_ctor* ctor = tr_ctorNew(session_);
        int torrent_count;
        tr_torrent** torrents = tr_sessionLoadTorrents(session_, ctor, &torrent_count);
        tr_free(torrents);
        tr_ctorFree(ctor);
    }

    emit sourceChanged();
}

bool Session::isServer() const
{
    return session_ != nullptr;
}

bool Session::isLocal() const
{
    if (!session_id_.isEmpty())
    {
        return is_definitely_local_session_;
    }

    return rpc_.isLocal();
}

/***
****
***/

namespace
{

void addOptionalIds(tr_variant* args, torrent_ids_t const& ids)
{
    if (&ids == &RecentlyActiveIDs)
    {
        dictAdd(args, TR_KEY_ids, "recently-active");
    }
    else if (!ids.empty())
    {
        dictAdd(args, TR_KEY_ids, ids);
    }
}

} // namespace

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, double value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    dictAdd(&args, key, value);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, int value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    dictAdd(&args, key, value);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, bool value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    dictAdd(&args, key, value);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, QStringList const& value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    dictAdd(&args, key, value);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, QList<int> const& value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    dictAdd(&args, key, value);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSet(torrent_ids_t const& ids, tr_quark const key, QPair<int, QString> const& value)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    tr_variant* list(tr_variantDictAddList(&args, key, 2));
    listAdd(list, value.first);
    listAdd(list, value.second);

    exec(TR_KEY_torrent_set, &args);
}

void Session::torrentSetLocation(torrent_ids_t const& ids, QString const& location, bool do_move)
{
    tr_variant args;
    tr_variantInitDict(&args, 3);
    addOptionalIds(&args, ids);
    dictAdd(&args, TR_KEY_location, location);
    dictAdd(&args, TR_KEY_move, do_move);

    exec(TR_KEY_torrent_set_location, &args);
}

void Session::torrentRenamePath(torrent_ids_t const& ids, QString const& oldpath, QString const& newname)
{
    tr_variant args;
    tr_variantInitDict(&args, 2);
    addOptionalIds(&args, ids);
    dictAdd(&args, TR_KEY_path, oldpath);
    dictAdd(&args, TR_KEY_name, newname);

    auto* q = new RpcQueue();

    q->add([this, &args]()
        {
            return exec("torrent-rename-path", &args);
        },
        [](RpcResponse const& r)
        {
            auto str = dictFind<QString>(r.args.get(), TR_KEY_path);
            auto const path = str ? *str : QStringLiteral("(unknown)");
            str = dictFind<QString>(r.args.get(), TR_KEY_name);
            auto const name = str ? *str : QStringLiteral("(unknown)");

            auto* d = new QMessageBox(QMessageBox::Information, tr("Error Renaming Path"),
                tr(R"(<p><b>Unable to rename "%1" as "%2": %3.</b></p><p>Please correct the errors and try again.</p>)").
                    arg(path).arg(name).arg(r.result), QMessageBox::Close,
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
    dictAdd(&args, TR_KEY_format, "table");

    std::vector<std::string_view> keystrs;
    keystrs.reserve(keys.size());
    for (auto const& key : keys)
    {
        auto len = size_t {};
        auto const* str = tr_quark_get_string(key, &len);
        keystrs.emplace_back(str, len);
    }

    dictAdd(&args, TR_KEY_fields, keystrs);
    addOptionalIds(&args, ids);

    auto* q = new RpcQueue();

    q->add([this, &args]()
        {
            return exec(TR_KEY_torrent_get, &args);
        });

    bool const all_torrents = ids.empty();

    q->add([this, all_torrents](RpcResponse const& r)
        {
            tr_variant* torrents;

            if (tr_variantDictFindList(r.args.get(), TR_KEY_torrents, &torrents))
            {
                emit torrentsUpdated(torrents, all_torrents);
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
    refreshTorrents(ids, Torrent::DetailInfoKeys);
}

void Session::refreshExtraStats(torrent_ids_t const& ids)
{
    refreshTorrents(ids, Torrent::MainStatKeys + Torrent::DetailStatKeys);
}

void Session::sendTorrentRequest(std::string_view request, torrent_ids_t const& ids)
{
    tr_variant args;
    tr_variantInitDict(&args, 1);
    addOptionalIds(&args, ids);

    auto* q = new RpcQueue();

    q->add([this, request, &args]()
        {
            return exec(request, &args);
        });

    q->add([this, ids]()
        {
            refreshTorrents(ids, Torrent::MainStatKeys);
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
    refreshTorrents(RecentlyActiveIDs, Torrent::MainStatKeys);
}

void Session::refreshAllTorrents()
{
    refreshTorrents(AllIDs, Torrent::MainStatKeys);
}

void Session::initTorrents(torrent_ids_t const& ids)
{
    refreshTorrents(ids, Torrent::AllMainKeys);
}

void Session::refreshSessionStats()
{
    auto* q = new RpcQueue();

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
    auto* q = new RpcQueue();

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
    auto* q = new RpcQueue();

    q->add([this]()
        {
            return exec("blocklist-update", nullptr);
        });

    q->add([this](RpcResponse const& r)
        {
            auto const size = dictFind<int>(r.args.get(), TR_KEY_blocklist_size);
            if (size)
            {
                setBlocklistSize(*size);
            }
        });

    q->run();
}

/***
****
***/

RpcResponseFuture Session::exec(tr_quark method, tr_variant* args)
{
    return rpc_.exec(method, args);
}

RpcResponseFuture Session::exec(std::string_view method, tr_variant* args)
{
    return rpc_.exec(method, args);
}

void Session::updateStats(tr_variant* d, tr_session_stats* stats)
{
    auto value = dictFind<int>(d, TR_KEY_uploadedBytes);
    if (value)
    {
        stats->uploadedBytes = *value;
    }

    if ((value = dictFind<int>(d, TR_KEY_downloadedBytes)))
    {
        stats->downloadedBytes = *value;
    }

    if ((value = dictFind<int>(d, TR_KEY_filesAdded)))
    {
        stats->filesAdded = *value;
    }

    if ((value = dictFind<int>(d, TR_KEY_sessionCount)))
    {
        stats->sessionCount = *value;
    }

    if ((value = dictFind<int>(d, TR_KEY_secondsActive)))
    {
        stats->secondsActive = *value;
    }

    stats->ratio = tr_getRatio(stats->uploadedBytes, stats->downloadedBytes);
}

void Session::updateStats(tr_variant* d)
{
    tr_variant* c;

    if (tr_variantDictFindDict(d, TR_KEY_current_stats, &c))
    {
        updateStats(c, &stats_);
    }

    if (tr_variantDictFindDict(d, TR_KEY_cumulative_stats, &c))
    {
        updateStats(c, &cumulative_stats_);
    }

    emit statsUpdated();
}

void Session::updateInfo(tr_variant* d)
{
    disconnect(&prefs_, SIGNAL(changed(int)), this, SLOT(updatePref(int)));

    for (int i = Prefs::FIRST_CORE_PREF; i <= Prefs::LAST_CORE_PREF; ++i)
    {
        tr_variant const* b(tr_variantDictFind(d, prefs_.getKey(i)));

        if (b == nullptr)
        {
            continue;
        }

        if (i == Prefs::ENCRYPTION)
        {
            auto const str = getValue<QString>(b);

            if (str)
            {
                if (*str == QStringLiteral("required"))
                {
                    prefs_.set(i, 2);
                }
                else if (*str == QStringLiteral("preferred"))
                {
                    prefs_.set(i, 1);
                }
                else if (*str == QStringLiteral("tolerated"))
                {
                    prefs_.set(i, 0);
                }
            }

            continue;
        }

        switch (prefs_.type(i))
        {
        case QVariant::Int:
            {
                auto const value = getValue<int>(b);

                if (value)
                {
                    prefs_.set(i, *value);
                }

                break;
            }

        case QVariant::Double:
            {
                auto const value = getValue<double>(b);

                if (value)
                {
                    prefs_.set(i, *value);
                }

                break;
            }

        case QVariant::Bool:
            {
                auto const value = getValue<bool>(b);

                if (value)
                {
                    prefs_.set(i, *value);
                }

                break;
            }

        case CustomVariantType::FilterModeType:
        case CustomVariantType::SortModeType:
        case QVariant::String:
            {
                auto const value = getValue<QString>(b);

                if (value)
                {
                    prefs_.set(i, *value);
                }

                break;
            }

        default:
            break;
        }
    }

    auto const b = dictFind<bool>(d, TR_KEY_seedRatioLimited);
    if (b)
    {
        prefs_.set(Prefs::RATIO_ENABLED, *b);
    }

    auto const x = dictFind<double>(d, TR_KEY_seedRatioLimit);
    if (x)
    {
        prefs_.set(Prefs::RATIO, *x);
    }

    /* Use the C API to get settings that, for security reasons, aren't supported by RPC */
    if (session_ != nullptr)
    {
        prefs_.set(Prefs::RPC_ENABLED, tr_sessionIsRPCEnabled(session_));
        prefs_.set(Prefs::RPC_AUTH_REQUIRED, tr_sessionIsRPCPasswordEnabled(session_));
        prefs_.set(Prefs::RPC_PASSWORD, QString::fromUtf8(tr_sessionGetRPCPassword(session_)));
        prefs_.set(Prefs::RPC_PORT, tr_sessionGetRPCPort(session_));
        prefs_.set(Prefs::RPC_USERNAME, QString::fromUtf8(tr_sessionGetRPCUsername(session_)));
        prefs_.set(Prefs::RPC_WHITELIST_ENABLED, tr_sessionGetRPCWhitelistEnabled(session_));
        prefs_.set(Prefs::RPC_WHITELIST, QString::fromUtf8(tr_sessionGetRPCWhitelist(session_)));
    }

    auto const size = dictFind<int>(d, TR_KEY_blocklist_size);
    if (size && *size != blocklistSize())
    {
        setBlocklistSize(*size);
    }

    auto str = dictFind<QString>(d, TR_KEY_version);
    if (str)
    {
        session_version_ = *str;
    }

    str = dictFind<QString>(d, TR_KEY_session_id);
    if (str)
    {
        session_id_ = *str;
        is_definitely_local_session_ = tr_session_id_is_local(session_id_.toUtf8().constData());
    }
    else
    {
        session_id_.clear();
    }

    // std::cerr << "Session::updateInfo end" << std::endl;
    connect(&prefs_, SIGNAL(changed(int)), this, SLOT(updatePref(int)));

    emit sessionUpdated();
}

void Session::setBlocklistSize(int64_t i)
{
    blocklist_size_ = i;

    emit blocklistUpdated(i);
}

void Session::addTorrent(AddData const& add_me, tr_variant* args, bool trash_original)
{
    assert(tr_variantDictFind(args, TR_KEY_filename) == nullptr);
    assert(tr_variantDictFind(args, TR_KEY_metainfo) == nullptr);

    if (tr_variantDictFind(args, TR_KEY_paused) == nullptr)
    {
        dictAdd(args, TR_KEY_paused, !prefs_.getBool(Prefs::START));
    }

    switch (add_me.type)
    {
    case AddData::MAGNET:
        dictAdd(args, TR_KEY_filename, add_me.magnet);
        break;

    case AddData::URL:
        dictAdd(args, TR_KEY_filename, add_me.url.toString());
        break;

    case AddData::FILENAME: /* fall-through */
    case AddData::METAINFO:
        dictAdd(args, TR_KEY_metainfo, add_me.toBase64());
        break;

    default:
        qWarning() << "Unhandled AddData type: " << add_me.type;
        break;
    }

    auto* q = new RpcQueue();

    q->add([this, args]()
        {
            return exec("torrent-add", args);
        },
        [add_me](RpcResponse const& r)
        {
            auto* d = new QMessageBox(QMessageBox::Warning, tr("Error Adding Torrent"),
                QStringLiteral("<p><b>%1</b></p><p>%2</p>").arg(r.result).arg(add_me.readableName()), QMessageBox::Close,
                qApp->activeWindow());
            QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
            d->show();
        });

    q->add([add_me](RpcResponse const& r)
        {
            tr_variant* dup;

            if (!tr_variantDictFindDict(r.args.get(), TR_KEY_torrent_duplicate, &dup))
            {
                return;
            }

            auto const name = dictFind<QString>(dup, TR_KEY_name);
            if (name)
            {
                auto* d = new QMessageBox(QMessageBox::Warning, tr("Add Torrent"),
                    tr(R"(<p><b>Unable to add "%1".</b></p><p>It is a duplicate of "%2" which is already added.</p>)").
                        arg(add_me.readableShortName()).arg(*name), QMessageBox::Close, qApp->activeWindow());
                QObject::connect(d, &QMessageBox::rejected, d, &QMessageBox::deleteLater);
                d->show();
            }
        });

    if (trash_original && add_me.type == AddData::FILENAME)
    {
        q->add([add_me]()
            {
                QFile original(add_me.filename);
                original.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
                original.remove();
            });
    }

    q->run();
}

void Session::addTorrent(AddData const& add_me)
{
    tr_variant args;
    tr_variantInitDict(&args, 3);

    addTorrent(add_me, &args, prefs_.getBool(Prefs::TRASH_ORIGINAL));
}

void Session::addNewlyCreatedTorrent(QString const& filename, QString const& local_path)
{
    QByteArray const b64 = AddData(filename).toBase64();

    tr_variant args;
    tr_variantInitDict(&args, 3);
    dictAdd(&args, TR_KEY_download_dir, local_path);
    dictAdd(&args, TR_KEY_paused, !prefs_.getBool(Prefs::START));
    dictAdd(&args, TR_KEY_metainfo, b64);

    exec("torrent-add", &args);
}

void Session::removeTorrents(torrent_ids_t const& ids, bool delete_files)
{
    if (!ids.empty())
    {
        tr_variant args;
        tr_variantInitDict(&args, 2);
        addOptionalIds(&args, ids);
        dictAdd(&args, TR_KEY_delete_local_data, delete_files);

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

    if (session_ == nullptr) // remote session
    {
        url = rpc_.url();
        url.setPath(QStringLiteral("/transmission/web/"));
    }
    else // local session
    {
        url.setScheme(QStringLiteral("http"));
        url.setHost(QStringLiteral("localhost"));
        url.setPort(prefs_.getInt(Prefs::RPC_PORT));
    }

    QDesktopServices::openUrl(url);
}
