// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // for size_t
#include <initializer_list>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/log.h" // for tr_log_level
#include "libtransmission/net.h" // for tr_port, tr_tos_t
#include "libtransmission/open-files.h" // for tr_open_files::Preallocation
#include "libtransmission/peer-io.h" // tr_preferred_transport
#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

namespace libtransmission
{

class Settings
{
public:
    void load(tr_variant const& src)
    {
        fields().load(src);
    }

    [[nodiscard]] tr_variant save() const
    {
        return const_cast<Settings*>(this)->fields().save();
    }

protected:
    Settings() = default;

    class Fields
    {
    public:
        using key_type = tr_quark;
        using mapped_type = std::variant<
            bool*,
            double*,
            size_t*,
            std::string*,
            tr_encryption_mode*,
            tr_log_level*,
            tr_mode_t*,
            tr_open_files::Preallocation*,
            tr_port*,
            tr_preferred_transport*,
            tr_tos_t*,
            tr_verify_added_mode*>;
        using value_type = std::pair<const key_type, mapped_type>;

        Fields(std::initializer_list<value_type> args)
            : props_{ args }
        {
        }

        [[nodiscard]] auto size() const noexcept
        {
            return std::size(props_);
        }

        [[nodiscard]] auto begin() const noexcept
        {
            return std::cbegin(props_);
        }

        [[nodiscard]] auto begin() noexcept
        {
            return std::begin(props_);
        }

        [[nodiscard]] auto end() const noexcept
        {
            return std::cend(props_);
        }

        [[nodiscard]] auto end() noexcept
        {
            return std::end(props_);
        }

        void load(tr_variant const& src);
        [[nodiscard]] tr_variant save() const;

    private:
        std::vector<value_type> props_;
    };

private:
    [[nodiscard]] virtual Fields fields() = 0;
};
} // namespace libtransmission
