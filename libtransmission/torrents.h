// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <ctime>
#include <iterator>
#include <map>
#include <string_view>
#include <vector>

#include "tr-macros.h" // tr_sha1_digest_t

struct tr_torrent;
struct tr_torrent_metainfo;

class tr_torrents
{
public:
    [[nodiscard]] tr_torrent const* fromHash(tr_sha1_digest_t const& hash) const
    {
        auto const it = by_hash_.find(hash);
        return it != std::end(by_hash_) ? it->second : nullptr;
    }

    [[nodiscard]] tr_torrent* fromHash(tr_sha1_digest_t const& hash)
    {
        auto const it = by_hash_.find(hash);
        return it != std::end(by_hash_) ? it->second : nullptr;
    }

    [[nodiscard]] bool contains(tr_sha1_digest_t const& hash) const
    {
        return fromHash(hash) != nullptr;
    }

    [[nodiscard]] tr_torrent const* fromId(int id) const;

    [[nodiscard]] tr_torrent const* fromMagnet(std::string_view magnet_link) const;

    [[nodiscard]] tr_torrent const* fromMetainfo(tr_torrent_metainfo const& metainfo) const;

    [[nodiscard]] tr_torrent* fromId(int id);

    [[nodiscard]] tr_torrent* fromMagnet(std::string_view magnet_link);

    [[nodiscard]] tr_torrent* fromMetainfo(tr_torrent_metainfo const* metainfo);

    // returns the unique ID of the torrent
    [[nodiscard]] int add(tr_torrent* tor);

    void remove(tr_torrent const* tor, time_t current_time);

    class iterator
    {
    public:
        using impl_t = std::map<tr_sha1_digest_t, tr_torrent*>::iterator;
        explicit iterator(impl_t in)
            : it{ in }
        {
        }

        bool operator!=(iterator that) const
        {
            return it != that.it;
        }

        tr_torrent* operator*() const
        {
            return it->second;
        }

        iterator& operator++()
        {
            ++it;
            return *this;
        }

    private:
        impl_t it;
    };

    iterator begin()
    {
        return iterator(std::begin(by_hash_));
    }

    iterator end()
    {
        return iterator(std::end(by_hash_));
    }

    class const_iterator
    {
    public:
        using impl_t = std::map<tr_sha1_digest_t, tr_torrent*>::const_iterator;

        explicit const_iterator(impl_t in)
            : it{ in }
        {
        }

        bool operator!=(const_iterator that) const
        {
            return it != that.it;
        }

        tr_torrent const* operator*() const
        {
            return it->second;
        }

        const_iterator& operator++()
        {
            ++it;
            return *this;
        }

    private:
        impl_t it;
    };

    const_iterator cbegin() const
    {
        return const_iterator(std::cbegin(by_hash_));
    }

    const_iterator cend() const
    {
        return const_iterator(std::cend(by_hash_));
    }

    auto size() const
    {
        return std::size(by_hash_);
    }

    auto empty() const
    {
        return std::empty(by_hash_);
    }

    [[nodiscard]] std::vector<int> removedSince(time_t) const;

private:
    std::vector<tr_torrent*> by_id_;
    std::map<tr_sha1_digest_t, tr_torrent*> by_hash_;
    std::map<int, time_t> removed_;
};
