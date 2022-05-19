// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

#include <event2/buffer.h>
#include <event2/event.h>

#include <fmt/format.h>

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

class tr_webseed;

void on_idle(tr_webseed* w);

class tr_webseed_task
{
private:
    std::shared_ptr<evbuffer> const content_{ evbuffer_new(), evbuffer_free };

public:
    tr_webseed_task(tr_torrent* tor, tr_webseed* webseed_in, tr_block_span_t blocks_in)
        : webseed{ webseed_in }
        , session{ tor->session }
        , blocks{ blocks_in }
        , end_byte{ tor->blockLoc(blocks.end - 1).byte + tor->blockSize(blocks.end - 1) }
        , loc{ tor->blockLoc(blocks.begin) }
    {
    }

    tr_webseed* const webseed;

    [[nodiscard]] auto* content() const
    {
        return content_.get();
    }

    tr_session* const session;
    tr_block_span_t const blocks;
    uint64_t const end_byte;

    // the current position in the task; i.e., the next block to save
    tr_block_info::Location loc;

    bool dead = false;
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
    constexpr void taskStarted() noexcept
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

    constexpr void gotData() noexcept
    {
        TR_ASSERT(n_tasks > 0);
        n_consecutive_failures = 0;
        paused_until = 0;
    }

    [[nodiscard]] size_t slotsAvailable() const noexcept
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
    [[nodiscard]] bool isPaused() const noexcept
    {
        return paused_until > tr_time();
    }

    [[nodiscard]] constexpr size_t maxConnections() const noexcept
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

class tr_webseed : public tr_peer
{
public:
    tr_webseed(struct tr_torrent* tor, std::string_view url, tr_peer_callback callback_in, void* callback_data_in)
        : tr_peer{ tor }
        , torrent_id{ tr_torrentId(tor) }
        , base_url{ url }
        , callback{ callback_in }
        , callback_data{ callback_data_in }
        , bandwidth(&tor->bandwidth_)
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

    [[nodiscard]] tr_torrent* getTorrent() const
    {
        return tr_torrentFindFromId(session, torrent_id);
    }

    [[nodiscard]] bool is_transferring_pieces(uint64_t now, tr_direction direction, unsigned int* setme_Bps) const override
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

    void gotPieceData(uint32_t n_bytes)
    {
        bandwidth.notifyBandwidthConsumed(TR_DOWN, n_bytes, true, tr_time_msec());
        publishClientGotPieceData(n_bytes);
        connection_limiter.gotData();
    }

    void publishRejection(tr_block_span_t block_span)
    {
        auto e = tr_peer_event{};
        e.eventType = TR_PEER_CLIENT_GOT_REJ;

        for (auto block = block_span.begin; block < block_span.end; ++block)
        {
            auto const loc = getTorrent()->blockLoc(block);
            e.pieceIndex = loc.piece;
            e.offset = loc.piece_offset;
            publish(&e);
        }
    }

    void publishGotBlock(tr_torrent const* tor, tr_block_info::Location loc)
    {
        TR_ASSERT(loc.block_offset == 0);
        TR_ASSERT(loc.block < tor->blockCount());

        auto e = tr_peer_event{};
        e.eventType = TR_PEER_CLIENT_GOT_BLOCK;
        e.pieceIndex = loc.piece;
        e.offset = loc.piece_offset;
        e.length = tor->blockSize(loc.block);
        publish(&e);
    }

    int const torrent_id;
    std::string const base_url;
    tr_peer_callback const callback;
    void* const callback_data;

    Bandwidth bandwidth;
    ConnectionLimiter connection_limiter;
    std::set<tr_webseed_task*> tasks;

private:
    void publish(tr_peer_event* event)
    {
        if (callback != nullptr)
        {
            (*callback)(this, event, callback_data);
        }
    }

    void publishClientGotPieceData(uint32_t length)
    {
        auto e = tr_peer_event{};
        e.eventType = TR_PEER_CLIENT_GOT_PIECE_DATA;
        e.length = length;
        publish(&e);
    }

    void startTimer()
    {
        tr_timerAddMsec(*pulse_timer, IdleTimerMsec);
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

struct write_block_data
{
private:
    std::shared_ptr<evbuffer> const content_{ evbuffer_new(), evbuffer_free };

public:
    write_block_data(tr_session* session_in, int torrent_id_in, tr_webseed* webseed_in, tr_block_info::Location loc_in)
        : session{ session_in }
        , torrent_id{ torrent_id_in }
        , webseed{ webseed_in }
        , loc{ loc_in }
    {
        TR_ASSERT(loc.block_offset == 0);
    }

    [[nodiscard]] auto* content() const
    {
        return content_.get();
    }

    void write_block_func()
    {
        auto* const buf = this->content();

        if (auto* const tor = tr_torrentFindFromId(this->session, this->torrent_id); tor != nullptr)
        {
            auto const len = evbuffer_get_length(buf);
            TR_ASSERT(tor->blockSize(this->loc.block) == len);
            tr_cacheWriteBlock(tor->session->cache, tor, this->loc, len, buf);
            webseed->publishGotBlock(tor, this->loc);
            TR_ASSERT(evbuffer_get_length(buf) == 0);
        }

        delete this;
    }

private:
    tr_session* const session;
    int const torrent_id;
    tr_webseed* const webseed;
    tr_block_info::Location const loc;
};

void useFetchedBlocks(tr_webseed_task* task)
{
    auto* const session = task->session;
    auto const lock = session->unique_lock();

    auto* const webseed = task->webseed;
    auto const* const tor = webseed->getTorrent();
    if (tor == nullptr)
    {
        return;
    }

    auto* const buf = task->content();
    for (;;)
    {
        auto const block_size = tor->blockSize(task->loc.block);
        if (evbuffer_get_length(buf) < block_size)
        {
            break;
        }

        if (tor->hasBlock(task->loc.block))
        {
            evbuffer_drain(buf, block_size);
        }
        else
        {
            auto* const data = new write_block_data{ session, tor->uniqueId, webseed, task->loc };
            evbuffer_remove_buffer(task->content(), data->content(), block_size);
            tr_runInEventThread(session, &write_block_data::write_block_func, data);
        }

        task->loc = tor->byteLoc(task->loc.byte + block_size);

        TR_ASSERT(task->loc.byte <= task->end_byte);
        TR_ASSERT(task->loc.byte == task->end_byte || task->loc.block_offset == 0);
    }
}

/***
****
***/

void onBufferGotData(evbuffer* /*buf*/, evbuffer_cb_info const* info, void* vtask)
{
    size_t const n_added = info->n_added;
    auto* const task = static_cast<tr_webseed_task*>(vtask);
    if (n_added == 0 || task->dead)
    {
        return;
    }

    auto const lock = task->session->unique_lock();
    task->webseed->gotPieceData(n_added);
}

void task_request_next_chunk(tr_webseed_task* task);

void on_idle(tr_webseed* w)
{
    auto* const tor = w->getTorrent();
    if (tor == nullptr || !tor->isRunning || tor->isDone())
    {
        return;
    }

    auto const slots_available = w->connection_limiter.slotsAvailable();
    if (slots_available == 0)
    {
        return;
    }

    // Prefer to request large, contiguous chunks from webseeds.
    // The actual value of '64' is arbitrary here; we could probably
    // be smarter about this.
    auto constexpr PreferredBlocksPerTask = size_t{ 64 };
    auto const spans = tr_peerMgrGetNextRequests(tor, w, slots_available * PreferredBlocksPerTask);
    for (size_t i = 0; i < slots_available && i < std::size(spans); ++i)
    {
        auto const& span = spans[i];
        auto* const task = new tr_webseed_task{ tor, w, span };
        evbuffer_add_cb(task->content(), onBufferGotData, task);
        w->tasks.insert(task);
        task_request_next_chunk(task);

        tr_peerMgrClientSentRequests(tor, w, span);
    }
}

void onPartialDataFetched(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, vtask] = web_response;
    bool const success = status == 206;

    auto* const task = static_cast<tr_webseed_task*>(vtask);
    auto* const webseed = task->webseed;

    webseed->connection_limiter.taskFinished(success);

    if (task->dead)
    {
        delete task;
        return;
    }

    if (auto const* const tor = webseed->getTorrent(); tor == nullptr)
    {
        return;
    }

    if (!success)
    {
        webseed->publishRejection({ task->loc.block, task->blocks.end });
        webseed->tasks.erase(task);
        delete task;
        return;
    }

    useFetchedBlocks(task);

    if (task->loc.byte < task->end_byte)
    {
        // Request finished successfully but there's still data missing.
        // That means we've reached the end of a file and need to request
        // the next one
        task_request_next_chunk(task);
        return;
    }

    TR_ASSERT(evbuffer_get_length(task->content()) == 0);
    TR_ASSERT(task->loc.byte == task->end_byte);
    webseed->tasks.erase(task);
    delete task;

    on_idle(webseed);
}

template<typename OutputIt>
void makeUrl(tr_webseed* w, std::string_view name, OutputIt out)
{
    auto const url = w->base_url;

    out = std::copy(std::begin(url), std::end(url), out);

    if (tr_strvEndsWith(url, "/"sv) && !std::empty(name))
    {
        tr_http_escape(out, name, false);
    }
}

void task_request_next_chunk(tr_webseed_task* task)
{
    auto* const webseed = task->webseed;
    auto* const tor = webseed->getTorrent();
    if (tor == nullptr)
    {
        return;
    }

    auto const loc = tor->byteLoc(task->loc.byte + evbuffer_get_length(task->content()));

    auto const [file_index, file_offset] = tor->fileOffset(loc);
    auto const left_in_file = tor->fileSize(file_index) - file_offset;
    auto const left_in_task = task->end_byte - loc.byte;
    auto const this_chunk = std::min(left_in_file, left_in_task);
    TR_ASSERT(this_chunk > 0U);

    webseed->connection_limiter.taskStarted();

    auto url = tr_urlbuf{};
    makeUrl(webseed, tor->fileSubpath(file_index), std::back_inserter(url));
    auto options = tr_web::FetchOptions{ url.sv(), onPartialDataFetched, task };
    options.range = fmt::format(FMT_STRING("{:d}-{:d}"), file_offset, file_offset + this_chunk - 1);
    options.speed_limit_tag = tor->uniqueId;
    options.buffer = task->content();
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
