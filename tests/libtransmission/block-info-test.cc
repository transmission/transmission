// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>

#include "gtest/gtest.h"

using BlockInfoTest = ::testing::Test;

TEST_F(BlockInfoTest, fieldsAreSet)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024 } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 4U };
    static auto constexpr TotalSize = PieceSize * PieceCount;

    auto info = tr_block_info{};
    info.init_sizes(TotalSize, PieceSize);

    EXPECT_EQ(ExpectedBlockSize, info.block_size(info.block_count() - 1));
    EXPECT_EQ(PieceCount, info.piece_count());
    EXPECT_EQ(PieceSize, info.piece_size(info.piece_count() - 1));
    EXPECT_EQ(PieceSize, info.piece_size());
    EXPECT_EQ(TotalSize, info.total_size());

    info.init_sizes(0, 0);
    EXPECT_EQ(0U, info.block_size(info.block_count() - 1));
    EXPECT_EQ(0U, info.piece_count());
    EXPECT_EQ(0U, info.piece_size(info.piece_count() - 1));
    EXPECT_EQ(0U, info.piece_size());
    EXPECT_EQ(0U, info.total_size());
}

TEST_F(BlockInfoTest, handlesOddSize)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.init_sizes(TotalSize, PieceSize);

    EXPECT_EQ(1U, info.block_size(info.block_count() - 1));
    EXPECT_EQ(1U, info.piece_size(info.piece_count() - 1));
    EXPECT_EQ(PieceCount, info.piece_count());
    EXPECT_EQ(PieceSize, info.piece_size());
    EXPECT_EQ(TotalSize, info.total_size());
}

TEST_F(BlockInfoTest, pieceSize)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.init_sizes(TotalSize, PieceSize);

    EXPECT_EQ(PieceSize, info.piece_size(info.piece_count() - 2));
    EXPECT_EQ(1U, info.piece_size(info.piece_count() - 1));
}

TEST_F(BlockInfoTest, blockSize)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;

    auto info = tr_block_info{};
    info.init_sizes(TotalSize, PieceSize);

    EXPECT_EQ(ExpectedBlockSize, info.block_size(info.block_count() - 2));
    EXPECT_EQ(1U, info.block_size(info.block_count() - 1));
}

TEST_F(BlockInfoTest, blockSpanForPiece)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.init_sizes(TotalSize, PieceSize);

    EXPECT_EQ(0U, info.block_span_for_piece(0).begin);
    EXPECT_EQ(4U, info.block_span_for_piece(0).end);
    EXPECT_EQ(12U, info.block_span_for_piece(3).begin);
    EXPECT_EQ(16U, info.block_span_for_piece(3).end);
    EXPECT_EQ(16U, info.block_span_for_piece(4).begin);
    EXPECT_EQ(17U, info.block_span_for_piece(4).end);

    // test that uninitialized block_info returns an invalid span
    info = tr_block_info{};
    EXPECT_EQ(0U, info.block_span_for_piece(0).begin);
    EXPECT_EQ(0U, info.block_span_for_piece(0).end);
}

TEST_F(BlockInfoTest, blockLoc)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.init_sizes(TotalSize, PieceSize);

    // begin
    auto loc = info.block_loc(0);
    EXPECT_EQ(tr_block_info::Location{}, loc);

    // third block is halfway through the first piece
    loc = info.block_loc(2);
    EXPECT_EQ(ExpectedBlockSize * 2U, loc.byte);
    EXPECT_EQ(2U, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(ExpectedBlockSize * 2U, loc.piece_offset);

    // second piece aligns with fifth block
    loc = info.block_loc(4);
    EXPECT_EQ(PieceSize, loc.byte);
    EXPECT_EQ(4U, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(1U, loc.piece);
    EXPECT_EQ(0U, loc.piece_offset);
}

TEST_F(BlockInfoTest, pieceLoc)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.init_sizes(TotalSize, PieceSize);

    // begin
    auto loc = info.piece_loc(0);
    EXPECT_EQ(tr_block_info::Location{}, loc);

    for (uint64_t i = 0; i < PieceCount; ++i)
    {
        loc = info.piece_loc(i);
        EXPECT_EQ(info.block_loc(i * ExpectedBlocksPerPiece), loc);
        EXPECT_EQ(PieceSize * i, loc.byte);
        EXPECT_EQ(ExpectedBlocksPerPiece * i, loc.block);
        EXPECT_EQ(0U, loc.block_offset);
        EXPECT_EQ(i, loc.piece);
        EXPECT_EQ(0U, loc.piece_offset);
    }

    loc = info.piece_loc(0, PieceSize - 1);
    EXPECT_EQ(PieceSize - 1, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece - 1, loc.block);
    EXPECT_EQ(ExpectedBlockSize - 1, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(PieceSize - 1, loc.piece_offset);

    loc = info.piece_loc(0, PieceSize);
    EXPECT_EQ(PieceSize, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(1U, loc.piece);
    EXPECT_EQ(0U, loc.piece_offset);

    loc = info.piece_loc(0, PieceSize + 1);
    EXPECT_EQ(PieceSize + 1, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece, loc.block);
    EXPECT_EQ(1U, loc.block_offset);
    EXPECT_EQ(1U, loc.piece);
    EXPECT_EQ(1U, loc.piece_offset);
}

TEST_F(BlockInfoTest, byteLoc)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.init_sizes(TotalSize, PieceSize);

    auto loc = info.byte_loc(0);
    EXPECT_EQ(tr_block_info::Location{}, loc);

    loc = info.byte_loc(1);
    EXPECT_EQ(1U, loc.byte);
    EXPECT_EQ(0U, loc.block);
    EXPECT_EQ(1U, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(1U, loc.piece_offset);

    auto n = ExpectedBlockSize - 1;
    loc = info.byte_loc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(0U, loc.block);
    EXPECT_EQ(n, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(n, loc.piece_offset);

    n = ExpectedBlockSize;
    loc = info.byte_loc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(1U, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(n, loc.piece_offset);

    n = ExpectedBlockSize + 1;
    loc = info.byte_loc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(1U, loc.block);
    EXPECT_EQ(1U, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(n, loc.piece_offset);

    n = PieceSize - 1;
    loc = info.byte_loc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece - 1, loc.block);
    EXPECT_EQ(ExpectedBlockSize - 1, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(n, loc.piece_offset);

    n = PieceSize;
    loc = info.byte_loc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(1U, loc.piece);
    EXPECT_EQ(0U, loc.piece_offset);
}
