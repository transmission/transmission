// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility> // for std::pair
#include <vector>

#include "libtransmission/net.h" // for tr_address
#include "libtransmission/observable.h"

namespace libtransmission
{

class Blocklists
{
public:
    Blocklists() = default;

    [[nodiscard]] bool contains(tr_address const& addr) const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] size_t size() const noexcept;

    void load(std::string_view folder, bool is_enabled);
    void set_enabled(bool is_enabled);
    size_t update_primary_blocklist(std::string_view external_file, bool is_enabled);

    libtransmission::SimpleObservable<> changed_;

private:
    class Blocklist
    {
    public:
        static std::optional<Blocklist> saveNew(std::string_view external_file, std::string_view bin_file, bool is_enabled);

        Blocklist() = default;

        Blocklist(std::string_view bin_file, bool is_enabled)
            : bin_file_{ bin_file }
            , is_enabled_{ is_enabled }
        {
        }

        [[nodiscard]] bool contains(tr_address const& addr) const;

        [[nodiscard]] auto size() const
        {
            ensureLoaded();

            return std::size(rules_);
        }

        [[nodiscard]] constexpr bool enabled() const noexcept
        {
            return is_enabled_;
        }

        constexpr void setEnabled(bool is_enabled) noexcept
        {
            is_enabled_ = is_enabled;
        }

        [[nodiscard]] constexpr auto const& binFile() const noexcept
        {
            return bin_file_;
        }

    private:
        void ensureLoaded() const;

        mutable std::vector<std::pair<tr_address, tr_address>> rules_;

        std::string bin_file_;
        bool is_enabled_ = false;
    };

    std::vector<Blocklist> blocklists_;

    std::string folder_;

    [[nodiscard]] static std::vector<Blocklist> load_folder(std::string_view folder, bool is_enabled);
};
} // namespace libtransmission
