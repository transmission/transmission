/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <iterator>
#include <vector>

#include "transmission.h"

#include "block-info.h"
#include "file-piece-map.h"

void tr_file_piece_map::init(tr_block_info const& block_info, uint64_t const* file_sizes, size_t n_files)
{
    files_.resize(n_files);
    files_.shrink_to_fit();

    uint64_t offset = 0;
    for (tr_file_index_t i = 0; i < n_files; ++i)
    {
        auto const file_size = file_sizes[i];
        auto const begin_piece = block_info.pieceOf(offset);
        tr_piece_index_t end_piece = 0;
        if (file_size != 0)
        {
            auto const last_byte = offset + file_size - 1;
            auto const final_piece = block_info.pieceOf(last_byte);
            end_piece = final_piece + 1;
        }
        else
        {
            end_piece = begin_piece + 1;
        }
        files_[i] = piece_span_t{ begin_piece, end_piece };
        offset += file_size;
    }
}

tr_file_piece_map::tr_file_piece_map(tr_block_info const& block_info, uint64_t const* file_sizes, size_t n_files)
{
    init(block_info, file_sizes, n_files);
}

tr_file_piece_map::tr_file_piece_map(tr_block_info const& block_info, tr_info const& info)
{
    tr_file_index_t const n = info.fileCount;
    auto file_sizes = std::vector<uint64_t>(n);
    std::transform(info.files, info.files + n, std::begin(file_sizes), [](tr_file const& file) { return file.length; });
    init(block_info, std::data(file_sizes), std::size(file_sizes));
}

tr_file_piece_map::piece_span_t tr_file_piece_map::pieceSpan(tr_file_index_t file) const
{
    return files_[file];
}

tr_file_piece_map::file_span_t tr_file_piece_map::fileSpan(tr_piece_index_t piece) const
{
    struct Compare
    {
        int compare(tr_piece_index_t piece, piece_span_t span) const // <=>
        {
            if (piece < span.begin)
            {
                return -1;
            }

            if (piece >= span.end)
            {
                return 1;
            }

            return 0;
        }

        bool operator()(tr_piece_index_t piece, piece_span_t span) const // <
        {
            return compare(piece, span) < 0;
        }

        int compare(piece_span_t span, tr_piece_index_t piece) const // <=>
        {
            return -compare(piece, span);
        }

        bool operator()(piece_span_t span, tr_piece_index_t piece) const // <
        {
            return compare(span, piece) < 0;
        }
    };

    auto const begin = std::begin(files_);
    auto const pair = std::equal_range(begin, std::end(files_), piece, Compare{});
    return { tr_piece_index_t(std::distance(begin, pair.first)), tr_piece_index_t(std::distance(begin, pair.second)) };
}

/***
****
***/

tr_file_priorities::tr_file_priorities(tr_file_piece_map const& fpm)
    : fpm_{ fpm }
{
    auto const n = std::size(fpm);
    priorities_.resize(n);
    priorities_.shrink_to_fit();
    std::fill_n(std::begin(priorities_), n, TR_PRI_NORMAL);
}

void tr_file_priorities::set(tr_file_index_t file, tr_priority_t priority)
{
    priorities_[file] = priority;
}

void tr_file_priorities::set(tr_file_index_t const* files, size_t n, tr_priority_t priority)
{
    for (size_t i = 0; i < n; ++i)
    {
        set(files[i], priority);
    }
}

tr_priority_t tr_file_priorities::filePriority(tr_file_index_t file) const
{
    return priorities_[file];
}

tr_priority_t tr_file_priorities::piecePriority(tr_piece_index_t piece) const
{
    auto const [begin_idx, end_idx] = fpm_.fileSpan(piece);
    auto const begin = std::begin(priorities_) + begin_idx;
    auto const end = std::begin(priorities_) + end_idx;
    auto const it = std::max_element(begin, end);
    if (it == end)
    {
        return TR_PRI_NORMAL;
    }
    return *it;
}

/***
****
***/

tr_files_wanted::tr_files_wanted(tr_file_piece_map const& fpm)
    : fpm_{ fpm }
    , wanted_{ std::size(fpm) }
{
    wanted_.setHasAll(); // by default we want all files
}

void tr_files_wanted::set(tr_file_index_t file, bool wanted)
{
    wanted_.set(file, wanted);
}

void tr_files_wanted::set(tr_file_index_t const* files, size_t n, bool wanted)
{
    for (size_t i = 0; i < n; ++i)
    {
        set(files[i], wanted);
    }
}

tr_priority_t tr_files_wanted::fileWanted(tr_file_index_t file) const
{
    return wanted_.test(file);
}

tr_priority_t tr_files_wanted::pieceWanted(tr_piece_index_t piece) const
{
    auto const [begin, end] = fpm_.fileSpan(piece);
    return wanted_.count(begin, end) != 0;
}
