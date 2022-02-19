// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <set>
#include <string>
#include <string_view>

#include <event2/buffer.h>
#include <event2/event.h>

#include "transmission.h"

#include "bandwidth.h"
#include "cache.h"
#include "peer-mgr.h"
#include "torrent.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "web-utils.h"
#include "web.h"
#include "webseed.h"

using namespace std::literals;

namespace
{

struct tr_webseed;

void on_idle(tr_webseed* w);

class tr_webseed_task
{
private:
    std::shared_ptr<evbuffer> const content_{ evbuffer_new(), evbuffer_free };

public:
    tr_webseed_task(tr_torrent* tor, tr_webseed* webseed_in, tr_block_span_t span)
        : webseed{ webseed_in }
        , session{ tor->session }
        , block{ span.begin } // TODO(ckerr): just own the loc
        , piece_index{ tor->blockLoc(this->block).piece }
        , piece_offset{ tor->blockLoc(this->block).piece_offset }
        , block_size{ tor->blockSize() }
        , length{ (span.end - 1 - span.begin) * tor->blockSize() + tor->blockSize(span.end - 1) }
    {
    }

    tr_webseed* const webseed;

    [[nodiscard]] auto* content() const
    {
        return content_.get();
    }

    tr_session* const session;
    tr_block_index_t const block;
    tr_piece_index_t const piece_index;
    uint32_t const piece_offset;
    uint32_t const block_size;
    uint32_t const length;

    bool dead = false;
    tr_block_index_t blocks_done = 0;
};

/**
 * Manages how many web tasks should be running at a time.
 *
 * - When all is well, allow multiple tasks running in parallel.
 * - If we get an error, throttle down to only one at a time
 *   until we get piece data.
 * - If we have too many errors in a row, put the peer in timeout
 *   and don't allow _any_ connections for awhile.
 */
class ConnectionLimiter
{
public:
    void taskStarted()
    {
        ++n_tasks;
    }

    void taskFinished(bool success)
    {
        if (!success)
        {
            taskFailed();
        }

        TR_ASSERT(n_tasks > 0);
        --n_tasks;
    }

    void gotData()
    {
        TR_ASSERT(n_tasks > 0);
        n_consecutive_failures = 0;
        paused_until = 0;
    }

    [[nodiscard]] size_t slotsAvailable() const
    {
        if (isPaused())
        {
            return 0;
        }

        auto const max = maxConnections();
        if (n_tasks >= max)
        {
            return 0;
        }

        return max - n_tasks;
    }

private:
    [[nodiscard]] bool isPaused() const
    {
        return paused_until > tr_time();
    }

    [[nodiscard]] size_t maxConnections() const
    {
        return n_consecutive_failures > 0 ? 1 : MaxConnections;
    }

    void taskFailed()
    {
        TR_ASSERT(n_tasks > 0);

        if (++n_consecutive_failures >= MaxConsecutiveFailures)
        {
            paused_until = tr_time() + TimeoutIntervalSecs;
        }
    }

    static time_t constexpr TimeoutIntervalSecs = 120;
    static size_t constexpr MaxConnections = 4;
    static size_t constexpr MaxConsecutiveFailures = MaxConnections;

    size_t n_tasks = 0;
    size_t n_consecutive_failures = 0;
    time_t paused_until = 0;
};

struct tr_webseed : public tr_peer
{
public:
    tr_webseed(struct tr_torrent* tor, std::string_view url, tr_peer_callback callback_in, void* callback_data_in)
        : tr_peer{ tor }
        , torrent_id{ tr_torrentId(tor) }
        , base_url{ url }
        , callback{ callback_in }
        , callback_data{ callback_data_in }
        , bandwidth(tor->bandwidth)
        , pulse_timer(evtimer_new(session->event_base, &tr_webseed::onTimer, this), event_free)
    {
        // init parent bits
        have.setHasAll();
        tr_peerUpdateProgress(tor, this);

        startTimer();
    }

    ~tr_webseed() override
    {
        // flag all the pending tasks as dead
        std::for_each(std::begin(tasks), std::end(tasks), [](auto* task) { task->dead = true; });
        tasks.clear();
    }

    bool is_transferring_pieces(uint64_t now, tr_direction direction, unsigned int* setme_Bps) const override
    {
        unsigned int Bps = 0;
        bool is_active = false;

        if (direction == TR_DOWN)
        {
            is_active = !std::empty(tasks);
            Bps = bandwidth.getPieceSpeedBytesPerSecond(now, direction);
        }

        if (setme_Bps != nullptr)
        {
            *setme_Bps = Bps;
        }

        return is_active;
    }

    int const torrent_id;
    std::string const base_url;
    tr_peer_callback const callback;
    void* const callback_data;

    Bandwidth bandwidth;
    ConnectionLimiter connection_limiter;
    std::set<tr_webseed_task*> tasks;

private:
    void startTimer()
    {
        tr_timerAddMsec(pulse_timer.get(), IdleTimerMsec);
    }

    static void onTimer(evutil_socket_t /*fd*/, short /*what*/, void* vwebseed)
    {
        auto* const webseed = static_cast<tr_webseed*>(vwebseed);
        on_idle(webseed);
        webseed->startTimer();
    }

    std::shared_ptr<event> const pulse_timer;
    static int constexpr IdleTimerMsec = 2000;
};

/***
****
***/

void publish(tr_webseed* w, tr_peer_event* e)
{
    if (w->callback != nullptr)
    {
        (*w->callback)(w, e, w->callback_data);
    }
}

void fire_client_got_rejs(tr_torrent* tor, tr_webseed* w, tr_block_index_t block, tr_block_index_t count)
{
    auto e = tr_peer_event{};
    e.eventType = TR_PEER_CLIENT_GOT_REJ;
    tr_torrentGetBlockLocation(tor, block, &e.pieceIndex, &e.offset, &e.length);

    for (tr_block_index_t i = 1; i <= count; i++)
    {
        if (i == count)
        {
            e.length = tor->blockSize(block + count - 1);
        }

        publish(w, &e);
        e.offset += e.length;
    }
}

void fire_client_got_blocks(tr_torrent* tor, tr_webseed* w, tr_block_index_t block, tr_block_index_t count)
{
    auto e = tr_peer_event{};
    e.eventType = TR_PEER_CLIENT_GOT_BLOCK;
    tr_torrentGetBlockLocation(tor, block, &e.pieceIndex, &e.offset, &e.length);

    for (tr_block_index_t i = 1; i <= count; i++)
    {
        if (i == count)
        {
            e.length = tor->blockSize(block + count - 1);
        }

        publish(w, &e);
        e.offset += e.length;
    }
}

void fire_client_got_piece_data(tr_webseed* w, uint32_t length)
{
    auto e = tr_peer_event{};
    e.eventType = TR_PEER_CLIENT_GOT_PIECE_DATA;
    e.length = length;
    publish(w, &e);
}

/***
****
***/

struct write_block_data
{
private:
    std::shared_ptr<evbuffer> const content_{ evbuffer_new(), evbuffer_free };

public:
    write_block_data(tr_session* session_in, int torrent_id_in, tr_webseed* webseed_in)
        : session{ session_in }
        , torrent_id{ torrent_id_in }
        , webseed{ webseed_in }
    {
    }

    [[nodiscard]] auto* content() const
    {
        return content_.get();
    }

    tr_session* const session;
    int const torrent_id;
    tr_webseed* const webseed;

    tr_piece_index_t piece_index;
    tr_block_index_t block_index;
    tr_block_index_t count;
    uint32_t block_offset;
};

void write_block_func(void* vdata)
{
    auto* const data = static_cast<write_block_data*>(vdata);
    struct tr_webseed* const w = data->webseed;
    auto* const buf = data->content();

    auto* const tor = tr_torrentFindFromId(data->session, data->torrent_id);
    if (tor != nullptr)
    {
        uint32_t const block_size = tor->blockSize();
        uint32_t len = evbuffer_get_length(buf);
        uint32_t const offset_end = data->block_offset + len;
        tr_cache* cache = data->session->cache;
        tr_piece_index_t const piece = data->piece_index;

        if (!tor->hasPiece(piece))
        {
            while (len > 0)
            {
                uint32_t const bytes_this_pass = std::min(len, block_size);
                tr_cacheWriteBlock(cache, tor, tor->pieceLoc(piece, offset_end - len), bytes_this_pass, buf);
                len -= bytes_this_pass;
            }

            fire_client_got_blocks(tor, w, data->block_index, data->count);
        }
    }

    delete data;
}

/***
****
***/

void on_content_changed(evbuffer* buf, evbuffer_cb_info const* info, void* vtask)
{
    size_t const n_added = info->n_added;
    auto* const task = static_cast<tr_webseed_task*>(vtask);
    auto* const session = task->session;
    auto const lock = session->unique_lock();

    if (!task->dead && n_added > 0)
    {
        auto* const w = task->webseed;

        w->bandwidth.notifyBandwidthConsumed(TR_DOWN, n_added, true, tr_time_msec());
        fire_client_got_piece_data(w, n_added);
        uint32_t const len = evbuffer_get_length(buf);

        task->webseed->connection_limiter.gotData();

        if (len >= task->block_size)
        {
            /* once we've got at least one full block, save it */

            uint32_t const block_size = task->block_size;
            tr_block_index_t const completed = len / block_size;

            auto* const data = new write_block_data{ session, w->torrent_id, task->webseed };
            data->piece_index = task->piece_index;
            data->block_index = task->block + task->blocks_done;
            data->count = completed;
            data->block_offset = task->piece_offset + task->blocks_done * block_size;

            /* we don't use locking on this evbuffer so we must copy out the data
               that will be needed when writing the block in a different thread */
            evbuffer_remove_buffer(task->content(), data->content(), (size_t)block_size * (size_t)completed);

            tr_runInEventThread(w->session, write_block_func, data);
            task->blocks_done += completed;
        }
    }
}

void task_request_next_chunk(tr_webseed_task* task);

void on_idle(tr_webseed* w)
{
    auto* const tor = tr_torrentFindFromId(w->session, w->torrent_id);
    if (tor == nullptr || !tor->isRunning || tor->isDone())
    {
        return;
    }

    for (auto const span : tr_peerMgrGetNextRequests(tor, w, w->connection_limiter.slotsAvailable()))
    {
        w->connection_limiter.taskStarted();
        auto* const task = new tr_webseed_task{ tor, w, span };
        evbuffer_add_cb(task->content(), on_content_changed, task);
        w->tasks.insert(task);
        task_request_next_chunk(task);

        tr_peerMgrClientSentRequests(tor, w, span);
    }
}

void onPartialDataFetched(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, vtask] = web_response;
    bool const success = status == 206;

    auto* const t = static_cast<tr_webseed_task*>(vtask);
    auto* const session = t->session;
    auto* const w = t->webseed;

    w->connection_limiter.taskFinished(success);

    if (t->dead)
    {
        delete t;
        return;
    }

    tr_torrent* tor = tr_torrentFindFromId(session, w->torrent_id);

    if (tor != nullptr)
    {
        if (!success)
        {
            tr_block_index_t const blocks_remain = (t->length + tor->blockSize() - 1) / tor->blockSize() - t->blocks_done;

            if (blocks_remain != 0)
            {
                fire_client_got_rejs(tor, w, t->block + t->blocks_done, blocks_remain);
            }

            w->tasks.erase(t);
            delete t;
        }
        else
        {
            uint32_t const bytes_done = t->blocks_done * tor->blockSize();
            uint32_t const buf_len = evbuffer_get_length(t->content());

            if (bytes_done + buf_len < t->length)
            {
                /* request finished successfully but there's still data missing. that
                   means we've reached the end of a file and need to request the next one */
                task_request_next_chunk(t);
            }
            else
            {
                if (buf_len != 0 && !tor->hasPiece(t->piece_index))
                {
                    /* on_content_changed() will not write a block if it is smaller than
                       the torrent's block size, i.e. the torrent's very last block */
                    tr_cacheWriteBlock(
                        session->cache,
                        tor,
                        tor->pieceLoc(t->piece_index, t->piece_offset + bytes_done),
                        buf_len,
                        t->content());

                    fire_client_got_blocks(tor, t->webseed, t->block + t->blocks_done, 1);
                }

                w->tasks.erase(t);
                delete t;

                on_idle(w);
            }
        }
    }
}

std::string make_url(tr_webseed* w, std::string_view name)
{
    auto url = w->base_url;

    if (tr_strvEndsWith(url, "/"sv) && !std::empty(name))
    {
        tr_http_escape(url, name, false);
    }

    return url;
}

void task_request_next_chunk(tr_webseed_task* t)
{
    tr_webseed* w = t->webseed;

    tr_torrent* const tor = tr_torrentFindFromId(w->session, w->torrent_id);
    if (tor == nullptr)
    {
        return;
    }

    auto const piece_size = tor->pieceSize();
    uint64_t const remain = t->length - t->blocks_done * tor->blockSize() - evbuffer_get_length(t->content());

    auto const total_offset = tor->pieceLoc(t->piece_index, t->piece_offset, t->length - remain).byte;
    tr_piece_index_t const step_piece = total_offset / piece_size;
    uint64_t const step_piece_offset = total_offset - uint64_t(piece_size) * step_piece;

    auto const [file_index, file_offset] = tor->fileOffset(step_piece, step_piece_offset);
    uint64_t this_pass = std::min(remain, tor->fileSize(file_index) - file_offset);

    auto const url = make_url(t->webseed, tor->fileSubpath(file_index));
    auto options = tr_web::FetchOptions{ url, onPartialDataFetched, t };
    options.range = tr_strvJoin(std::to_string(file_offset), "-"sv, std::to_string(file_offset + this_pass - 1));
    options.speed_limit_tag = tor->uniqueId;
    options.buffer = t->content();
    tor->session->web->fetch(std::move(options));
}

} // namespace

/***
****
***/

tr_peer* tr_webseedNew(tr_torrent* torrent, std::string_view url, tr_peer_callback callback, void* callback_data)
{
    return new tr_webseed(torrent, url, callback, callback_data);
}

tr_webseed_view tr_webseedView(tr_peer const* peer)
{
    auto const* w = dynamic_cast<tr_webseed const*>(peer);
    if (w == nullptr)
    {
        return {};
    }

    auto bytes_per_second = unsigned{ 0 };
    auto const is_downloading = peer->is_transferring_pieces(tr_time_msec(), TR_DOWN, &bytes_per_second);
    return { w->base_url.c_str(), is_downloading, bytes_per_second };
}
