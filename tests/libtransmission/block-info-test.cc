/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

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

    EXPECT_EQ(ExpectedBlockSize, info.block_size);
    EXPECT_EQ(ExpectedBlockSize, info.final_block_size);
    EXPECT_EQ(ExpectedBlocksPerPiece, info.n_blocks_in_final_piece);
    EXPECT_EQ(ExpectedBlocksPerPiece, info.n_blocks_in_piece);
    EXPECT_EQ(PieceCount, info.n_pieces);
    EXPECT_EQ(PieceSize, info.final_piece_size);
    EXPECT_EQ(PieceSize, info.piece_size);
    EXPECT_EQ(TotalSize, info.total_size);

    info.initSizes(0, 0);
    EXPECT_EQ(0, info.block_size);
    EXPECT_EQ(0, info.final_block_size);
    EXPECT_EQ(0, info.n_blocks_in_final_piece);
    EXPECT_EQ(0, info.n_blocks_in_piece);
    EXPECT_EQ(0, info.n_pieces);
    EXPECT_EQ(0, info.final_piece_size);
    EXPECT_EQ(0, info.piece_size);
    EXPECT_EQ(0, info.total_size);
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

    EXPECT_EQ(1, info.final_block_size);
    EXPECT_EQ(1, info.final_piece_size);
    EXPECT_EQ(1, info.n_blocks_in_final_piece);
    EXPECT_EQ(ExpectedBlockSize, info.block_size);
    EXPECT_EQ(ExpectedBlocksPerPiece, info.n_blocks_in_piece);
    EXPECT_EQ(PieceCount, info.n_pieces);
    EXPECT_EQ(PieceSize, info.piece_size);
    EXPECT_EQ(TotalSize, info.total_size);
}

TEST_F(BlockInfoTest, pieceForBlock)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 4;
    uint64_t constexpr TotalSize = PieceSize * PieceCount;
    info.initSizes(TotalSize, PieceSize);

    for (uint64_t i = 0; i < info.n_blocks; ++i)
    {
        EXPECT_EQ((i * ExpectedBlockSize) / PieceSize, info.pieceForBlock(i));
    }
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
    EXPECT_EQ(1, info.pieceSize(info.n_pieces - 1));
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
    EXPECT_EQ(1, info.blockSize(info.n_blocks - 1));
}

TEST_F(BlockInfoTest, offset)
{
    auto info = tr_block_info{};

    uint64_t constexpr ExpectedBlockSize = 1024 * 16;
    uint64_t constexpr ExpectedBlocksPerPiece = 4;
    uint64_t constexpr PieceSize = ExpectedBlockSize * ExpectedBlocksPerPiece;
    uint64_t constexpr PieceCount = 5;
    uint64_t constexpr TotalSize = PieceSize * (PieceCount - 1) + 1;
    info.initSizes(TotalSize, PieceSize);

    EXPECT_EQ(0, info.offset(0, 0));
    EXPECT_EQ(1, info.offset(0, 0, 1));
    EXPECT_EQ(PieceSize * 2 + 100 + 1, info.offset(2, 100, 1));
    EXPECT_EQ(
        info.total_size - 1,
        info.offset(
            info.n_pieces - 1,
            ((info.n_blocks_in_final_piece - 1) * info.block_size) + info.n_blocks_in_final_piece - 1));
    EXPECT_EQ(info.n_blocks_in_piece, info.blockOf(info.offset(1, 0)));
    EXPECT_EQ(info.n_blocks_in_piece, info.blockOf(info.offset(1, info.block_size - 1)));
    EXPECT_EQ(info.n_blocks_in_piece + 1, info.blockOf(info.offset(1, info.block_size)));
    EXPECT_EQ(info.n_blocks - 1, info.blockOf(info.total_size - 1));
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

    EXPECT_EQ(0, info.blockSpanForPiece(0).begin);
    EXPECT_EQ(4, info.blockSpanForPiece(0).end);
    EXPECT_EQ(12, info.blockSpanForPiece(3).begin);
    EXPECT_EQ(16, info.blockSpanForPiece(3).end);
    EXPECT_EQ(16, info.blockSpanForPiece(4).begin);
    EXPECT_EQ(17, info.blockSpanForPiece(4).end);

    // test that uninitialized block_info returns an invalid span
    info = tr_block_info{};
    EXPECT_EQ(0, info.blockSpanForPiece(0).begin);
    EXPECT_EQ(0, info.blockSpanForPiece(0).end);
}
