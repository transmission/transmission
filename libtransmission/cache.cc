// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno> // EINVAL
#include <cstddef>
#include <cstdint> // uint8_t
#include <iterator> // std::distance(), std::next(), std::prev()
#include <memory>
#include <numeric> // std::accumulate()
#include <utility> // std::make_pair()
#include <vector>

#include <fmt/format.h>

#include "libtransmission/transmission.h"

#include "libtransmission/cache.h"
#include "libtransmission/inout.h"
#include "libtransmission/log.h"
#include "libtransmission/torrent.h"
#include "libtransmission/torrents.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h" // _()

Cache::Key Cache::make_key(tr_torrent const& tor, tr_block_info::Location const loc) noexcept
{
    return std::make_pair(tor.id(), loc.block);
}

Cache::CIter Cache::find_span_end(CIter const& span_begin, CIter const& end) noexcept
{
    static constexpr auto NotAdjacent = [](CacheBlock const& block1, CacheBlock const& block2)
    {
        return block1.key.first != block2.key.first || block1.key.second + 1 != block2.key.second;
    };
    auto const span_end = std::adjacent_find(span_begin, end, NotAdjacent);
    return span_end == end ? end : std::next(span_end);
}

std::pair<Cache::CIter, Cache::CIter> Cache::find_biggest_span(CIter const& begin, CIter const& end) noexcept
{
    auto biggest_begin = begin;
    auto biggest_end = begin;
    auto biggest_len = std::distance(biggest_begin, biggest_end);

    for (auto span_begin = begin; span_begin < end;)
    {
        auto span_end = find_span_end(span_begin, end);

        if (auto const len = std::distance(span_begin, span_end); len > biggest_len)
        {
            biggest_begin = span_begin;
            biggest_end = span_end;
            biggest_len = len;
        }

        span_begin = span_end;
    }

    return { biggest_begin, biggest_end };
}

int Cache::write_contiguous(CIter const& begin, CIter const& end) const
{
    // The most common case without an extra data copy.
    auto const* out = std::data(*begin->buf);
    auto outlen = std::size(*begin->buf);

    // Contiguous area to join more than one block, if any.
    auto buf = std::vector<uint8_t>{};

    if (end - begin > 1)
    {
        // copy blocks into contiguous memory
        auto const buflen = std::accumulate(
            begin,
            end,
            size_t{},
            [](size_t sum, auto const& block) { return sum + std::size(*block.buf); });
        buf.resize(buflen);
        auto* walk = std::data(buf);
        for (auto iter = begin; iter != end; ++iter)
        {
            TR_ASSERT(begin->key.first == iter->key.first);
            TR_ASSERT(begin->key.second + std::distance(begin, iter) == iter->key.second);
            walk = std::copy_n(std::data(*iter->buf), std::size(*iter->buf), walk);
        }
        TR_ASSERT(std::data(buf) + std::size(buf) == walk);
        out = std::data(buf);
        outlen = std::size(buf);
    }

    // save it
    auto const& [torrent_id, block] = begin->key;
    auto* const tor = torrents_.get(torrent_id);
    if (tor == nullptr)
    {
        return EINVAL;
    }

    auto const loc = tor->block_loc(block);

    if (auto const err = tr_ioWrite(*tor, loc, outlen, out); err != 0)
    {
        return err;
    }

    ++disk_writes_;
    disk_write_bytes_ += outlen;
    return {};
}

int Cache::set_limit(Memory const max_size)
{
    max_blocks_ = get_max_blocks(max_size);
    tr_logAddDebug(fmt::format("Maximum cache size set to {} ({} blocks)", max_size.to_string(), max_blocks_));

    return cache_trim().first;
}

Cache::Cache(tr_torrents const& torrents, Memory const max_size)
    : torrents_{ torrents }
    , max_blocks_{ get_max_blocks(max_size) }
{
}

// ---

int Cache::write_block(tr_torrent_id_t const tor_id, tr_block_index_t const block, std::unique_ptr<BlockData> writeme)
{
    if (max_blocks_ == 0U)
    {
        TR_ASSERT(std::empty(blocks_));

        // Bypass cache. This may be helpful for those whose filesystem
        // already has a cache layer for the very purpose of this cache
        // https://github.com/transmission/transmission/pull/5668
        auto* const tor = torrents_.get(tor_id);
        return tor == nullptr ? EINVAL : tr_ioWrite(*tor, tor->block_loc(block), std::size(*writeme), std::data(*writeme));
    }

    auto const key = Key{ tor_id, block };
    auto iter = std::lower_bound(std::begin(blocks_), std::end(blocks_), key, CompareCacheBlockByKey);
    if (iter == std::end(blocks_) || iter->key != key)
    {
        iter = blocks_.emplace(iter);
        iter->key = key;
    }

    iter->buf = std::move(writeme);

    ++cache_writes_;
    cache_write_bytes_ += std::size(*iter->buf);

    if (auto const [err, err_tor_id] = cache_trim(); err != 0)
    {
        // The error belongs to whichever torrent failed to flush.
        // Report it here only if that torrent is the caller's, so that
        // the caller doesn't mark this block as complete.
        if (err_tor_id == tor_id || torrents_.get(tor_id) == nullptr)
        {
            return err;
        }
    }

    return 0;
}

Cache::CIter Cache::get_block(tr_torrent const& tor, tr_block_info::Location const& loc) noexcept
{
    if (auto const [begin, end] = std::equal_range(
            std::begin(blocks_),
            std::end(blocks_),
            make_key(tor, loc),
            CompareCacheBlockByKey);
        begin < end)
    {
        return begin;
    }

    return std::end(blocks_);
}

int Cache::read_block(tr_torrent const& tor, tr_block_info::Location const& loc, size_t len, uint8_t* setme)
{
    if (auto const iter = get_block(tor, loc); iter != std::end(blocks_))
    {
        std::copy_n(std::begin(*iter->buf), len, setme);
        return {};
    }

    return tr_ioRead(tor, loc, len, setme);
}

// ---

Cache::FlushResult Cache::flush_span(CIter const& begin, CIter const& end)
{
    if (begin == end)
    {
        return {};
    }

    // Extract the span from the cache before writing it, for two reasons:
    //
    // 1. A failed tr_ioWrite() stops the offending torrent with a local
    //    error, which reentrantly flushes and erases that torrent's other
    //    cached blocks, invalidating any iterator into `blocks_`.
    //
    // 2. A span that can't be written must be dropped, not kept. Keeping
    //    unwritable blocks would wedge the cache and starve the torrents
    //    that can still write. (#5747)
    auto const first = std::distance(std::cbegin(blocks_), begin);
    auto const count = std::distance(begin, end);
    auto pending = Blocks{};
    pending.reserve(static_cast<size_t>(count));
    std::move(std::begin(blocks_) + first, std::begin(blocks_) + first + count, std::back_inserter(pending));
    blocks_.erase(begin, end);

    // all blocks in [begin, end) belong to the same torrent
    auto const tor_id = pending.front().key.first;

    for (auto span_begin = std::cbegin(pending), pending_end = std::cend(pending); span_begin < pending_end;)
    {
        auto const span_end = find_span_end(span_begin, pending_end);

        if (auto const err = write_contiguous(span_begin, span_end); err != 0)
        {
            // Stop at the first failure instead of writing the rest:
            // a failed tr_ioWrite() has stopped the torrent and closed
            // its files, and writing on would reopen them for a torrent
            // that can no longer use them.
            //
            // The unwritten blocks can't stay cached, so their pieces
            // are cleared from the torrent's completion. That keeps
            // completion, resume files, and verification truthful, and
            // the pieces are simply downloaded again. (If the torrent
            // is gone from the session there's nothing left to clear.)
            if (auto* const tor = torrents_.get(tor_id); tor != nullptr)
            {
                for (auto iter = span_begin; iter != pending_end; ++iter)
                {
                    auto const& [key, buf] = *iter;
                    auto const begin_piece = tor->block_loc(key.second).piece;
                    auto const last_byte = tor->block_loc(key.second).byte + std::size(*buf) - 1U;
                    auto const end_piece = tor->byte_loc(last_byte).piece;
                    for (auto piece = begin_piece; piece <= end_piece; ++piece)
                    {
                        tor->set_has_piece(piece, false);
                    }
                }

                tr_logAddWarnTor(
                    tor,
                    fmt::format(
                        fmt::runtime(_("Couldn't write {count} cached blocks to disk; they will be downloaded again")),
                        fmt::arg("count", std::distance(span_begin, pending_end))));
            }

            return { err, tor_id };
        }

        span_begin = span_end;
    }

    return {};
}

int Cache::flush_file(tr_torrent const& tor, tr_file_index_t const file)
{
    auto const tor_id = tor.id();
    auto const [block_begin, block_end] = tor.block_span_for_file(file);

    return flush_span(
               std::lower_bound(
                   std::begin(blocks_),
                   std::end(blocks_),
                   std::make_pair(tor_id, block_begin),
                   CompareCacheBlockByKey),
               std::lower_bound(
                   std::begin(blocks_),
                   std::end(blocks_),
                   std::make_pair(tor_id, block_end),
                   CompareCacheBlockByKey))
        .first;
}

int Cache::flush_torrent(tr_torrent_id_t const tor_id)
{
    return flush_span(
               std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, 0), CompareCacheBlockByKey),
               std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id + 1, 0), CompareCacheBlockByKey))
        .first;
}

Cache::FlushResult Cache::flush_biggest()
{
    auto const [begin, end] = find_biggest_span(std::begin(blocks_), std::end(blocks_));

    if (begin == end) // nothing to flush
    {
        return {};
    }

    return flush_span(begin, end);
}

Cache::FlushResult Cache::cache_trim()
{
    while (std::size(blocks_) > max_blocks_)
    {
        if (auto const res = flush_biggest(); res.first != 0)
        {
            return res;
        }
    }

    return {};
}
