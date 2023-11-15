// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QCoreApplication> // Q_DECLARE_TR_FUNCTIONS
#include <QString>

#include "libtransmission/values.h"

class Speed : public libtransmission::Values::Speed
{
    Q_DECLARE_TR_FUNCTIONS(Speed)

public:
    Speed() = default;

    template<typename Number, typename std::enable_if_t<std::is_integral_v<Number>>* = nullptr>
    constexpr Speed(Number value, Units multiple)
        : libtransmission::Values::Speed{ value, multiple }
    {
    }

    template<typename Number, typename std::enable_if_t<std::is_floating_point_v<Number>>* = nullptr>
    Speed(Number value, Units multiple)
        : libtransmission::Values::Speed{ value, multiple }
    {
    }

    [[nodiscard]] auto to_qstring() const noexcept
    {
        return QString::fromStdString(to_string());
    }

    [[nodiscard]] auto to_upload_qstring() const
    {
        static auto constexpr UploadSymbol = QChar{ 0x25B4 };
        return tr("%1 %2").arg(to_qstring()).arg(UploadSymbol);
    }

    [[nodiscard]] auto to_download_qstring() const
    {
        static auto constexpr DownloadSymbol = QChar{ 0x25BE };
        return tr("%1 %2").arg(to_qstring()).arg(DownloadSymbol);
    }

    [[nodiscard]] constexpr auto operator+(Speed const& other) const noexcept
    {
        return Speed{ base_quantity() + other.base_quantity(), Speed::Units::Byps };
    }

    [[nodiscard]] static auto display_name(Speed::Units const units)
    {
        auto const speed_unit_sv = Speed::units().display_name(units);
        return QString::fromUtf8(std::data(speed_unit_sv), std::size(speed_unit_sv));
    }
};
