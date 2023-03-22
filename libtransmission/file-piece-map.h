// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm> // for std::binary_search()
#include <cstdint> // for uint64_t
#include <cstddef> // for size_t
#include <vector>

#include "transmission.h"

#include "bitfield.h"
#include "torrent-metainfo.h"

struct tr_block_info;
struct tr_torrent_metainfo;

class tr_file_piece_map
{
public:
    template<typename T>
    struct index_span_t
    {
        T begin;
        T end;
    };
    using file_span_t = index_span_t<tr_file_index_t>;
    using piece_span_t = index_span_t<tr_piece_index_t>;

    template<typename T>
    struct offset_t
    {
        T index;
        uint64_t offset;
    };

    using file_offset_t = offset_t<tr_file_index_t>;

    explicit tr_file_piece_map(tr_torrent_metainfo const& tm)
    {
        reset(tm);
    }

    tr_file_piece_map(tr_block_info const& block_info, uint64_t const* file_sizes, size_t n_files)
    {
        reset(block_info, file_sizes, n_files);
    }

    void reset(tr_torrent_metainfo const& tm);

    [[nodiscard]] TR_CONSTEXPR20 piece_span_t pieceSpan(tr_file_index_t file) const noexcept
    {
        return file_pieces_[file];
    }

    [[nodiscard]] file_span_t fileSpan(tr_piece_index_t piece) const;

    [[nodiscard]] file_offset_t fileOffset(uint64_t offset) const;

    [[nodiscard]] TR_CONSTEXPR20 size_t size() const
    {
        return std::size(file_pieces_);
    }

    [[nodiscard]] TR_CONSTEXPR20 bool empty() const noexcept
    {
        return std::empty(file_pieces_);
    }

    // TODO(ckerr) minor wart here, two identical span types
    [[nodiscard]] TR_CONSTEXPR20 tr_byte_span_t byteSpan(tr_file_index_t file) const
    {
        auto const& span = file_bytes_.at(file);
        return tr_byte_span_t{ span.begin, span.end };
    }

    [[nodiscard]] TR_CONSTEXPR20 bool is_edge_piece(tr_piece_index_t piece) const
    {
        return std::binary_search(std::begin(edge_pieces_), std::end(edge_pieces_), piece);
    }

private:
    void reset(tr_block_info const& block_info, uint64_t const* file_sizes, size_t n_files);

    using byte_span_t = index_span_t<uint64_t>;
    std::vector<byte_span_t> file_bytes_;

    std::vector<piece_span_t> file_pieces_;

    std::vector<tr_piece_index_t> edge_pieces_;

    template<typename T>
    struct CompareToSpan
    {
        using span_t = index_span_t<T>;

        [[nodiscard]] constexpr int compare(T item, span_t span) const // <=>
        {
            if (item < span.begin)
            {
                return -1;
            }

            if (item >= span.end)
            {
                return 1;
            }

            return 0;
        }

        [[nodiscard]] constexpr bool operator()(T item, span_t span) const // <
        {
            return compare(item, span) < 0;
        }

        [[nodiscard]] constexpr int compare(span_t span, T item) const // <=>
        {
            return -compare(item, span);
        }

        [[nodiscard]] constexpr bool operator()(span_t span, T item) const // <
        {
            return compare(span, item) < 0;
        }
    };
};

class tr_file_priorities
{
public:
    TR_CONSTEXPR20 explicit tr_file_priorities(tr_file_piece_map const* fpm) noexcept
        : fpm_{ fpm }
    {
    }

    void reset(tr_file_piece_map const*);
    void set(tr_file_index_t file, tr_priority_t priority);
    void set(tr_file_index_t const* files, size_t n, tr_priority_t priority);

    [[nodiscard]] tr_priority_t filePriority(tr_file_index_t file) const;
    [[nodiscard]] tr_priority_t piecePriority(tr_piece_index_t piece) const;

private:
    tr_file_piece_map const* fpm_;
    std::vector<tr_priority_t> priorities_;
};

class tr_files_wanted
{
public:
    explicit tr_files_wanted(tr_file_piece_map const* fpm)
        : wanted_(std::size(*fpm))
    {
        reset(fpm);
    }
    void reset(tr_file_piece_map const* fpm);

    void set(tr_file_index_t file, bool wanted);
    void set(tr_file_index_t const* files, size_t n, bool wanted);

    [[nodiscard]] TR_CONSTEXPR20 bool fileWanted(tr_file_index_t file) const
    {
        return wanted_.test(file);
    }

    [[nodiscard]] bool pieceWanted(tr_piece_index_t piece) const;

private:
    tr_file_piece_map const* fpm_;
    tr_bitfield wanted_;
};
