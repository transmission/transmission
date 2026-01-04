// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <climits>
#include <cstring>
#include <cstdint>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <libtransmission/net.h>
#include <libtransmission/quark.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using SerializerTest = ::libtransmission::test::TransmissionTest;
using namespace std::literals;
using libtransmission::serializer::Converters;

namespace
{

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(Rect const& that) const noexcept
    {
        return x == that.x && y == that.y && width == that.width && height == that.height;
    }
};

void registerRectConverter()
{
    static auto const ToRect = [](tr_variant const& src, Rect* tgt)
    {
        auto const* const v = src.get_if<tr_variant::Vector>();
        if (v == nullptr || std::size(*v) != 4U)
        {
            return false;
        }

        auto const x = (*v)[0].value_if<int64_t>();
        auto const y = (*v)[1].value_if<int64_t>();
        auto const w = (*v)[2].value_if<int64_t>();
        auto const h = (*v)[3].value_if<int64_t>();

        if (!x || !y || !w || !h)
        {
            return false;
        }

        *tgt = Rect{ static_cast<int>(*x), static_cast<int>(*y), static_cast<int>(*w), static_cast<int>(*h) };
        return true;
    };

    static auto const FromRect = [](Rect const& r) -> tr_variant
    {
        auto v = tr_variant::Vector{};
        v.reserve(4U);
        v.emplace_back(int64_t{ r.x });
        v.emplace_back(int64_t{ r.y });
        v.emplace_back(int64_t{ r.width });
        v.emplace_back(int64_t{ r.height });
        return v;
    };

    static std::once_flag once;
    std::call_once(once, [] { Converters::add<Rect>(ToRect, FromRect); });
}

TEST_F(SerializerTest, usesBuiltins)
{
    {
        auto const var = Converters::serialize(true);
        EXPECT_TRUE(var.holds_alternative<bool>());

        auto out = false;
        EXPECT_TRUE(Converters::deserialize(var, &out));
        EXPECT_EQ(out, true);
    }

    {
        auto const var = Converters::serialize(3.5);
        EXPECT_TRUE(var.holds_alternative<double>());

        auto out = 0.0;
        EXPECT_TRUE(Converters::deserialize(var, &out));
        EXPECT_DOUBLE_EQ(out, 3.5);
    }

    {
        auto const s = "hello"s;
        auto const var = Converters::serialize(s);
        EXPECT_TRUE(var.holds_alternative<std::string_view>());
        EXPECT_EQ(var.value_if<std::string_view>().value_or(""sv), "hello"sv);

        auto out = std::string{};
        EXPECT_TRUE(Converters::deserialize(var, &out));
        EXPECT_EQ(out, s);
    }

    {
        auto const s = std::optional<std::string>{ "opt"s };
        auto const var = Converters::serialize(s);
        EXPECT_TRUE(var.holds_alternative<std::string_view>());
        EXPECT_EQ(var.value_if<std::string_view>().value_or(""sv), "opt"sv);

        auto out = std::optional<std::string>{};
        EXPECT_TRUE(Converters::deserialize(var, &out));
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(*out, *s);
    }

    {
        auto const s = std::optional<std::string>{};
        auto const var = Converters::serialize(s);
        EXPECT_TRUE(var.holds_alternative<std::nullptr_t>());

        auto out = std::optional<std::string>{ "will reset"s };
        EXPECT_TRUE(Converters::deserialize(var, &out));
        EXPECT_FALSE(out.has_value());
    }

    {
        auto const expected = uint64_t{ 12345678901234ULL };
        auto const var = Converters::serialize(expected);
        EXPECT_TRUE(var.holds_alternative<int64_t>());

        auto out = uint64_t{};
        EXPECT_TRUE(Converters::deserialize(var, &out));
        EXPECT_EQ(out, expected);
    }
}

TEST_F(SerializerTest, usesCustomTypes)
{
    registerRectConverter();

    static constexpr Rect Expected{ 10, 20, 640, 480 };
    auto const var = Converters::serialize(Expected);

    auto actual = Rect{};
    EXPECT_TRUE(Converters::deserialize(var, &actual));
    EXPECT_EQ(actual, Expected);
}

TEST_F(SerializerTest, usesLists)
{
    auto const expected = std::list<std::string>{ "apple", "ball", "cat" };
    auto const var = Converters::serialize(expected);

    auto const* const l = var.get_if<tr_variant::Vector>();
    ASSERT_NE(l, nullptr);
    ASSERT_EQ(std::size(*l), 3U);
    EXPECT_EQ((*l)[0].value_if<std::string_view>().value_or(""sv), "apple"sv);
    EXPECT_EQ((*l)[1].value_if<std::string_view>().value_or(""sv), "ball"sv);
    EXPECT_EQ((*l)[2].value_if<std::string_view>().value_or(""sv), "cat"sv);

    auto actual = decltype(expected){};
    EXPECT_TRUE(Converters::deserialize(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesVectors)
{
    auto const expected = std::vector<std::string>{ "apple", "ball", "cat" };
    auto const var = Converters::serialize(expected);

    auto const* const l = var.get_if<tr_variant::Vector>();
    ASSERT_NE(l, nullptr);
    ASSERT_EQ(std::size(*l), 3U);
    EXPECT_EQ((*l)[0].value_if<std::string_view>().value_or(""sv), "apple"sv);
    EXPECT_EQ((*l)[1].value_if<std::string_view>().value_or(""sv), "ball"sv);
    EXPECT_EQ((*l)[2].value_if<std::string_view>().value_or(""sv), "cat"sv);

    auto actual = decltype(expected){};
    EXPECT_TRUE(Converters::deserialize(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesVectorsOfCustom)
{
    registerRectConverter();

    auto const expected = std::vector<Rect>{ { 1, 2, 3, 4 }, { 10, 20, 640, 480 } };
    auto const var = Converters::serialize(expected);

    auto actual = decltype(expected){};
    EXPECT_TRUE(Converters::deserialize(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesNestedVectors)
{
    auto const expected = std::vector<std::vector<std::string>>{ { "a", "b" }, { "c" } };
    auto const var = Converters::serialize(expected);

    auto const* const outer = var.get_if<tr_variant::Vector>();
    ASSERT_NE(outer, nullptr);
    ASSERT_EQ(std::size(*outer), 2U);

    auto const* const inner0 = (*outer)[0].get_if<tr_variant::Vector>();
    ASSERT_NE(inner0, nullptr);
    ASSERT_EQ(std::size(*inner0), 2U);
    EXPECT_EQ((*inner0)[0].value_if<std::string_view>().value_or(""sv), "a"sv);
    EXPECT_EQ((*inner0)[1].value_if<std::string_view>().value_or(""sv), "b"sv);

    auto const* const inner1 = (*outer)[1].get_if<tr_variant::Vector>();
    ASSERT_NE(inner1, nullptr);
    ASSERT_EQ(std::size(*inner1), 1U);
    EXPECT_EQ((*inner1)[0].value_if<std::string_view>().value_or(""sv), "c"sv);

    auto actual = decltype(expected){};
    EXPECT_TRUE(Converters::deserialize(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, vectorRejectsWrongType)
{
    auto const var = tr_variant{ true };
    auto out = std::vector<std::string>{ "keep" };
    EXPECT_FALSE(Converters::deserialize(var, &out));
    EXPECT_EQ(out, (std::vector<std::string>{ "keep" }));
}

TEST_F(SerializerTest, vectorIsNondestructiveOnPartialFailure)
{
    auto list = tr_variant::Vector{};
    list.reserve(3U);
    list.emplace_back("ok"sv);
    list.emplace_back(nullptr);
    list.emplace_back("ok"sv);

    auto const var = tr_variant{ std::move(list) };
    auto out = std::vector<std::string>{ "keep" };
    EXPECT_FALSE(Converters::deserialize(var, &out));
    EXPECT_EQ(out, (std::vector<std::string>{ "keep" }));
}

TEST_F(SerializerTest, usesOptional)
{
    auto const expected = std::optional{ "apple"s };
    auto const var = Converters::serialize(expected);

    auto const sv = var.value_if<std::string_view>();
    ASSERT_EQ(sv, "apple"sv);

    auto actual = decltype(expected){};
    EXPECT_TRUE(Converters::deserialize(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesNullOptional)
{
    auto const expected = std::optional<std::string>{};
    auto const var = Converters::serialize(expected);

    auto const sv = var.value_if<std::string_view>();
    ASSERT_FALSE(sv);

    auto actual = decltype(expected){ "discard"s };
    EXPECT_TRUE(Converters::deserialize(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesOptionalOfCustom)
{
    registerRectConverter();

    constexpr auto Expected = std::optional{ Rect{ 1, 2, 3, 4 } };
    auto const var = Converters::serialize(Expected);

    auto actual = decltype(Expected){};
    EXPECT_TRUE(Converters::deserialize(var, &actual));
    EXPECT_EQ(actual, Expected);
}

TEST_F(SerializerTest, optionalRejectsWrongType)
{
    auto const var = tr_variant{ true };
    auto out = std::optional{ "keep"s };
    EXPECT_FALSE(Converters::deserialize(var, &out));
    EXPECT_EQ(out, "keep"s);
}

// ---

using libtransmission::serializer::contains;
using libtransmission::serializer::Field;
using libtransmission::serializer::get_value;
using libtransmission::serializer::load;
using libtransmission::serializer::save;
using libtransmission::serializer::set_if_changed;

struct Endpoint
{
    std::string address;
    tr_port port;

    static constexpr auto Fields = std::tuple{
        Field<&Endpoint::address>{ TR_KEY_address },
        Field<&Endpoint::port>{ TR_KEY_port },
    };

    [[nodiscard]] bool operator==(Endpoint const& that) const noexcept
    {
        return address == that.address && port == that.port;
    }

    // C++17 requires explicit operator!=; C++20 would auto-generate from operator==
    [[nodiscard]] bool operator!=(Endpoint const& that) const noexcept
    {
        return !(*this == that);
    }
};

struct Thing
{
    int i = 0;
    double d = 0.0;
    std::string s;

    static constexpr auto Fields = std::tuple{
        Field<&Thing::i>{ TR_KEY_port },
        Field<&Thing::d>{ TR_KEY_alt_speed_up },
        Field<&Thing::s>{ TR_KEY_name },
    };

    [[nodiscard]] bool operator==(Thing const& that) const noexcept
    {
        auto const dbl_bits = [](double v)
        {
            auto u = uint64_t{};
            std::memcpy(&u, &v, sizeof(u));
            return u;
        };

        return i == that.i && dbl_bits(d) == dbl_bits(that.d) && s == that.s;
    }
};

TEST_F(SerializerTest, fieldSaveLoad)
{
    auto const expected = Endpoint{ "localhost", tr_port::from_host(51413) };

    // Save to variant
    auto constexpr Expected = R"({"address":"localhost","port":51413})"sv;
    auto const var = tr_variant{ save(expected, Endpoint::Fields) };
    EXPECT_EQ(Expected, tr_variant_serde::json().compact().to_string(var));

    // Load back into a new instance
    auto actual = Endpoint{};
    EXPECT_NE(actual, expected);
    load(actual, Endpoint::Fields, var);
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, fieldLoadIgnoresMissingKeys)
{
    auto endpoint = Endpoint{ "default", tr_port::from_host(9999) };
    auto const original = endpoint;

    load(endpoint, Endpoint::Fields, tr_variant::make_map());

    // Should remain unchanged
    EXPECT_EQ(original, endpoint);
}

TEST_F(SerializerTest, fieldLoadIgnoresNonMap)
{
    auto endpoint = Endpoint{ "default", tr_port::from_host(9999) };
    auto const original = endpoint;

    load(endpoint, Endpoint::Fields, tr_variant{ 42 });

    // Should remain unchanged
    EXPECT_EQ(original, endpoint);
}

TEST_F(SerializerTest, setMissingKey)
{
    auto thing = Thing{};
    thing.i = 1;
    thing.d = 2.0;
    thing.s = "x";
    auto const original = thing;

    EXPECT_FALSE(set_if_changed(thing, Thing::Fields, TR_KEY_address, 123));
    EXPECT_EQ(original, thing);
}

TEST_F(SerializerTest, setSameInt)
{
    auto thing = Thing{};
    thing.i = 5;
    thing.s = "";
    auto const original = thing;

    EXPECT_FALSE(set_if_changed(thing, Thing::Fields, TR_KEY_port, 5));
    EXPECT_EQ(original, thing);
}

TEST_F(SerializerTest, setNewInt)
{
    auto thing = Thing{};
    thing.i = 5;
    thing.s = "";

    EXPECT_TRUE(set_if_changed(thing, Thing::Fields, TR_KEY_port, 6));
    EXPECT_EQ(thing.i, 6);
}

TEST_F(SerializerTest, setTypeMismatch)
{
    auto thing = Thing{};
    thing.d = 1.0;
    thing.s = "";
    auto const original = thing;

    EXPECT_FALSE(set_if_changed(thing, Thing::Fields, TR_KEY_alt_speed_up, 1.0F));
    EXPECT_EQ(original, thing);
}

TEST_F(SerializerTest, setFloatNear)
{
    auto thing = Thing{};
    thing.d = 1.0;
    thing.s = "";
    auto const original = thing;

    auto const eps = std::numeric_limits<double>::epsilon();
    EXPECT_FALSE(set_if_changed(thing, Thing::Fields, TR_KEY_alt_speed_up, 1.0 + (eps / 2.0)));
    EXPECT_EQ(original, thing);
}

TEST_F(SerializerTest, setFloatFar)
{
    auto thing = Thing{};
    thing.d = 1.0;
    thing.s = "";

    auto const eps = std::numeric_limits<double>::epsilon();
    auto const val = 1.0 + (eps * 2.0);
    EXPECT_TRUE(set_if_changed(thing, Thing::Fields, TR_KEY_alt_speed_up, val));
    EXPECT_DOUBLE_EQ(thing.d, val);
}

TEST_F(SerializerTest, setFloatScaled)
{
    auto thing = Thing{};
    thing.d = 1e12;
    thing.s = "";
    auto const original = thing;

    auto const eps = std::numeric_limits<double>::epsilon();

    // delta_small is within epsilon*scale; should be treated as equal
    EXPECT_FALSE(set_if_changed(thing, Thing::Fields, TR_KEY_alt_speed_up, 1e12 + ((eps * 1e12) / 2.0)));
    EXPECT_EQ(original, thing);

    // delta_big is beyond epsilon*scale; should be treated as different
    auto const val = 1e12 + ((eps * 1e12) * 2.0);
    EXPECT_TRUE(set_if_changed(thing, Thing::Fields, TR_KEY_alt_speed_up, val));
    EXPECT_DOUBLE_EQ(thing.d, val);
}

TEST_F(SerializerTest, containsMissingKey)
{
    EXPECT_FALSE(contains(Thing::Fields, TR_KEY_address));
}

TEST_F(SerializerTest, containsOk)
{
    EXPECT_TRUE(contains(Thing::Fields, TR_KEY_port));
    EXPECT_TRUE(contains(Thing::Fields, TR_KEY_alt_speed_up));
    EXPECT_TRUE(contains(Thing::Fields, TR_KEY_name));
}

TEST_F(SerializerTest, containsEmptyFields)
{
    static constexpr auto Empty = std::tuple{};
    EXPECT_FALSE(contains(Empty, TR_KEY_name));
}

TEST_F(SerializerTest, getMissingKey)
{
    auto thing = Thing{};
    thing.i = 1;
    thing.d = 2.0;
    thing.s = "x";
    EXPECT_FALSE(get_value<int>(thing, Thing::Fields, TR_KEY_address).has_value());
}

TEST_F(SerializerTest, getOk)
{
    auto thing = Thing{};
    thing.s = "hello";
    auto const val = get_value<std::string>(thing, Thing::Fields, TR_KEY_name);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "hello");
}

TEST_F(SerializerTest, getTypeMismatch)
{
    auto thing = Thing{};
    thing.s = "hello";
    EXPECT_FALSE(get_value<std::string_view>(thing, Thing::Fields, TR_KEY_name).has_value());
}

} // namespace
