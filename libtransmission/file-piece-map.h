/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <vector>

#include "transmission.h"

#include "bitfield.h"

struct tr_block_info;

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

    tr_file_piece_map(tr_block_info const& block_info, uint64_t const* file_sizes, size_t n_files);
    tr_file_piece_map(tr_block_info const& block_info, tr_info const& info);
    [[nodiscard]] piece_span_t pieceSpan(tr_file_index_t file) const;
    [[nodiscard]] file_span_t fileSpan(tr_piece_index_t piece) const;
    [[nodiscard]] size_t size() const
    {
        return std::size(files_);
    }

private:
    void init(tr_block_info const& block_info, uint64_t const* file_sizes, size_t n_files);
    std::vector<piece_span_t> files_;
};

class tr_file_priorities
{
public:
    explicit tr_file_priorities(tr_file_piece_map const& fpm);
    void set(tr_file_index_t file, tr_priority_t priority);
    void set(tr_file_index_t const* files, size_t n, tr_priority_t priority);

    [[nodiscard]] tr_priority_t filePriority(tr_file_index_t file) const;
    [[nodiscard]] tr_priority_t piecePriority(tr_piece_index_t piece) const;

private:
    tr_file_piece_map const& fpm_;
    std::vector<tr_priority_t> priorities_;
};

class tr_files_wanted
{
public:
    explicit tr_files_wanted(tr_file_piece_map const& fpm);
    void set(tr_file_index_t file, bool wanted);
    void set(tr_file_index_t const* files, size_t n, bool wanted);

    [[nodiscard]] tr_priority_t fileWanted(tr_file_index_t file) const;
    [[nodiscard]] tr_priority_t pieceWanted(tr_piece_index_t piece) const;

private:
    tr_file_piece_map const& fpm_;
    tr_bitfield wanted_;
};
