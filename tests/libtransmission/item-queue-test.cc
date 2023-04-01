// This file Copyright (C) 2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <sstream>
#include <string_view>
#include <utility>

#include <libtransmission/transmission.h>

#include <libtransmission/item-queue.h>

#include "test-fixtures.h"

using ItemQueueTest = ::testing::Test;
using libtransmission::ItemQueue;

namespace
{
template<typename Type>
[[nodiscard]] auto toString(std::vector<Type> const& items)
{
    auto out = std::ostringstream{};
    for (auto const& item : items)
    {
        out << '[' << item << ']';
    }
    return out.str();
}
} // namespace

TEST_F(ItemQueueTest, construct)
{
    auto items = ItemQueue<size_t>{};
    EXPECT_TRUE(std::empty(items));
    EXPECT_EQ(0U, std::size(items));
}

TEST_F(ItemQueueTest, setInOrder)
{
    using Type = std::string_view;
    static auto constexpr Key1 = "hello"sv;
    static auto constexpr Key2 = "world"sv;

    auto items = ItemQueue<Type>{};
    items.set(Key1, 0U);
    items.set(Key2, 1U);

    EXPECT_EQ(2U, std::size(items));
    EXPECT_FALSE(std::empty(items));

    auto const expected_queue = std::vector<Type>{ Key1, Key2 };
    auto const actual_queue = items.queue();
    EXPECT_EQ(expected_queue, actual_queue);
}

TEST_F(ItemQueueTest, eraseFront)
{
    using Type = std::string_view;
    static auto constexpr Key1 = "a"sv;
    static auto constexpr Key2 = "b"sv;
    static auto constexpr Key3 = "c"sv;

    auto items = ItemQueue<Type>{};
    items.set(Key1, 0U);
    items.set(Key2, 1U);
    items.set(Key3, 2U);
    items.erase(Key1);
    auto expected_queue = std::vector<Type>{ Key2, Key3 };
    auto actual_queue = items.queue();
    EXPECT_EQ(expected_queue, actual_queue);
}

TEST_F(ItemQueueTest, eraseMiddle)
{
    using Type = std::string_view;
    static auto constexpr Key1 = "a"sv;
    static auto constexpr Key2 = "b"sv;
    static auto constexpr Key3 = "c"sv;

    auto items = ItemQueue<Type>{};
    items.set(Key1, 0U);
    items.set(Key2, 1U);
    items.set(Key3, 2U);
    items.erase(Key2);
    auto expected_queue = std::vector<Type>{ Key1, Key3 };
    auto actual_queue = items.queue();
    EXPECT_EQ(expected_queue, actual_queue);
}

TEST_F(ItemQueueTest, eraseBack)
{
    using Type = std::string_view;
    static auto constexpr Key1 = "a"sv;
    static auto constexpr Key2 = "b"sv;
    static auto constexpr Key3 = "c"sv;

    auto items = ItemQueue<Type>{};
    items.set(Key1, 0U);
    items.set(Key2, 1U);
    items.set(Key3, 2U);
    items.erase(Key3);
    auto expected_queue = std::vector<Type>{ Key1, Key2 };
    auto actual_queue = items.queue();
    EXPECT_EQ(expected_queue, actual_queue);
}

TEST_F(ItemQueueTest, setInReverseOrder)
{
    using Type = std::string_view;
    static auto constexpr Key1 = "hello"sv;
    static auto constexpr Key2 = "world"sv;

    auto items = ItemQueue<Type>{};
    items.set(Key1, 1U);
    items.set(Key2, 0U);
    EXPECT_EQ(2U, std::size(items));
    EXPECT_FALSE(std::empty(items));

    auto const expected_queue = std::vector<Type>{ Key2, Key1 };
    auto const actual_queue = items.queue();
    EXPECT_EQ(expected_queue, actual_queue);
}

TEST_F(ItemQueueTest, setReplacesPreviousPosition)
{
    using Type = std::string_view;
    static auto constexpr Key1 = "hello"sv;
    static auto constexpr Key2 = "world"sv;

    auto items = ItemQueue<Type>{};
    items.set(Key1, 0U);
    items.set(Key2, 1U);
    items.set(Key1, 2U);
    EXPECT_EQ(2U, std::size(items));
    EXPECT_FALSE(std::empty(items));

    auto const expected_queue = std::vector<Type>{ Key2, Key1 };
    auto const actual_queue = items.queue();
    EXPECT_EQ(expected_queue, actual_queue);
}

TEST_F(ItemQueueTest, pop)
{
    using Type = std::string_view;
    static auto constexpr Key1 = "hello"sv;
    static auto constexpr Key2 = "world"sv;

    auto items = ItemQueue<Type>{};
    items.set(Key1, 0U);
    items.set(Key2, 1U);

    auto popped = items.pop();
    EXPECT_TRUE(popped.has_value());
    EXPECT_EQ(Key1, *popped);
    EXPECT_EQ(1U, std::size(items));

    popped = items.pop();
    EXPECT_TRUE(popped.has_value());
    EXPECT_EQ(Key2, *popped);
    EXPECT_TRUE(std::empty(items));

    popped = items.pop();
    EXPECT_FALSE(popped.has_value());
}

TEST_F(ItemQueueTest, moveUp)
{
    using Type = std::string_view;

    struct Test
    {
        std::vector<Type> initial;
        std::vector<Type> moved;
        std::vector<Type> expected;
    };

    auto const tests = std::array<Test, 7>{{
        { { "a", "b", "c", "d", "e", "f" }, { "a" },      { "a", "b", "c", "d", "e", "f" } },
        { { "a", "b", "c", "d", "e", "f" }, { "a", "b" }, { "b", "a", "c", "d", "e", "f" } },
        { { "a", "b", "c", "d", "e", "f" }, { "b", "d" }, { "b", "a", "d", "c", "e", "f" } },
        { { "a", "b", "c", "d", "e", "f" }, { "c", "d" }, { "a", "c", "d", "b", "e", "f" } },
        { { "a", "b", "c", "d", "e", "f" }, { "f" },      { "a", "b", "c", "d", "f", "e" } },
        { { "a", "b", "c", "d", "e", "f" }, { "f", "f" }, { "a", "b", "c", "d", "f", "e" } },
        { { "a", "b", "c", "d", "e", "f" }, { "z", "q" }, { "a", "b", "c", "d", "e", "f" } },
    }};

    for (auto const& [initial, moved, expected] : tests)
    {
        // set up the test queue
        auto items = ItemQueue<Type>{};
        for (size_t i = 0; i < std::size(initial); ++i)
        {
            items.set(initial[i], i);
        }

        items.move_up(std::data(moved), std::size(moved));

        auto const actual = items.queue();
        EXPECT_EQ(expected, actual) << "initial: " << toString(initial) << " move " << toString(moved) << " expected " << toString(expected) << " actual " << toString(actual) << std::endl;
    }
}
