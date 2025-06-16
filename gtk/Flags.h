// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/tr-macros.h>

#include <bitset>
#include <initializer_list>
#include <type_traits>

// NOLINTBEGIN(bugprone-macro-parentheses, cppcoreguidelines-macro-usage)

#define DEFINE_FLAGS_OPERATORS(FlagType) \
    TR_CONSTEXPR23 inline Flags<FlagType> operator|(FlagType lhs, FlagType rhs) noexcept \
    { \
        return { lhs, rhs }; \
    }

// NOLINTEND(bugprone-macro-parentheses, cppcoreguidelines-macro-usage)

template<typename T>
class Flags
{
public:
    using FlagType = T;
    using EnumValueType = std::make_unsigned_t<std::underlying_type_t<FlagType>>;
    using BitsetType = std::bitset<static_cast<EnumValueType>(FlagType::N_FLAGS)>;

    static_assert(std::is_enum_v<FlagType> && !std::is_convertible_v<FlagType, BitsetType>);

public:
    constexpr Flags() noexcept = default;

    // NOLINTNEXTLINE(hicpp-explicit-conversions)
    TR_CONSTEXPR23 Flags(FlagType flag) noexcept
    {
        set(flag);
    }

    TR_CONSTEXPR23 Flags(std::initializer_list<FlagType> flags) noexcept
    {
        for (auto const flag : flags)
        {
            set(flag);
        }
    }

    [[nodiscard]] TR_CONSTEXPR23 bool none() const noexcept
    {
        return value_.none();
    }

    [[nodiscard]] TR_CONSTEXPR23 bool any() const noexcept
    {
        return value_.any();
    }

    [[nodiscard]] TR_CONSTEXPR23 bool test(FlagType flag) const noexcept
    {
        return value_.test(static_cast<EnumValueType>(flag));
    }

    [[nodiscard]] TR_CONSTEXPR23 bool test(Flags rhs) const noexcept
    {
        return (value_ & rhs.value_).any();
    }

    TR_CONSTEXPR23 void set(FlagType flag) noexcept
    {
        value_.set(static_cast<EnumValueType>(flag));
    }

    [[nodiscard]] TR_CONSTEXPR23 Flags operator|(Flags rhs) const noexcept
    {
        return Flags(value_ | rhs.value_);
    }

    TR_CONSTEXPR23 Flags& operator|=(Flags rhs) noexcept
    {
        value_ |= rhs.value_;
        return *this;
    }

    [[nodiscard]] TR_CONSTEXPR23 Flags operator~() const noexcept
    {
        return Flags(~value_);
    }

private:
    constexpr explicit Flags(BitsetType value) noexcept
        : value_(std::move(value))
    {
    }

private:
    BitsetType value_ = {};
};
