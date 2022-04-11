// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::copy_n
#include <array>
#include <iterator>
#include <map>
#include <limits>
#include <utility> // std::pair

#include "transmission.h"

#include "block-info.h"
#include "tr-assert.h"
#include "cache-new.h"

/****
*****
****/

namespace
{

auto constexpr BlockSize = tr_block_info::BlockSize;

struct cache_block
{
    std::array<uint8_t, BlockSize> data;
    uint64_t age_;
    uint32_t length;
};

using cache_key_t = std::pair<tr_torrent_id_t, tr_block_index_t>;

constexpr cache_key_t makeKey(tr_torrent_id_t tor_id, tr_block_index_t block_index) noexcept
{
    return std::make_pair(tor_id, block_index);
}

using block_map_t = std::map<cache_key_t, cache_block>;

} // namespace

/****
*****
****/

class tr_write_cache_impl final : public tr_write_cache
{
public:
    tr_write_cache_impl(tr_torrent_io& torrent_io, size_t max_blocks)
        : io_{ torrent_io }
    {
        setMaxBlocks(max_blocks);
    }

    ~tr_write_cache_impl() final
    {
        setMaxBlocks(0);
    }

    bool put(tr_torrent_id_t tor_id, tr_block_span_t span, uint8_t const* block_data) override
    {
        for (auto block = span.begin; block < span.end; ++block)
        {
            auto& entry = blocks_[makeKey(tor_id, block)];
            auto const n_bytes = io_.blockSize(tor_id, block);
            entry.length = n_bytes;
            entry.age_ = age_++;
            std::copy_n(block_data, n_bytes, std::data(entry.data));
            block_data += n_bytes;
        }
        trim();
        return true;
    }

    bool get(tr_torrent_id_t tor_id, tr_block_span_t span, uint8_t* data_out) override
    {
        for (auto begin = span.begin; begin < span.end;)
        {
            auto const iter = blocks_.find(makeKey(tor_id, begin));
            if (iter != std::end(blocks_))
            {
                data_out = std::copy_n(std::data(iter->second.data), iter->second.length, data_out);
                ++begin;
            }
            else // request the uncached span from io_
            {
                // build a subspan of uncached blocks
                auto end = begin;
                auto span_bytes = size_t{};
                while (end != span.end && !has(tor_id, end))
                {
                    span_bytes += blockSize(tor_id, end);
                    ++end;
                }

                // get that subspan fro mio_
                if (!io_.get(tor_id, { begin, end }, data_out))
                {
                    return false;
                }
                data_out += span_bytes;
                begin = end;
            }
        }

        return true;
    }

    void prefetch(tr_torrent_id_t tor_id, tr_block_span_t span) override
    {
        for (auto begin = span.begin; begin < span.end; ++begin)
        {
            auto end = begin;
            while (end != span.end && !has(tor_id, end))
            {
                ++end;
            }

            if (end != begin)
            {
                io_.prefetch(tor_id, { begin, end });
            }

            begin = end;
        }
    }

    void setMaxBlocks(size_t max_blocks) override
    {
        max_blocks_ = max_blocks;
        trim();
    }

    [[nodiscard]] uint32_t blockSize(tr_torrent_id_t tor_id, tr_block_index_t block) const override
    {
        return io_.blockSize(tor_id, block);
    }

    [[nodiscard]] size_t maxBlocks() const noexcept override
    {
        return max_blocks_;
    }

    bool saveTorrent(tr_torrent_id_t tor_id) override
    {
        using block_limits = std::numeric_limits<tr_block_index_t>;
        return flushRange(
            blocks_.lower_bound(makeKey(tor_id, block_limits::min())),
            blocks_.upper_bound(makeKey(tor_id, block_limits::max())));
    }

    bool saveSpan(tr_torrent_id_t tor_id, tr_block_span_t span) override
    {
        return flushRange(blocks_.lower_bound(makeKey(tor_id, span.begin)), blocks_.lower_bound(makeKey(tor_id, span.end)));
    }

private:
    [[nodiscard]] bool has(tr_torrent_id_t tor_id, tr_block_index_t block) const
    {
        return blocks_.count(makeKey(tor_id, block)) != 0U;
    }

    [[nodiscard]] bool has(tr_torrent_id_t tor_id, tr_block_span_t span) const
    {
        for (auto block = span.begin; block < span.end; ++block)
        {
            if (!has(tor_id, block))
            {
                return false;
            }
        }
        return true;
    }

    void trim()
    {
        while (std::size(blocks_) > max_blocks_)
        {
            trimNext();
        }
    }

    [[nodiscard]] static auto findRunBegin(block_map_t::const_iterator begin, block_map_t::iterator iter) noexcept
    {
        for (;;)
        {
            if (iter == begin)
            {
                return iter;
            }

            auto const [torrent_id, block] = iter->first;
            auto prev = iter;
            --prev;
            auto const [prev_torrent_id, prev_block] = prev->first;
            if (prev_torrent_id != torrent_id || prev_block + 1 != block)
            {
                return iter;
            }

            iter = prev;
        }
    }

    [[nodiscard]] static auto findRunLast(block_map_t::const_iterator iter, block_map_t::const_iterator end)
    {
        TR_ASSERT(iter != end);

        for (;;)
        {
            auto const [tor_id, block] = iter->first;

            auto next = iter;
            ++next;
            if (next == end)
            {
                return iter;
            }

            auto const [next_tor_id, next_block] = next->first;
            if (tor_id != next_tor_id || block + 1 != next_block)
            {
                return iter;
            }

            iter = next;
        }
    }

    [[nodiscard]] bool flushSpan(block_map_t::const_iterator begin, block_map_t::const_iterator end)
    {
        auto const [tor_id, first_block] = begin->first;
        auto block = tr_block_index_t{ first_block };

        using buf_t = std::vector<uint8_t>;
        auto buf = buf_t{};
        buf.reserve(BlockSize * std::distance(begin, end));

        for (auto walk = begin; walk != end; ++walk)
        {
            auto const [walk_tor_id, walk_block] = walk->first;
            TR_ASSERT(tor_id == walk_tor_id);
            TR_ASSERT(block == walk_block);
            std::copy_n(std::data(walk->second.data), walk->second.length, std::back_inserter(buf));
            ++block;
        }

        // save it + remove it
        auto const did_save = io_.put(tor_id, { first_block, block }, std::data(buf));
        blocks_.erase(begin, end);
        return did_save;
    }

    [[nodiscard]] bool flushRange(block_map_t::const_iterator begin, block_map_t::const_iterator end)
    {
        using buf_t = std::vector<uint8_t>;
        auto buf = buf_t{};

        while (begin != end)
        {
            auto span_begin = begin;
            auto span_end = ++findRunLast(span_begin, end);
            if (!flushSpan(span_begin, span_end))
            {
                return false;
            }

            begin = span_end;
        }

        return true;
    }

    bool trimNext()
    {
        // find the oldest cached block
        auto iter = std::min_element(
            std::begin(blocks_),
            std::end(blocks_),
            [](auto const& lhs, auto const& rhs) { return lhs.second.age_ < rhs.second.age_; });
        if (iter == std::end(blocks_))
        {
            return false;
        }

        // find the bounds of the span which has oldest block
        auto const begin = findRunBegin(std::begin(blocks_), iter);
        auto const end = ++findRunLast(iter, std::end(blocks_));
        return flushRange(begin, end);
    }

    static uint64_t age_;

    block_map_t blocks_ = {};

    tr_torrent_io& io_;

    size_t max_blocks_ = {};

    // size_t disk_writes = {};
    // size_t disk_write_bytes = {};
    // size_t cache_writes = {};
    // size_t cache_write_bytes = {};
};

uint64_t tr_write_cache_impl::age_ = {};

/****
*****
****/

tr_write_cache* tr_writeCacheNew(tr_torrent_io& torrent_io, size_t max_blocks)
{
    return new tr_write_cache_impl(torrent_io, max_blocks);
}
