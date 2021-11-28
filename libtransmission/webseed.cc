/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cstring> /* strlen() */
#include <set>
#include <vector>

#include <event2/buffer.h>
#include <event2/event.h>

#include "transmission.h"

#include "bandwidth.h"
#include "cache.h"
#include "inout.h" /* tr_ioFindFileLocation() */
#include "peer-mgr.h"
#include "torrent.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "web-utils.h"
#include "web.h"
#include "webseed.h"

namespace
{

struct tr_webseed;

struct tr_webseed_task
{
    bool dead;
    struct evbuffer* content;
    struct tr_webseed* webseed;
    tr_session* session;
    tr_block_index_t block;
    tr_piece_index_t piece_index;
    uint32_t piece_offset;
    uint32_t length;
    tr_block_index_t blocks_done;
    uint32_t block_size;
    struct tr_web_task* web_task;
    long response_code;
};

auto constexpr TR_IDLE_TIMER_MSEC = 2000;

auto constexpr FAILURE_RETRY_INTERVAL = 150;

auto constexpr MAX_CONSECUTIVE_FAILURES = 5;

auto constexpr MAX_WEBSEED_CONNECTIONS = 4;

void webseed_timer_func(evutil_socket_t fd, short what, void* vw);

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
    {
        // init parent bits
        have.setHasAll();
        tr_peerUpdateProgress(tor, this);

        file_urls.resize(tr_torrentInfo(tor)->fileCount);

        timer = evtimer_new(session->event_base, webseed_timer_func, this);
        tr_timerAddMsec(timer, TR_IDLE_TIMER_MSEC);
    }

    ~tr_webseed() override
    {
        // flag all the pending tasks as dead
        std::for_each(std::begin(tasks), std::end(tasks), [](auto* task) { task->dead = true; });
        tasks.clear();

        event_free(timer);
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
    std::set<tr_webseed_task*> tasks;
    struct event* timer = nullptr;
    int consecutive_failures = 0;
    int retry_tickcount = 0;
    int retry_challenge = 0;
    int idle_connections = 0;
    int active_transfers = 0;
    std::vector<std::string> file_urls;
};

} // namespace

/***
****
***/

static void publish(tr_webseed* w, tr_peer_event* e)
{
    if (w->callback != nullptr)
    {
        (*w->callback)(w, e, w->callback_data);
    }
}

static void fire_client_got_rejs(tr_torrent* tor, tr_webseed* w, tr_block_index_t block, tr_block_index_t count)
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

static void fire_client_got_blocks(tr_torrent* tor, tr_webseed* w, tr_block_index_t block, tr_block_index_t count)
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

static void fire_client_got_piece_data(tr_webseed* w, uint32_t length)
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
    tr_session* session;
    int torrent_id;
    struct tr_webseed* webseed;
    struct evbuffer* content;
    tr_piece_index_t piece_index;
    tr_block_index_t block_index;
    tr_block_index_t count;
    uint32_t block_offset;
};

static void write_block_func(void* vdata)
{
    auto* const data = static_cast<struct write_block_data*>(vdata);
    struct tr_webseed* const w = data->webseed;
    struct evbuffer* const buf = data->content;

    auto* const tor = tr_torrentFindFromId(data->session, data->torrent_id);
    if (tor != nullptr)
    {
        uint32_t const block_size = tor->block_size;
        uint32_t len = evbuffer_get_length(buf);
        uint32_t const offset_end = data->block_offset + len;
        tr_cache* cache = data->session->cache;
        tr_piece_index_t const piece = data->piece_index;

        if (!tor->hasPiece(piece))
        {
            while (len > 0)
            {
                uint32_t const bytes_this_pass = std::min(len, block_size);
                tr_cacheWriteBlock(cache, tor, piece, offset_end - len, bytes_this_pass, buf);
                len -= bytes_this_pass;
            }

            fire_client_got_blocks(tor, w, data->block_index, data->count);
        }
    }

    evbuffer_free(buf);
    tr_free(data);
}

/***
****
***/

struct connection_succeeded_data
{
    struct tr_webseed* webseed;
    char* real_url;
    tr_piece_index_t piece_index;
    uint32_t piece_offset;
};

static void connection_succeeded(void* vdata)
{
    auto* data = static_cast<struct connection_succeeded_data*>(vdata);
    struct tr_webseed* w = data->webseed;

    if (++w->active_transfers >= w->retry_challenge && w->retry_challenge != 0)
    {
        /* the server seems to be accepting more connections now */
        w->consecutive_failures = w->retry_tickcount = w->retry_challenge = 0;
    }

    if (data->real_url != nullptr)
    {
        tr_torrent const* const tor = tr_torrentFindFromId(w->session, w->torrent_id);

        if (tor != nullptr)
        {
            auto file_index = tr_file_index_t{};
            auto file_offset = uint64_t{};
            tr_ioFindFileLocation(tor, data->piece_index, data->piece_offset, &file_index, &file_offset);
            w->file_urls[file_index].assign(data->real_url);
            data->real_url = nullptr;
        }
    }

    tr_free(data->real_url);
    tr_free(data);
}

/***
****
***/

static void on_content_changed(struct evbuffer* buf, struct evbuffer_cb_info const* info, void* vtask)
{
    size_t const n_added = info->n_added;
    auto* task = static_cast<struct tr_webseed_task*>(vtask);
    auto* session = task->session;
    auto const lock = session->unique_lock();

    if (!task->dead && n_added > 0)
    {
        struct tr_webseed* w = task->webseed;

        w->bandwidth.notifyBandwidthConsumed(TR_DOWN, n_added, true, tr_time_msec());
        fire_client_got_piece_data(w, n_added);
        uint32_t const len = evbuffer_get_length(buf);

        if (task->response_code == 0)
        {
            task->response_code = tr_webGetTaskResponseCode(task->web_task);

            if (task->response_code == 206)
            {
                auto* const data = tr_new(struct connection_succeeded_data, 1);
                data->webseed = w;
                data->real_url = tr_strdup(tr_webGetTaskRealUrl(task->web_task));
                data->piece_index = task->piece_index;
                data->piece_offset = task->piece_offset + task->blocks_done * task->block_size + len - 1;

                /* processing this uses a tr_torrent pointer,
                   so push the work to the libevent thread... */
                tr_runInEventThread(session, connection_succeeded, data);
            }
        }

        if (task->response_code == 206 && len >= task->block_size)
        {
            /* once we've got at least one full block, save it */

            uint32_t const block_size = task->block_size;
            tr_block_index_t const completed = len / block_size;

            auto* const data = tr_new(struct write_block_data, 1);
            data->webseed = task->webseed;
            data->piece_index = task->piece_index;
            data->block_index = task->block + task->blocks_done;
            data->count = completed;
            data->block_offset = task->piece_offset + task->blocks_done * block_size;
            data->content = evbuffer_new();
            data->torrent_id = w->torrent_id;
            data->session = w->session;

            /* we don't use locking on this evbuffer so we must copy out the data
               that will be needed when writing the block in a different thread */
            evbuffer_remove_buffer(task->content, data->content, (size_t)block_size * (size_t)completed);

            tr_runInEventThread(w->session, write_block_func, data);
            task->blocks_done += completed;
        }
    }
}

static void task_request_next_chunk(struct tr_webseed_task* task);

static void on_idle(tr_webseed* w)
{
    auto want = int{};
    int const running_tasks = std::size(w->tasks);
    tr_torrent* tor = tr_torrentFindFromId(w->session, w->torrent_id);

    if (w->consecutive_failures >= MAX_CONSECUTIVE_FAILURES)
    {
        want = w->idle_connections;

        if (w->retry_tickcount >= FAILURE_RETRY_INTERVAL)
        {
            /* some time has passed since our connection attempts failed. try again */
            ++want;
            /* if this challenge is fulfilled we will reset consecutive_failures */
            w->retry_challenge = running_tasks + want;
        }
    }
    else
    {
        want = MAX_WEBSEED_CONNECTIONS - running_tasks;
        w->retry_challenge = running_tasks + w->idle_connections + 1;
    }

    if (tor != nullptr && tor->isRunning && !tr_torrentIsSeed(tor) && want > 0)
    {
        auto n_tasks = size_t{};

        for (auto const span : tr_peerMgrGetNextRequests(tor, w, want))
        {
            auto const [begin, end] = span;
            auto* const task = tr_new0(tr_webseed_task, 1);
            task->session = tor->session;
            task->webseed = w;
            task->block = begin;
            task->piece_index = tor->pieceForBlock(begin);
            task->piece_offset = tor->block_size * begin - tor->info.pieceSize * task->piece_index;
            task->length = (end - 1 - begin) * tor->block_size + tor->blockSize(end - 1);
            task->blocks_done = 0;
            task->response_code = 0;
            task->block_size = tor->block_size;
            task->content = evbuffer_new();
            evbuffer_add_cb(task->content, on_content_changed, task);
            w->tasks.insert(task);
            task_request_next_chunk(task);

            --w->idle_connections;
            ++n_tasks;
            tr_peerMgrClientSentRequests(tor, w, span);
        }

        if (w->retry_tickcount >= FAILURE_RETRY_INTERVAL && n_tasks > 0)
        {
            w->retry_tickcount = 0;
        }
    }
}

static void web_response_func(
    tr_session* session,
    bool /*did_connect*/,
    bool /*did_timeout*/,
    long response_code,
    std::string_view /*response*/,
    void* vtask)
{
    auto* t = static_cast<struct tr_webseed_task*>(vtask);
    bool const success = response_code == 206;

    if (t->dead)
    {
        evbuffer_free(t->content);
        tr_free(t);
        return;
    }

    tr_webseed* w = t->webseed;
    tr_torrent* tor = tr_torrentFindFromId(session, w->torrent_id);

    if (tor != nullptr)
    {
        /* active_transfers was only increased if the connection was successful */
        if (t->response_code == 206)
        {
            --w->active_transfers;
        }

        if (!success)
        {
            tr_block_index_t const blocks_remain = (t->length + tor->block_size - 1) / tor->block_size - t->blocks_done;

            if (blocks_remain != 0)
            {
                fire_client_got_rejs(tor, w, t->block + t->blocks_done, blocks_remain);
            }

            if (t->blocks_done != 0)
            {
                ++w->idle_connections;
            }
            else if (++w->consecutive_failures >= MAX_CONSECUTIVE_FAILURES && w->retry_tickcount == 0)
            {
                /* now wait a while until retrying to establish a connection */
                ++w->retry_tickcount;
            }

            w->tasks.erase(t);
            evbuffer_free(t->content);
            tr_free(t);
        }
        else
        {
            uint32_t const bytes_done = t->blocks_done * tor->block_size;
            uint32_t const buf_len = evbuffer_get_length(t->content);

            if (bytes_done + buf_len < t->length)
            {
                /* request finished successfully but there's still data missing. that
                   means we've reached the end of a file and need to request the next one */
                t->response_code = 0;
                task_request_next_chunk(t);
            }
            else
            {
                if (buf_len != 0 && !tor->hasPiece(t->piece_index))
                {
                    /* on_content_changed() will not write a block if it is smaller than
                       the torrent's block size, i.e. the torrent's very last block */
                    tr_cacheWriteBlock(session->cache, tor, t->piece_index, t->piece_offset + bytes_done, buf_len, t->content);

                    fire_client_got_blocks(tor, t->webseed, t->block + t->blocks_done, 1);
                }

                ++w->idle_connections;

                w->tasks.erase(t);
                evbuffer_free(t->content);
                tr_free(t);

                on_idle(w);
            }
        }
    }
}

static std::string make_url(tr_webseed* w, char const* name)
{
    struct evbuffer* buf = evbuffer_new();

    evbuffer_add(buf, std::data(w->base_url), std::size(w->base_url));

    /* if url ends with a '/', add the torrent name */
    if (*std::rbegin(w->base_url) == '/' && name != nullptr)
    {
        tr_http_escape(buf, name, false);
    }

    auto url = std::string{ (char const*)evbuffer_pullup(buf, -1), evbuffer_get_length(buf) };
    evbuffer_free(buf);
    return url;
}

static void task_request_next_chunk(struct tr_webseed_task* t)
{
    tr_webseed* w = t->webseed;
    tr_torrent* tor = tr_torrentFindFromId(w->session, w->torrent_id);

    if (tor != nullptr)
    {
        auto& urls = t->webseed->file_urls;

        tr_info const* inf = tr_torrentInfo(tor);
        uint64_t const remain = t->length - t->blocks_done * tor->block_size - evbuffer_get_length(t->content);

        uint64_t const total_offset = tr_pieceOffset(tor, t->piece_index, t->piece_offset, t->length - remain);
        tr_piece_index_t const step_piece = total_offset / inf->pieceSize;
        uint64_t const step_piece_offset = total_offset - (uint64_t)inf->pieceSize * step_piece;

        auto file_index = tr_file_index_t{};
        auto file_offset = uint64_t{};
        tr_ioFindFileLocation(tor, step_piece, step_piece_offset, &file_index, &file_offset);

        auto const& file = inf->files[file_index];
        uint64_t this_pass = std::min(remain, file.length - file_offset);

        if (std::empty(urls[file_index]))
        {
            urls[file_index] = make_url(t->webseed, file.name);
        }

        char range[64];
        tr_snprintf(range, sizeof(range), "%" PRIu64 "-%" PRIu64, file_offset, file_offset + this_pass - 1);

        t->web_task = tr_webRunWebseed(tor, urls[file_index].c_str(), range, web_response_func, t, t->content);
    }
}

/***
****
***/

namespace
{

void webseed_timer_func(evutil_socket_t /*fd*/, short /*what*/, void* vw)
{
    auto* w = static_cast<tr_webseed*>(vw);

    if (w->retry_tickcount != 0)
    {
        ++w->retry_tickcount;
    }

    on_idle(w);

    tr_timerAddMsec(w->timer, TR_IDLE_TIMER_MSEC);
}

} // unnamed namespace

tr_peer* tr_webseedNew(struct tr_torrent* torrent, std::string_view url, tr_peer_callback callback, void* callback_data)
{
    return new tr_webseed(torrent, url, callback, callback_data);
}
