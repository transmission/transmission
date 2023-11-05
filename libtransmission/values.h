// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstdint> // for uint64_t
#include <string>
#include <string_view>

#include <fmt/core.h>

namespace libtransmission::Values
{
enum Base
{
    Kilo = 1000U,
    Kibi = 1024U
};

enum MemoryUnits
{
    Bytes,
    KBytes,
    MBytes,
    GBytes,
    TBytes
};

using StorageUnits = MemoryUnits;

enum SpeedUnits
{
    Byps,
    KByps,
    MByps,
    GByps,
    TByps
};

struct Config
{
    template<typename UnitsEnum>
    struct Units
    {
        using DisplayNames = std::array<std::string_view, 5>;

        Units(Base base, DisplayNames display_names)
            : display_names_{ display_names }
            , base_{ base }
        {
            set_base(base);
        }

        [[nodiscard]] constexpr auto base() const noexcept
        {
            return base_;
        }

        [[nodiscard]] constexpr auto const& display_name(int units) const noexcept
        {
            return display_names_[units];
        }

        [[nodiscard]] constexpr auto multiplier(UnitsEnum multiplier) const noexcept
        {
            return multipliers_[multiplier];
        }

    private:
        void set_base(Base base)
        {
            base_ = base;

            auto val = 1ULL;
            for (auto& multiplier : multipliers_)
            {
                multiplier = val;
                val *= base;
            }
        }

        std::array<std::string_view, 5> display_names_;
        std::array<uint64_t, 5> multipliers_;
        Base base_;
    };

    static Units<MemoryUnits> Memory;
    static Units<SpeedUnits> Speed;
    static Units<StorageUnits> Storage;
};

template<typename UnitsEnum, Config::Units<UnitsEnum> const& units_>
class Value
{
public:
    constexpr Value() = default;

    constexpr Value(uint64_t value, UnitsEnum multiple)
        : base_quantity_{ value * units_.multiplier(multiple) }
        , multiple_{ multiple }
    {
    }

    template<typename Number>
    Value(Number value, UnitsEnum multiple)
        : base_quantity_{ static_cast<uint64_t>(value * units_.multiplier(multiple)) }
        , multiple_{ multiple }
    {
    }

    constexpr auto& operator+=(Value const& that) noexcept
    {
        base_quantity_ += that.base_quantity_;
        return *this;
    }

    [[nodiscard]] constexpr auto base_quantity() const noexcept
    {
        return base_quantity_;
    }

    [[nodiscard]] constexpr auto count(UnitsEnum tgt) const noexcept
    {
        return base_quantity_ / (1.0 * units_.multiplier(tgt));
    }

    [[nodiscard]] constexpr auto operator+(Value const& that) noexcept
    {
        auto ret = *this;
        return ret += that;
    }

    constexpr auto& operator*=(uint64_t mult) noexcept
    {
        base_quantity_ *= mult;
        return *this;
    }

    [[nodiscard]] constexpr auto operator*(uint64_t mult) noexcept
    {
        auto ret = *this;
        return ret *= mult;
    }

    constexpr auto& operator/=(uint64_t mult) noexcept
    {
        base_quantity_ /= mult;
        return *this;
    }

    [[nodiscard]] constexpr auto operator/(uint64_t mult) noexcept
    {
        auto ret = *this;
        return ret /= mult;
    }

    [[nodiscard]] constexpr auto to(UnitsEnum tgt) const noexcept
    {
        auto ret = Value{};
        ret.base_quantity_ = base_quantity_;
        ret.multiple_ = tgt;
        return ret;
    }

    [[nodiscard]] constexpr auto operator<(Value const& that) const noexcept
    {
        return compare(that) < 0;
    }

    [[nodiscard]] constexpr auto operator<=(Value const& that) const noexcept
    {
        return compare(that) <= 0;
    }

    [[nodiscard]] constexpr auto operator==(Value const& that) const noexcept
    {
        return compare(that) == 0;
    }

    [[nodiscard]] constexpr auto operator!=(Value const& that) const noexcept
    {
        return compare(that) != 0;
    }

    [[nodiscard]] constexpr auto operator>(Value const& that) const noexcept
    {
        return compare(that) > 0;
    }

    [[nodiscard]] constexpr auto operator>=(Value const& that) const noexcept
    {
        return compare(that) >= 0;
    }

    [[nodiscard]] std::string_view to_string(char* buf, size_t buflen) const noexcept
    {
        auto const value = count(multiple_);
        auto const precision = display_precision(value);
        auto const [out, len] = fmt::format_to_n(
            buf,
            buflen - 1,
            "{:.{}Lf} {:s}",
            value,
            precision,
            units_.display_name(multiple_));
        *out = '\0';
        return buf;
    }

    [[nodiscard]] std::string to_string() const
    {
        auto buf = std::array<char, 64>{};
        return std::string{ to_string(std::data(buf), std::size(buf)) };
    }

private:
    uint64_t base_quantity_;
    UnitsEnum multiple_;

    [[nodiscard]] static constexpr int display_precision(double value) noexcept
    {
        if (0.99 < value && value < 1.001)
        {
            return 0;
        }

        if (value < 100)
        {
            return 2;
        }

        return 1;
    }

    [[nodiscard]] constexpr int compare(Value const& that) const noexcept // <=>
    {
        if (base_quantity_ < that.base_quantity_)
        {
            return -1;
        }

        if (base_quantity_ > that.base_quantity_)
        {
            return 1;
        }

        return 0;
    }
};

using Memory = Value<MemoryUnits, Config::Memory>;
using Storage = Value<StorageUnits, Config::Storage>;
using Speed = Value<SpeedUnits, Config::Speed>;

} // namespace libtransmission::Values
