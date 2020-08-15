/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "VariantHelpers.h"

#include <cmath>
#include <limits>

#include <QUrl>

#include "Application.h" // qApp
#include "Filters.h"
#include "Speed.h"
#include "Torrent.h"

namespace trqt::variant_helpers
{

bool change(double& setme, double const& value)
{
    bool const changed = std::fabs(setme - value) > std::numeric_limits<double>::epsilon();

    if (changed)
    {
        setme = value;
    }

    return changed;
}

bool change(Speed& setme, tr_variant const* variant)
{
    auto const value = getValue<int>(variant);
    return value && change(setme, Speed::fromBps(*value));
}

bool change(Peer& setme, tr_variant const* value)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* child;
    while (tr_variantDictChild(const_cast<tr_variant*>(value), pos++, &key, &child))
    {
        switch (key)
        {
#define HANDLE_KEY(key, field) case TR_KEY_ ## key: \
    changed = change(setme.field, child) || changed; break;

            HANDLE_KEY(address, address)
            HANDLE_KEY(clientIsChoked, client_is_choked)
            HANDLE_KEY(clientIsInterested, client_is_interested)
            HANDLE_KEY(clientName, client_name)
            HANDLE_KEY(flagStr, flags)
            HANDLE_KEY(isDownloadingFrom, is_downloading_from)
            HANDLE_KEY(isEncrypted, is_encrypted)
            HANDLE_KEY(isIncoming, is_incoming)
            HANDLE_KEY(isUploadingTo, is_uploading_to)
            HANDLE_KEY(peerIsChoked, peer_is_choked)
            HANDLE_KEY(peerIsInterested, peer_is_interested)
            HANDLE_KEY(port, port)
            HANDLE_KEY(progress, progress)
            HANDLE_KEY(rateToClient, rate_to_client)
            HANDLE_KEY(rateToPeer, rate_to_peer)
#undef HANDLE_KEY
        default:
            break;
        }
    }

    return changed;
}

bool change(TorrentFile& setme, tr_variant const* value)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* child;
    while (tr_variantDictChild(const_cast<tr_variant*>(value), pos++, &key, &child))
    {
        switch (key)
        {
#define HANDLE_KEY(key) case TR_KEY_ ## key: \
    changed = change(setme.key, child) || changed; break;

            HANDLE_KEY(have)
            HANDLE_KEY(priority)
            HANDLE_KEY(wanted)
#undef HANDLE_KEY
#define HANDLE_KEY(key, field) case TR_KEY_ ## key: \
    changed = change(setme.field, child) || changed; break;

            HANDLE_KEY(bytesCompleted, have)
            HANDLE_KEY(length, size)
            HANDLE_KEY(name, filename)
#undef HANDLE_KEY
        default:
            break;
        }
    }

    return changed;
}

bool change(TrackerStat& setme, tr_variant const* value)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* child;
    while (tr_variantDictChild(const_cast<tr_variant*>(value), pos++, &key, &child))
    {
        bool field_changed = false;

        switch (key)
        {
#define HANDLE_KEY(key, field) case TR_KEY_ ## key: \
    field_changed = change(setme.field, child); break;
            HANDLE_KEY(announce, announce)
            HANDLE_KEY(announceState, announce_state)
            HANDLE_KEY(downloadCount, download_count)
            HANDLE_KEY(hasAnnounced, has_announced)
            HANDLE_KEY(hasScraped, has_scraped)
            HANDLE_KEY(host, host)
            HANDLE_KEY(id, id)
            HANDLE_KEY(isBackup, is_backup)
            HANDLE_KEY(lastAnnouncePeerCount, last_announce_peer_count)
            HANDLE_KEY(lastAnnounceResult, last_announce_result)
            HANDLE_KEY(lastAnnounceStartTime, last_announce_start_time)
            HANDLE_KEY(lastAnnounceSucceeded, last_announce_succeeded)
            HANDLE_KEY(lastAnnounceTime, last_announce_time)
            HANDLE_KEY(lastAnnounceTimedOut, last_announce_timed_out)
            HANDLE_KEY(lastScrapeResult, last_scrape_result)
            HANDLE_KEY(lastScrapeStartTime, last_scrape_start_time)
            HANDLE_KEY(lastScrapeSucceeded, last_scrape_succeeded)
            HANDLE_KEY(lastScrapeTime, last_scrape_time)
            HANDLE_KEY(lastScrapeTimedOut, last_scrape_timed_out)
            HANDLE_KEY(leecherCount, leecher_count)
            HANDLE_KEY(nextAnnounceTime, next_announce_time)
            HANDLE_KEY(nextScrapeTime, next_scrape_time)
            HANDLE_KEY(scrapeState, scrape_state)
            HANDLE_KEY(seederCount, seeder_count)
            HANDLE_KEY(tier, tier)

#undef HANDLE_KEY
        default:
            break;
        }

        if (field_changed)
        {
            if (key == TR_KEY_announce)
            {
                setme.favicon_key = qApp->faviconCache().add(QUrl(setme.announce));
            }

            changed = true;
        }
    }

    return changed;
}

///

void variantInit(tr_variant* init_me, bool value)
{
    tr_variantInitBool(init_me, value);
}

void variantInit(tr_variant* init_me, int64_t value)
{
    tr_variantInitInt(init_me, value);
}

void variantInit(tr_variant* init_me, int value)
{
    tr_variantInitInt(init_me, value);
}

void variantInit(tr_variant* init_me, unsigned int value)
{
    tr_variantInitInt(init_me, value);
}

void variantInit(tr_variant* init_me, double value)
{
    tr_variantInitReal(init_me, value);
}

void variantInit(tr_variant* init_me, QByteArray const& value)
{
    tr_variantInitRaw(init_me, value.constData(), value.size());
}

void variantInit(tr_variant* init_me, QString const& value)
{
    variantInit(init_me, value.toUtf8());
}

void variantInit(tr_variant* init_me, std::string_view value)
{
    tr_variantInitStr(init_me, std::data(value), std::size(value));
}

} // namespace trqt::variant_helpers
