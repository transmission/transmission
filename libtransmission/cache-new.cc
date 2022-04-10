// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::copy_n
#include <array>
#include <map>
#include <iterator>
#include <utility> // std::pair

#include <iostream>

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

constexpr cache_key_t makeKey(tr_torrent_id_t tor_id, tr_block_index_t block_index)
{
    return std::make_pair(tor_id, block_index);
}

using block_map_t = std::map<cache_key_t, cache_block>;

}

/****
*****
****/

class tr_write_cache_impl final: public tr_write_cache
{
public:
    tr_write_cache_impl(tr_torrent_io& torrent_io, size_t max_bytes): io_{ torrent_io }
    {
        setMaxBytes(max_bytes);
    }

    ~tr_write_cache_impl() final
    {
        setMaxBytes(0);
    }

    bool put(tr_torrent_id_t tor_id, tr_block_span_t span, uint8_t const* block_data) override
    {
        for (auto block = span.begin; block < span.end; ++block)
        {
            std::cerr << __FILE__ << ':' << __LINE__ << " put tor_id " << tor_id << " block " << block << std::endl;
            auto& entry = blocks_[makeKey(tor_id, block)];
            auto const n_bytes = io_.blockSize(block);
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
        for (auto begin = span.begin; begin < span.end; )
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
                    span_bytes += blockSize(end);
                    ++end;
                }

                // get that subspan fro mio_
                std::cerr << __FILE__ << ':' << __LINE__ << " about to call io_.get for { begin:" << begin << ", end:" << end << " }" << std::endl;
                if (!io_.get(tor_id, { begin, end }, data_out))
                {
                    std::cerr << __FILE__ << ':' << __LINE__ << " io_.get " << begin << ", " << end << " returned false" << std::endl;
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

    void setMaxBytes(size_t max_bytes) override
    {
        max_bytes_ = max_bytes;
        max_blocks_ = max_bytes > 0U ? max_bytes_ / BlockSize: 0U;
        trim();
    }

    [[nodiscard]] uint32_t blockSize(tr_block_index_t block) const override
    {
        return io_.blockSize(block);
    }

    [[nodiscard]] size_t maxBytes() const override
    {
        return max_bytes_;
    }

    void saveTorrent(tr_torrent_id_t /*tor_id*/) override
    {
        // FIXME
    }

    void saveSpan(tr_torrent_id_t /*tor_id*/, tr_block_span_t /*span*/) override
    {
        // FIXME
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

    static auto findRunBegin(block_map_t::const_iterator begin, block_map_t::iterator iter)
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

    static auto findRunLast(block_map_t::iterator iter, block_map_t::const_iterator end)
    {
        TR_ASSERT(iter != end);

        for (;;)
        {
            auto const [tor_id, block] = iter->first;
            std::cerr << __FILE__ << ':' << __LINE__ << " iter tor_id " << tor_id << " block " << block << std::endl;
            std::cerr << __FILE__ << ':' << __LINE__ << " step next" << std::endl;

            auto next = iter;
            ++next;
            if (next == end || next->first != makeKey(tor_id, block + 1))
            {
                std::cerr << __FILE__ << ':' << __LINE__ << " next is end or not a match; returning" << std::endl;
                return iter;
            }
            iter = next;
        }
    }

    bool trimNext()
    {
        std::cerr << __FILE__ << ':' << __LINE__ << " blocks_.size() is " << std::size(blocks_) << std::endl;

        // find the oldest cached block
        auto iter = std::min_element(
            std::begin(blocks_),
            std::end(blocks_),
            [](auto const& lhs, auto const& rhs) { return lhs.second.age_ < rhs.second.age_; });
        if (iter == std::end(blocks_))
        {
            return false;
        }
        std::cerr << __FILE__ << ':' << __LINE__ << " oldest is tor_id " << iter->first.first << " block " << iter->first.second << std::endl;

        // find the span that includes that oldest block
        auto const begin = findRunBegin(std::begin(blocks_), iter);
        std::cerr << __FILE__ << ':' << __LINE__ << " begin is tor_id " << begin->first.first << " block " << begin->first.second << std::endl;
        auto const last = findRunLast(iter, std::end(blocks_));

        // build a block span for it
        auto const [torrent_id, first_block] = begin->first;
        auto end = last;
        ++end;
        std::cerr << __FILE__ << ':' << __LINE__ << " last is tor_id " << last->first.first << " block " << last->first.second << std::endl;
        auto const span = tr_block_span_t{ begin->first.second, last->first.second + 1 };
        std::cerr << __FILE__ << ':' << __LINE__ << " span is { " << span.begin << ", " << span.end << " }" << std::endl;
        auto const n_blocks = span.end - span.begin;
        TR_ASSERT(begin->first.first == last->first.first);
        TR_ASSERT(std::distance(begin, end) == span.end - span.begin);

        // save that span
        using buf_t = std::vector<uint8_t>;
        auto buf = buf_t{};
        buf.reserve(n_blocks * static_cast<buf_t::size_type>(BlockSize));
        for (auto walk = begin; walk != end; ++walk)
        {
            TR_ASSERT(walk->first.first == torrent_id);
            std::copy_n(std::data(walk->second.data), walk->second.length, std::back_inserter(buf));
        }
        auto const did_save = io_.put(torrent_id, span, std::data(buf));

        // remove that span from the cache
        blocks_.erase(begin, end);

        return did_save;
    }

    static uint64_t age_;

    block_map_t blocks_ = {};

    tr_torrent_io& io_;

    size_t max_blocks_ = {};
    size_t max_bytes_ = {};

    // size_t disk_writes = {};
    // size_t disk_write_bytes = {};
    // size_t cache_writes = {};
    // size_t cache_write_bytes = {};
};

uint64_t tr_write_cache_impl::age_ = {};

/****
*****
****/

tr_write_cache* tr_writeCacheNew(tr_torrent_io& torrent_io, size_t max_bytes)
{
    return new tr_write_cache_impl(torrent_io, max_bytes);
}
