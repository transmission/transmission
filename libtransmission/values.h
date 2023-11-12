// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cmath> // for std::fabs(), std::floor()
#include <cstdint> // for uint64_t
#include <string>
#include <string_view>

#include <fmt/core.h>

namespace libtransmission::Values
{
enum class MemoryUnits
{
    Bytes,
    KBytes,
    MBytes,
    GBytes,
    TBytes
};

using StorageUnits = MemoryUnits;

enum class SpeedUnits
{
    Byps,
    KByps,
    MByps,
    GByps,
    TByps
};

struct Config
{
    enum class Base
    {
        Kilo = 1000U,
        Kibi = 1024U
    };

    template<typename UnitsEnum>
    struct Units
    {
        template<typename... Names>
        Units(Base base, Names... names)
        {
            set_base(base);

            auto idx = size_t{ 0U };
            (set_name(idx++, names), ...);
        }

        [[nodiscard]] constexpr auto base() const noexcept
        {
            return static_cast<size_t>(base_);
        }

        [[nodiscard]] constexpr auto display_name(size_t units) const noexcept
        {
            return std::string_view{ units < std::size(display_names_) ? std::data(display_names_[units]) : "" };
        }

        [[nodiscard]] constexpr auto multiplier(UnitsEnum multiplier) const noexcept
        {
            return multipliers_[static_cast<int>(multiplier)];
        }

    private:
        constexpr void set_base(Base base)
        {
            base_ = base;

            auto val = uint64_t{ 1U };
            for (auto& multiplier : multipliers_)
            {
                multiplier = val;
                val *= static_cast<size_t>(base);
            }
        }

        void set_name(size_t idx, std::string_view name)
        {
            *fmt::format_to_n(std::data(display_names_[idx]), std::size(display_names_[idx]) - 1, "{:s}", name).out = '\0';
        }

        std::array<std::array<char, 32>, 5> display_names_ = {};
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
    using Units = UnitsEnum;

    constexpr Value() = default;

    constexpr Value(uint64_t value, Units multiple)
        : base_quantity_{ value * units_.multiplier(multiple) }
    {
    }

    template<typename Number>
    Value(Number value, Units multiple)
        : base_quantity_{ static_cast<uint64_t>(value * units_.multiplier(multiple)) }
    {
    }

    [[nodiscard]] constexpr auto base_quantity() const noexcept
    {
        return base_quantity_;
    }

    [[nodiscard]] constexpr auto count(Units tgt) const noexcept
    {
        return base_quantity_ / (1.0 * units_.multiplier(tgt));
    }

    constexpr auto& operator+=(Value const& that) noexcept
    {
        base_quantity_ += that.base_quantity_;
        return *this;
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

    std::string_view to_string(char* buf, size_t buflen) const noexcept
    {
        auto idx = size_t{ 0 };
        auto val = 1.0 * base_quantity_;
        for (;;)
        {
            if (std::fabs(val - std::floor(val)) < 0.001 && (val < 999.5 || std::empty(units_.display_name(idx + 1))))
            {
                *fmt::format_to_n(buf, buflen - 1, "{:.0Lf} {:s}", val, units_.display_name(idx)).out = '\0';
                return buf;
            }

            if (val < 99.995) // 0.98 to 99.99
            {
                *fmt::format_to_n(buf, buflen - 1, "{:.2Lf} {:s}", val, units_.display_name(idx)).out = '\0';
                return buf;
            }

            if (val < 999.95 || std::empty(units_.display_name(idx + 1))) // 100.0 to 999.9
            {
                *fmt::format_to_n(buf, buflen - 1, "{:.1Lf} {:s}", val, units_.display_name(idx)).out = '\0';
                return buf;
            }

            val /= units_.base();
            ++idx;
        }
    }

    [[nodiscard]] std::string to_string() const
    {
        auto buf = std::array<char, 64>{};
        return std::string{ to_string(std::data(buf), std::size(buf)) };
    }

    [[nodiscard]] static constexpr auto const& units() noexcept
    {
        return units_;
    }

private:
    uint64_t base_quantity_ = {};

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
