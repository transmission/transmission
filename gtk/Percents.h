#pragma once

#include <cmath>
#include <string>

class Percents
{
public:
    Percents() = default;

    explicit Percents(int value) noexcept
        : raw_value_(value * 100)
    {
    }

    explicit Percents(float value) noexcept
        : raw_value_(std::round(value * 10000))
    {
    }

    [[nodiscard]] constexpr int to_int() const noexcept
    {
        return raw_value_ / 100;
    }

    [[nodiscard]] constexpr float to_fraction() const noexcept
    {
        return raw_value_ / 10000.F;
    }

    [[nodiscard]] std::string to_string() const;

    constexpr bool operator==(Percents const& rhs) const noexcept
    {
        return raw_value_ == rhs.raw_value_;
    }

    constexpr bool operator<(Percents const& rhs) const noexcept
    {
        return raw_value_ < rhs.raw_value_;
    }

private:
    int raw_value_ = 0;
};
