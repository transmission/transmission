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
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(ExpectedBlockSize, info.blockSize(info.blockCount() - 1));
    EXPECT_EQ(PieceCount, info.pieceCount());
    EXPECT_EQ(PieceSize, info.pieceSize(info.pieceCount() - 1));
    EXPECT_EQ(PieceSize, info.pieceSize());
    EXPECT_EQ(TotalSize, info.totalSize());

    info.initSizes(0, 0);
    EXPECT_EQ(0U, info.blockSize(info.blockCount() - 1));
    EXPECT_EQ(0U, info.pieceCount());
    EXPECT_EQ(0U, info.pieceSize(info.pieceCount() - 1));
    EXPECT_EQ(0U, info.pieceSize());
    EXPECT_EQ(0U, info.totalSize());
}

TEST_F(BlockInfoTest, handlesOddSize)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(1U, info.blockSize(info.blockCount() - 1));
    EXPECT_EQ(1U, info.pieceSize(info.pieceCount() - 1));
    EXPECT_EQ(PieceCount, info.pieceCount());
    EXPECT_EQ(PieceSize, info.pieceSize());
    EXPECT_EQ(TotalSize, info.totalSize());
}

TEST_F(BlockInfoTest, pieceSize)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(PieceSize, info.pieceSize(info.pieceCount() - 2));
    EXPECT_EQ(1U, info.pieceSize(info.pieceCount() - 1));
}

TEST_F(BlockInfoTest, blockSize)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;

    auto info = tr_block_info{};
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(ExpectedBlockSize, info.blockSize(info.blockCount() - 2));
    EXPECT_EQ(1U, info.blockSize(info.blockCount() - 1));
}

TEST_F(BlockInfoTest, blockSpanForPiece)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(0U, info.blockSpanForPiece(0).begin);
    EXPECT_EQ(4U, info.blockSpanForPiece(0).end);
    EXPECT_EQ(12U, info.blockSpanForPiece(3).begin);
    EXPECT_EQ(16U, info.blockSpanForPiece(3).end);
    EXPECT_EQ(16U, info.blockSpanForPiece(4).begin);
    EXPECT_EQ(17U, info.blockSpanForPiece(4).end);

    // test that uninitialized block_info returns an invalid span
    info = tr_block_info{};
    EXPECT_EQ(0U, info.blockSpanForPiece(0).begin);
    EXPECT_EQ(0U, info.blockSpanForPiece(0).end);
}

TEST_F(BlockInfoTest, blockLoc)
{
    static auto constexpr ExpectedBlockSize = uint64_t{ 1024U } * 16U;
    static auto constexpr ExpectedBlocksPerPiece = uint64_t{ 4U };
    static auto constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    static auto constexpr PieceCount = uint64_t{ 5U };
    static auto constexpr TotalSize = PieceSize * (PieceCount - 1U) + 1U;

    auto info = tr_block_info{};
    info.initSizes(TotalSize, PieceSize);

    // begin
    auto loc = info.blockLoc(0);
    EXPECT_EQ(tr_block_info::Location{}, loc);

    // third block is halfway through the first piece
    loc = info.blockLoc(2);
    EXPECT_EQ(ExpectedBlockSize * 2U, loc.byte);
    EXPECT_EQ(2U, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(ExpectedBlockSize * 2U, loc.piece_offset);

    // second piece aligns with fifth block
    loc = info.blockLoc(4);
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
    info.initSizes(TotalSize, PieceSize);

    // begin
    auto loc = info.pieceLoc(0);
    EXPECT_EQ(tr_block_info::Location{}, loc);

    for (uint64_t i = 0; i < PieceCount; ++i)
    {
        loc = info.pieceLoc(i);
        EXPECT_EQ(info.blockLoc(i * ExpectedBlocksPerPiece), loc);
        EXPECT_EQ(PieceSize * i, loc.byte);
        EXPECT_EQ(ExpectedBlocksPerPiece * i, loc.block);
        EXPECT_EQ(0U, loc.block_offset);
        EXPECT_EQ(i, loc.piece);
        EXPECT_EQ(0U, loc.piece_offset);
    }

    loc = info.pieceLoc(0, PieceSize - 1);
    EXPECT_EQ(PieceSize - 1, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece - 1, loc.block);
    EXPECT_EQ(ExpectedBlockSize - 1, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(PieceSize - 1, loc.piece_offset);

    loc = info.pieceLoc(0, PieceSize);
    EXPECT_EQ(PieceSize, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(1U, loc.piece);
    EXPECT_EQ(0U, loc.piece_offset);

    loc = info.pieceLoc(0, PieceSize + 1);
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
    info.initSizes(TotalSize, PieceSize);

    auto loc = info.byteLoc(0);
    EXPECT_EQ(tr_block_info::Location{}, loc);

    loc = info.byteLoc(1);
    EXPECT_EQ(1U, loc.byte);
    EXPECT_EQ(0U, loc.block);
    EXPECT_EQ(1U, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(1U, loc.piece_offset);

    auto n = ExpectedBlockSize - 1;
    loc = info.byteLoc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(0U, loc.block);
    EXPECT_EQ(n, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(n, loc.piece_offset);

    n = ExpectedBlockSize;
    loc = info.byteLoc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(1U, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(n, loc.piece_offset);

    n = ExpectedBlockSize + 1;
    loc = info.byteLoc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(1U, loc.block);
    EXPECT_EQ(1U, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(n, loc.piece_offset);

    n = PieceSize - 1;
    loc = info.byteLoc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece - 1, loc.block);
    EXPECT_EQ(ExpectedBlockSize - 1, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(n, loc.piece_offset);

    n = PieceSize;
    loc = info.byteLoc(n);
    EXPECT_EQ(n, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece, loc.block);
    EXPECT_EQ(0U, loc.block_offset);
    EXPECT_EQ(1U, loc.piece);
    EXPECT_EQ(0U, loc.piece_offset);
}
