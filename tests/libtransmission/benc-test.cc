// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/format.h>

#include <libtransmission/transmission.h>

#include <libtransmission/benc.h>

#include "gtest/gtest.h"

using BencTest = ::testing::Test;
using namespace std::literals;

TEST_F(BencTest, MalformedBenc)
{
    // malformed benc seen in the wild. "119" length is incorrect.
    auto constexpr Benc =
        "d14:failure reason119:The tracker was unable to process your request. It may be down, overloaded, under attack or it just does not like you.12:min intervali1800e8:intervali1800e5:peers0:ee\n"sv;
    auto constexpr MaxBencDepth = 8;
    using TestHandler = transmission::benc::BasicHandler<MaxBencDepth>;

    auto stack = transmission::benc::ParserStack<MaxBencDepth>{};
    auto handler = TestHandler{};
    tr_error* error = nullptr;
    EXPECT_FALSE(transmission::benc::parse(Benc, stack, handler, nullptr, &error));
    EXPECT_NE(nullptr, error);
    if (error != nullptr)
    {
        EXPECT_NE(nullptr, error->message);
    }
    tr_error_clear(&error);
}

TEST_F(BencTest, ContextTokenIsCorrect)
{
    // clang-format off
    auto constexpr Benc =
        "d"
            "8:complete" "i3e"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers" "0:"
        "e"sv;
    // clang-format on

    auto constexpr MaxBencDepth = 32;
    struct ContextHandler final : public transmission::benc::BasicHandler<MaxBencDepth>
    {
        using BasicHandler = transmission::benc::BasicHandler<MaxBencDepth>;

        bool StartArray(Context const& context) override
        {
            BasicHandler::StartArray(context);
            EXPECT_EQ("l"sv, context.raw());
            return true;
        }

        bool EndArray(Context const& context) override
        {
            BasicHandler::EndArray(context);
            EXPECT_EQ("e"sv, context.raw());
            return true;
        }

        bool StartDict(Context const& context) override
        {
            BasicHandler::StartDict(context);
            EXPECT_EQ("d"sv, context.raw());
            return true;
        }

        bool EndDict(Context const& context) override
        {
            BasicHandler::EndDict(context);
            EXPECT_EQ("e"sv, context.raw());
            return true;
        }

        bool Int64(int64_t value, Context const& context) override
        {
            EXPECT_EQ(fmt::format(FMT_STRING("i{:d}e"), value), context.raw());
            return true;
        }

        bool String(std::string_view value, Context const& context) override
        {
            EXPECT_EQ(fmt::format(FMT_STRING("{:d}:{:s}"), std::size(value), value), context.raw());
            return true;
        }
    };

    auto stack = transmission::benc::ParserStack<MaxBencDepth>{};
    auto handler = ContextHandler{};
    tr_error* error = nullptr;
    transmission::benc::parse(Benc, stack, handler, nullptr, &error);
}
