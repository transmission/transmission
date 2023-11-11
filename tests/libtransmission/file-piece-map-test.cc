// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <numeric>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/bitfield.h>
#include <libtransmission/block-info.h>
#include <libtransmission/file-piece-map.h>

#include "gtest/gtest.h"

class FilePieceMapTest : public ::testing::Test
{
protected:
    static constexpr size_t PieceSize{ tr_block_info::BlockSize };
    static constexpr size_t TotalSize{ 10 * PieceSize + 1 };
    tr_block_info const block_info_{ TotalSize, PieceSize };

    static constexpr std::array<uint64_t, 18> FileSizes{
        0U, // [offset 0] zero-sized file
        PieceSize, // [offset 0] begins and ends on a piece boundary (preceded by zero sized file)
        4U * PieceSize, // [offset P] begins and ends on a piece boundary
        0U, // [offset 5 P] zero-sized files
        0U,
        0U,
        PieceSize / 2U, // [offset 5 P] begins on a piece boundary
        PieceSize, // [offset 5.5 P] neither begins nor ends on a piece boundary, spans >1 piece
        10U, // [offset 6.5 P] small files all contained in a single piece
        9U,
        8U,
        7U,
        6U,
        (3U * PieceSize + PieceSize / 2U + 1U - 10U - 9U - 8U - 7U - 6U), // [offset 5.75P +10+9+8+7+6] ends end-of-torrent
        0U, // [offset 10P+1] zero-sized files at the end-of-torrent
        0U,
        0U,
        0U,
        // sum is 10P + 1 == TotalSize
    };

    void SetUp() override
    {
        static_assert(
            FileSizes[0] + FileSizes[1] + FileSizes[2] + FileSizes[3] + FileSizes[4] + FileSizes[5] + FileSizes[6] +
                FileSizes[7] + FileSizes[8] + FileSizes[9] + FileSizes[10] + FileSizes[11] + FileSizes[12] + FileSizes[13] +
                FileSizes[14] + FileSizes[15] + FileSizes[16] + FileSizes[17] ==
            TotalSize);

        EXPECT_EQ(11U, block_info_.piece_count());
        EXPECT_EQ(PieceSize, block_info_.piece_size());
        EXPECT_EQ(TotalSize, block_info_.total_size());
        EXPECT_EQ(TotalSize, std::accumulate(std::begin(FileSizes), std::end(FileSizes), uint64_t{ 0 }));
    }
};

TEST_F(FilePieceMapTest, fileOffsetNoEmptyFiles)
{
    auto const fpm = tr_file_piece_map{ block_info_, std::data(FileSizes), std::size(FileSizes) };

    // first byte of file #1 (first nonzero file)
    auto file_offset = fpm.file_offset(0U, false);
    EXPECT_EQ(1U, file_offset.index);
    EXPECT_EQ(0U, file_offset.offset);

    // final byte of file #1 (first nonzero file)
    file_offset = fpm.file_offset(FileSizes[1] - 1U, false);
    EXPECT_EQ(1U, file_offset.index);
    EXPECT_EQ(FileSizes[1] - 1U, file_offset.offset);

    // first byte of file #6 (nonzero file preceded by zero file)
    // NB: this is an edge case, file #3 is 0 bytes.
    // The next nonzero file is file #6
    file_offset = fpm.file_offset(FileSizes[1] + FileSizes[2], false);
    EXPECT_EQ(6U, file_offset.index);
    EXPECT_EQ(0U, file_offset.offset);

    // the last byte of in the torrent.
    // NB: reverse of previous edge case, since
    // the final 4 files in the torrent are all 0 bytes
    // the fifth file from end will be selected
    file_offset = fpm.file_offset(TotalSize - 1U, false);
    EXPECT_EQ(13U, file_offset.index);
    EXPECT_EQ(FileSizes[13] - 1U, file_offset.offset);
}

TEST_F(FilePieceMapTest, fileOffsetWithEmptyFiles)
{
    auto const fpm = tr_file_piece_map{ block_info_, std::data(FileSizes), std::size(FileSizes) };

    // first byte of file #0 (zero file)
    auto file_offset = fpm.file_offset(0U, true);
    EXPECT_EQ(0U, file_offset.index);
    EXPECT_EQ(0U, file_offset.offset);

    // first byte of file #3 (zero file preceded by nonzero file)
    // NB: this is an edge case, file #3 is 0 bytes.
    // The next nonzero file is file #6
    file_offset = fpm.file_offset(FileSizes[1] + FileSizes[2], true);
    EXPECT_EQ(3U, file_offset.index);
    EXPECT_EQ(0U, file_offset.offset);

    // the last byte of in the torrent.
    // NB: reverse of previous edge case, since
    // the final 4 files in the torrent are all 0 bytes
    // the fifth file from end will be selected
    file_offset = fpm.file_offset(TotalSize - 1U, true);
    EXPECT_EQ(13U, file_offset.index);
    EXPECT_EQ(FileSizes[13] - 1U, file_offset.offset);
}

TEST_F(FilePieceMapTest, pieceSpan)
{
    // Note to reviewers: it's easy to see a nonexistent fencepost error here.
    // Remember everything is zero-indexed, so the 11 valid pieces are [0..10]
    // and that last piece #10 has one byte in it. Piece #11 is the 'end' iterator position.
    static auto constexpr ExpectedPieceSpans = std::array<tr_file_piece_map::piece_span_t, 18>{ {
        { 0U, 1U },
        { 0U, 1U },
        { 1U, 5U },
        { 5U, 6U },
        { 5U, 6U },
        { 5U, 6U },
        { 5U, 6U },
        { 5U, 7U },
        { 6U, 7U },
        { 6U, 7U },
        { 6U, 7U },
        { 6U, 7U },
        { 6U, 7U },
        { 6U, 11U },
        { 10U, 11U },
        { 10U, 11U },
        { 10U, 11U },
        { 10U, 11U },
    } };
    EXPECT_EQ(std::size(FileSizes), std::size(ExpectedPieceSpans));

    auto const fpm = tr_file_piece_map{ block_info_, std::data(FileSizes), std::size(FileSizes) };
    tr_file_index_t const n = std::size(fpm);
    EXPECT_EQ(std::size(FileSizes), n);
    uint64_t offset = 0U;
    for (tr_file_index_t file = 0U; file < n; ++file)
    {
        EXPECT_EQ(ExpectedPieceSpans[file].begin, fpm.piece_span(file).begin);
        EXPECT_EQ(ExpectedPieceSpans[file].end, fpm.piece_span(file).end);
        offset += FileSizes[file];
    }
    EXPECT_EQ(TotalSize, offset);
    EXPECT_EQ(block_info_.piece_count(), fpm.piece_span(std::size(FileSizes) - 1U).end);
}

TEST_F(FilePieceMapTest, priorities)
{
    auto const fpm = tr_file_piece_map{ block_info_, std::data(FileSizes), std::size(FileSizes) };
    auto file_priorities = tr_file_priorities(&fpm);
    tr_file_index_t const n_files = std::size(FileSizes);

    // make a helper to compare file & piece priorities
    auto expected_file_priorities = std::vector<tr_priority_t>(n_files, TR_PRI_NORMAL);
    auto expected_piece_priorities = std::vector<tr_priority_t>(block_info_.piece_count(), TR_PRI_NORMAL);
    auto const compare_to_expected = [&, this]()
    {
        for (tr_file_index_t i = 0U; i < n_files; ++i)
        {
            auto const expected = expected_file_priorities[i];
            auto const actual = file_priorities.file_priority(i);
            EXPECT_EQ(expected, actual) << "idx[" << i << "] expected [" << expected << "] actual [" << actual << ']';
        }
        for (tr_piece_index_t i = 0U; i < block_info_.piece_count(); ++i)
        {
            auto const expected = expected_piece_priorities[i];
            auto const actual = file_priorities.piece_priority(i);
            EXPECT_EQ(expected, actual) << "idx[" << i << "] expected [" << expected << "] actual [" << actual << ']';
        }
    };

    auto const mark_file_endpoints_as_high_priority = [&]()
    {
        for (tr_file_index_t i = 0U; i < n_files; ++i)
        {
            auto const [begin_piece, end_piece] = fpm.piece_span(i);
            expected_piece_priorities[begin_piece] = TR_PRI_HIGH;
            if (end_piece > begin_piece)
            {
                expected_piece_priorities[end_piece - 1U] = TR_PRI_HIGH;
            }
        }
    };

    // check default priority is normal
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // set the file #2 as high priority.
    // since this begins and ends on a piece boundary,
    // and it is not preceded by a zero sized file,
    // this shouldn't affect any other files' pieces
    auto pri = TR_PRI_HIGH;
    file_priorities.set(2U, pri);
    expected_file_priorities[2] = pri;
    for (size_t i = 1U; i < 5U; ++i)
    {
        expected_piece_priorities[i] = pri;
    }
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // This file shares a piece with another file.
    // If _either_ is set to high, the piece's priority should be high.
    // file #6: byte [500..550) piece [5, 6)
    // file #7: byte [550..650) piece [5, 7)
    //
    // first test setting file #6...
    pri = TR_PRI_HIGH;
    file_priorities.set(6U, pri);
    expected_file_priorities[6] = pri;
    expected_piece_priorities[5] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    // ...and that shared piece should still be the same when both are high...
    file_priorities.set(7U, pri);
    expected_file_priorities[7] = pri;
    expected_piece_priorities[5] = pri;
    expected_piece_priorities[6] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    // ...and that shared piece should still be the same when only 7 is high...
    pri = TR_PRI_NORMAL;
    file_priorities.set(6U, pri);
    expected_file_priorities[6] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // setup for the next test: set all files to low priority
    pri = TR_PRI_LOW;
    for (tr_file_index_t i = 0U; i < n_files; ++i)
    {
        file_priorities.set(i, pri);
    }
    std::fill(std::begin(expected_file_priorities), std::end(expected_file_priorities), pri);
    std::fill(std::begin(expected_piece_priorities), std::end(expected_piece_priorities), pri);
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // Raise the priority of a small 1-piece file.
    // Since it's the highest priority in the piece, piecePriority() should return its value.
    // file #9: byte [650, 659) piece [6, 7)
    pri = TR_PRI_NORMAL;
    file_priorities.set(9U, pri);
    expected_file_priorities[9] = pri;
    expected_piece_priorities[6] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    // Raise the priority of another small 1-piece file in the same piece.
    // Since _it_ now has the highest priority in the piece, piecePriority should return _its_ value.
    // file #10: byte [659, 667) piece [6, 7)
    pri = TR_PRI_HIGH;
    file_priorities.set(10U, pri);
    expected_file_priorities[10] = pri;
    expected_piece_priorities[6] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // Prep for the next test: set all files to normal priority
    pri = TR_PRI_NORMAL;
    for (tr_file_index_t i = 0U; i < n_files; ++i)
    {
        file_priorities.set(i, pri);
    }
    std::fill(std::begin(expected_file_priorities), std::end(expected_file_priorities), pri);
    std::fill(std::begin(expected_piece_priorities), std::end(expected_piece_priorities), pri);
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // *Sigh* OK what happens to piece priorities if you set the priority
    // of a zero-byte file. Arguably nothing should happen since you can't
    // download an empty file. But that would complicate the code for a
    // pretty stupid use case, and treating 0-sized files the same as any
    // other does no real harm. Let's KISS.
    //
    // Check that even zero-sized files can change a piece's priority
    // file #3: byte [500, 500) piece [5, 6)
    pri = TR_PRI_HIGH;
    file_priorities.set(3U, pri);
    expected_file_priorities[3] = pri;
    expected_piece_priorities[5] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    // Check that zero-sized files at the end of a torrent change the last piece's priority.
    // file #17 byte [1001, 1001) piece [10, 11)
    file_priorities.set(17U, pri);
    expected_file_priorities[17] = pri;
    expected_piece_priorities[10] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // test the batch API
    auto file_indices = std::vector<tr_file_index_t>(n_files);
    std::iota(std::begin(file_indices), std::end(file_indices), 0U);
    pri = TR_PRI_HIGH;
    file_priorities.set(std::data(file_indices), std::size(file_indices), pri);
    std::fill(std::begin(expected_file_priorities), std::end(expected_file_priorities), pri);
    std::fill(std::begin(expected_piece_priorities), std::end(expected_piece_priorities), pri);
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    pri = TR_PRI_LOW;
    file_priorities.set(std::data(file_indices), std::size(file_indices), pri);
    std::fill(std::begin(expected_file_priorities), std::end(expected_file_priorities), pri);
    std::fill(std::begin(expected_piece_priorities), std::end(expected_piece_priorities), pri);
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
}

TEST_F(FilePieceMapTest, wanted)
{
    auto const fpm = tr_file_piece_map{ block_info_, std::data(FileSizes), std::size(FileSizes) };
    auto files_wanted = tr_files_wanted(&fpm);
    tr_file_index_t const n_files = std::size(FileSizes);

    // make a helper to compare file & piece wanted bitfields
    auto expected_files_wanted = tr_bitfield(n_files);
    auto expected_pieces_wanted = tr_bitfield(block_info_.piece_count());
    auto const compare_to_expected = [&, this]()
    {
        for (tr_file_index_t i = 0U; i < n_files; ++i)
        {
            auto const expected = expected_files_wanted.test(i);
            auto const actual = files_wanted.file_wanted(i);
            EXPECT_EQ(expected, actual) << "idx[" << i << "] expected [" << expected << "] actual [" << actual << ']';
        }
        for (tr_piece_index_t i = 0U; i < block_info_.piece_count(); ++i)
        {
            auto const expected = expected_pieces_wanted.test(i);
            auto const actual = files_wanted.piece_wanted(i);
            EXPECT_EQ(expected, actual) << "idx[" << i << "] expected [" << expected << "] actual [" << actual << ']';
        }
    };

    // check everything is wanted by default
    expected_files_wanted.set_has_all();
    expected_pieces_wanted.set_has_all();
    compare_to_expected();

    // set file #2 as not wanted.
    // since this file begins and ends on a piece boundary,
    // and it is not preceded by a zero sized file,
    // this shouldn't normally affect any other files' pieces
    bool const wanted = false;
    files_wanted.set(2U, wanted);
    expected_files_wanted.set(2U, wanted);
    expected_pieces_wanted.set_span(1U, 5U, wanted);
    compare_to_expected();

    // now test when a piece has >1 file.
    // if *any* file in that piece is wanted, then we want the piece too.
    // file #3: byte [100..100) piece [5, 6) (zero-byte file)
    // file #4: byte [100..100) piece [5, 6) (zero-byte file)
    // file #5: byte [100..100) piece [5, 6) (zero-byte file)
    // file #6: byte [500..550) piece [5, 6)
    // file #7: byte [550..650) piece [5, 7)
    //
    // first test setting file #6...
    files_wanted.set(6U, false);
    expected_files_wanted.unset(6U);
    compare_to_expected();
    // marking all the files in the piece as unwanted
    // should cause the piece to become unwanted
    files_wanted.set(3U, false);
    files_wanted.set(4U, false);
    files_wanted.set(5U, false);
    files_wanted.set(6U, false);
    files_wanted.set(7U, false);
    expected_files_wanted.set_span(3U, 8U, false);
    expected_pieces_wanted.unset(5U);
    compare_to_expected();
    // but as soon as any of them is turned back to wanted,
    // the piece should pop back.
    files_wanted.set(7U, true);
    expected_files_wanted.set(7U, true);
    expected_pieces_wanted.set(5U);
    compare_to_expected();
    files_wanted.set(6U, true);
    files_wanted.set(7U, false);
    expected_files_wanted.set(6U);
    expected_files_wanted.unset(7U);
    compare_to_expected();
    files_wanted.set(5U, true);
    files_wanted.set(6U, false);
    expected_files_wanted.set(5U);
    expected_files_wanted.unset(6U);
    compare_to_expected();

    // Prep for the next test: set all files to unwanted
    for (tr_file_index_t i = 0U; i < n_files; ++i)
    {
        files_wanted.set(i, false);
    }
    expected_files_wanted.set_has_none();
    expected_pieces_wanted.set_has_none();
    compare_to_expected();

    // *Sigh* OK what happens to files_wanted if you say the only
    // file you want is a zero-byte file? Arguably nothing should happen
    // since you can't download a zero-byte file. But that would complicate
    // the code for a stupid use case, so let's KISS.
    //
    // Check that even zero-sized files can change a piece's 'wanted' state
    // file #3: byte [500, 500) piece [5, 6)
    files_wanted.set(3U, true);
    expected_files_wanted.set(3U);
    expected_pieces_wanted.set(5U);
    compare_to_expected();
    // Check that zero-sized files at the end of a torrent change the last piece's state.
    // file #17 byte [1001, 1001) piece [10, 11)
    files_wanted.set(17U, true);
    expected_files_wanted.set(17U);
    expected_pieces_wanted.set(10U);
    compare_to_expected();

    // test the batch API
    auto file_indices = std::vector<tr_file_index_t>(n_files);
    std::iota(std::begin(file_indices), std::end(file_indices), 0);
    files_wanted.set(std::data(file_indices), std::size(file_indices), true);
    expected_files_wanted.set_has_all();
    expected_pieces_wanted.set_has_all();
    compare_to_expected();
    files_wanted.set(std::data(file_indices), std::size(file_indices), false);
    expected_files_wanted.set_has_none();
    expected_pieces_wanted.set_has_none();
    compare_to_expected();
}
