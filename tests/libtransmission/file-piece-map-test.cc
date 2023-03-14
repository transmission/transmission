// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <numeric>
#include <cstdint>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/file-piece-map.h>

#include "gtest/gtest.h"

class FilePieceMapTest : public ::testing::Test
{
protected:
    static constexpr size_t PieceSize{ tr_block_info::BlockSize };
    static constexpr size_t TotalSize{ 10 * PieceSize + 1 };
    tr_block_info const block_info_{ TotalSize, PieceSize };

    static constexpr std::array<uint64_t, 17> FileSizes{
        5 * PieceSize, // [offset 0] begins and ends on a piece boundary
        0, // [offset 5 P] zero-sized files
        0,
        0,
        0,
        PieceSize / 2, // [offset 5 P] begins on a piece boundary
        PieceSize, // [offset 5.5 P] neither begins nor ends on a piece boundary, spans >1 piece
        10, // [offset 6.5 P] small files all contained in a single piece
        9,
        8,
        7,
        6,
        (3 * PieceSize + PieceSize / 2 + 1 - 10 - 9 - 8 - 7 - 6), // [offset 5.75P +10+9+8+7+6] ends end-of-torrent
        0, // [offset 10P+1] zero-sized files at the end-of-torrent
        0,
        0,
        0,
        // sum is 10P + 1 == TotalSize
    };

    void SetUp() override
    {
        static_assert(
            FileSizes[0] + FileSizes[1] + FileSizes[2] + FileSizes[3] + FileSizes[4] + FileSizes[5] + FileSizes[6] +
                FileSizes[7] + FileSizes[8] + FileSizes[9] + FileSizes[10] + FileSizes[11] + FileSizes[12] + FileSizes[13] +
                FileSizes[14] + FileSizes[15] + FileSizes[16] ==
            TotalSize);

        EXPECT_EQ(11U, block_info_.pieceCount());
        EXPECT_EQ(PieceSize, block_info_.pieceSize());
        EXPECT_EQ(TotalSize, block_info_.totalSize());
        EXPECT_EQ(TotalSize, std::accumulate(std::begin(FileSizes), std::end(FileSizes), uint64_t{ 0 }));
    }
};

TEST_F(FilePieceMapTest, fileOffset)
{
    auto const fpm = tr_file_piece_map{ block_info_, std::data(FileSizes), std::size(FileSizes) };

    // first byte of the first file
    auto file_offset = fpm.fileOffset(0);
    EXPECT_EQ(0U, file_offset.index);
    EXPECT_EQ(0U, file_offset.offset);

    // final byte of the first file
    file_offset = fpm.fileOffset(FileSizes[0] - 1);
    EXPECT_EQ(0U, file_offset.index);
    EXPECT_EQ(FileSizes[0] - 1, file_offset.offset);

    // first byte of the second file
    // NB: this is an edge case, second file is 0 bytes.
    // The second nonzero file is file #5
    file_offset = fpm.fileOffset(FileSizes[0]);
    EXPECT_EQ(5U, file_offset.index);
    EXPECT_EQ(0U, file_offset.offset);

    // the last byte of in the torrent.
    // NB: reverse of previous edge case, since
    // the final 4 files in the torrent are all 0 bytes
    file_offset = fpm.fileOffset(TotalSize - 1);
    EXPECT_EQ(12U, file_offset.index);
    EXPECT_EQ(FileSizes[12] - 1, file_offset.offset);
}

TEST_F(FilePieceMapTest, pieceSpan)
{
    // Note to reviewers: it's easy to see a nonexistent fencepost error here.
    // Remember everything is zero-indexed, so the 11 valid pieces are [0..10]
    // and that last piece #10 has one byte in it. Piece #11 is the 'end' iterator position.
    auto constexpr ExpectedPieceSpans = std::array<tr_file_piece_map::piece_span_t, 17>{ {
        { 0, 5 },
        { 5, 6 },
        { 5, 6 },
        { 5, 6 },
        { 5, 6 },
        { 5, 6 },
        { 5, 7 },
        { 6, 7 },
        { 6, 7 },
        { 6, 7 },
        { 6, 7 },
        { 6, 7 },
        { 6, 11 },
        { 10, 11 },
        { 10, 11 },
        { 10, 11 },
        { 10, 11 },
    } };
    EXPECT_EQ(std::size(FileSizes), std::size(ExpectedPieceSpans));

    auto const fpm = tr_file_piece_map{ block_info_, std::data(FileSizes), std::size(FileSizes) };
    tr_file_index_t const n = std::size(fpm);
    EXPECT_EQ(std::size(FileSizes), n);
    uint64_t offset = 0;
    for (tr_file_index_t file = 0; file < n; ++file)
    {
        EXPECT_EQ(ExpectedPieceSpans[file].begin, fpm.pieceSpan(file).begin);
        EXPECT_EQ(ExpectedPieceSpans[file].end, fpm.pieceSpan(file).end);
        offset += FileSizes[file];
    }
    EXPECT_EQ(TotalSize, offset);
    EXPECT_EQ(block_info_.pieceCount(), fpm.pieceSpan(std::size(FileSizes) - 1).end);
}

TEST_F(FilePieceMapTest, priorities)
{
    auto const fpm = tr_file_piece_map{ block_info_, std::data(FileSizes), std::size(FileSizes) };
    auto file_priorities = tr_file_priorities(&fpm);
    tr_file_index_t const n_files = std::size(FileSizes);

    // make a helper to compare file & piece priorities
    auto expected_file_priorities = std::vector<tr_priority_t>(n_files, TR_PRI_NORMAL);
    auto expected_piece_priorities = std::vector<tr_priority_t>(block_info_.pieceCount(), TR_PRI_NORMAL);
    auto const compare_to_expected = [&, this]()
    {
        for (tr_file_index_t i = 0; i < n_files; ++i)
        {
            auto const expected = int{ expected_file_priorities[i] };
            auto const actual = int{ file_priorities.filePriority(i) };
            EXPECT_EQ(expected, actual) << "idx[" << i << "] expected [" << expected << "] actual [" << actual << ']';
        }
        for (tr_piece_index_t i = 0; i < block_info_.pieceCount(); ++i)
        {
            auto const expected = int{ expected_piece_priorities[i] };
            auto const actual = int{ file_priorities.piecePriority(i) };
            EXPECT_EQ(expected, actual) << "idx[" << i << "] expected [" << expected << "] actual [" << actual << ']';
        }
    };

    auto const mark_file_endpoints_as_high_priority = [&]()
    {
        for (tr_file_index_t i = 0; i < n_files; ++i)
        {
            auto const [begin_piece, end_piece] = fpm.pieceSpan(i);
            expected_piece_priorities[begin_piece] = TR_PRI_HIGH;
            if (end_piece > begin_piece)
            {
                expected_piece_priorities[end_piece - 1] = TR_PRI_HIGH;
            }
        }
    };

    // check default priority is normal
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // set the first file as high priority.
    // since this begins and ends on a piece boundary,
    // this shouldn't affect any other files' pieces
    auto pri = TR_PRI_HIGH;
    file_priorities.set(0, pri);
    expected_file_priorities[0] = pri;
    for (size_t i = 0; i < 5; ++i)
    {
        expected_piece_priorities[i] = pri;
    }
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // This file shares a piece with another file.
    // If _either_ is set to high, the piece's priority should be high.
    // file #5: byte [500..550) piece [5, 6)
    // file #6: byte [550..650) piece [5, 7)
    //
    // first test setting file #5...
    pri = TR_PRI_HIGH;
    file_priorities.set(5, pri);
    expected_file_priorities[5] = pri;
    expected_piece_priorities[5] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    // ...and that shared piece should still be the same when both are high...
    file_priorities.set(6, pri);
    expected_file_priorities[6] = pri;
    expected_piece_priorities[5] = pri;
    expected_piece_priorities[6] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    // ...and that shared piece should still be the same when only 6 is high...
    pri = TR_PRI_NORMAL;
    file_priorities.set(5, pri);
    expected_file_priorities[5] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // setup for the next test: set all files to low priority
    pri = TR_PRI_LOW;
    for (tr_file_index_t i = 0; i < n_files; ++i)
    {
        file_priorities.set(i, pri);
    }
    std::fill(std::begin(expected_file_priorities), std::end(expected_file_priorities), pri);
    std::fill(std::begin(expected_piece_priorities), std::end(expected_piece_priorities), pri);
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // Raise the priority of a small 1-piece file.
    // Since it's the highest priority in the piece, piecePriority() should return its value.
    // file #8: byte [650, 659) piece [6, 7)
    pri = TR_PRI_NORMAL;
    file_priorities.set(8, pri);
    expected_file_priorities[8] = pri;
    expected_piece_priorities[6] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    // Raise the priority of another small 1-piece file in the same piece.
    // Since _it_ now has the highest priority in the piece, piecePriority should return _its_ value.
    // file #9: byte [659, 667) piece [6, 7)
    pri = TR_PRI_HIGH;
    file_priorities.set(9, pri);
    expected_file_priorities[9] = pri;
    expected_piece_priorities[6] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // Prep for the next test: set all files to normal priority
    pri = TR_PRI_NORMAL;
    for (tr_file_index_t i = 0; i < n_files; ++i)
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
    // file #1: byte [500, 500) piece [5, 6)
    pri = TR_PRI_HIGH;
    file_priorities.set(1, pri);
    expected_file_priorities[1] = pri;
    expected_piece_priorities[5] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();
    // Check that zero-sized files at the end of a torrent change the last piece's priority.
    // file #16 byte [1001, 1001) piece [10, 11)
    file_priorities.set(16, pri);
    expected_file_priorities[16] = pri;
    expected_piece_priorities[10] = pri;
    mark_file_endpoints_as_high_priority();
    compare_to_expected();

    // test the batch API
    auto file_indices = std::vector<tr_file_index_t>(n_files);
    std::iota(std::begin(file_indices), std::end(file_indices), 0);
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

    // make a helper to compare file & piece priorities
    auto expected_files_wanted = tr_bitfield(n_files);
    auto expected_pieces_wanted = tr_bitfield(block_info_.pieceCount());
    auto const compare_to_expected = [&, this]()
    {
        for (tr_file_index_t i = 0; i < n_files; ++i)
        {
            EXPECT_EQ(int(expected_files_wanted.test(i)), int(files_wanted.fileWanted(i)));
        }
        for (tr_piece_index_t i = 0; i < block_info_.pieceCount(); ++i)
        {
            EXPECT_EQ(int(expected_pieces_wanted.test(i)), int(files_wanted.pieceWanted(i)));
        }
    };

    // check everything is wanted by default
    expected_files_wanted.setHasAll();
    expected_pieces_wanted.setHasAll();
    compare_to_expected();

    // set the first file as not wanted.
    // since this begins and ends on a piece boundary,
    // this shouldn't affect any other files' pieces
    bool const wanted = false;
    files_wanted.set(0, wanted);
    expected_files_wanted.set(0, wanted);
    expected_pieces_wanted.setSpan(0, 5, wanted);
    compare_to_expected();

    // now test when a piece has >1 file.
    // if *any* file in that piece is wanted, then we want the piece too.
    // file #1: byte [100..100) piece [5, 6) (zero-byte file)
    // file #2: byte [100..100) piece [5, 6) (zero-byte file)
    // file #3: byte [100..100) piece [5, 6) (zero-byte file)
    // file #4: byte [100..100) piece [5, 6) (zero-byte file)
    // file #5: byte [500..550) piece [5, 6)
    // file #6: byte [550..650) piece [5, 7)
    //
    // first test setting file #5...
    files_wanted.set(5, false);
    expected_files_wanted.unset(5);
    compare_to_expected();
    // marking all the files in the piece as unwanted
    // should cause the piece to become unwanted
    files_wanted.set(1, false);
    files_wanted.set(2, false);
    files_wanted.set(3, false);
    files_wanted.set(4, false);
    files_wanted.set(5, false);
    files_wanted.set(6, false);
    expected_files_wanted.setSpan(1, 7, false);
    expected_pieces_wanted.unset(5);
    compare_to_expected();
    // but as soon as any of them is turned back to wanted,
    // the piece should pop back.
    files_wanted.set(6, true);
    expected_files_wanted.set(6, true);
    expected_pieces_wanted.set(5);
    compare_to_expected();
    files_wanted.set(5, true);
    files_wanted.set(6, false);
    expected_files_wanted.set(5);
    expected_files_wanted.unset(6);
    compare_to_expected();
    files_wanted.set(4, true);
    files_wanted.set(5, false);
    expected_files_wanted.set(4);
    expected_files_wanted.unset(5);
    compare_to_expected();

    // Prep for the next test: set all files to unwanted priority
    for (tr_file_index_t i = 0; i < n_files; ++i)
    {
        files_wanted.set(i, false);
    }
    expected_files_wanted.setHasNone();
    expected_pieces_wanted.setHasNone();
    compare_to_expected();

    // *Sigh* OK what happens to files_wanted if you say the only
    // file you want is a zero-byte file? Arguably nothing should happen
    // since you can't download a zero-byte file. But that would complicate
    // the coe for a stupid use case, so let's KISS.
    //
    // Check that even zero-sized files can change a file's 'wanted' state
    // file #1: byte [500, 500) piece [5, 6)
    files_wanted.set(1, true);
    expected_files_wanted.set(1);
    expected_pieces_wanted.set(5);
    compare_to_expected();
    // Check that zero-sized files at the end of a torrent change the last piece's state.
    // file #16 byte [1001, 1001) piece [10, 11)
    files_wanted.set(16, true);
    expected_files_wanted.set(16);
    expected_pieces_wanted.set(10);
    compare_to_expected();

    // test the batch API
    auto file_indices = std::vector<tr_file_index_t>(n_files);
    std::iota(std::begin(file_indices), std::end(file_indices), 0);
    files_wanted.set(std::data(file_indices), std::size(file_indices), true);
    expected_files_wanted.setHasAll();
    expected_pieces_wanted.setHasAll();
    compare_to_expected();
    files_wanted.set(std::data(file_indices), std::size(file_indices), false);
    expected_files_wanted.setHasNone();
    expected_pieces_wanted.setHasNone();
    compare_to_expected();
}
