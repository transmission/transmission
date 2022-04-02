// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>

#include "transmission.h"

#include "block-info.h"

#include "gtest/gtest.h"

using BlockInfoTest = ::testing::Test;

TEST_F(BlockInfoTest, fieldsAreSet)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 4;
    uint64_t constexpr TotalSize = PieceSize * PieceCount;
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(ExpectedBlockSize, info.final_block_size);
    EXPECT_EQ(PieceCount, info.n_pieces);
    EXPECT_EQ(PieceSize, info.final_piece_size);
    EXPECT_EQ(PieceSize, info.piece_size);
    EXPECT_EQ(TotalSize, info.total_size);

    info.initSizes(0, 0);
    EXPECT_EQ(0U, info.final_block_size);
    EXPECT_EQ(0U, info.n_pieces);
    EXPECT_EQ(0U, info.final_piece_size);
    EXPECT_EQ(0U, info.piece_size);
    EXPECT_EQ(0U, info.total_size);
}

TEST_F(BlockInfoTest, handlesOddSize)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(1U, info.final_block_size);
    EXPECT_EQ(1U, info.final_piece_size);
    EXPECT_EQ(PieceCount, info.n_pieces);
    EXPECT_EQ(PieceSize, info.piece_size);
    EXPECT_EQ(TotalSize, info.total_size);
}

TEST_F(BlockInfoTest, pieceSize)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(PieceSize, info.pieceSize(info.n_pieces - 2));
    EXPECT_EQ(1U, info.pieceSize(info.n_pieces - 1));
}

TEST_F(BlockInfoTest, blockSize)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(ExpectedBlockSize, info.blockSize(info.n_blocks - 2));
    EXPECT_EQ(1U, info.blockSize(info.n_blocks - 1));
}

TEST_F(BlockInfoTest, blockSpanForPiece)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
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
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
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

TEST_F(BlockInfoTest, blockLastLoc)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
    info.initSizes(TotalSize, PieceSize);

    auto loc = info.blockLastLoc(0);
    EXPECT_EQ(ExpectedBlockSize - 1, loc.byte);
    EXPECT_EQ(0U, loc.block);
    EXPECT_EQ(ExpectedBlockSize - 1, loc.block_offset);
    EXPECT_EQ(0U, loc.piece);
    EXPECT_EQ(ExpectedBlockSize - 1, loc.piece_offset);

    loc = info.blockLastLoc(info.blockCount() - 1);
    EXPECT_EQ(info.totalSize() - 1, loc.byte);
    EXPECT_EQ(info.blockCount() - 1, loc.block);
    EXPECT_EQ(info.totalSize() - 1 - (ExpectedBlockSize * (info.blockCount() - 1)), loc.block_offset);
    EXPECT_EQ(info.pieceCount() - 1, loc.piece);
    EXPECT_EQ(info.totalSize() - 1 - PieceSize * (PieceCount - 1), loc.piece_offset);
}

TEST_F(BlockInfoTest, pieceLoc)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
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

TEST_F(BlockInfoTest, pieceLastLoc)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
    info.initSizes(TotalSize, PieceSize);

    auto loc = info.pieceLastLoc(0);
    EXPECT_EQ(PieceSize - 1, loc.byte);
    EXPECT_EQ(ExpectedBlocksPerPiece - 1, loc.block);
    EXPECT_EQ(ExpectedBlockSize - 1, loc.block_offset);
    EXPECT_EQ(0, loc.piece);
    EXPECT_EQ(PieceSize - 1, loc.piece_offset);

    loc = info.pieceLastLoc(info.pieceCount() - 1);
    EXPECT_EQ(info.totalSize() - 1, loc.byte);
    EXPECT_EQ(info.blockCount() - 1, loc.block);
    EXPECT_EQ(info.totalSize() - 1 - (ExpectedBlockSize * (info.blockCount() - 1)), loc.block_offset);
    EXPECT_EQ(info.pieceCount() - 1, loc.piece);
    EXPECT_EQ(info.totalSize() - 1 - PieceSize * (PieceCount - 1), loc.piece_offset);
}

TEST_F(BlockInfoTest, byteLoc)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
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
